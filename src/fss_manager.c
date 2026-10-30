//./fss_manager -l <manager_logfile> -c <config_file> -n <worker_limit>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <poll.h>
#include <stdbool.h>
#include "Deque_Interface.h"
#include "variables.h"

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define BUFFER_SIZE 4096
#define MAX_PIPE_BUFFER_SIZE 4096


int inotify_fd,read_fd;
int worker_limit = 5; // Default value for worker limit
int still_config = 1;
FILE *log_file;
int pid_fd_map_size = 0;

queued_Deque Q_D;	//declared here so they can be used in signal handlers
running_Deque R_D;







// Structure definition remains the same
typedef struct {
    pid_t pid;
    int pipe_read_fd;
    bool active;
} PidFdMapping;

// --- Declare a global POINTER and a size variable ---
PidFdMapping *pid_fd_map = NULL; // Pointer, initially NULL
         // Will hold the actual worker_limit

void create_fd_map(int size) {
    pid_fd_map_size = size; // Store the size globally
    pid_fd_map = malloc(pid_fd_map_size * sizeof(PidFdMapping));
    if (pid_fd_map == NULL) {
        printf("Failed to allocate memory for pid_fd_map\n");
        exit(EXIT_FAILURE);
    }
}

// Initialize the map (uses global pid_fd_map_size)
void init_pid_fd_map() {

    pid_fd_map = malloc(pid_fd_map_size * sizeof(PidFdMapping));
    if (pid_fd_map == NULL) {
        printf("Failed to allocate memory for pid_fd_map\n");
        exit(EXIT_FAILURE);
    }
    
    if (pid_fd_map == NULL || pid_fd_map_size <= 0) { // Check if allocated
        printf("Error: pid_fd_map not allocated or size is invalid in init_pid_fd_map\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < pid_fd_map_size; ++i) {
        pid_fd_map[i].pid = -1;
        pid_fd_map[i].pipe_read_fd = -1;
        pid_fd_map[i].active = false;
    }
}

// Add a mapping - returns true on success, false on failure (map full)
int add_pid_fd_mapping(pid_t pid, int fd) {
    if (pid_fd_map == NULL) return 0; // Check allocation
    for (int i = 0; i < pid_fd_map_size; ++i) {
        if (!pid_fd_map[i].active) {
            pid_fd_map[i].pid = pid;
            pid_fd_map[i].pipe_read_fd = fd;
            pid_fd_map[i].active = 1;
            return 1;
        }
    }
    printf("ERROR: PID-FD map (size %d) is full! Cannot add PID %d\n", pid_fd_map_size, pid);
    return 0; // Map is full
}

// Find the FD for a given PID - returns FD or -1 if not found
int find_pipe_fd_for_pid(pid_t pid) {
    if (pid_fd_map == NULL) return -1; // Check allocation
    for (int i = 0; i < pid_fd_map_size; ++i) { // Use pid_fd_map_size
        if (pid_fd_map[i].active && pid_fd_map[i].pid == pid) {
            return pid_fd_map[i].pipe_read_fd;
        }
    }
    return -1; // Not found
}

// Remove a mapping by PID - returns 1 on success, 0 if not found
int remove_pid_fd_mapping(pid_t pid) {
     if (pid_fd_map == NULL) return 0; // Check allocation
    for (int i = 0; i < pid_fd_map_size; ++i) { // Use pid_fd_map_size
        if (pid_fd_map[i].active && pid_fd_map[i].pid == pid) {
            pid_fd_map[i].active = 0;
            pid_fd_map[i].pid = -1;
            pid_fd_map[i].pipe_read_fd = -1;
            return 1;
        }
    }
    return 0; // Not found
}


void destroy_fd_map() {
    if (pid_fd_map != NULL) {
        free(pid_fd_map);
        pid_fd_map = NULL; // Set to NULL after freeing
    }
}








void send_to_console(char *str_to_cons) { //send message back to console to be printed
	int write_fd;
	char write_str[max__size];
	if( (write_fd=open(fifo2, O_WRONLY)) < 0) {
        printf("\ncan't open-write fifo2\n");
        return;
    }
    strcpy(write_str, str_to_cons);
    if (write(write_fd, write_str, max__size) == -1) {
        printf("\nCan't write to fifo2\n");
        return;
    }

    if (strcmp(str_to_cons, "close") == 0) {
        close(write_fd);
    }
}

// Function to get the current time as a formatted string
void time_format(char *buffer, size_t size) {
    time_t t_time;
    struct tm *bd_time;
    time(&t_time);
    bd_time = localtime(&t_time);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", bd_time);
}

void time_format___no_brackets(char *buffer, size_t size) {
    time_t t_time;
    struct tm *bd_time;
    time(&t_time);
    bd_time = localtime(&t_time);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", bd_time);
}

void actually_close_descriptors() {
    run_close_all_watch_descriptors(inotify_fd, &R_D);
    destroy_fd_map();
    close(inotify_fd);
    close(read_fd);
    remove(fifo1);
    remove(fifo2);
    printf("\nCleanup done. Exiting.\n");
}

void closing_descriptors(int signum) {
    actually_close_descriptors();
    exit(EXIT_SUCCESS);
}

void spawn_worker(int, char *,  char *, char *, char *,queued_Deque *,running_Deque *);

void just_spawn_it(int wd,char *source_dir,char *target_dir,char *filename,char *operation) {
    spawn_worker(wd,source_dir, target_dir, filename, operation, &Q_D, &R_D);
}

void add_watch(char *source_dir,char *target_dir,char *filename,char *operation) {
    char str_to_console[max__size];
    int wd = inotify_add_watch(inotify_fd, source_dir, IN_CREATE | IN_CLOSE_WRITE | IN_DELETE);
    if (wd == -1) {
        printf("Error adding watch for directory %s: %s\n\n", source_dir, strerror(errno));

        if(still_config == 0) { //we aren't in the config file
            char time_str[30];
            time_format(time_str, sizeof(time_str));
            sprintf(str_to_console, "%s Error adding watch for directory %s: %s\n\n", time_str, source_dir, strerror(errno));
            send_to_console(str_to_console);
            sprintf(str_to_console, "close");
            send_to_console(str_to_console);
        }
        return;
    } else {
        char time_str[30];
        time_format(time_str, sizeof(time_str));
        char *abs_source = realpath(source_dir, NULL);
        char *abs_target = realpath(target_dir, NULL);
        fprintf(log_file, "%s Added directory: %s -> %s\n", time_str, abs_source, abs_target);
        fprintf(log_file, "%s Monitoring started for %s\n\n", time_str, abs_source);
        fflush(log_file);
        sprintf(str_to_console, "%s Added directory: %s -> %s\n%s Monitoring started for %s\n", time_str, abs_source, abs_target, time_str, abs_source);
        printf("%s\n", str_to_console);

        if(still_config == 0) {
            send_to_console(str_to_console);
            sprintf(str_to_console, "close");
            send_to_console(str_to_console);
        }
        free(abs_source);
        free(abs_target);
        spawn_worker(wd,source_dir, target_dir, filename, operation, &Q_D, &R_D);
    }
}

void process_config_file(const char *config_file) {
    FILE *file = fopen(config_file, "r");
    if (!file) {
        printf("Error opening config file %s: %s\n", config_file, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        char *source_dir = strtok(line, " ");
        char *target_dir = strtok(NULL, " ");

        if (source_dir && target_dir) {
            add_watch(source_dir, target_dir,"ALL", "FULL");
        } else {
            printf("Invalid line format in config file: %s\n", line);
        }
    }
    fclose(file);

    still_config = 0; // Setting it to 0 after processing the config file
}



// trims leading/trailing whitespace from a string
char *trim_whitespace(char *str) {
    if (str == NULL) return NULL;
    char *end;

    while (isspace((unsigned char)*str)) str++; //triming the leading space
    if (*str == 0) return str;

    
    end = str + strlen(str) - 1; //triming the trailing space
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0'; //null-terminate the string

    return str;
}

void process_report(const char *full_report, pid_t worker_pid, int *error_count_out) {
    // --- Variables Parsed from Report ---
    char source_dir[PATH_MAX] = "N/A";
    char target_dir[PATH_MAX] = "N/A";
    char filename[PATH_MAX] = "N/A";
    char operation[20] = "N/A";
    char result[10] = "N/A";
    char raw_details[1000] = "N/A";
    int error_count = -1;  // initializing error count to -1

    char line_buffer[PATH_MAX * 2]; // Buffer for reading lines
    const char *current_pos = full_report;
    const char *next_nl;

    if (full_report == NULL) {
        printf("Error: Received NULL report.\n");
        return;
    }

    // Find the start of the report content, skipping EXEC_REPORT_START
    const char *report_content_start = strstr(current_pos, "EXEC_REPORT_START");
    if (report_content_start) {
        report_content_start = strchr(report_content_start, '\n'); // Move to the next line
        if (report_content_start) {
            current_pos = report_content_start + 1; // Start parsing after the newline
        }
    }

    while ((next_nl = strchr(current_pos, '\n')) != NULL) {
        size_t line_len = next_nl - current_pos;

        // Stop if we hit EXEC_REPORT_END
        if (strncmp(current_pos, "EXEC_REPORT_END", 15) == 0) {
            break;
        }
        // Skip ERRORS: header line itself
        if (strncmp(current_pos, "ERRORS:", 7) == 0) {
            current_pos = next_nl + 1;
            continue;
        }
         // Skip lines starting with '-' under ERRORS: for now
         // More sophisticated error parsing could be added here if needed
        if (isspace((unsigned char)*current_pos) || *current_pos == '-') {
            current_pos = next_nl + 1;
            continue;
        }


        if (line_len >= sizeof(line_buffer)) {
            // Avoid buffer overflow if a line is extremely long
            printf("Warning: Report line too long, truncating.\n");
            line_len = sizeof(line_buffer) - 1;
        }
        strncpy(line_buffer, current_pos, line_len);
        line_buffer[line_len] = '\0';

        // Find the first colon
        char *colon_pos = strchr(line_buffer, ':');
        if (colon_pos != NULL) {
            *colon_pos = '\0'; // Split key and value
            char *key = trim_whitespace(line_buffer);
            char *value = trim_whitespace(colon_pos + 1);

            if (key && value) { // Ensure key and value are not NULL after trimming
                // --- Use standard keys (adjust if yours are different) ---
                if (strcmp(key, "SOURCE_DIR") == 0) {
                    strncpy(source_dir, value, sizeof(source_dir) - 1);
                    source_dir[sizeof(source_dir) - 1] = '\0';
                } else if (strcmp(key, "TARGET_DIR") == 0) {
                    strncpy(target_dir, value, sizeof(target_dir) - 1);
                    target_dir[sizeof(target_dir) - 1] = '\0';
                } else if (strcmp(key, "FILENAME") == 0) {
                    strncpy(filename, value, sizeof(filename) - 1);
                    filename[sizeof(filename) - 1] = '\0';
                }else if (strcmp(key, "OPERATION") == 0) {
                    strncpy(operation, value, sizeof(operation) - 1);
                    operation[sizeof(operation) - 1] = '\0';
                } else if (strcmp(key, "STATUS") == 0) {
                    strncpy(result, value, sizeof(result) - 1);
                    result[sizeof(result) - 1] = '\0';
                } else if (strcmp(key, "DETAILS") == 0) {
                    // Store the raw details string
                    strncpy(raw_details, value, sizeof(raw_details) - 1);
                    raw_details[sizeof(raw_details) - 1] = '\0';
                } else if (strcmp(key, "ERROR COUNT") == 0) {
                    if (sscanf(value, "%d", &error_count) != 1) {
                        error_count = -1; // Parsing failed
                    }
                }
            }
        }
        current_pos = next_nl + 1; // Moving to the start of the next line
    }

    // Error count prep
    if (error_count_out != NULL) {
        *error_count_out = error_count;
    }

    char formatted_details[PATH_MAX]; // Buffer for the final details string

    // Default to raw details if operation is unknown or N/A
    strncpy(formatted_details, raw_details, sizeof(formatted_details) - 1);
    formatted_details[sizeof(formatted_details) - 1] = '\0';

    if (strcmp(operation, "FULL") == 0) {
        strncpy(formatted_details, raw_details, sizeof(formatted_details) - 1);
    }

    // Ensure null termination after potential snprintf/strncpy
    formatted_details[sizeof(formatted_details) - 1] = '\0';


    // Writing log
    char timestamp[30];
    time_format___no_brackets(timestamp, sizeof(timestamp));

    char final_log_entry[max__size * 2];

    if (strcmp(filename,"ALL") != 0) {
        snprintf(final_log_entry, sizeof(final_log_entry), "[%s] [%s] [%s] [%d] [%s] [%s] [File : %s]\n\n", timestamp, source_dir, target_dir,(int)worker_pid,operation,result,filename);
        printf("%s", final_log_entry);
        fprintf(log_file, "%s", final_log_entry);
        fflush(log_file);
    } else {
        snprintf(final_log_entry, sizeof(final_log_entry), "[%s] [%s] [%s] [%d] [%s] [%s] [%s]\n\n",timestamp,source_dir,target_dir,(int)worker_pid,operation,result,formatted_details);
        fprintf(log_file, "%s", final_log_entry);
        fflush(log_file);
    }
}



void chld_handler(int signal_1) {
	int stat_loc;
	pid_t id;
	int run_jobs;
	char src_dir[max__size],tar_dir[max__size],filename[max__size],operation[max__size];
    int extracted_error_count = -17;

	while ((id = waitpid(-1, &stat_loc, WNOHANG)) > 0) { //once a job is done running then it removes it from the running queue

        int pipe_read_fd = find_pipe_fd_for_pid(id);

        if (pipe_read_fd != -1) { // worker pid found in the map
            char pipe_buffer[MAX_PIPE_BUFFER_SIZE];
            char full_report_buffer[MAX_PIPE_BUFFER_SIZE];
            ssize_t bytes_read;
            size_t total_bytes_read = 0;

            full_report_buffer[0] = '\0';
            extracted_error_count = -17;

            // printf("-------->Worker %d Report Start\n", id);
            while ((bytes_read = read(pipe_read_fd, pipe_buffer, sizeof(pipe_buffer) - 1)) > 0) { // Read exec_report from the correct pipe
                pipe_buffer[bytes_read] = '\0';
                // printf("%s", pipe_buffer);      // Print the chunks

                if (total_bytes_read + bytes_read < MAX_PIPE_BUFFER_SIZE) {
                    strcat(full_report_buffer, pipe_buffer);
                    total_bytes_read += bytes_read;
                } else {
                    printf("Warning: Report too large for buffer, truncating.\n");
                    break;
                }
            }

            if (total_bytes_read > 0) {
                process_report(full_report_buffer, id, &extracted_error_count);
            } else {
                printf("Handler: read from pipe failed");
            }

            // printf("\nWorker %d Report End <---\n", id);

            if (close(pipe_read_fd) == -1) { // closing the read end of the pipe
                printf("\nHandler: close pipe_read_fd failed");
            }
            
            if (!remove_pid_fd_mapping(id)) { // removing that pid's mapping from the map
                printf("\n Failed to remove PID %d from map.\n", id);
            }

        } else { // worker pid not found in the map
            printf("\nError: PID %d not found in pid_fd_map\n", id);
        }

        run_update_pid_error_count(id, extracted_error_count, &R_D); //updates the error count of the source_dir
		run_make_pid_active_to_no(id,&R_D); //updates the status of the job to NO (which means it is not running anymore)
        run_reduce_running_count(&R_D);
	}

	run_jobs = run_running_count(&R_D);

	for(int i = 0 ; i < (worker_limit-run_jobs) ;i++) { //it then pops the waiting jobs
		int que_jobs = que_SizeOf(&Q_D);
		if(que_jobs > 0 ) {
			strcpy(src_dir,(&Q_D)->Front->source_dir);
			strcpy(tar_dir,(&Q_D)->Front->target_dir);
            strcpy(filename,(&Q_D)->Front->filename);
            strcpy(operation,(&Q_D)->Front->operation);
            int wd = (&Q_D)->Front->watch_disc_inot;
			que_Pop(&Q_D);

            spawn_worker(wd,src_dir, tar_dir, filename, operation, &Q_D, &R_D);
		}
	}
}


void spawn_worker(int wd, char *source_directory, char *target_directory, char *filename, char *operation, queued_Deque *Q_D, running_Deque *R_D) {

    signal(SIGCHLD, chld_handler);


    int running = run_running_count(R_D);
    if (running < worker_limit) {

        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            printf("pipe failed");
            que_Enqueue(wd, source_directory, target_directory, filename, operation, Q_D); // Use correct Q_D
            return;
        }


        pid_t pid = fork();

        if (pid == 0) { // Child process

            close(pipe_fd[0]); // Close read end

            // Redirect stdout
            if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
                printf("dup2 stdout failed");
                close(pipe_fd[1]);
                exit(EXIT_FAILURE);
            }

            close(pipe_fd[1]); // Close original write FD after dup

            char *worker_executable = "./worker";
            char *args_for_worker[] = {
                worker_executable,
                source_directory,
                target_directory,
                filename,
                operation,
                NULL // MUST end with NULL
            };

            // sleep(3); // Sleep for 1 second to allow the parent process to set up the environment
            execvp(args_for_worker[0], args_for_worker);

            // execvp only returns on error
            printf("execvp failed\n");
            exit(EXIT_FAILURE);

        } else if (pid < 0) { // Fork failed
            printf("fork failed\n");
            close(pipe_fd[0]);
            close(pipe_fd[1]);
            que_Enqueue(wd, source_directory, target_directory, filename, operation, Q_D);
            return;

        } else { // Parent process

            close(pipe_fd[1]); // Close write end

            if (!add_pid_fd_mapping(pid, pipe_fd[0])) { //couldn't add mapping
                printf("Failed to add PID-FD mapping for PID %d\n", pid);
                close(pipe_fd[0]);
                que_Enqueue(wd, source_directory, target_directory, filename, operation, Q_D);
                return;

            } else {
                char time_str[30];
                time_format___no_brackets(time_str, sizeof(time_str));
                if (run_wd_exists(wd, R_D) == NULL) { //wd doesnt exist in the running queue
                    run_Enqueue(pid, wd, source_directory, target_directory, "Active", "YES", time_str, 0, R_D);
                } else {
                    run_make_wd_active_to_yes(pid, wd, R_D);
                }
            }
            
        }
    } else { // Worker limit reached
        que_Enqueue(wd, source_directory, target_directory, filename, operation, Q_D);
    }
}



int main(int argc, char *argv[]) {
    if(argc != 7) {
        printf("\nfss_manager::::wrong number of arguments!!\n");
        return -2;
    }

    run_Create(&R_D);
	que_Create(&Q_D);

    char config_file[PATH_MAX],manager_log_file[PATH_MAX];

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-l") == 0) {
            strcpy(manager_log_file, argv[i + 1]);
        } else if(strcmp(argv[i], "-c") == 0) {
            strcpy(config_file, argv[i + 1]);
        } else if(strcmp(argv[i], "-n") == 0) {           
            worker_limit = atoi(argv[i + 1]);
            if (worker_limit <= 0) {
                worker_limit = 5; // Default value
            }
        }
    }

    create_fd_map(worker_limit);
    
    init_pid_fd_map(); // initialize the pipe map

    // // Print arguments for testing
    // printf("\nmanager_logfile: %s\n", manager_log_file);
    // printf("config_file: %s\n", config_file);
    // printf("worker_limit: %d\n\n", worker_limit);

    // creating/opening the log file
    log_file = fopen(manager_log_file, "w");
    if (!log_file) {
        printf("Error opening log file\n");
        return -9;
    }

    // fprintf(log_file, "\n-----------------------------------------\n");
    // fflush(log_file);
    
    signal(SIGINT, closing_descriptors);

    // signal(SIGCHLD, chld_handler);

    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        printf("inotify_init error\n");
        exit(EXIT_FAILURE);
    }

    process_config_file(config_file);
    
    if (mkfifo (fifo1 , 0666) == -1){ //creating fifo2
		if (errno != EEXIST ) {
			printf("\ncouldn't make fifo1\n");
			return -4;
		}
	}
    
    if (mkfifo (fifo2 , 0666) == -1){ //creating fifo2
		if (errno != EEXIST ) {
			printf("\ncouldn't make fifo2\n");
			return -5;
		}
	}

    int flag;

    if((read_fd = open(fifo1, O_RDONLY | O_NONBLOCK)) < 0) {
        printf("\ncan't open-read fifo1\n");
        return -6;	
    }

    struct pollfd fds[2];
    fds[0].fd = read_fd;
    fds[0].events = POLLIN;
    fds[1].fd = inotify_fd;
    fds[1].events = POLLIN;

    char buffer[EVENT_BUF_LEN];
    int length, i = 0;
    char read_str[max__size],write_str[max__size],time_str[30], *parameters, *command;
    char str_to_console[max__size];

    while (1) {
        int ret = poll(fds, 2, -1); // Wait indefinitely
        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, just continue polling
                continue;
            }
            printf("poll error\n");
            break;
        }

        // FIFO input is ready
        if (fds[0].revents & POLLIN) {
            flag = 0;
            if(read(read_fd, read_str, max__size ) < 0) { //reading from fss_console
                printf("\nCan't read from fifo2\n");
                return -7;
            }

            // printf("\nReceived command: %s\n", read_str);
    
            command = strtok(read_str, " ");
            if(strcmp(command, "add") == 0) {
                flag = 1;
                parameters = strtok(NULL, "");
                if (parameters == NULL) {
                    printf("\"add\" command error: Missing parameters\n\n");
                    sprintf(str_to_console, "\"add\" command error: Missing parameters\n\n");
                    send_to_console(str_to_console);
                    sprintf(str_to_console, "close");
                    send_to_console(str_to_console);
                } else {
                    char *source_dir = strtok(parameters, " ");
                    char *target_dir = strtok(NULL, " ");
                    if (source_dir == NULL || target_dir == NULL) {
                        printf("\"add\" command error: Missing source or target directory\n\n");
                        sprintf(str_to_console, "\"add\" command error: Missing source or target directory\n\n");
                        send_to_console(str_to_console);
                        sprintf(str_to_console, "close");
                        send_to_console(str_to_console);
                    } else {
                        running_DequeNode *found_match = run_find_source_with_target(source_dir, target_dir, &R_D);
                        if (que_find_source(source_dir, &Q_D) == 0 && run_find_source(source_dir, &R_D) == 0) {     //if the source directory is not in the queues
                            add_watch(source_dir, target_dir,"ALL", "FULL");
                        } else if(found_match != NULL) { //if the match is in the running queue
                            char time_str[30];
                            if(strcmp(found_match->status,"Inactive") == 0) { //if the match is in the running queue but inactive
                                run_set_status_to_active(found_match->source_dir,&R_D);

                                time_format(time_str, sizeof(time_str));
                                char *abs_source = realpath(source_dir, NULL);
                                char *abs_target = realpath(target_dir, NULL);
                                fprintf(log_file, "%s Added directory: %s -> %s\n", time_str, abs_source, abs_target);
                                fprintf(log_file, "%s Monitoring started for %s\n\n", time_str, abs_source);
                                fflush(log_file);
                                sprintf(str_to_console, "%s Added directory: %s -> %s\n%s Monitoring started for %s\n\n", time_str, abs_source, abs_target, time_str, abs_source);
                                printf("%s", str_to_console);
                                send_to_console(str_to_console);
                                sprintf(str_to_console, "close");
                                send_to_console(str_to_console);


                                free(abs_source);
                                free(abs_target);
                                spawn_worker(found_match->watch_disc_inot,source_dir, target_dir,"ALL", "FULL", &Q_D, &R_D);
                            } else if(strcmp(found_match->status,"Active") == 0) { //if the match is in the running queue and active
                                time_format(time_str, sizeof(time_str));
                                char *abs_source = realpath(source_dir, NULL);
                                printf("%s Already in queue: %s\n\n", time_str, abs_source);
                                sprintf(str_to_console, "%s Already in queue: %s\n\n", time_str, abs_source);
                                free(abs_source);
                                send_to_console(str_to_console);
                                sprintf(str_to_console, "close");
                                send_to_console(str_to_console);
                            }
                        } else { 
                            time_format(time_str, sizeof(time_str));
                            char *abs_source = realpath(source_dir, NULL);
                            printf("%s Already in queue: %s\n\n", time_str, abs_source);
                            sprintf(str_to_console, "%s Already in queue: %s\n\n", time_str, abs_source);
                            free(abs_source);
                            send_to_console(str_to_console);
                            sprintf(str_to_console, "close");
                            send_to_console(str_to_console);
                        }
                    }
                }
            } else if(strcmp(command, "cancel") == 0) {
                flag = 1;
                parameters = strtok(NULL, "");
                if (parameters == NULL) {
                    printf("\"cancel\" command error: Missing parameters\n\n");
                    sprintf(str_to_console, "\"cancel\" command error: Missing parameters\n\n");
                    send_to_console(str_to_console);
                    sprintf(str_to_console, "close");
                    send_to_console(str_to_console);
                }
                else {
                    // Handle cancel command
                    if (run_cancel_source(inotify_fd,parameters, &R_D) == 1) {
                        time_format(time_str, sizeof(time_str));
                        char *abs_source = realpath(parameters, NULL);
                        fprintf(log_file, "%s Monitoring stopped for %s\n\n", time_str, abs_source);
                        fflush(log_file);
                        printf("%s Monitoring stopped for %s\n\n", time_str, abs_source);
                        sprintf(str_to_console, "%s Monitoring stopped for %s\n\n", time_str, abs_source);
                        send_to_console(str_to_console);
                        sprintf(str_to_console, "close");
                        send_to_console(str_to_console);
                        free(abs_source);
                    } else {
                        time_format(time_str, sizeof(time_str));
                        char *abs_source = realpath(parameters, NULL);
                        printf("%s Directory not monitored: %s\n\n", time_str, abs_source);
                        sprintf(str_to_console, "%s Directory not monitored: %s\n\n", time_str, abs_source);
                        send_to_console(str_to_console);
                        sprintf(str_to_console, "close");
                        send_to_console(str_to_console);
                        free(abs_source);
                    }
                }
            } else if(strcmp(command, "status") == 0) {
                flag = 1;
                parameters = strtok(NULL, "");
                if (parameters == NULL) {
                    printf("\"status\" command error: Missing parameters\n\n");
                    sprintf(str_to_console, "\"status\" command error: Missing parameters\n\n");
                    send_to_console(str_to_console);
                    sprintf(str_to_console, "close");
                    send_to_console(str_to_console);
                }
                else {
                    // Handle status command
                    time_format(time_str, sizeof(time_str));
                    char *abs_source = realpath(parameters, NULL);
                    running_DequeNode *node = run_get_source_info(parameters, &R_D);
                    if (node != NULL) { // source directory is in the running queue
                        char *abs_target = realpath(node->target_dir, NULL);
                        printf("%s Status requested for %s\nDirectory: %s\nTarget: %s\nLast Sync: %s\nErrors: %d\nStatus: %s\n\n",
                            time_str, abs_source,abs_source, abs_target, node->last_sync_time, node->error_count, node->status);
                        sprintf(str_to_console, "%s Status requested for %s\nDirectory: %s\nTarget: %s\nLast Sync: %s\nErrors: %d\nStatus: %s\n\n",
                            time_str, abs_source,abs_source, abs_target, node->last_sync_time, node->error_count, node->status);
                        free(abs_target);
                        send_to_console(str_to_console);
                        sprintf(str_to_console, "close");
                        send_to_console(str_to_console);
                    } else { //source directory is not in the queue
                        printf("%s Directory not monitored: %s\n\n", time_str, parameters);
                        fflush(stdout);
                        sprintf(str_to_console, "%s Directory not monitored: %s\n\n", time_str, parameters);
                        send_to_console(str_to_console);
                        sprintf(str_to_console, "close");
                        send_to_console(str_to_console);
                    }
                    free(abs_source);
                }
            } else if(strcmp(command, "sync") == 0) {
                flag = 1;
                parameters = strtok(NULL, "");
                if (parameters == NULL) {
                    printf("\"sync\" command error: Missing parameters\n\n");
                    sprintf(str_to_console, "\"sync\" command error: Missing parameters\n\n");
                    send_to_console(str_to_console);
                    sprintf(str_to_console, "close");
                    send_to_console(str_to_console);
                }
                else {
                    // Handle sync command
                    running_DequeNode *node = run_get_source_info(parameters, &R_D);
                    if (node != NULL) { // source directory is in the running queue
                        char *abs_source = realpath(node->source_dir, NULL);
                        char time_str[30];

                        if (strcmp(node->status,"Inactive") == 0 ) { //if the job is in the running queue and active

                            run_set_status_to_active(node->source_dir,&R_D);
                            
                            time_format(time_str, sizeof(time_str));                            
                            char *abs_target = realpath(node->target_dir, NULL);
                            fprintf(log_file, "%s Syncing directory: %s -> %s\n\n", time_str, abs_source, abs_target);
                            fflush(log_file);
                            printf("%s Syncing directory: %s -> %s\n", time_str, abs_source, abs_target);
                            sprintf(str_to_console, "%s Syncing directory: %s -> %s\n", time_str, abs_source, abs_target);
                            send_to_console(str_to_console);

                            int temp_errors = node->error_count;
                            spawn_worker(node->watch_disc_inot,node->source_dir,node->target_dir,"ALL", "FULL", &Q_D, &R_D);

                            while(run_wd_active_yes(node->watch_disc_inot,&R_D) == 1) {
                                sleep(100);
                            }

                            running_DequeNode *node_1 = run_get_source_info(parameters, &R_D);
                            time_format(time_str, sizeof(time_str));
                            fprintf(log_file, "%s Sync completed %s -> %s Errors:%d\n\n", time_str, abs_source, abs_target, (node_1->error_count-temp_errors));
                            fflush(log_file);
                            printf("%s Sync completed %s -> %s Errors:%d\n\n", time_str, abs_source, abs_target, (node_1->error_count-temp_errors));
                            sprintf(str_to_console, "%s Sync completed %s -> %s Errors:%d\n\n", time_str, abs_source, abs_target, (node_1->error_count-temp_errors));
                            send_to_console(str_to_console);
                            sprintf(str_to_console, "close");
                            send_to_console(str_to_console);
                            free(abs_target);
                        } else if(strcmp(node->status,"Active") == 0) { //if the match is in the running queue and active
                            if (strcmp(node->active,"YES") == 0) { //if the match is in the running queue and is running
                                time_format(time_str, sizeof(time_str));
                                char *abs_source = realpath(node->source_dir, NULL);
                                printf("%s Sync already in progress: %s\n\n", time_str, abs_source);
                                sprintf(str_to_console, "%s Sync already in progress: %s\n\n", time_str, abs_source);
                                send_to_console(str_to_console);
                                sprintf(str_to_console, "close");
                                send_to_console(str_to_console);

                            } else { //if the match is in the running queue but is not running
                                time_format(time_str, sizeof(time_str));                            
                                char *abs_target = realpath(node->target_dir, NULL);
                                fprintf(log_file, "%s Syncing directory: %s -> %s\n\n", time_str, abs_source, abs_target);
                                fflush(log_file);
                                printf("%s Syncing directory: %s -> %s\n", time_str, abs_source, abs_target);
                                sprintf(str_to_console, "%s Syncing directory: %s -> %s\n", time_str, abs_source, abs_target);
                                send_to_console(str_to_console);
                                
                                int temp_errors = node->error_count;
                                spawn_worker(node->watch_disc_inot,node->source_dir,node->target_dir,"ALL", "FULL", &Q_D, &R_D);

                                while(run_wd_active_yes(node->watch_disc_inot,&R_D) == 1) {
                                    sleep(100);
                                }
    
                                running_DequeNode *node_1 = run_get_source_info(parameters, &R_D);
                                time_format(time_str, sizeof(time_str));
                                fprintf(log_file, "%s Sync completed %s -> %s Errors:%d\n\n", time_str, abs_source, abs_target, (node_1->error_count-temp_errors));
                                fflush(log_file);
                                printf("%s Sync completed %s -> %s Errors:%d\n\n", time_str, abs_source, abs_target, (node_1->error_count-temp_errors));
                                sprintf(str_to_console, "%s Sync completed %s -> %s Errors:%d\n\n", time_str, abs_source, abs_target, (node_1->error_count-temp_errors));
                                free(abs_target);
                                send_to_console(str_to_console);

                                sprintf(str_to_console, "close");
                                send_to_console(str_to_console);
                            }
                        } else { //not used
                            time_format(time_str, sizeof(time_str));
                            char *abs_source = realpath(node->source_dir, NULL);
                            printf("%s Sync already in progress: %s\n\n", time_str, abs_source);
                            sprintf(str_to_console, "%s Sync already in progress: %s\n\n", time_str, abs_source);
                            send_to_console(str_to_console);
                            sprintf(str_to_console, "close");
                            send_to_console(str_to_console);
                        }
                        free(abs_source);
                    } else { //source directory is not in the queue
                        time_format(time_str, sizeof(time_str));
                        printf("%s Directory not monitored: %s\n\n", time_str, parameters);
                        sprintf(str_to_console, "%s Directory not monitored: %s\n\n", time_str, parameters);
                        send_to_console(str_to_console);
                        sprintf(str_to_console, "close");
                        send_to_console(str_to_console);
                    }
                }
            } else if(strcmp(command, "shutdown") == 0) {
                flag = 1;

                time_format(time_str, sizeof(time_str));
                printf("%s Shutting down manager...\n", time_str);
                sprintf(str_to_console, "%s Shutting down manager...\n", time_str);
                send_to_console(str_to_console);
                printf("%s Waiting for all active workers to finish.\n", time_str);
                sprintf(str_to_console, "%s Waiting for all active workers to finish.\n", time_str);
                send_to_console(str_to_console);

                run_close_all_watch_descriptors(inotify_fd, &R_D); //closing all the watch descriptors
                close(inotify_fd);

                time_format(time_str, sizeof(time_str));
                printf("%s Processing remaining queued tasks.\n", time_str);
                sprintf(str_to_console, "%s Processing remaining queued tasks.\n", time_str);
                send_to_console(str_to_console);

                while (run_running_count(&R_D) != 0 ) {
                    // printf("%s Waiting for all active workers to finish.\n", time_str);
                    sleep(5);
                }

                time_format(time_str, sizeof(time_str));
                printf("%s Manager shutdown complete.\n", time_str);
                sprintf(str_to_console, "%s ", time_str);
                send_to_console(str_to_console);
                sprintf(str_to_console, "Manager shutdown complete.\n");
                send_to_console(str_to_console);
                sprintf(str_to_console, "close");
                send_to_console(str_to_console);
                break;
            }
    
            if(flag == 0) {
                printf("FALSE COMMAND ---> NOT: add,cancel,status,sync,shutdown ---> %s\n\n", read_str);
                sprintf(str_to_console,"FALSE COMMAND ---> NOT: add,cancel,status,sync,shutdown --->  %s\n\n", read_str);
                send_to_console(str_to_console);
                sprintf(str_to_console, "close");
                send_to_console(str_to_console);
            }
        }

        if(fds[1].revents & POLLIN) { // Inotify event is ready
            length = read(inotify_fd, buffer, EVENT_BUF_LEN);
            if (length < 0) {
                printf("read\n");
                exit(EXIT_FAILURE);
            }

            while (i < length) {
                struct inotify_event *event = (struct inotify_event *) &buffer[i];
                if (event->len) {
                    running_DequeNode *node = run_wd_exists(event->wd, &R_D);
                    if (node != NULL && strcmp(node->status,"Active") == 0) { // Check if the event is for a monitored directory
                        char operation[10];
                        if (event->mask & IN_CREATE) {
                            strcpy(operation, "ADDED");
                        } else if (event->mask & IN_CLOSE_WRITE || event->mask & IN_MODIFY ) {  // } else if (event->mask & IN_MODIFY) {
                            strcpy(operation, "MODIFIED");
                        } else if (event->mask & IN_DELETE) {
                            strcpy(operation, "DELETED");
                        }
                        printf("\nEvent: %s, Filename: %s\n", operation, event->name);
                        if(strcmp(node->status,"Active") == 0) {
                            spawn_worker(event->wd,node->source_dir, node->target_dir, event->name, operation, &Q_D, &R_D);
                        }
                        break;
                    }
                }
                i += EVENT_SIZE + event->len;
            }
            i = 0;
        }
    }

    actually_close_descriptors();

    return 0;
}