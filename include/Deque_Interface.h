#include <linux/limits.h>

typedef struct que_DequeNodeTag *que_Dequepointer;

typedef struct que_DequeNodeTag {
    int quequePositon;
    int watch_disc_inot;
    char source_dir[PATH_MAX];
    char target_dir[PATH_MAX];
    char filename[PATH_MAX];
    char operation[PATH_MAX];
    struct que_DequeNodeTag *Link;
} queued_DequeNode;


typedef struct que_DequeTag {
    queued_DequeNode *Front;
    queued_DequeNode *Rear;
    int number_of_items;
} queued_Deque;


void que_Create(queued_Deque *); //Creates an empty Deque
int que_SizeOf(queued_Deque *); //returns the number of items of that are in the Deque
void que_Enqueue(int watch_disc_inot,char *source_dir,char *target_dir,char *filename,char *operation, queued_Deque *D); //inserts an item in the end of the queue
void que_Pop(queued_Deque *D); //removes an item from the start of the queue 
int que_find_source(char *,queued_Deque *); //checks if the source_dir is in the queue
queued_DequeNode* que_get_source_info(char *source_dir, queued_Deque *D);


typedef struct run_DequeNodeTag {
    pid_t pid;
    int watch_disc_inot;
    int quequePositon;
    char source_dir[PATH_MAX];
    char target_dir[PATH_MAX];
    char status[10];
    char active[5];
    char last_sync_time[30];
    int error_count;
    struct run_DequeNodeTag *Link;
} running_DequeNode;

typedef struct run_DequeTag {
    running_DequeNode *Front;
    running_DequeNode *Rear;
    int number_of_items;
    int being_watched_count;
    int running_count;
} running_Deque;

typedef struct run_DequeNodeTag *run_Dequepointer;
void run_Create(running_Deque *); //Creates an empty Deque
int run_SizeOf(running_Deque *); //returns the number of items of that are in the Deque
int run_running_count(running_Deque* D); //returns the number of items of that are in the Deque
int run_being_watched_count(running_Deque* D); //returns the number of items of that are in the Deque
void run_Enqueue(pid_t pid,int watch_disc_inot,char *source_dir,char *target_dir,char *status,char *active,char *last_sync_time,int error_count, running_Deque *D); //inserts an item in the end of the queue
int run_being_watched_count(running_Deque *); //returns the number of items of that are in the Deque
void run_make_pid_active_to_no(pid_t id,running_Deque *D); //sets the status of the job to inactive
int run_find_source(char *source_dir,running_Deque *D); //checks if the source_dir is in the queue
running_DequeNode* run_wd_exists(int watch_disc,running_Deque *D);
void run_close_all_watch_descriptors(int fd ,running_Deque *D);
int run_cancel_source(int fd, char *source_dir,running_Deque *D);
running_DequeNode* run_get_source_info(char *source_dir,running_Deque *D);
void run_make_wd_active_to_yes(pid_t pid,int wd,running_Deque *D); //sets the status of the job to inactive
running_DequeNode *run_find_source_with_target(char *source_dir,char *target_dir,running_Deque *D); //checks if the source_dir and target_dir are in the queue
void run_set_status_to_active(char *source_dir,running_Deque *D); //sets the status of the job to inactive
void run_reduce_running_count(running_Deque *D); //reduces the running count of the running queue
void run_update_pid_error_count(pid_t id,int error_count,running_Deque *D); //updates the error count of the job
int run_wd_active_yes(int wd,running_Deque *D); //checks if the watch descriptor is active
