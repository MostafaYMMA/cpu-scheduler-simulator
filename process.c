#include "headers.h"
#include <sys/msg.h>

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: process <runtime> <original_pid>\n");
        exit(1);
    }

    initClk();

    int remainingtime = atoi(argv[1]);
    int original_pid = atoi(argv[2]);

    while (remainingtime > 0)
    {
        remainingtime--;
        sleep(1);
    }

    // Notify scheduler with both PIDs
    int msqid = msgget(MSG_QUEUE_KEY, 0666);
    if (msqid != -1)
    {
        struct msgbuf msg;
        msg.mtype = PROCESS_FINISHED;
        msg.msg.pid = getpid();              // System PID
        msg.msg.original_pid = original_pid; // Original PID from input
        msgsnd(msqid, &msg, sizeof(struct prcss), 0);
    }

    destroyClk(false);
    return 0;
}