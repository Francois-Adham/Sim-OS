#include "headers.h"

/* Modify this file as needed*/
int remainingtime, msgq;
int main(int agrc, char * argv[])
{
    initClk();
    struct remain message;
    remainingtime = atoi(argv[1]);
    msgq = msgget(60, 0666 | IPC_CREAT);
    if(msgq == -1)
    {
        fprintf(stderr, "Error Creating the msg queue");
        exit(-1);
    }
    while(remainingtime > 0)
    {
        msgrcv(msgq, &message, sizeof(message.remainig), 0, !IPC_NOWAIT);
        remainingtime = message.remainig;
    }
    int clk = getClk();
    destroyClk(false);
    exit(clk);
}
