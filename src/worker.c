// worker.c    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/limits.h>

#define BUFFER_SIZE 4096
#define ERROR_BUFFER_MAX 4096



int copy_file_report(const char *source, const char *target, char *error_msg, size_t msg_size) {
    int src_fd = -1;
    int trgt_fd = -1;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    int success = 1; // Assumes success until proven otherwise

    src_fd = open(source, O_RDONLY);
    if (src_fd == -1) {
        snprintf(error_msg, msg_size, "%s",strerror(errno));
	if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        return -1;
    }

    trgt_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (trgt_fd == -1) {
        snprintf(error_msg, msg_size, "Error opening target file %s: %s", target, strerror(errno));
	if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        close(src_fd); // Close source file before returning
        return -1;
    }

    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        char *ptr = buffer;
        ssize_t remaining = bytes_read;

        while (remaining > 0) {
            bytes_written = write(trgt_fd, ptr, remaining);
            if (bytes_written == -1) {
                if (errno == EINTR) {
                    continue; // Retry the write call
                }
                snprintf(error_msg, msg_size, "Error writing to target file %s: %s", target, strerror(errno));
		if (msg_size > 0) error_msg[msg_size - 1] = '\0';
                success = 0; // Mark as failed
                break;       // Exit inner write loop
            }
            if (bytes_written == 0) { // No bytes written
                snprintf(error_msg, msg_size, "Error writing to target file %s: Wrote 0 bytes unexpectedly", target);
		if (msg_size > 0) error_msg[msg_size - 1] = '\0';
                success = 0;
                break;
            }
            remaining -= bytes_written;
            ptr += bytes_written;
        }
        if (!success) {
            break; // Exit outer read loop if write failed
        }
    }

    // Checking read error after the loop
    if (bytes_read == -1) {
        snprintf(error_msg, msg_size, "Error reading from source file %s: %s", source, strerror(errno));
	if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        success = 0;
    }

    // Error handling for closing files
    if (close(src_fd) == -1 && success) { // Report close error only if no other error occurred
        snprintf(error_msg, msg_size, "Error closing source file %s: %s", source, strerror(errno));
	if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        success = 0;
    }
    if (close(trgt_fd) == -1 && success) {
        snprintf(error_msg, msg_size, "Error closing target file %s: %s", target, strerror(errno));
	if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        success = 0;
    }

    if (success) {
        error_msg[0] = '\0'; // Clear error message on success
        return 0;
    } else {
        if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        return -1;
    }
}


int delete_file_report(const char *target, char *error_msg, size_t msg_size) {
    if (unlink(target) == -1) {
        snprintf(error_msg, msg_size, "Error deleting file %s: %s", target, strerror(errno));
	if (msg_size > 0) error_msg[msg_size - 1] = '\0';
        return -1;
    }
    error_msg[0] = '\0'; // Clear error message on success
    return 0;
}


// Function to append error messages to the error buffer
static void append_error_c(char *error_buffer, size_t buffer_max, size_t *current_len, const char *err_str) {
    size_t needed = strlen(err_str);
    int add_newline = 0;
    
    if (*current_len > 0 && error_buffer[*current_len - 1] != '\n') {
        add_newline = 1;
        needed += 1;
    }
    // Add "- " prefix
    needed += 2;

    // Logging error message
    if (*current_len + needed < buffer_max ) { // Check bounds (-1 for null terminator)
        if (add_newline) {
	    if (*current_len < buffer_max - 1) {
                error_buffer[*current_len] = '\n';
                (*current_len)++;
		if (buffer_max > 0) error_buffer[*current_len] = '\0';
	    } else{
		return;
	    }
        }
	if (*current_len < buffer_max -2 ) {
        	strncpy(error_buffer + *current_len, "- ", 2);
        	*current_len += 2;
		if (buffer_max > 0) error_buffer[*current_len] = '\0';
	} else{
		return;
	}
	if ( (buffer_max > 0) && ( *current_len + strlen(err_str) < buffer_max)) {
           strncpy(error_buffer + *current_len, err_str, buffer_max - *current_len - 1);
            *current_len += strlen(error_buffer + *current_len);
            if (buffer_max > 0) error_buffer[*current_len] = '\0';
        }
	else if (buffer_max > 0) {
	    strncpy(error_buffer + *current_len, err_str, buffer_max - *current_len -1);
	    error_buffer[buffer_max -1] = '\0';

	    *current_len = buffer_max -1;
	}
	if (buffer_max > 0) error_buffer[*current_len] = '\0';
    } else if (*current_len < buffer_max - 25) { // Add overflow only if space
        const char *overflow_msg = "\n- ... (error buffer full)";
        
        if (*current_len + strlen(overflow_msg) < buffer_max) { // Check if overflow message itself fits
            strncpy(error_buffer + *current_len, overflow_msg, buffer_max - *current_len - 1);
            *current_len += strlen(error_buffer + *current_len);
        }
	if (buffer_max > 0) error_buffer[*current_len] = '\0';
    }
}

int main(int argc, char *argv[]) {
    
    // Arguments expected:
    // argv[0]: program name ("./worker")
    // argv[1]: source_directory
    // argv[2]: target_directory
    // argv[3]: filename (specific file, or "ALL" for FULL sync)
    // argv[4]: operation ("FULL", "ADDED", "MODIFIED", "DELETED")
    if (argc != 5) {
        fprintf(stderr, "[Worker] Usage: %s <src_dir> <tgt_dir> <filename> <operation>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *source_directory = argv[1];
    char *target_directory = argv[2];
    char *filename = argv[3];
    char *operation = argv[4];
    
    
    int files_processed = 0,files_copied = 0,files_deleted = 0,files_skipped = 0;
    char error_buffer[ERROR_BUFFER_MAX] = "";
    size_t error_buffer_len = 0;
    char current_error[ERROR_BUFFER_MAX]; // Buffer for every single error message
    int error_count = 0; // Initialize the error count

    if (strcmp(operation, "FULL") == 0) { // Full sync
        DIR *dir = opendir(source_directory);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                // Skip '.' and '..' directories
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                char source_path[PATH_MAX-30];
                char target_path[PATH_MAX-30];

                snprintf(source_path, PATH_MAX-30, "%s/%s", source_directory, entry->d_name);
                snprintf(target_path, PATH_MAX-30, "%s/%s", target_directory, entry->d_name);

                
                source_path[PATH_MAX - 31] = '\0'; //ensuring null termination beacause snprintf doesn't guarantee it
                target_path[PATH_MAX - 31] = '\0';

                // Check if it's a regular file before attempting copy
                struct stat st;
                if (stat(source_path, &st) == 0) {
                    if (S_ISREG(st.st_mode)) { // if it is a regular file
                        files_processed++;
                        if (copy_file_report(source_path, target_path, current_error, sizeof(current_error)) == 0) {
                            files_copied++;
                        } else {
                            // char final_error[ERROR_BUFFER_MAX+100];
                            // snprintf(final_error, ERROR_BUFFER_MAX, " File %s : %s", entry->d_name, current_error); 
                            // files_skipped++;
                            // append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, final_error);
                            // error_count++; // Increment error count
                            char final_error[ERROR_BUFFER_MAX+500]; // Ensure final_error is appropriately sized.
                            snprintf(final_error, sizeof(final_error), " File %s : %s", entry->d_name, current_error); //Correct size
			                if (sizeof(final_error) > 0) final_error[sizeof(final_error)-1] = '\0'; // Ensure null termination
                            files_skipped++;
                            append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, final_error);
                            error_count++; // Increment error count
                        }
                    } else {
                        files_skipped++;
                        snprintf(current_error, sizeof(current_error), "Skipping non-regular file: %s", source_path);
			if(sizeof(current_error) > 0 ) current_error[sizeof(current_error) - 1] = '\0';
                        append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
                    }
                } else { // Error stating the source file - might be permission denied or non-existent during check
                     files_processed++; // Count as processed but failed
                     files_skipped++;
                     snprintf(current_error, sizeof(current_error), "Error stating source file %s: %s", source_path, strerror(errno));
		     if(sizeof(current_error) > 0 ) current_error[sizeof(current_error) - 1] = '\0';
                     append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
                     error_count++; // Increment error count
                }
            }
            closedir(dir);
        } else { // Error opening the source directory
            files_skipped++; // Count this as a failure
            snprintf(current_error, sizeof(current_error), "Error opening source directory %s: %s", source_directory, strerror(errno));
	    if(sizeof(current_error) > 0 ) current_error[sizeof(current_error) - 1] = '\0';
            append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
            error_count++;
            files_processed = 0; // No files could be processed
        }
    } else if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
        char source_path[PATH_MAX];
        char target_path[PATH_MAX];
        snprintf(source_path, PATH_MAX, "%s/%s", source_directory, filename);
        snprintf(target_path, PATH_MAX, "%s/%s", target_directory, filename);
        source_path[PATH_MAX - 1] = '\0'; //ensuring null termination beacause snprintf doesn't guarantee it
        target_path[PATH_MAX - 1] = '\0';

        files_processed = 1;
        if (copy_file_report(source_path, target_path, current_error, sizeof(current_error)) == 0) {
            files_copied = 1;
        } else {
            files_skipped = 1;
	    if(sizeof(current_error) > 0 ) current_error[sizeof(current_error) - 1] = '\0';
            append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
            error_count++; // Increment error count
        }
    } else if (strcmp(operation, "DELETED") == 0) {
        char target_path[PATH_MAX];
        snprintf(target_path, PATH_MAX, "%s/%s", target_directory, filename);
        target_path[PATH_MAX - 1] = '\0'; // Ensure null termination

        files_processed = 1;
        if (delete_file_report(target_path, current_error, sizeof(current_error)) == 0) {
            files_deleted = 1;
        } else { // Error deleting the file
            files_skipped = 1;
	    if(sizeof(current_error) > 0 ) current_error[sizeof(current_error) - 1] = '\0';
            append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
            error_count++; // Increment error count
        }
    } else { // Unknown operation
        files_skipped = 1; // Indicate failure
        snprintf(current_error, sizeof(current_error), "Unknown operation requested: %s", operation);
	if(sizeof(current_error) > 0 ) current_error[sizeof(current_error) - 1] = '\0';
        append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
        error_count++; // Increment error count
    }

    // Generating report
    char details_buffer[200];
    const char *status = (files_skipped > 0) ? "PARTIAL" : "SUCCESS"; // Simplified status
    const char *report_filename = (strcmp(operation, "FULL") == 0) ? "ALL" : filename;

    // Adjust status if needed
    if ((files_processed == 0 && files_skipped > 0) || (files_skipped > 0 && files_copied == 0)) {
         status = "ERROR"; // Sticking to requested terms, signifies attempt failed
    }

    // Format details string based on operation type
    if (strcmp(operation, "FULL") == 0) {
        snprintf(details_buffer, sizeof(details_buffer), "%d files copied, %d skipped", files_copied, files_skipped);
    } else if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
        snprintf(details_buffer, sizeof(details_buffer), "%d file copied, %d skipped", files_copied, files_skipped);
    } else if (strcmp(operation, "DELETED") == 0) {
        snprintf(details_buffer, sizeof(details_buffer), "%d file deleted, %d skipped", files_deleted, files_skipped);
    } else {
        snprintf(details_buffer, sizeof(details_buffer), "0 files processed, 1 operation failed");
    }
    details_buffer[sizeof(details_buffer)-1] = '\0'; // Ensure null termination

    // Print the report in stdout
    printf("\nEXEC_REPORT_START\n");
    printf("SOURCE_DIR: %s\n", source_directory);
    printf("TARGET_DIR: %s\n", target_directory);
    printf("FILENAME: %s\n", report_filename);
    printf("OPERATION: %s\n", operation);
    printf("STATUS: %s\n", status);
    printf("DETAILS: %s\n", details_buffer);
    printf("ERROR COUNT: %d\n", error_count); // Print error count
    printf("ERRORS:\n"); //
    if (error_buffer_len > 0) {
        printf("%s", error_buffer); // print all found errors
        // Ensure the errors section ends with a newline if not empty
        if (error_buffer[strlen(error_buffer) - 1] != '\n') {
            printf("\n");
        }
    } else {
        printf("- None\n"); //if there were no errors found !
    }
    printf("EXEC_REPORT_END\n\n");
    fflush(stdout); // Making sure the output is flushed to stdout

    return 0;
}






// // worker.c    
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <errno.h>
// #include <dirent.h>
// #include <sys/stat.h>
// #include <linux/limits.h>

// #define BUFFER_SIZE 4096
// #define ERROR_BUFFER_MAX 4096



// int copy_file_report(const char *source, const char *target, char *error_msg, size_t msg_size) {
//     int src_fd = -1;
//     int trgt_fd = -1;
//     char buffer[BUFFER_SIZE];
//     ssize_t bytes_read, bytes_written;
//     int success = 1; // Assumes success until proven otherwise

//     src_fd = open(source, O_RDONLY);
//     if (src_fd == -1) {
//         snprintf(error_msg, msg_size, "%s",strerror(errno));
//         return -1;
//     }

//     trgt_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
//     if (trgt_fd == -1) {
//         snprintf(error_msg, msg_size, "Error opening target file %s: %s", target, strerror(errno));
//         close(src_fd); // Close source file before returning
//         return -1;
//     }

//     while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
//         char *ptr = buffer;
//         ssize_t remaining = bytes_read;

//         while (remaining > 0) {
//             bytes_written = write(trgt_fd, ptr, remaining);
//             if (bytes_written == -1) {
//                 if (errno == EINTR) {
//                     continue; // Retry the write call
//                 }
//                 snprintf(error_msg, msg_size, "Error writing to target file %s: %s", target, strerror(errno));
//                 success = 0; // Mark as failed
//                 break;       // Exit inner write loop
//             }
//             if (bytes_written == 0) { // No bytes written
//                 snprintf(error_msg, msg_size, "Error writing to target file %s: Wrote 0 bytes unexpectedly", target);
//                 success = 0;
//                 break;
//             }
//             remaining -= bytes_written;
//             ptr += bytes_written;
//         }
//         if (!success) {
//             break; // Exit outer read loop if write failed
//         }
//     }

//     // Checking read error after the loop
//     if (bytes_read == -1) {
//         snprintf(error_msg, msg_size, "Error reading from source file %s: %s", source, strerror(errno));
//         success = 0;
//     }

//     // Error handling for closing files
//     if (close(src_fd) == -1 && success) { // Report close error only if no other error occurred
//         snprintf(error_msg, msg_size, "Error closing source file %s: %s", source, strerror(errno));
//         success = 0;
//     }
//     if (close(trgt_fd) == -1 && success) {
//         snprintf(error_msg, msg_size, "Error closing target file %s: %s", target, strerror(errno));
//         success = 0;
//     }

//     if (success) {
//         error_msg[0] = '\0'; // Clear error message on success
//         return 0;
//     } else {
//         error_msg[msg_size - 1] = '\0';
//         return -1;
//     }
// }


// int delete_file_report(const char *target, char *error_msg, size_t msg_size) {
//     if (unlink(target) == -1) {
//         snprintf(error_msg, msg_size, "Error deleting file %s: %s", target, strerror(errno));
//         return -1;
//     }
//     error_msg[0] = '\0'; // Clear error message on success
//     return 0;
// }


// // Function to append error messages to the error buffer
// static void append_error_c(char *error_buffer, size_t buffer_max, size_t *current_len, const char *err_str) {
//     size_t needed = strlen(err_str);
//     int add_newline = 0;
    
//     if (*current_len > 0 && error_buffer[*current_len - 1] != '\n') {
//         add_newline = 1;
//         needed += 1;
//     }
//     // Add "- " prefix
//     needed += 2;

//     // Logging error message
//     if (*current_len + needed < buffer_max - 1 ) { // Check bounds (-1 for null terminator)
//         if (add_newline) {
//             strcat(error_buffer, "\n");
//             (*current_len)++;
//         }
//         strcat(error_buffer, "- ");
//         strcat(error_buffer, err_str);
//         *current_len += (needed - add_newline);
//     } else if (*current_len < buffer_max - 25) { // Add overflow only if space
//         const char *overflow_msg = "\n- ... (error buffer full)";
        
//         if (*current_len + strlen(overflow_msg) < buffer_max -1) { // Check if overflow message itself fits
//              strcat(error_buffer, overflow_msg);
//              *current_len += strlen(overflow_msg);
//         }
//     }
// }

// int main(int argc, char *argv[]) {
    
//     // Arguments expected:
//     // argv[0]: program name ("./worker")
//     // argv[1]: source_directory
//     // argv[2]: target_directory
//     // argv[3]: filename (specific file, or "ALL" for FULL sync)
//     // argv[4]: operation ("FULL", "ADDED", "MODIFIED", "DELETED")
//     if (argc != 5) {
//         fprintf(stderr, "[Worker] Usage: %s <src_dir> <tgt_dir> <filename> <operation>\n", argv[0]);
//         exit(EXIT_FAILURE);
//     }

//     char *source_directory = argv[1];
//     char *target_directory = argv[2];
//     char *filename = argv[3];
//     char *operation = argv[4];
    
    
//     int files_processed = 0,files_copied = 0,files_deleted = 0,files_skipped = 0;
//     char error_buffer[ERROR_BUFFER_MAX] = "";
//     size_t error_buffer_len = 0;
//     char current_error[PATH_MAX]; // Buffer for every single error message
//     int error_count = 0; // Initialize the error count

//     if (strcmp(operation, "FULL") == 0) { // Full sync
//         DIR *dir = opendir(source_directory);
//         if (dir) {
//             struct dirent *entry;
//             while ((entry = readdir(dir)) != NULL) {
//                 // Skip '.' and '..' directories
//                 if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
//                     continue;
//                 }
//                 char source_path[PATH_MAX];
//                 char target_path[PATH_MAX];

//                 snprintf(source_path, PATH_MAX, "%s/%s", source_directory, entry->d_name);
//                 snprintf(target_path, PATH_MAX, "%s/%s", target_directory, entry->d_name);

                
//                 source_path[PATH_MAX - 1] = '\0'; //ensuring null termination beacause snprintf doesn't guarantee it
//                 target_path[PATH_MAX - 1] = '\0';

//                 // Check if it's a regular file before attempting copy
//                 struct stat st;
//                 if (stat(source_path, &st) == 0) {
//                     if (S_ISREG(st.st_mode)) { // if it is a regular file
//                         files_processed++;
//                         if (copy_file_report(source_path, target_path, current_error, sizeof(current_error)) == 0) {
//                             files_copied++;
//                         } else {
//                             // char final_error[ERROR_BUFFER_MAX+100];
//                             // snprintf(final_error, ERROR_BUFFER_MAX, " File %s : %s", entry->d_name, current_error); 
//                             // files_skipped++;
//                             // append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, final_error);
//                             // error_count++; // Increment error count
//                             char final_error[ERROR_BUFFER_MAX]; // Ensure final_error is appropriately sized.
//                             snprintf(final_error, sizeof(final_error), " File %s : %s", entry->d_name, current_error); //Correct size
//                             final_error[sizeof(final_error) - 1] = '\0'; // Ensure null termination
//                             files_skipped++;
//                             append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, final_error);
//                             error_count++; // Increment error count
//                         }
//                     } else {
//                         files_skipped++;
//                         snprintf(current_error, sizeof(current_error), "Skipping non-regular file: %s", source_path);
//                         append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
//                     }
//                 } else { // Error stating the source file - might be permission denied or non-existent during check
//                      files_processed++; // Count as processed but failed
//                      files_skipped++;
//                      snprintf(current_error, sizeof(current_error), "Error stating source file %s: %s", source_path, strerror(errno));
//                      append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
//                      error_count++; // Increment error count
//                 }
//             }
//             closedir(dir);
//         } else { // Error opening the source directory
//             files_skipped++; // Count this as a failure
//             snprintf(current_error, sizeof(current_error), "Error opening source directory %s: %s", source_directory, strerror(errno));
//             append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
//             error_count++;
//             files_processed = 0; // No files could be processed
//         }
//     } else if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
//         char source_path[PATH_MAX];
//         char target_path[PATH_MAX];
//         snprintf(source_path, PATH_MAX, "%s/%s", source_directory, filename);
//         snprintf(target_path, PATH_MAX, "%s/%s", target_directory, filename);
//         source_path[PATH_MAX - 1] = '\0';
//         target_path[PATH_MAX - 1] = '\0';
//         files_processed = 1;
//         if (copy_file_report(source_path, target_path, current_error, sizeof(current_error)) == 0) {
//             files_copied = 1;
//         } else {
//             files_skipped = 1;
//             append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
//             error_count++; // Increment error count
//         }
//     } else if (strcmp(operation, "DELETED") == 0) {
//         char target_path[PATH_MAX];
//         snprintf(target_path, PATH_MAX, "%s/%s", target_directory, filename);
//         target_path[PATH_MAX - 1] = '\0'; // Ensure null termination

//         files_processed = 1;
//         if (delete_file_report(target_path, current_error, sizeof(current_error)) == 0) {
//             files_deleted = 1;
//         } else { // Error deleting the file
//             files_skipped = 1;
//             append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
//             error_count++; // Increment error count
//         }
//     } else { // Unknown operation
//         files_skipped = 1; // Indicate failure
//         snprintf(current_error, sizeof(current_error), "Unknown operation requested: %s", operation);
//         append_error_c(error_buffer, ERROR_BUFFER_MAX, &error_buffer_len, current_error);
//         error_count++; // Increment error count
//     }

//     // Generating report
//     char details_buffer[200];
//     const char *status = (files_skipped > 0) ? "PARTIAL" : "SUCCESS"; // Simplified status
//     const char *report_filename = (strcmp(operation, "FULL") == 0) ? "ALL" : filename;

//     // Adjust status if needed
//     if ((files_processed == 0 && files_skipped > 0) || (files_skipped > 0 && files_copied == 0)) {
//          status = "ERROR"; // Sticking to requested terms, signifies attempt failed
//     }

//     // Format details string based on operation type
//     if (strcmp(operation, "FULL") == 0) {
//         snprintf(details_buffer, sizeof(details_buffer), "%d files copied, %d skipped", files_copied, files_skipped);
//     } else if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
//         snprintf(details_buffer, sizeof(details_buffer), "%d file copied, %d skipped", files_copied, files_skipped);
//     } else if (strcmp(operation, "DELETED") == 0) {
//         snprintf(details_buffer, sizeof(details_buffer), "%d file deleted, %d skipped", files_deleted, files_skipped);
//     } else {
//         snprintf(details_buffer, sizeof(details_buffer), "0 files processed, 1 operation failed");
//     }
//     details_buffer[sizeof(details_buffer)-1] = '\0'; // Ensure null termination

//     // Print the report in stdout
//     printf("\nEXEC_REPORT_START\n");
//     printf("SOURCE_DIR: %s\n", source_directory);
//     printf("TARGET_DIR: %s\n", target_directory);
//     printf("FILENAME: %s\n", report_filename);
//     printf("OPERATION: %s\n", operation);
//     printf("STATUS: %s\n", status);
//     printf("DETAILS: %s\n", details_buffer);
//     printf("ERROR COUNT: %d\n", error_count); // Print error count
//     printf("ERRORS:\n"); //
//     if (error_buffer_len > 0) {
//         printf("%s", error_buffer); // print all found errors
//         // Ensure the errors section ends with a newline if not empty
//         if (error_buffer[error_buffer_len - 1] != '\n') {
//             printf("\n");
//         }
//     } else {
//         printf("- None\n"); //if there were no errors found !
//     }
//     printf("EXEC_REPORT_END\n\n");
//     fflush(stdout); // Making sure the output is flushed to stdout
// }