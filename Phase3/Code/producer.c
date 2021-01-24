#include <stdio.h>      
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

#define BUFFER_SIZE 10

// Globals variables for semaphores and shared memory
int mutex,full,empty,bufferID,addID,remID,numID;

// Semaphores Struct and functions
union Semun
{
    int val;               /* value for SETVAL */
    struct semid_ds *buf;  /* buffer for IPC_STAT & IPC_SET */
    ushort *array;         /* array for GETALL & SETALL */
    struct seminfo *__buf; /* buffer for IPC_INFO */
    void *__pad;
};

void down(int sem)
{
    struct sembuf p_op;

    p_op.sem_num = 0;
    p_op.sem_op = -1;
    p_op.sem_flg = !IPC_NOWAIT;

    if (semop(sem, &p_op, 1) == -1)
    {
        perror("Error in down()");
        exit(-1);
    }
}

void up(int sem)
{
    struct sembuf v_op;

    v_op.sem_num = 0;
    v_op.sem_op = 1;
    v_op.sem_flg = !IPC_NOWAIT;

    if (semop(sem, &v_op, 1) == -1)
    {
        perror("Error in up()");
        exit(-1);
    }
}

// Signal Handler to delete all memory when exiting the last producer/consumer
void handler(int sigNum){
    FILE *fptr;
    fptr = fopen("counter.txt","r+");   // File save the shared data about the total no. of producers and consumers
    int num;                            // The number of the current producers and consumers (Total)
    fscanf(fptr,"%d",&num);
    num--;
    if(num == 0){                       // If this is the last one so clear the memory and remover the init file from the folder
        // Shared memory -> removeal
        shmctl(bufferID, IPC_RMID, (struct shmid_ds *)0);
        shmctl(addID, IPC_RMID, (struct shmid_ds *)0);
        shmctl(remID, IPC_RMID, (struct shmid_ds *)0);
        shmctl(numID, IPC_RMID, (struct shmid_ds *)0);
        // Semphores -> removal
        semctl(mutex,0,IPC_RMID,NULL);
        semctl(full,0,IPC_RMID,NULL);
        semctl(empty,0,IPC_RMID,NULL);
        // File -> removal
        remove("init.txt");
        remove("producerCount.txt");
    }
    // Edit the file for the current num of producers and consumers
    fptr = fopen("counter.txt","w+");
    fprintf(fptr,"%d",num);
    fclose(fptr);
    kill(getpid(),SIGKILL);   // Kill the process
}

int main(){
    // First check in the counter text by adding one to the value 
    FILE *fptr;
    fptr = fopen("counter.txt","r+");
    if( fptr != NULL){
        int num;
        fscanf(fptr,"%d",&num);
        fptr = fopen("counter.txt","w+");
        num++;
        fprintf(fptr,"%d",num);
    }else{
        fptr = fopen("counter.txt","a");
        fprintf(fptr,"%d",1);
    }
    fclose(fptr);
    signal(SIGINT,handler);             // Add the handler if the process is closed to clear the memory
    // Create the initialization shared memory and semaphores
    key_t key_id;
    key_id = ftok("keyProducerConsumer", 65);
    union Semun semun;
    // Semaphores
    mutex = semget(key_id,1,0666 | IPC_CREAT);
    full = semget(key_id+10,1,0666 | IPC_CREAT);
    empty = semget(key_id+20,1,0666 | IPC_CREAT);
    printf("the mutexID: %d, the fullID: %d, the emptyID: %d \n",mutex,full,empty);
    if (mutex == -1 || full == -1 || empty == -1)
    {
        perror("Error in create semphores");
        exit(-1);
    }
    // Intialize the Semaphores by creating the file if not exist so intialize , if exist don't intialize it
    fptr = fopen("init.txt","r");
    if(fptr == NULL){
        fptr = fopen("init.txt","a+");
        semun.val = 1;
        if (semctl(mutex, 0, SETVAL, semun) == -1)
        {
            perror("Error in semctl(mutex)");
            exit(-1);
        } 
        semun.val = 0;
        if (semctl(full, 0, SETVAL, semun) == -1)
        {
            perror("Error in semctl(full)");
            exit(-1);
        }
        semun.val = BUFFER_SIZE;
        if (semctl(empty, 0, SETVAL, semun) == -1)
        {
            perror("Error in semctl(empty)");
            exit(-1);
        }
    }
    fclose(fptr);

    // Shared memory 
    bufferID = shmget(key_id,sizeof(int)*BUFFER_SIZE,0666 | IPC_CREAT);
    addID = shmget(key_id+10,sizeof(int),0666 | IPC_CREAT);
    remID = shmget(key_id+20,sizeof(int),0666 | IPC_CREAT);
    numID = shmget(key_id+30,sizeof(int),0666 | IPC_CREAT);
    if (bufferID == -1 || addID == -1 || remID == -1 || numID == -1)
    {
        perror("Error in create the shared memory");
        exit(-1);
    }

    // Attach the shared memory to the program address
    int *bufferAddr = shmat(bufferID, (void *)0, 0);
    int *remAddr = shmat(addID, (void *)0, 0);
    int *addAddr = shmat(remID, (void *)0, 0);
    int *numAddr = shmat(numID, (void *)0, 0);

    // Input for this producer(1,101,...)
    int input;
    fptr = fopen("producerCount.txt","r");
    if( fptr != NULL){
        fscanf(fptr,"%d",&input);
        fptr = fopen("producerCount.txt","w+");
        input+=100;
        fprintf(fptr,"%d",input);
    }else{
        fptr = fopen("producerCount.txt","a");
        fprintf(fptr,"%d",1);
        input = 1;
    }
    fclose(fptr);

    // Main loop of the producer
    printf("Enter the Producer loop\n");
    while(1){
        down(empty);
        down(mutex);
        bufferAddr[addAddr[0]]= input;
        printf("Item is produced: %d\n",bufferAddr[addAddr[0]]);
        input++;
        addAddr[0]=(addAddr[0]+1)%BUFFER_SIZE;
        numAddr[0]+=1;
        printf("Add index: %d, Remove index:%d, Array num: %d\n",addAddr[0],remAddr[0],numAddr[0]);
        up(mutex);
        up(full);
        sleep(5);
    }


    return 0;
}