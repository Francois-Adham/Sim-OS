#include "headers.h"

// signal handler to clear the resources
void clearResources(int);

int msgq_id, schedulerPid, clkPid, stat_loc;

// boolean to indicate whether the signal is sent due to termination of the scheduler
// or it's an interrupt from the keyboard
bool interrupt = true;


int main(int argc, char * argv[])
{
    signal(SIGINT, clearResources);
    
    struct process arr[100000];
    FILE * file_pointer;
    file_pointer = fopen("processes.txt", "r");

    // read the first line and discard it
    char str1[3], str2[7], str3[7], str4[8];
    fscanf(file_pointer, "%s %s %s %s", str1, str2, str3, str4);
    
    // loop to read the processes
    int p_num=0;
    while(1){
        long id,arrival,runt,p;
        if(fscanf(file_pointer, "%ld %ld %ld %ld", &id,&arrival,&runt,&p)!=EOF)
        {
            arr[p_num].arrivalTime = arrival;
            arr[p_num].runTime = runt;
            arr[p_num].priority = p;
            arr[p_num].id = p_num+1;
            p_num++;
        }
        else break;
    }

    // let the user to choose the algorithm
    char ch, Q[2];
    printf("Choose scheduler algorithm \n");
    printf("1 for HPF \n");
    printf("2 for SRTN \n");
    printf("3 for RR \n");
    scanf("%c", &ch);
    
    if(ch=='3')
    {
        // let the user to choose Quantum if the algorithm is RR
        printf("Please enter your Quantum : \n");
        scanf("%s", Q);
    }

    // loop to fork the scheduler and the clk 
    int pid;
    for(int i = 0; i < 2; i++)
    {
        pid = fork();
        if (pid == -1)
      	    perror("error in fork");
  	
        else if (!pid)
        {
            if(!i)
                execl("clk.out", "clk.out", NULL);
            else
                execl("scheduler.out", "scheduler.out", &ch, &Q, NULL);    
        }
        // save the scheduler and clk ids
        if(!i)
            clkPid = pid;
        else
            schedulerPid = pid;
    }
    initClk();
    

    key_t key_id;
    int send_val;

    key_id = ftok("key", 65);
    msgq_id = msgget(key_id, 0666 | IPC_CREAT);

    if (msgq_id == -1)
    {
        perror("Error in create");
        exit(-1);
    }

    struct msgbuffer message;

    int Iterator=0;
    long CurrentClk=-1;
    bool flag;

    // loop to send the processes to the scheduler at the correct clk using a msg queue
    while(Iterator<p_num)
    {
        flag = false;
        // To get time use this
        if(CurrentClk != getClk())
        {
            CurrentClk = getClk();
            for(int i = Iterator; i <= p_num; i++)
            {
                if(arr[i].arrivalTime==CurrentClk)
                {
                    message.mtype = 7; 
                    message.id = arr[i].id;
                    message.arrivalTime = arr[i].arrivalTime;
                    message.runTime = arr[i].runTime;
                    message.priority = arr[i].priority;
                    send_val = msgsnd(msgq_id, &message, sizeof(message), !IPC_NOWAIT);
                    if (send_val == -1)
                        perror("Error in send");
                    flag = true;
                }
                else
                {
                    Iterator=i;
                    break;
                }
            }
            // send signal to the scheduler to receive the sent processes
            if(flag)
            {
                kill(pid, SIGUSR1);
            }
        }
    }
    // delay to let the scheduler receive the sent processes
    sleep(2);
    
    // send signal to scheduler to inform that we've finished
    kill(schedulerPid, SIGUSR2);
    
    // wait for the scheduler to terminate
    waitpid(schedulerPid, &stat_loc, 0);

    // de-attaching the clk and terminating all the other processes
    interrupt = false;
    destroyClk(true);
}

void clearResources(int signum)
{
    msgctl(msgq_id, IPC_RMID, (struct msqid_ds *)0);
    if(interrupt)
    {
        kill(schedulerPid, SIGINT);
        kill(clkPid, SIGINT);
        waitpid(schedulerPid, &stat_loc, 0);
    }
}
