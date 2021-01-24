#include "headers.h"

void receiveProcess(int signum);
void lastProcess(int signum);
void HPF(FILE *fptr, FILE * m);
void SRTN(FILE *fptr, FILE * m);
void RR(int Quantum,FILE *fptr, FILE * m);
void writeStatus(FILE *fptr,float util, float avgWTA , float avgW);
void writeLogs_scheduler(FILE *fptr,int time,int process,char* state,int w,int z,int y,int k);
void writeLogs_memory(FILE *fptr, int time, int bytes, int process, int start, int end, bool flag);
int Algorithm, rec_val, stat_loc, msgq_id, pid,LastFinish=0, msgq;
Queue readyQueue, waitingQueue;
Memory * memory;	
Array Wt;
bool finished = false;
float TWT=0,TTA=0,TWTA=0,Wasted=0,ProcessesNum=0;
double STD=0;
struct remain message;
int main(int argc, char * argv[])
{
	FILE *fptr, *fptr2, *m;
    fptr = fopen("./Scheduler.log","w");
    m = fopen("./memory.log","w");
    fptr2 = fopen("./Scheduler.perf","w");
    if( fptr == NULL || fptr2 == NULL || m == NULL)
    {
        printf("Error opening the file!");
        exit(1);
    }
    fprintf(m,"#At time x allocated y bytes for process z from i to j \n");
    fprintf(fptr,"#At time x process y state arr w total z remain y wait k \n");
    
    memory = initMemory();
    signal(SIGUSR1, receiveProcess);
    signal(SIGUSR2, lastProcess);
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
            HPF(fptr,m);
            break;            
        case 2:
        	SRTN(fptr,m);
            break;
        default:
            RR(atoi(argv[2]),fptr,m);
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
            receivedProcess.Id = message.Id;
            receivedProcess.ArrivalTime = message.ArrivalTime;
            receivedProcess.lastTime = message.ArrivalTime;
            receivedProcess.processId = -1;
            receivedProcess.RunTime = message.RunTime;
            receivedProcess.Priority = message.Priority;
            receivedProcess.WaitingTime = 0;
            receivedProcess.RemainingTime = message.RunTime;
            receivedProcess.memory = message.memory;
            receivedProcess.next = NULL;
            receivedProcess.Sector = NULL;
            push(&readyQueue, &receivedProcess, Algorithm);
        }
    } while(rec_val!=-1);
}

// ========================================================================================
// =============== Signal To Know if Process generator finishs its prcesses ===============
// ========================================================================================
void lastProcess(int signum){
    finished = true;
}

// =============================================
// =============== HPF Algorithm ===============
// =============================================

void HPF(FILE *fptr, FILE * m)
{
    //Check if there is another coming process
	while(!finished || (readyQueue.head != NULL))
    {
        //check if there is process now ?? pop and fork it
        if(readyQueue.head != NULL)
        {
            struct process * currentProcess = pop(&readyQueue);
            currentProcess->WaitingTime = getClk() - currentProcess->ArrivalTime;
            currentProcess->Sector = allocate(memory, currentProcess->memory);
            pid = fork();
            
            if(!pid)
            {
                char number[6];
                snprintf(number, sizeof(number), "%d", currentProcess->RemainingTime);
                execl("process.out", "process.out", number, NULL);
            }
            //wasted time between switching to Calc. utilization.
            Wasted+=getClk()-LastFinish;        

            writeLogs_memory(m, getClk(), currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, true);
            writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"started",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);
            int last = getClk();
            //check if the process finished
            while(currentProcess->RemainingTime > 0)
            {   
                if(getClk() - last >0)
                {
                    last = getClk();
                    message.remainig = --(currentProcess->RemainingTime);
                    msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);
                }
            }
            waitpid(pid, &stat_loc, 0);     //receive exit code from process to free it
            LastFinish=stat_loc>>8;
            writeLogs_memory(m, LastFinish, currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, false);
            writeLogs_scheduler(fptr,stat_loc>>8,currentProcess->Id,"finished",currentProcess->ArrivalTime,currentProcess->RunTime,0,currentProcess->WaitingTime);
            deallocate(memory, currentProcess->Sector);
            free(currentProcess);

        }
    }
}

// ==============================================
// =============== SRTN Algorithm ===============
// ==============================================

void SRTN(FILE *fptr, FILE * m)
{
    int startTime = -1, currentRemaining = -1, lastUpdate = 0, size = 0;
    struct process * currentProcess;
    //Check if there is another coming process
    while(!finished || (readyQueue.head != NULL) || (waitingQueue.head != NULL))
    {
        //check if there is process now ?? pop and fork it
        if(readyQueue.head != NULL)
        {
            //Check to pop a new process in case it's the first one 
            //OR a process with less remaining time arrives 
            //OR the current process finished
            if((startTime == -1) || (currentRemaining > readyQueue.head->RemainingTime) || (!currentRemaining))
            {
                //it's the first one pop it.
                if(startTime == -1)
                {
                    currentProcess = pop(&readyQueue);
                    startTime = getClk();
                    lastUpdate = getClk();
                    currentRemaining = currentProcess->RemainingTime;
                }
                else if(!currentRemaining){         //Current process finished free it and pop new one to run.
                    waitpid(currentProcess->processId, &stat_loc, 0);
                    LastFinish=stat_loc>>8;
                    writeLogs_memory(m, LastFinish, currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, false);
                    size = deallocate(memory, currentProcess->Sector);
                    writeLogs_scheduler(fptr,stat_loc>>8,currentProcess->Id,"finished",currentProcess->ArrivalTime,currentProcess->RunTime,0,currentProcess->WaitingTime);
                    free(currentProcess);
                    if(waitingQueue.head == NULL)     //check if the waiting queue is empty
                        pop_SRTN:
                        currentProcess = pop(&readyQueue);
                    else
                    {
                        currentProcess = popReady(&waitingQueue, size);
                        if(currentProcess == NULL)
                            currentProcess = pop(&readyQueue);
                        else if(currentProcess->RemainingTime > readyQueue.head->RemainingTime)
                        {
                            push(&waitingQueue, currentProcess, 2);
                            currentProcess = pop(&readyQueue);
                        }
                    }
                    
                    currentProcess->WaitingTime += (getClk() - currentProcess->lastTime);
      
                }
                else        //Shorter process arrived so Stop the current and pop the shortest. 
                {
                    kill(currentProcess->processId, SIGSTOP);
                    LastFinish=getClk();
                    currentProcess->RemainingTime = currentRemaining;
                    currentProcess->lastTime = getClk();
                  
                    push(&readyQueue, currentProcess, Algorithm);
                    writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"stopped",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);
                    currentProcess = pop(&readyQueue);
                    currentProcess->WaitingTime +=(getClk() - currentProcess->lastTime);
                }
                if(currentProcess->processId == -1)     //check if the popped procees is new (Not stopped)
                {
                    //try to allocate if you can't push it in the waited queue.
                    currentProcess->Sector = allocate(memory,currentProcess->memory);
                    if(currentProcess->Sector)
                    {   
                        pid = fork();
                        currentProcess->processId = pid;
                        Wasted+=getClk()-LastFinish;
                        writeLogs_memory(m, getClk(), currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, true);
                        writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"started",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);
                        if(!pid)
                        {
                            char number[6];
                            snprintf(number, sizeof(number), "%d", currentProcess->RemainingTime);
                            execl("process.out", "process.out", number, NULL);
                        }
                        currentRemaining = currentProcess->RemainingTime;
                    }
                    else
                    {
                        push(&waitingQueue, currentProcess, 2);
                        goto pop_SRTN;
                    }
                }
                else        //if it was stopped resume it
                {
                    startTime = getClk();
                    currentRemaining = currentProcess->RemainingTime;
                    
                    kill(currentProcess->processId, SIGCONT);
                    Wasted+=getClk()-LastFinish;
                    writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"resumed",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);
                    
                }
                
            }
        }
         //Update the remainig time of current runnig process
        if(currentProcess && (getClk() - lastUpdate > 0))
        {
            currentRemaining -= (getClk() - lastUpdate);
            lastUpdate = getClk();
            message.remainig = currentRemaining;
            msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);      //send the new remaining time
        }
    }
    //for the last process run.
    while(currentRemaining > 0 || (getClk() - lastUpdate > 0))
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
    writeLogs_memory(m, LastFinish, currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, false);
    writeLogs_scheduler(fptr,stat_loc>>8,currentProcess->Id,"finished",currentProcess->ArrivalTime,currentProcess->RunTime,0,currentProcess->WaitingTime);
    free(currentProcess);
}


// ============================================
// =============== RR Algorithm ===============
// ============================================

void RR(int Quantum,FILE *fptr, FILE * m)
{
    int currentRuning = Quantum + 1, currentRemaining = 1, lastUpdate = 0, startTime = -1, size = 0;
    struct process * currentProcess = NULL;
    //Check if there is another coming process
    while(!finished || (readyQueue.head != NULL) || (waitingQueue.head != NULL))
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
                    currentProcess->RemainingTime = currentRemaining;
                    currentProcess->lastTime = getClk();
                    writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"stopped",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);
                    push(&readyQueue, currentProcess, Algorithm);
                }
                //the current process finished so free it and pop another one
                if(currentRemaining <= 0)
                {
                    waitpid(currentProcess->processId, &stat_loc, 0);
                    LastFinish=stat_loc>>8;
                    writeLogs_memory(m, LastFinish, currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, false);
                    size = deallocate(memory, currentProcess->Sector);
                    writeLogs_scheduler(fptr,stat_loc>>8,currentProcess->Id,"finished",currentProcess->ArrivalTime,currentProcess->RunTime,0,currentProcess->WaitingTime);
                    free(currentProcess);
                }
                //check if the waiting queue is empty
                if(waitingQueue.head == NULL)
                    pop_RR:
                    currentProcess = pop(&readyQueue);
                else
                {
                    currentProcess = popReady(&waitingQueue, size);
                    if(currentProcess == NULL)
                        currentProcess = pop(&readyQueue);
                }
                currentRemaining = currentProcess->RemainingTime;
                currentProcess->WaitingTime += (getClk() - currentProcess->lastTime);
                currentRuning = 0;
                //check if it's new or stopped process
                if(currentProcess->processId == -1)
                {
                    //try to allocate if you can't push it in the waited queue.
                    currentProcess->Sector = allocate(memory,currentProcess->memory);
                    if(currentProcess->Sector)
                    { 
                        pid = fork();
                        if(pid == 0)
                        {
                            char number[6];
                            snprintf(number, sizeof(number), "%d", currentProcess->RemainingTime);
                            execl("process.out", "process.out", number, NULL);
                        }
                        Wasted+=getClk()-LastFinish;
                        writeLogs_memory(m, getClk(), currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, true);
                        writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"started",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);
                        currentProcess->processId = pid;
                    }
                    else    
                    {
                        push(&waitingQueue, currentProcess, 4);
                        goto pop_RR;
                    }
                }
                else    //it was stopped so resume it
                {
                    kill(currentProcess->processId, SIGCONT);
                    Wasted+=getClk()-LastFinish;
                    writeLogs_scheduler(fptr,getClk(),currentProcess->Id,"resumed",currentProcess->ArrivalTime,currentProcess->RunTime,currentProcess->RemainingTime,currentProcess->WaitingTime);

                }
            }
        }
        //Update remaining time of current process
        if((getClk() - lastUpdate > 0) && (currentProcess != NULL))
        {
            int clk = getClk();
            if(startTime == -1)     //if it's the first one
            {
                currentRuning += (clk - currentProcess->ArrivalTime);
                startTime = 1;
            }
            else
            {
                currentRuning += (clk - lastUpdate);
                currentRemaining -= (clk - lastUpdate);
            }
            lastUpdate = clk;
            if((readyQueue.head == NULL) && ( currentRuning>= Quantum))      //if there is no processes in the queue take another quantum
            {
                currentRuning = 0;
            }
            message.remainig = currentRemaining;
            msgsnd(msgq, &message, sizeof(message.remainig), !IPC_NOWAIT);      //send the new remaining time
        }
    }
    //for the last process
    while(currentRemaining > 0 || (getClk() - lastUpdate > 0))
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
    writeLogs_memory(m, LastFinish, currentProcess->memory, currentProcess->Id, currentProcess->Sector->s, currentProcess->Sector->e, false);
    writeLogs_scheduler(fptr,stat_loc>>8,currentProcess->Id,"finished",currentProcess->ArrivalTime,currentProcess->RunTime,0,currentProcess->WaitingTime);
    free(currentProcess);
}

// ==================================================
// =============== Printing Functions ===============
// ==================================================

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

void writeLogs_scheduler(FILE *fptr,int time,int process,char* state,int w,int z,int y,int k){
    if( state == "finished"){
        float TA = (w == 1)?time:time - w ;
        float WTA;
        if(z)
            WTA = TA/(z);
        else
            WTA = TA;
        TTA+=TA;
        TWTA+=WTA;
        TWT+=k;
        insertArray(&Wt,WTA);
        ProcessesNum+=1;
        fprintf(fptr,"At time %d process %d %s arr %d total %d remain %d wait %d  TA %.2f WTA %.2f \n",time,process,state,w,z,y,k,TA,WTA);
    }
    else {
        fprintf(fptr,"At time %d process %d %s arr %d total %d remain %d wait %d \n",time,process,state,w,z,y,k);
    }
}

// utility function to write the memory
//  arguments:
//          - fptr ==> pointer to the file 
//          - time ==> current clock
//          - bytes ==> allocated or deallocated bytes
//          - process ==> process id
//          - start ==> start addres allocated/deallocated
//          - end ==> end addres allocated/deallocated
//          - flag ==> which determine if this process allocated or deallocated
void writeLogs_memory(FILE *fptr, int time, int bytes, int process, int start, int end, bool flag)
{
    if(flag)
    {
        fprintf(fptr,"At time %d allocated %d bytes for process %d from %d to %d\n", time, bytes, process, start, end);
    }
    else {
        fprintf(fptr,"At time %d freed %d bytes from process %d from %d to %d\n", time, bytes, process, start, end);
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


