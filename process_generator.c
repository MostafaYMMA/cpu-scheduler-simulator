#include "headers.h"

void clearResources(int);

int main(int argc, char *argv[])
{
    signal(SIGINT, clearResources);
    // TODO Initialization
    // 1. Read the input files.
    // 2. Ask the user for the chosen scheduling algorithm and its parameters, if there are any.
    // 3. Initiate and create the scheduler and clock processes.
    // 4. Use this function after creating the clock process to initialize clock
    char *filename = argv[1]; // file

    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Error opening file");
        exit(1);
    }
    int pri1;
    int pid1;
    int AT1;
    int RT1;
    int memsize1;

    struct prcss *prccsarray[5];

    int c = 0;
    while (fscanf(file, "%d %d %d %d %d", &pid1, &AT1, &RT1, &pri1, &memsize1) != -1)
    {
        struct prcss *prc = (struct prcss *)malloc(sizeof(struct prcss));
        if (prc == NULL)
        {
            perror("malloc failed");
            exit(1);
        }

        prc->AT = AT1;
        prc->pid = pid1;
        prc->Pri = pri1;
        prc->RT = RT1;
        prc->RemTime = RT1;
        prc->memsize = memsize1;
        prccsarray[c] = prc;
        c++;
    }
    for (int i = 0; i < 5; i++)
    {
        if (prccsarray[i])
            printf("%d\n", prccsarray[i]->AT);
    }
    printf("Choose the required shceuling algorithm\n1 for SRTN\n2 for HPF\n3 for RR\n");
    int alg;
    int Q = 0;
    scanf("%d", &alg);
    if (alg == 3)
    {
        printf("Please Enter the Quantum number for RR\n");
        scanf("%d", &Q);
    }
    char algStr[10];
    char qStr[10];
    sprintf(algStr, "%d", alg);
    sprintf(qStr, "%d", Q);

    msgqueue = msgget(MSG_QUEUE_KEY, IPC_CREAT | 0666);

    pid_t myscheduler = fork();
    if (myscheduler == 0)
    {
        printf("sch\n");
        execl("./scheduler.out", "./scheduler.out", algStr, qStr, (char *)NULL);
        perror("Failed to start scheduler\n");
        exit(1);
    }

    pid_t clk_pid = fork();
    if (clk_pid == 0)
    {
        printf("clkl\n");
        execl("./clk.out", "./clk.out", NULL);
        perror("Failed to start Clock\n");
        exit(1);
    }

    initClk();

    int curr_time = 0;
    int sent_processes = 0;
    bool sent[c];
    for (int i = 0; i < c; i++)
    {
        sent[i] = false;
    }

    while (sent_processes < c)
    {
        // printf("current time is %d\n", curr_time);
        curr_time = getClk();
        for (int i = 0; i < c; i++)
        {
            if (prccsarray[i]->AT == curr_time && !sent[i])
            {

                struct msgbuf msg1;
                msg1.mtype = NEW_PROCESS;
                msg1.msg = *prccsarray[i];
                msgsnd(msgqueue, &msg1, sizeof(struct prcss), 0);
                sent[i] = true;
                // printf("Messagesent at %d\n", getClk());
                //  printf("Messagesent ID:%d ,Arrival Time:%d ,RunTime:%d , Priority:%d \n", prccsarray[i]->pid, prccsarray[i]->AT, prccsarray[i]->RT, prccsarray[i]->Pri);
                sent_processes++;
            }
        }
        usleep(10000); // Small delay to avoid tight loop
    }
    //  exit(0);

    // TODO Generation Main Loop
    // 5. Create a data structure for processes and provide it with its parameters.
    // 6. Send the information to the scheduler at the appropriate time.
    // 7. Clear clock resources
    while (wait(NULL) > 0)
        destroyClk(true);
}

void clearResources(int signum)
{
    msgctl(msgqueue, IPC_RMID, NULL);
    exit(1);
}