#include <stdio.h>      //if you don't use scanf/printf change this include
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <math.h>

typedef short bool;
#define true 1
#define false 0

#define SHKEY 300


///==============================
//don't mess with this variable//
int * shmaddr;                 //
//===============================

// buffer used to send processes data between process generator and scheduler
struct msgbuffer
{
    int mtype;
    int arrivalTime;
    int runTime;
    int priority;
    int id;
};

// used to send the remaining time to the process
struct remain
{
    int remainig;
};

// struct used to keep track of the process information
//      - processId  ===> after forking the process we save the pid in this variable 
//                      so we could send signals to the running process
//      - lastTime  ===> helds the last time the process has started and stopped
//                     initially has the arrival time of the process 
//                     used to calculate the waiting time of the process
//      - arrivalTime  ===> helds the clock at which the procces has arrived
//      - runTime  ===> helds the total time needed by the process to terminate
//      - priority  ===> process' priority
//      - id  ===> id reffers to the id of the process in process.txt 
//      - waitingTime  ===> helds the total waiting time of the process
//      - remainingTime  ===> an attribute to check if the process had finished or not
//                          initially it is equal to the running time 
//      - next  ===> pointer to the next process in the queue whether in Ready Queue or Waiting Queue
struct process
{
    int processId;
    int lastTime;
    int arrivalTime;
    int runTime;
    int priority;
    int id;
    int waitingTime;
    int remainingTime;
    struct process* next; 
};

int getClk()
{
    return *shmaddr;
}


/*
 * All process call this function at the beginning to establish communication between them and the clock module.
 * Again, remember that the clock is only emulation!
*/
void initClk()
{
    int shmid = shmget(SHKEY, 4, 0444);
    while ((int)shmid == -1)
    {
        //Make sure that the clock exists
        printf("Wait! The clock not initialized yet!\n");
        sleep(1);
        shmid = shmget(SHKEY, 4, 0444);
    }
    shmaddr = (int *) shmat(shmid, (void *)0, 0);
}


/*
 * All process call this function at the end to release the communication
 * resources between them and the clock module.
 * Again, Remember that the clock is only emulation!
 * Input: terminateAll: a flag to indicate whether that this is the end of simulation.
 *                      It terminates the whole system and releases resources.
*/

void destroyClk(bool terminateAll)
{
    shmdt(shmaddr);
    if (terminateAll)
    {
        killpg(getpgrp(), SIGINT);
    }
}






// ===============================================
// =============== Priority Queue ================
// ===============================================

// struct used to represent the Ready Queue or Waiting Queue
//      - head  ==> pointer to the first process int the queue
//                  initially equals to NULL
struct Queue{
    struct process * head;
};
typedef struct Queue Queue;


// funtion to create a new process to be inserted in the queue
// arguments:
//          - p  ==> pointer to a process to get copy of
// returns: 
//          - temp  ==> pointer to the copy process
struct process * newNode(struct process * p) 
{ 
    struct process * temp = (struct process *)malloc(sizeof(struct process)); 
    temp->arrivalTime = p->arrivalTime;
    temp->runTime = p->runTime;
    temp->priority = p->priority;
    temp->id = p->id;
    temp->waitingTime = p->waitingTime;
    temp->remainingTime = p->remainingTime;
    temp->lastTime = p->lastTime;
    temp->processId = p->processId;
    temp->next = NULL;
    
    return temp; 
}

// utility function to print the queue
void printQueue(Queue * q){
    struct process* start = (q->head); 
    if(start == NULL)
    {
        printf("Empty Queue\n");
        return;
    }
    while(start){
        fprintf(stderr,"ID: %d, R:%d\n", start->id, start->remainingTime);
        start = start->next;
    }
    printf("========\n");
    return;
};

// arguments: 
//          - q  ==> Queue to popped from
// returns:
//          - temp ==> process pointer to the first process in the queue
struct process * pop(Queue * q) 
{ 
    struct process * temp = q->head; 
    q->head = q->head->next; 
    return temp; 
} 

// arguments: 
//          - q  ==> Queue to pushed in
//          - p  ==> process to be pushed
//          - Algorithm  ==> integer indicating the type of the queue
//                           1 priority queue based on process' priority
//                           2 priority queue based on process' remaining time
//                           3 normal queue
//                           other normal queue
void push(Queue * q, struct process * p, int Algorithm) 
{ 
    struct process* start = (q->head); 
    struct process* temp = newNode(p); 

    if((q->head) == NULL)
    {
        q->head = temp;
        return;
    }
    switch (Algorithm)
    {
        case 1:
            if ((q->head)->priority > p->priority) 
            { 
                temp->next = q->head; 
                (q->head) = temp; 
            } 
            else
            { 
                while (start->next != NULL && start->next->priority < p->priority)
                { 
                    start = start->next; 
                } 
        
                temp->next = start->next; 
                start->next = temp; 
            }
            break;
        case 2:
            if ((q->head)->remainingTime > p->remainingTime) 
            { 
                temp->next = q->head; 
                (q->head) = temp; 
            } 
            else
            { 
                while (start->next != NULL && start->next->remainingTime < p->remainingTime)
                { 
                    start = start->next; 
                } 
                temp->next = start->next; 
                start->next = temp; 
            }
            break;
        default:
            while (start->next != NULL)
            { 
                start = start->next; 
            } 
            temp->next = start->next; 
            start->next = temp; 
            break;        
    } 
} 

// ===============================================
// =============== Array =========================
// ===============================================

typedef struct {
  float *array;
  size_t used;
  size_t size;
} Array;


// utility function to initialise the size of the array
// and force the " used " attribute to be 0
// arguments:
//         - a ==> pointer to the array
//         - initialSize ==> desired size
void initArray(Array *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof(float));
  a->used = 0;
  a->size = initialSize;
}

// utility function to insert an elemenet to the array
// and check if the array is fulll it doubles the size
// arguments:
//         - a ==> pointer to the array
//         - element ==> element to be inserted
void insertArray(Array *a, float element) {
  // a->used is the number of used entries, because a->array[a->used++] updates a->used only after the array has been accessed.
  // Therefore a->used can go up to a->size 
  if (a->used == a->size) {
    a->size *= 2;
    a->array = realloc(a->array, a->size * sizeof(float));
  }
  a->array[a->used++] = element;
}

// utility function to free the array
// arguments:
//         - a ==> pointer to the array 
void freeArray(Array *a) {
  free(a->array);
  a->array = NULL;
  a->used = a->size = 0;
}
