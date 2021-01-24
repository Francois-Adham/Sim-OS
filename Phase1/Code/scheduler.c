#include "headers.h"

void receiveProcess(int signum);
void lastProcess(int signum);
void HPF(FILE *fptr);
void SRTN(FILE *fptr);
void RR(int Quantum,FILE *fptr);
void writeStatus(FILE *fptr,float util, float avgWTA , float avgW);
void writeLogs(FILE *fptr, int time, int process, char* state, int arrival, int running, int remain, int waiting);
void clear(int signum);

int Algorithm, rec_val, stat_loc, msgq_id, pid,LastFinish=0, msgq;
Queue readyQueue;	
Array Wt;
bool finished = false;
float TWT=0,TTA=0,TWTA=0,Wasted=0,ProcessesNum=0;
double STD=0;
struct remain message;

int main(int argc, char * argv[])
{
	
	FILE *fptr,*fptr2;
    fptr = fopen("./Scheduler.log","w");
    fptr2 = fopen("./Scheduler.perf","w");
    if( fptr == NULL ||fptr2 ==NULL)
    {
        printf("Error opening the file!");
        exit(1);
    }
    fprintf(fptr,"#At time x process y state arr w total z remain y wait k \n");


    
    signal(SIGUSR1, receiveProcess);
    signal(SIGUSR2, lastProcess);
    signal(SIGINT, clear);

    Algorithm = atoi(argv[1]);
    printf("Algorithm Number is %d\n",Algorithm);

    initClk();
    initArray(&Wt,5);
    key_t key_id;
    key_id = ftok("key", 65);               
    msgq_id = msgget(key_id, 0666 | IPC_CREAT); 
    msgq = msgget(60, 0666 | IPC_CREAT); 
    if (msgq_id == -1)
    {
        perror("Error in create");
        exit(-1);
    }

    switch(Algorithm)
    {
        case 1:
            HPF(fptr);
            break;            
        case 2:
        	SRTN(fptr);
            break;
        default:
            RR(atoi(argv[2]),fptr);
            break;
    }

    writeStatus(fptr2,(LastFinish-Wasted)*100/LastFinish,TWTA/ProcessesNum , TWT/ProcessesNum);
    freeArray(&Wt);
    fclose(fptr);
    fclose(fptr2);
    destroyClk(false);
    msgctl(msgq, IPC_RMID, (struct msqid_ds *)0);
}

// =============================================================
// =============== SIGNAL TO CLEAR THE RESOURCES ===============
// =============================================================
void clear(int signum)
{
    msgctl(msgq, IPC_RMID, (struct msqid_ds *)0);
    exit(1);
}

// ===========================================================
// =============== Signal To Receive Processes ===============
// ===========================================================
void receiveProcess(int signum){
    signal(SIGUSR1, receiveProcess);
    struct msgbuffer message;
    struct process receivedProcess;
    do
    {
        rec_val = msgrcv(msgq_id, &message, sizeof(message), 0, IPC_NOWAIT);
        if(rec_val!=-1)
        {
            receivedProcess.id = message.id;
            receivedProcess.arrivalTime = message.arrivalTime;
            receivedProcess.lastTime = message.arrivalTime;
            receivedProcess.processId = -1;
            receivedProcess.runTime = message.runTime;
            receivedProcess.priority = message.priority;
            receivedProcess.waitingTime = 0;
            receivedProcess.remainingTime = message.runTime;
            receivedProcess.next = NULL;
            //Push on Ready Queue
            push(&readyQueue, &receivedProcess, Algorithm);
        }
    } while(rec_val!=-1);
}

// ==================================================================================================
// =============== Signal To Know if Process generator finishs its prcesses =========================
// ==================================================================================================
void lastProcess(int signum){
    finished = true;
}

// =============================================================================================================================================
// ==================================================================== HPF ====================================================================
// =============================================================================================================================================

void HPF(FILE *fptr)
{
    //Check if there is another coming process
	while(!finished || (readyQueue.head != NULL))
    {
        //check if there is process now ?? pop and fork it
        if(readyQueue.head != NULL)
        {
            struct process * currentProcess = pop(&readyQueue);
            currentProcess->waitingTime = getClk() - currentProcess->arrivalTime;
            pid = fork();
            
            if(!pid)
            {
                char number[6];
                snprintf(number, sizeof(number), "%d", currentProcess->remainingTime);
                execl("process.out", "process.out", number, NULL);
            }
            Wasted+=getClk()-LastFinish;    //wasted time between switching to Calc utilization.
            writeLogs(fptr,getClk(),currentProcess->id,"started",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);
            int last = getClk();    
            //check if the process finished
            while(currentProcess->remainingTime > 0)
            {   
                if(getClk() - last >0)
                {
                    last = getClk();
                    message.remainig = --(currentProcess->remainingTime);
                    msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);  //send new remaining time to the process
                }
            }
            waitpid(pid, &stat_loc, 0);     //receive exit code from process to free it
            LastFinish=stat_loc>>8;
            writeLogs(fptr,stat_loc>>8,currentProcess->id,"finished",currentProcess->arrivalTime,currentProcess->runTime,0,currentProcess->waitingTime);
            free(currentProcess);

        }
    }
}

// ==============================================================================================================================================
// ==================================================================== SRTN ====================================================================
// ==============================================================================================================================================

void SRTN(FILE *fptr)
{
    int startTime = -1, currentRemaining = -1, lastUpdate = 0;
    struct process * currentProcess;
    //Check if there is another coming process
    while(!finished || (readyQueue.head != NULL))
    {
        //check if there is process now ?? pop and fork it
        if(readyQueue.head != NULL)
        {
        	//Check to pop a new process in case it's the first one 
            //OR a process with less remaining time arrives 
            //OR the current process finished
            if((startTime == -1) || (currentRemaining > readyQueue.head->remainingTime) || (!currentRemaining))
            {
                //it's the first one pop it.
                if(startTime == -1)
                {
                    currentProcess = pop(&readyQueue);
                    startTime = getClk();
                    lastUpdate = getClk();
                    currentRemaining = currentProcess->remainingTime;
                }
                else if(!currentRemaining){     //Current process finished free it and pop new one to run.
                    waitpid(currentProcess->processId, &stat_loc, 0);
                    LastFinish=stat_loc>>8;
                    writeLogs(fptr,stat_loc>>8,currentProcess->id,"finished",currentProcess->arrivalTime,currentProcess->runTime,0,currentProcess->waitingTime);
                    free(currentProcess);
                    currentProcess = pop(&readyQueue);
                    
                    currentProcess->waitingTime += (getClk() - currentProcess->lastTime);
      
                }
                else    //Shorter process arrived so Stop the current and pop the shortest. 
                {
                    kill(currentProcess->processId, SIGSTOP);
                    LastFinish=getClk();
                    currentProcess->remainingTime = currentRemaining;
                    currentProcess->lastTime = getClk();
                  
                    push(&readyQueue, currentProcess, Algorithm);
                    writeLogs(fptr,getClk(),currentProcess->id,"stopped",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);
                    currentProcess = pop(&readyQueue);
                    currentProcess->waitingTime +=(getClk() - currentProcess->lastTime);
                }
                if(currentProcess->processId == -1)     //check if the popped procees is new (Not stopped)
                {
                    pid = fork();
                    currentProcess->processId = pid;
                    currentRemaining = currentProcess->remainingTime;
                    Wasted+=getClk()-LastFinish;
                    writeLogs(fptr,getClk(),currentProcess->id,"started",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);
                    if(!pid)
                    {
                        char number[6];
                        snprintf(number, sizeof(number), "%d", currentProcess->remainingTime);
                        execl("process.out", "process.out", number, NULL);
                    }
                }else   //if it was stopped resume it
                {
                    startTime = getClk();
                    currentRemaining = currentProcess->remainingTime;
                    
                    kill(currentProcess->processId, SIGCONT);
                    Wasted+=getClk()-LastFinish;
                    writeLogs(fptr,getClk(),currentProcess->id,"resumed",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);
                }
                
            }
        }
        if(currentProcess && (getClk() - lastUpdate > 0))   //Update the remainig time of current runnig process
        {
            currentRemaining -= (getClk() - lastUpdate);
            lastUpdate = getClk();
            message.remainig = currentRemaining;
            msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);      //send the new remaining time
        }
    }
    //for the last process run.
    while(currentRemaining || (getClk() - lastUpdate > 0))
    {
        if(getClk() - lastUpdate > 0)
        {
            currentRemaining -= (getClk() - lastUpdate);
            lastUpdate = getClk();
            message.remainig = currentRemaining;
            msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);
        }
    }
    waitpid(currentProcess->processId, &stat_loc, 0);
    LastFinish=stat_loc>>8;
    writeLogs(fptr,stat_loc>>8,currentProcess->id,"finished",currentProcess->arrivalTime,currentProcess->runTime,0,currentProcess->waitingTime);
    free(currentProcess);
}


// ============================================================================================================================================
// ==================================================================== RR ====================================================================
// ============================================================================================================================================

void RR(int Quantum,FILE *fptr)
{
    int currentRuning = Quantum + 1, currentRemaining = 1, lastUpdate = 0, startTime = -1;
    struct process * currentProcess = NULL;
    //Check if there is another coming process
    while(!finished || (readyQueue.head != NULL))
    {
        //check if there is process now ?? pop and fork it
        if(readyQueue.head != NULL)
        {
            //Check to pop a new process in case the current one finished 
            //OR it finished its quantum 
            if(((currentRuning >= Quantum)) || (currentRemaining <= 0))
            {
                //it finished its quantum so stop it and pop a new one
                if(currentProcess && (currentRemaining > 0))  
                {
                    kill(currentProcess->processId, SIGSTOP);
                    LastFinish=getClk();
                    currentProcess->remainingTime = currentRemaining;
                    currentProcess->lastTime = getClk();
                    writeLogs(fptr,getClk(),currentProcess->id,"stopped",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);
                    push(&readyQueue, currentProcess, Algorithm);
                }
                //the current process finished so free it and pop another one
                if(currentRemaining <= 0)
                {
                    waitpid(currentProcess->processId, &stat_loc, 0);
                    LastFinish=stat_loc>>8;
                    writeLogs(fptr,stat_loc>>8,currentProcess->id,"finished",currentProcess->arrivalTime,currentProcess->runTime,0,currentProcess->waitingTime);
                    free(currentProcess);
                }
                currentProcess = pop(&readyQueue);
                currentRemaining = currentProcess->remainingTime;
                currentProcess->waitingTime += (getClk() - currentProcess->lastTime);
                currentRuning = 0;
                if(currentProcess->processId == -1)     //check if it's new or stopped process
                {
                    pid = fork();
                    if(pid == 0)
                    {
                        char number[6];
                        snprintf(number, sizeof(number), "%d", currentProcess->remainingTime);
                        execl("process.out", "process.out", number, NULL);
                    }
                    Wasted+=getClk()-LastFinish;
                    writeLogs(fptr,getClk(),currentProcess->id,"started",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);
                    currentProcess->processId = pid;
                }
                else    //it was stopped so resume it
                {
                    kill(currentProcess->processId, SIGCONT);
                    Wasted+=getClk()-LastFinish;
                    writeLogs(fptr,getClk(),currentProcess->id,"resumed",currentProcess->arrivalTime,currentProcess->runTime,currentProcess->remainingTime,currentProcess->waitingTime);

                }
            }
        }
        //Update remaining time of current process
        if((getClk() - lastUpdate > 0) && (currentProcess != NULL))
        {
            int clk = getClk();
            if(startTime == -1)     //if it's the first one
            {
                currentRuning += (clk - currentProcess->arrivalTime);
                startTime = 1;
            }
            else
            {
                currentRuning += (clk - lastUpdate);
                currentRemaining -= (clk - lastUpdate);
            }
            lastUpdate = clk;
            if((readyQueue.head == NULL) && ( currentRuning>= Quantum))     //if there is no processes in the queue take another quantum
            {
                currentRuning = 0;
            }
            message.remainig = currentRemaining;
            msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);      //send the new remaining time
        }
    }
    //for the last process
    while(currentRemaining || (getClk() - lastUpdate > 0))
    {
        if(getClk() - lastUpdate > 0)
        {
            currentRemaining -= (getClk() - lastUpdate);
            lastUpdate = getClk();
            message.remainig = currentRemaining;
            msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);
        }
    }
    waitpid(currentProcess->processId, &stat_loc, 0);
    LastFinish=stat_loc>>8;
    writeLogs(fptr,stat_loc>>8,currentProcess->id,"finished",currentProcess->arrivalTime,currentProcess->runTime,0,currentProcess->waitingTime);
    free(currentProcess);
}

// ===================================================
// =============== Printing Functions ================
// ===================================================

//  utility function to write the logs into the file 
//  arguments:
//          - fptr ==> pointer to the file 
//          - time ==> current clock
//          - process ==> process id
//          - state ==> state of the process (started / finished / resumed / stopped)
//          - arrival ==> arrival time of the process 
//          - running ==> total running time of the process
//          - remain ==> remaining time
//          - waiting ==> total waiting time
void writeLogs(FILE *fptr, int time, int process, char* state, int arrival, int running, int remain, int waiting){
    if(state == "finished")
    {
        float TA = (arrival == 1)?time:time - arrival ;
        float WTA;
        if(running)
            WTA = TA/(running);
        else
            WTA = TA;
        TTA+=TA;
        TWTA+=WTA;
        TWT+=waiting;
        insertArray(&Wt,WTA);
        ProcessesNum+=1;
        fprintf(fptr,"AT time %d process %d %s arr %d total %d remain %d wait %d  TA %.2f WTA %.2f \n", time, process, state, arrival, running, remain, waiting, TA, WTA);
    }
    else {
        fprintf(fptr,"AT time %d process %d %s arr %d total %d remain %d wait %d \n",time,process,state,arrival,running,remain,waiting);
    }
}

// utility function to write the performance
//  arguments:
//          - fptr ==> pointer to the file 
//          - util ==> utilization
//          - avgWTA ==> average weighted turn arround time
//          - avgW ==> avg waiting time
void writeStatus(FILE *fptr,float util, float avgWTA , float avgW){

	//Calculate STD
	double totalSum = 0;
    for(int i =0; i<ProcessesNum;i++){
        Wt.array[i] -= avgWTA;
        totalSum += Wt.array[i]*Wt.array[i];
    }
    STD = totalSum/ProcessesNum;
    STD = sqrt(STD);
    
    fprintf(fptr,"CPU utilization = %.2f%% \n",util);
    fprintf(fptr,"Avg WTA = %.2f \n",avgWTA);
    fprintf(fptr,"Avg Waiting = %.2f \n",avgW);
    fprintf(fptr,"Std WTA = %.2f \n",STD);
}

