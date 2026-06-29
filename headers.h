#include <stdio.h> //if you don't use scanf/printf change this include
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
#include <math.h>
#include <string.h>
#include <time.h>
#define MSG_QUEUE_KEY 111
int msgqueue;

typedef short bool;
#define true 1
#define false 0

// states
#define READY 0
#define RUNNING 1
#define FINISHED 2

//
#define INT_MAX 100

#define SHKEY 300
#define NEW_PROCESS 1
#define PROCESS_FINISHED 2
///==============================
// don't mess with this variable//
int *shmaddr; //
//===============================

struct prcss
{
    pid_t pid;
    int AT;
    int RT;
    int Pri;
    int ST;
    int FT;
    int state;
    int WT;
    int RemTime;
    int original_pid;
    int last_run_time; // Last time the process was scheduled (for RR)
    int TA;
    float WTA;
    int wait_time; // Add this line
    int memsize;
};

struct msgbuf
{
    long mtype;
    struct prcss msg;
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
        // Make sure that the clock exists
        printf("Wait! The clock not initialized yet!\n");
        sleep(1);
        shmid = shmget(SHKEY, 4, 0444);
    }
    shmaddr = (int *)shmat(shmid, (void *)0, 0);
};

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
