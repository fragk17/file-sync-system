#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include "Deque_Interface.h"


void que_Create(queued_Deque *D) {
    D->Front=NULL;
    D->Rear=NULL;
    D->number_of_items = 0;
}

int que_SizeOf(queued_Deque* D)
{
    return D->number_of_items;
}


void que_Enqueue(int watch_disc_inot,char *source_dir,char *target_dir,char *filename,char *operation, queued_Deque *D) {
    queued_DequeNode *Temp;
    Temp = (queued_DequeNode *)malloc(sizeof(queued_DequeNode)); //allocates memory for the new node

    //printf("\nque_Enqueue: \nwd:%d\nsource:%s\ntarget:%s\nfilename:%s\noperation:%s\n\n",watch_disc_inot,source_dir,target_dir,filename,operation);
    
    if (Temp==NULL) printf("the memory is full\n");
    else { //else it sets the values of the new node and adds it to the end of the Deque
        Temp->Link=NULL;
        strcpy(Temp->source_dir,source_dir);
        strcpy(Temp->target_dir,target_dir);
        strcpy(Temp->filename,filename);
        strcpy(Temp->operation,operation);
        Temp->watch_disc_inot = watch_disc_inot;
        if(D->Rear==NULL) { //if the Deque is previously empty it sets the front and last item of the Deque as the first one  
            D->Front=Temp; 
            D->Rear=Temp;
        }
        else {
            D->Rear->Link=Temp;
            D->Rear=Temp;
        }
        (D->number_of_items)++; //adds 1 to the numbers of items that are in the Deque
        Temp->quequePositon = D->number_of_items;
    }
}
    


void que_Pop(queued_Deque *D) {
    queued_DequeNode *Temp;
    if(D->Front == NULL) printf("\nattempting to pop an empty deque(que_Pop)\n");
    else {
        Temp=D->Front;
        D->Front=Temp->Link;
        free(Temp);
        if (D->Front == NULL) D->Rear = NULL; //if the last item was deleted it sets the pointer to the rear item of the deque to null
        (D->number_of_items)--; //reduces the number of items by 1 
    }
    if(D->Front != NULL) {
        queued_DequeNode *Temp1;
        Temp1 = D->Front;
        Temp1->quequePositon = 1;
        int i = 2;
        while(Temp1->Link!=NULL) {
            Temp1 = Temp1->Link;
            Temp1->quequePositon = i;
            i++;
        }
    }
}

queued_DequeNode* que_get_source_info(char *source_dir, queued_Deque *D) {
    if(D->Front==NULL) {
        return NULL;
    }
    else {
        queued_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0) {
                return curr;
            }
            curr = curr->Link;
        }
    }
    return NULL;
}


int que_find_source(char *source_dir,queued_Deque *D) {
    if(D->Front==NULL) {
        return 0;
    }
    else {
        queued_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0) {
                return 1;
            }
            curr = curr->Link;
        }
    }
    return 0;
}


//------------
//RUNNING QUEUE
//------------



void run_Create(running_Deque *D) {
    D->Front=NULL;
    D->Rear=NULL;
    D->number_of_items = 0;
}

int run_SizeOf(running_Deque* D)
{
    return D->number_of_items;
}

int run_being_watched_count(running_Deque* D)
{
    return D->being_watched_count;
}

int run_running_count(running_Deque* D)
{
    return D->running_count;
}


void run_Enqueue(pid_t pid,int watch_disc_inot,char *source_dir,char *target_dir,char *status,char *active,char *last_sync_time,int error_count, running_Deque *D) {
    running_DequeNode *Temp;
    Temp = (running_DequeNode *)malloc(sizeof(running_DequeNode)); //allocates memory for the new node
    
    // printf("\nrun_Enqueue: pid:%d\nwd:%d\nsource:%s\ntarget:%s\nstatus:%s\nactive:%s\nlast_sync:%s\nerror_count:%d\n\n",pid,watch_disc_inot,source_dir,target_dir,status,active,last_sync_time,error_count);
    
    if (Temp==NULL) printf("the memory is full\n");
    else { //else it sets the values of the new node and adds it to the end of the Deque
        Temp->Link=NULL;
        strcpy(Temp->source_dir,source_dir);
        strcpy(Temp->target_dir,target_dir);
        strcpy(Temp->status,status);
        strcpy(Temp->active,active);
        strcpy(Temp->last_sync_time,last_sync_time);
        Temp->error_count = error_count;
        Temp->pid = pid;
        Temp->watch_disc_inot = watch_disc_inot;
        if(D->Rear==NULL) { //if the Deque is previously empty it sets the front and last item of the Deque as the first one  
            D->Front=Temp; 
            D->Rear=Temp;
        }
        else {
            D->Rear->Link=Temp;
            D->Rear=Temp;
        }
        if(strcmp(active,"YES")==0) {
            D->running_count++;
        }
        if(strcmp(status,"Active")==0) {
            D->being_watched_count++;
        }
        (D->number_of_items)++; //adds 1 to the numbers of items that are in the Deque
        Temp->quequePositon = D->number_of_items;
    }
}

void run_make_pid_active_to_no(pid_t id,running_Deque *D) {
    if(D->Front==NULL) {
        return;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(curr->pid == id) {
                // printf("run_make_pid_active_to_no : %s\n",curr->source_dir);
                strcpy(curr->active,"NO");
                // D->running_count--;
                return;
            }
            curr = curr->Link;
        }
    }
}


void run_reduce_running_count(running_Deque *D) {
    if(D->running_count > 0) {
        D->running_count--;
    }
}

void run_make_wd_active_to_yes(pid_t pid,int wd,running_Deque *D) {
    if(D->Front==NULL) {
        return;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(curr->watch_disc_inot == wd) {
                // printf("run_make_wd_active_to_yes : %s\n",curr->source_dir);
                strcpy(curr->active,"YES");
                curr->pid = pid;
                D->running_count++;
                return;
            }
            curr = curr->Link;
        }
    }
}


// finds the source_dir in the running queue
int run_find_source(char *source_dir,running_Deque *D) {
    if(D->Front==NULL) {
        return 0;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0) {
                return 1;
            }
            curr = curr->Link;
        }
    }
    return 0;
}


running_DequeNode* run_wd_exists(int watch_disc, running_Deque *D) {
    running_DequeNode *current = D->Front;
    while (current != NULL) {
        if (current->watch_disc_inot == watch_disc) {
            return current; // Return the node if the watch descriptor matches
        }
        current = current->Link;
    }
    return NULL; // Return NULL if not found
}


void run_close_all_watch_descriptors(int fd ,running_Deque *D) {
    running_DequeNode *current = D->Front;
    while (current != NULL) {
        inotify_rm_watch(fd, current->watch_disc_inot);
        current = current->Link;
    }
}

// returns 1 if the source_dir is in the running queue and sets the status to INACTIVE , else returns 0
int run_cancel_source(int fd, char *source_dir,running_Deque *D) {
    if(D->Front==NULL) {
        return 0;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0) {
                // inotify_rm_watch(fd,curr->watch_disc_inot);
                if(strcmp(curr->status,"Active")==0) {
                    strcpy(curr->status,"Inactive");
                    D->being_watched_count--;
                    return 1;
                } else if (strcmp(curr->status,"Inactive")==0) {
                    return 0;
                }
            }
            curr = curr->Link;
        }
    }
    return 0;
}


//returns the source_dir in the running queue and NULL if not found
running_DequeNode* run_get_source_info(char *source_dir,running_Deque *D) {
    if(D->Front==NULL) {
        return NULL;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0) {
                return curr;
            }
            curr = curr->Link;
        }
    }
    return NULL;
}


//returns the source_dir in the running queue and NULL if not found
running_DequeNode *run_find_source_with_target(char *source_dir,char *target_dir,running_Deque *D) {
    if(D->Front==NULL) {
        return NULL;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0 && strcmp(curr->target_dir,target_dir)==0) {
                return curr;
            }
            curr = curr->Link;
        }
    }
    return NULL;
}


void run_set_status_to_active(char *source_dir,running_Deque *D) {
    if(D->Front==NULL) {
        return;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(strcmp(curr->source_dir,source_dir)==0) {
                // printf("run_make_status_to_active : %s\n",curr->source_dir);
                strcpy(curr->status,"Active");
                D->being_watched_count++;
                return;
            }
            curr = curr->Link;
        }
    }
}


void run_update_pid_error_count(pid_t id,int error_count,running_Deque *D) {
    if(D->Front==NULL) {
        return;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(curr->pid == id) {
                curr->error_count += error_count;
                return;
            }
            curr = curr->Link;
        }
    }
}

int run_wd_active_yes(int wd,running_Deque *D) {
    if(D->Front==NULL) {
        return 0;
    }
    else {
        running_DequeNode *curr = D->Front;
        while(curr != NULL) {
            if(curr->watch_disc_inot == wd) {
                if(strcmp(curr->active,"YES")==0) {
                    return 1;
                }
                else {
                    return 0;
                }
            }
            curr = curr->Link;
        }
    }
    return 0;
}