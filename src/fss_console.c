//./fss_console -l <console_log_file>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include "variables.h"


// Function to get the current time as a formatted string
void time_format(char *buffer, size_t size) {
    time_t t_time;
    struct tm *bd_time;
    time(&t_time);
    bd_time = localtime(&t_time);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", bd_time);
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
        printf("\nfss_console::::wrong number of arguments!!\n");
        return -2;
    }

    char console_log_file[max__size];

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-l") == 0) {
            strcpy(console_log_file, argv[i + 1]);

        }
    }

    // creating/opening the log file
    FILE *log_file = fopen(console_log_file, "w");
    if (!log_file) {
        perror("Error opening log file");
        return -9;
    }

    // fprintf(log_file, "\n-----------------------------------------\n");
    // fflush(log_file);

    // Print the parsed arguments as strings
    printf("\nconsole_log_file: %s\n", console_log_file);


    int write_fd, read_fd;
    char write_arg[max__size], read_arg[max__size], time_str[30];
    int flag = 0;

    while(1) {
        printf("\nEnter input (add/cancel/status/sync/shutdown): ");
        fgets(write_arg, max__size, stdin);
        write_arg[strcspn(write_arg, "\n")] = '\0'; // Remove newline character

        time_format(time_str, sizeof(time_str));
        fprintf(log_file, "%s %s\n", time_str, write_arg);

        if ((write_fd = open(fifo1, O_WRONLY)) < 0) {
            printf("\ncan't open-write fifo1\n");
            return -3;
        }

        if (write(write_fd, write_arg, max__size) == -1) { // Writing to server
            printf("\nCan't write to fifo1\n");
            return -4;
        }
        close(write_fd);

        if ((read_fd = open(fifo2, O_RDONLY )) < 0) {
            printf("\nCan't open-read fifo2\n");
            return -5;
        }

        while(1) {
            if (read(read_fd, read_arg, max__size) < 0) { // Reading from server
                printf("\nCan't read from fifo2\n");
                return -6;
            }
            // close(read_fd);

            if(strcmp(read_arg, "close") != 0) {
                printf("%s", read_arg);
                // time_format(time_str, sizeof(time_str));
                fprintf(log_file, "%s", read_arg);
                fflush(log_file); // log is written immediately
            }
            
            
            if(strcmp(read_arg, "Manager shutdown complete.\n") == 0) {
                flag = 1;
            }

            // Check if the command is "shutdown"
            if (strcmp(read_arg, "close") == 0) {
                printf("\n");
                fprintf(log_file, "\n");
                break;
            }
        }
        read_arg[0] = '\0'; // reset buffer
        close(read_fd);
        if (flag == 1) {
            break; // Exit the loop if "Manager shutdown complete.\n" command is received
        }
    }

    
    // close(read_fd);
    fclose(log_file);
    return 0;
}
