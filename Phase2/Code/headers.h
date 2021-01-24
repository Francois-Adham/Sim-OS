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
    int memory;
    int ArrivalTime;
    int RunTime;
    int Priority;
    int Id;
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
//      - memory  ===> the memory size of the process 
//      - sector  ===> the range poninter in the memory
struct process
{
    int processId;
    int memory;
    int lastTime;
    int ArrivalTime;
    int RunTime;
    int Priority;
    int Id;
    int WaitingTime;
    int RemainingTime;
    struct process* next; 
    struct sector * Sector;
};

// struct used to keep track of the process information
//      - s  ===> start range of the sector
//      - e  ===> end range of the sector
//      - next  ===> next sector in the memory
struct sector
{
    short s;
    short e;
    struct sector * next; 
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
    long size;
};

typedef struct Queue Queue;

Queue * initQueue()
{
    Queue * q = (Queue *)malloc(sizeof(Queue));
    q->head = NULL;
    q->size = 0;
    return q;
}

// funtion to create a new process to be inserted in the queue
// arguments:
//          - p  ==> pointer to a process to get copy of
// returns: 
//          - temp  ==> pointer to the copy proces
struct process * newNode(struct process * p) 
{ 
    struct process * temp = (struct process *)malloc(sizeof(struct process)); 
    temp->ArrivalTime = p->ArrivalTime;
    temp->RunTime = p->RunTime;
    temp->Priority = p->Priority;
    temp->Id = p->Id;
    temp->WaitingTime = p->WaitingTime;
    temp->RemainingTime = p->RemainingTime;
    temp->lastTime = p->lastTime;
    temp->processId = p->processId;
    temp->memory = p->memory;
    temp->next = NULL;
    temp->Sector = p->Sector;
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
        fprintf(stderr,"ID: %d, M:%d\n", start->Id, start->memory);
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
//          - q  ==> Queue to popped from
//          - s  ==> size of the memory freed to pop if the ready is equal or less than it
// returns:
//          - temp ==> process pointer to the first process in the queue
struct process * popReady(Queue * q, int size)
{   
    struct process * temp = q->head;
    struct process * previous = NULL; 
    while((temp->next != NULL) && (temp->memory > size))
    {
        previous = temp;
        temp = temp->next;   
    }
    if(temp->memory > size)
        return NULL;
    if(previous == NULL)
    {
        q->head = q->head->next;
    }
    else
    {
        previous->next = temp->next;
    }
    return temp;
}

// arguments: 
//          - q  ==> Queue to pushed in
//          - p  ==> process to be pushed
//          - Algorithm  ==> integer indicating the type of the queue
//                           1 priority queue based on process' priority
//                           2 priority queue based on process' remaining time
//                           3 normal queue
//                           4 priority queue based on process' memory
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
            if ((q->head)->Priority > p->Priority) 
            { 
                temp->next = q->head; 
                (q->head) = temp; 
            } 
            else
            { 
                while (start->next != NULL && start->next->Priority <= p->Priority)
                { 
                    start = start->next; 
                } 
        
                temp->next = start->next; 
                start->next = temp; 
            }
            break;
        case 2:
            if ((q->head)->RemainingTime > p->RemainingTime) 
            { 
                temp->next = q->head; 
                (q->head) = temp; 
            } 
            else
            { 
                while (start->next != NULL && start->next->RemainingTime <= p->RemainingTime)
                { 
                    start = start->next; 
                } 
                temp->next = start->next; 
                start->next = temp; 
            }
            break;
        case 4:
            if ((q->head)->memory < p->memory) 
            { 
                temp->next = q->head; 
                (q->head) = temp; 
            } 
            else
            { 
                while (start->next != NULL && start->next->memory >= p->memory)
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



// ===============================================
// ==================== MEMORY ===================
// ===============================================

// arguments:
//          - n ==> the integer number desired to get the nerest 2^n
// return:
//          - next power of 2
int nextPowerOf2(unsigned int n) 
{ 
    unsigned count = 0; 
    if (n && !(n & (n - 1))) 
        return n; 
    
    while( n != 0) 
    { 
        n >>= 1; 
        count += 1; 
    } 
    
    return 1 << count; 
} 
// Struct for the Memory Empty sectors
struct MemoryQueue{
    struct sector * head;
    long size;
};

typedef struct MemoryQueue MemoryQueue;
// initialize the memory queue
MemoryQueue * initMemoryQ()
{
    MemoryQueue * q = (MemoryQueue *)malloc(sizeof(MemoryQueue));
    q->head = NULL;
    q->size = 0;
    return q;
}

// Create new sector initialization
struct sector * newSector(struct sector * p) 
{ 
    struct sector * temp = (struct sector *)malloc(sizeof(struct sector)); 
    temp->s = p->s;
    temp->e = p->e;
    temp->next = p->next;
    
    return temp; 
}

// Print the sector Queue
void printSector(MemoryQueue * q){
    struct sector* start = (q->head); 
    if(start == NULL)
    {
        printf("Empty Queue\n");
        return;
    }
    while(start){
        fprintf(stderr,"S: %d, E: %d\n", start->s, start->e);
        start = start->next;
    }
    printf("========\n");
    return;
};

// arguments:
//          - q ==> memory queue
// return:
//          - the poped sector pointer
struct sector * popSector(MemoryQueue * q) 
{ 
    struct sector * temp = q->head; 
    q->head = q->head->next; 
    return temp; 
} 

// arguments:
//          - q ==> the memory queue to push(allocate) the process in it 
//          - p ==> the sector taken by the process
//          - merge ==> if 2 sectors are empty and can merge into there sum 
// return:
//          - pointer to the location in memory
struct sector * pushSector(MemoryQueue * q, struct sector * p, bool merge) 
{ 
    struct sector* start = (q->head);
    struct sector* previous = (q->head); 
    struct sector* temp = newSector(p); 

    if((q->head) == NULL)
    {
        q->head = temp;
        return NULL;
    }
    if ((q->head)->s > p->s) 
    { 
        temp->next = q->head; 
        (q->head) = temp; 
    } 
    else
    { 
        while (start->next != NULL && start->next->s < p->s)
        { 
            previous = start;
            start = start->next;
        } 

        temp->next = start->next; 
        start->next = temp; 
    }
    if(merge && ((p->e - p->s) < 255))
    {
        if(temp->next && !((temp->s / (temp->e - temp->s))%2))
        {
            if(temp->next->s == temp->e + 1)
            {
                struct sector * t = (struct sector *)malloc(sizeof(struct sector));
                t->s = temp->s;
                t->e = temp->next->e;
                if(temp == q->head)
                    q->head = temp->next->next;
                else
                    start->next = temp->next->next;
                return t;
            }
        }

        if(start->next && !((start->s / (temp->e - temp->s))%2))
        {
            if(temp->s == start->e + 1)
            {
                struct sector * t = (struct sector *)malloc(sizeof(struct sector));
                t->s = start->s;
                t->e = temp->e;
                if(start == q->head)
                    q->head = temp->next;
                else
                    previous->next = temp->next;
                return t;
            }
        }
    }
    return NULL;
}

// Memory struct with head array of sizes where size (8 -> 256)
struct Memory{
    struct MemoryQueue * head[6];
};

typedef struct Memory Memory;
// Intialize the memory
Memory * initMemory()
{
    Memory * q = (Memory *)malloc(sizeof(Memory));
    for(int i = 0; i<6; i++)
        q->head[i] = initMemoryQ();
    
    struct sector * s = (struct sector *)malloc(sizeof(struct sector));
    s->s = 0;
    s->e = 255;
    pushSector(q->head[5], s, false);
    s->s = 256;
    s->e = 511;
    pushSector(q->head[5], s, false);
    s->s = 512;
    s->e = 767;
    pushSector(q->head[5], s, false);
    s->s = 768;
    s->e = 1023;
    pushSector(q->head[5], s, false);

    return q;
}
// arguments:
//          - m ==> the memory of the schedular
//          - size ==> size of the sector to take it to the process
// return:
//          - s ==> pointer to the sector
struct sector * allocate(Memory * m, int size)
{
    int real_size = nextPowerOf2(size);

    int index = (log(real_size) / log(2)) - 3 ;
    if(index < 0)
    {
        index = 0;
        real_size = 8;
    }
    while((index < 6) && (m->head[index]->head == NULL))
        index++;
    if(index == 6)
        return NULL;

    struct sector * s = popSector(m->head[index]);
    struct sector * temp = (struct sector *)malloc(sizeof(struct sector));
    while(((s->e + 1) - s->s) > real_size)
    {
        temp->s = s->s + (s->e - s->s)/2 + 1;
        temp->e = s->e;
        index--;
        pushSector(m->head[index], temp, false);
        s->e = s->s + (s->e - s->s)/2;
    }
    return s;
}
// arguments:
//          - m ==> the memory of the schedular
//          - s ==> the sector taken by process to be freed
// return:
//          - the size of the freed memory sector
int deallocate(Memory * m, struct sector *s)
{   
    int index = (log(s->e + 1 - s->s) / log(2)) - 3 ;
    if(index < 0)
        index = 0;
    struct sector * pushed = pushSector(m->head[index], s, true);
    while((pushed != NULL))
    {
        index++;
        if(index < 5)
            pushed = pushSector(m->head[index], pushed, true);
        else
            pushed = pushSector(m->head[index], pushed, false);
        
    }
    if(pushed)
        return (pushed->e - pushed->s +1);
    else 
        return 256;
}