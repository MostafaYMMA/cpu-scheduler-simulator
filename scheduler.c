#include "headers.h"
#include <sys/msg.h>
#include <errno.h>
#include <math.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// Memory management constants
#define TOTAL_MEMORY_SIZE 1024 // Total memory size in bytes
#define MAX_PROCESS_SIZE 256   // Maximum memory a process can request
#define MIN_BLOCK_SIZE 8       // Minimum block size for buddy system

// Block status codes
#define BLOCK_FREE 0
#define BLOCK_ALLOCATED 1

// Structure to represent a memory block in buddy system
struct MemoryBlock
{
    int start;      // Start address of block
    int size;       // Size of the block
    int status;     // Status: free or allocated
    int process_id; // ID of process that owns this block (-1 if free)
};

// Memory pool for buddy system
struct MemoryBlock memory_blocks[128]; // Array to store memory blocks (max possible blocks)
int block_count = 0;                   // Current number of blocks in the memory_blocks array

void scheduleHPF(struct prcss processes[], int count, int *running_pid, int current_time);
void scheduleSRTN(struct prcss processes[], int count, int *running_pid, int current_time);
void scheduleRR(struct prcss processes[], int count, int *running_pid, int current_time, int quantum);
void logProcessState(int time, int pid, const char *state, struct prcss *p);
void calculatePerformance(struct prcss processes[], int count);
void startProcess(struct prcss processes[], int idx, int *running_pid, int current_time);

// Memory management functions
void initializeMemory();
int allocateMemory(int process_id, int size, int current_time);
void freeMemory(int process_id, int current_time, int actual_size);
int getNextPowerOfTwo(int n);
void logMemoryAllocation(int time, int process_id, int size, int start, int end);
void logMemoryDeallocation(int time, int process_id, int start, int end, int actual_size);
void mergeBuddies();

// Add to headers.h
void lockFile(FILE *file);
void unlockFile(FILE *file);

// Add to scheduler.c
void lockFile(FILE *file)
{
    if (file == NULL)
        return;
    while (flock(fileno(file), LOCK_EX) != 0)
    {
        // Wait if lock is held by another process
        usleep(1000);
    }
}

void unlockFile(FILE *file)
{
    if (file == NULL)
        return;
    flock(fileno(file), LOCK_UN);
}
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: scheduler <algorithm> [quantum]\n");
        exit(1);
    }

    initClk();

    // Initialize memory system
    initializeMemory();

    // Create memory log file
    FILE *memLogFile = fopen("memory.log", "w");
    if (memLogFile == NULL)
    {
        perror("fopen memory.log");
        exit(1);
    }
    fprintf(memLogFile, "#At time t: [allocated/deallocated] n bytes for process p from address i to j\n");
    fclose(memLogFile);

    int msgqueue = msgget(MSG_QUEUE_KEY, 0666 | IPC_CREAT);
    if (msgqueue == -1)
    {
        perror("msgget");
        exit(1);
    }

    struct prcss processes[100] = {0};
    int process_count = 0;
    int running_pid = -1;
    int quantum = (argc > 2) ? atoi(argv[2]) : 1;

    FILE *logFile = fopen("scheduler.log", "w");
    if (logFile == NULL)
    {
        perror("fopen scheduler.log");
        exit(1);
    }
    fprintf(logFile, "#At time x process y state arr w total z remain y wait k\n");
    fclose(logFile);

    while (1)
    {
        int current_time = getClk();

        struct msgbuf msg;
        while (msgrcv(msgqueue, &msg, sizeof(struct prcss), NEW_PROCESS, IPC_NOWAIT) != -1)
        {
            if (process_count >= 100)
            {
                fprintf(stderr, "Maximum process count reached\n");
                continue;
            }
            processes[process_count] = msg.msg;
            processes[process_count].state = READY;
            processes[process_count].RemTime = processes[process_count].RT;
            processes[process_count].original_pid = msg.msg.pid;
            processes[process_count].last_run_time = -1;

            // Allocate memory for the process
            int allocated = allocateMemory(processes[process_count].original_pid,
                                           processes[process_count].memsize,
                                           current_time);

            if (allocated)
            {
                logProcessState(current_time, msg.msg.pid, "arrived", &processes[process_count]);
                process_count++;
            }
            else
            {
                // If memory allocation fails, process can't enter the system
                fprintf(stderr, "Memory allocation failed for process %d\n", msg.msg.pid);
                // We could implement a wait list here for processes that can't be allocated yet
            }
        }

        while (msgrcv(msgqueue, &msg, sizeof(struct prcss), PROCESS_FINISHED, IPC_NOWAIT) != -1)
        {
            for (int i = 0; i < process_count; i++)
            {
                if (processes[i].pid == msg.msg.pid)
                {
                    // Free memory used by the process BEFORE changing process state
                    freeMemory(processes[i].original_pid, current_time, processes[i].memsize);

                    processes[i].FT = current_time;
                    processes[i].state = FINISHED;
                    processes[i].RemTime = 0;
                    running_pid = -1;

                    logProcessState(current_time, processes[i].original_pid, "finished", &processes[i]);
                    break;
                }
            }
        }

        switch (atoi(argv[1]))
        {
        case 1:
            scheduleSRTN(processes, process_count, &running_pid, current_time);
            break;
        case 2:
            scheduleHPF(processes, process_count, &running_pid, current_time);
            break;
        case 3:
            scheduleRR(processes, process_count, &running_pid, current_time, quantum);
            break;
        default:
            printf("Invalid scheduling algorithm\n");
            exit(1);
        }

        int all_finished = 1;
        for (int i = 0; i < process_count; i++)
        {
            if (processes[i].state != FINISHED)
            {
                all_finished = 0;
                break;
            }
        }

        if (all_finished && process_count > 0)
        {
            calculatePerformance(processes, process_count);
            break;
        }

        usleep(10000);
    }

    destroyClk(true);
    return 0;
}

// Initialize the memory system with a single large free block
void initializeMemory()
{
    memory_blocks[0].start = 0;
    memory_blocks[0].size = TOTAL_MEMORY_SIZE;
    memory_blocks[0].status = BLOCK_FREE;
    memory_blocks[0].process_id = -1;
    block_count = 1;
}

// Round up to the next power of 2
int getNextPowerOfTwo(int n)
{
    int power = 1;
    while (power < n)
    {
        power *= 2;
    }
    return power;
}

// Allocate memory for a process using buddy memory allocation
int allocateMemory(int process_id, int size, int current_time)
{
    // Round up the requested size to the next power of 2
    int required_size = getNextPowerOfTwo(size);

    // Make sure request doesn't exceed maximum size
    if (required_size > MAX_PROCESS_SIZE)
    {
        printf("Memory request for process %d exceeds maximum allocation size\n", process_id);
        return 0;
    }

    // Find the smallest free block that can fit this request
    int best_fit_idx = -1;
    int best_fit_size = TOTAL_MEMORY_SIZE + 1; // Start with a size larger than any possible block

    for (int i = 0; i < block_count; i++)
    {
        if (memory_blocks[i].status == BLOCK_FREE &&
            memory_blocks[i].size >= required_size &&
            memory_blocks[i].size < best_fit_size)
        {
            best_fit_idx = i;
            best_fit_size = memory_blocks[i].size;
        }
    }

    // If no suitable block found
    if (best_fit_idx == -1)
    {
        return 0;
    }

    // Keep splitting the block until we have the right size
    int current_idx = best_fit_idx;
    while (memory_blocks[current_idx].size > required_size)
    {
        // Split the block into two equal parts
        int new_size = memory_blocks[current_idx].size / 2;
        int new_start = memory_blocks[current_idx].start + new_size;

        // Create the buddy block
        memory_blocks[block_count].start = new_start;
        memory_blocks[block_count].size = new_size;
        memory_blocks[block_count].status = BLOCK_FREE;
        memory_blocks[block_count].process_id = -1;

        // Update the current block
        memory_blocks[current_idx].size = new_size;

        block_count++;
    }

    // Allocate the block to the process
    memory_blocks[current_idx].status = BLOCK_ALLOCATED;
    memory_blocks[current_idx].process_id = process_id;

    // Log the memory allocation
    int end = memory_blocks[current_idx].start + memory_blocks[current_idx].size - 1;
    logMemoryAllocation(current_time, process_id, size, memory_blocks[current_idx].start, end);

    return 1;
}

// Free memory used by a process
void freeMemory(int process_id, int current_time, int actual_size)
{
    int found = 0;
    for (int i = 0; i < block_count; i++)
    {
        if (memory_blocks[i].status == BLOCK_ALLOCATED && memory_blocks[i].process_id == process_id)
        {
            // Log the memory deallocation BEFORE changing the block status
            int end = memory_blocks[i].start + memory_blocks[i].size - 1;
            logMemoryDeallocation(current_time, process_id, memory_blocks[i].start, end, actual_size);

            // Free the block
            memory_blocks[i].status = BLOCK_FREE;
            memory_blocks[i].process_id = -1;
            found = 1;

            // Try to merge with buddies to reduce fragmentation
            mergeBuddies();
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "Warning: Attempted to free memory for process %d but no allocated block was found\n", process_id);
    }
}

// Merge adjacent free buddy blocks
void mergeBuddies()
{
    int merged;

    do
    {
        merged = 0;

        // Sort blocks by start address to make merging easier
        for (int i = 0; i < block_count - 1; i++)
        {
            for (int j = 0; j < block_count - i - 1; j++)
            {
                if (memory_blocks[j].start > memory_blocks[j + 1].start)
                {
                    // Swap blocks
                    struct MemoryBlock temp = memory_blocks[j];
                    memory_blocks[j] = memory_blocks[j + 1];
                    memory_blocks[j + 1] = temp;
                }
            }
        }

        // Try to merge adjacent buddy blocks
        for (int i = 0; i < block_count - 1; i++)
        {
            // Two blocks can be merged if:
            // 1. Both are free
            // 2. They have the same size
            // 3. The first block starts at an address that is a multiple of twice their size
            // 4. They are adjacent

            if (memory_blocks[i].status == BLOCK_FREE &&
                memory_blocks[i + 1].status == BLOCK_FREE &&
                memory_blocks[i].size == memory_blocks[i + 1].size &&
                memory_blocks[i].start % (2 * memory_blocks[i].size) == 0 &&
                memory_blocks[i + 1].start == memory_blocks[i].start + memory_blocks[i].size)
            {

                // Merge the blocks
                memory_blocks[i].size *= 2;

                // Remove the second block by shifting all blocks after it
                for (int j = i + 1; j < block_count - 1; j++)
                {
                    memory_blocks[j] = memory_blocks[j + 1];
                }

                block_count--;
                merged = 1;
                break; // Start over since we modified the array
            }
        }
    } while (merged);
}

// Log memory allocation to memory.log
void logMemoryAllocation(int time, int process_id, int size, int start, int end)
{
    FILE *memLogFile = fopen("memory.log", "a");
    if (memLogFile == NULL)
    {
        perror("fopen memory.log");
        return;
    }
    lockFile(memLogFile);
    fprintf(memLogFile, "At time %d allocated %d bytes for process %d from %d to %d\n",
            time, size, process_id, start, end);
    unlockFile(memLogFile);
    fclose(memLogFile);
}

// Log memory deallocation to memory.log
void logMemoryDeallocation(int time, int process_id, int start, int end, int actual_size)
{
    FILE *memLogFile = fopen("memory.log", "a");
    if (memLogFile == NULL)
    {
        perror("fopen memory.log");
        return;
    }
    lockFile(memLogFile);
    fprintf(memLogFile, "At time %d deallocated %d bytes for process %d from %d to %d\n",
            time, actual_size, process_id, start, end);
    unlockFile(memLogFile);
    fclose(memLogFile);
}

void scheduleRR(struct prcss processes[], int count, int *running_pid, int current_time, int quantum)
{
    static int last_time = -1;
    static int remaining_quantum = 0;
    static int current_idx = -1;

    // Only act when time advances
    if (current_time == last_time)
    {
        return;
    }
    int time_elapsed = (last_time == -1) ? 1 : (current_time - last_time);
    last_time = current_time;

    // 1. Handle currently running process
    if (*running_pid != -1)
    {
        int running_idx = -1;

        // Find the running process
        for (int i = 0; i < count; i++)
        {
            if (processes[i].pid == *running_pid && processes[i].state == RUNNING)
            {
                running_idx = i;
                break;
            }
        }

        if (running_idx != -1)
        {
            // Update process state
            processes[running_idx].RemTime -= time_elapsed;
            remaining_quantum -= time_elapsed;

            // Check for completion
            if (processes[running_idx].RemTime <= 0)
            {
                processes[running_idx].RemTime = 0;
                processes[running_idx].state = FINISHED;
                processes[running_idx].FT = current_time;
                processes[running_idx].wait_time = (current_time - processes[running_idx].AT) - processes[running_idx].RT;

                logProcessState(current_time, processes[running_idx].original_pid,
                                "finished", &processes[running_idx]);

                // Terminate the process
                if (kill(*running_pid, SIGTERM) == -1 && errno != ESRCH)
                {
                    perror("RR kill failed");
                }

                *running_pid = -1;
                remaining_quantum = 0;
            }
            // Check for quantum expiration
            else if (remaining_quantum <= 0)
            {
                processes[running_idx].state = READY;
                logProcessState(current_time, processes[running_idx].original_pid,
                                "stopped", &processes[running_idx]);

                // Stop the process
                if (kill(*running_pid, SIGSTOP) == -1 && errno != ESRCH)
                {
                    perror("RR stop failed");
                }

                *running_pid = -1;
                remaining_quantum = 0;
            }
        }
    }

    // 2. Schedule new process if CPU is free
    if (*running_pid == -1)
    {
        int start_idx = (current_idx == -1) ? 0 : (current_idx + 1) % count;
        int selected = -1;

        // Find next ready process in round-robin order
        for (int rounds = 0; rounds < count; rounds++)
        {
            int idx = (start_idx + rounds) % count;

            if (processes[idx].state == READY &&
                processes[idx].AT <= current_time &&
                processes[idx].RemTime > 0)
            {
                selected = idx;
                break;
            }
        }

        if (selected != -1)
        {
            // Calculate wait time
            if (processes[selected].last_run_time == -1)
            {
                processes[selected].wait_time = current_time - processes[selected].AT;
            }
            else
            {
                processes[selected].wait_time += current_time - processes[selected].last_run_time;
            }

            // Fork new process
            pid_t pid = fork();
            if (pid == 0)
            {
                // Child process
                char rt_str[16], id_str[16];
                snprintf(rt_str, sizeof(rt_str), "%d", processes[selected].RemTime);
                snprintf(id_str, sizeof(id_str), "%d", processes[selected].original_pid);
                execl("./process.out", "./process.out", rt_str, id_str, NULL);
                exit(1);
            }
            else if (pid > 0)
            {
                // Parent process
                processes[selected].pid = pid;
                processes[selected].state = RUNNING;
                processes[selected].last_run_time = current_time;

                if (processes[selected].ST == 0)
                {
                    processes[selected].ST = current_time;
                }

                *running_pid = pid;
                current_idx = selected;
                remaining_quantum = quantum;

                const char *state = (processes[selected].ST == current_time) ? "started" : "resumed";
                logProcessState(current_time, processes[selected].original_pid, state, &processes[selected]);
            }
            else
            {
                perror("fork failed");
                exit(1);
            }
        }
    }
}
void scheduleSRTN(struct prcss processes[], int count, int *running_pid, int current_time)
{
    int selected = -1;
    int running_idx = -1;

    // 1. Update remaining time for currently running process and check for completion
    if (*running_pid != -1)
    {
        for (int i = 0; i < count; i++)
        {
            if (processes[i].pid == *running_pid && processes[i].state == RUNNING)
            {
                int elapsed = current_time - processes[i].last_run_time;
                processes[i].RemTime = max(0, processes[i].RemTime - elapsed);
                processes[i].last_run_time = current_time;
                running_idx = i;

                // Check if process has completed
                if (processes[i].RemTime == 0)
                {
                    processes[i].state = FINISHED;
                    logProcessState(current_time, processes[i].original_pid,
                                    "finished", &processes[i]);
                    *running_pid = -1;
                    running_idx = -1;
                }
                break;
            }
        }
    }

    // 2. Find the READY process with shortest remaining time
    for (int i = 0; i < count; i++)
    {
        if (processes[i].state == READY &&
            processes[i].AT <= current_time &&
            processes[i].RemTime > 0)
        {
            if (selected == -1 || processes[i].RemTime < processes[selected].RemTime)
            {
                selected = i;
            }
        }
    }

    // 3. Check if scheduling/preemption is needed
    if (selected != -1)
    {
        // Case 1: No process running - just start the selected one
        if (*running_pid == -1)
        {
            startProcess(processes, selected, running_pid, current_time);
        }
        // Case 2: Selected process has shorter remaining time than running process
        else if (processes[selected].RemTime < processes[running_idx].RemTime)
        {
            // Preempt the running process
            if (kill(*running_pid, SIGKILL) == -1)
            {
                perror("SRTN kill failed");
                exit(1);
            }
            processes[running_idx].state = READY;
            logProcessState(current_time, processes[running_idx].original_pid,
                            "preempted", &processes[running_idx]);

            // Start the new process
            startProcess(processes, selected, running_pid, current_time);
        }
    }
}

// Helper function to start a process
void startProcess(struct prcss processes[], int idx, int *running_pid, int current_time)
{
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("SRTN fork failed");
        exit(1);
    }
    else if (pid == 0)
    {
        // Child process
        char rt_str[16], id_str[16];
        snprintf(rt_str, sizeof(rt_str), "%d", processes[idx].RemTime);
        snprintf(id_str, sizeof(id_str), "%d", processes[idx].original_pid);
        execl("./process.out", "./process.out", rt_str, id_str, NULL);
        exit(1); // If execl fails
    }
    else
    {
        // Parent process
        processes[idx].pid = pid;
        processes[idx].ST = (processes[idx].ST == 0) ? current_time : processes[idx].ST;
        processes[idx].state = RUNNING;
        processes[idx].last_run_time = current_time;
        *running_pid = pid;

        const char *state_str = (processes[idx].ST == current_time) ? "started" : "resumed";
        logProcessState(current_time, processes[idx].original_pid, state_str, &processes[idx]);
    }
}

void scheduleHPF(struct prcss processes[], int count, int *running_pid, int current_time)
{
    static int last_time = -1;

    // Only make scheduling decisions when time advances
    if (current_time == last_time)
    {
        return;
    }
    int time_elapsed = (last_time == -1) ? 0 : (current_time - last_time);
    last_time = current_time;

    // 0. Mark arriving processes as READY
    for (int i = 0; i < count; i++)
    {
        if (processes[i].AT == current_time)
        {
            processes[i].state = READY;
            logProcessState(current_time, processes[i].original_pid, "arrived", &processes[i]);
        }
    }

    int running_idx = -1;

    // 1. Update state of currently running process, if any
    if (*running_pid != -1)
    {
        for (int i = 0; i < count; i++)
        {
            if (processes[i].pid == *running_pid && processes[i].state == RUNNING)
            {
                // Update remaining time based on elapsed time
                processes[i].RemTime -= time_elapsed;
                processes[i].last_run_time = current_time;
                running_idx = i;

                // Check if process has completed
                if (processes[i].RemTime <= 0)
                {
                    processes[i].RemTime = 0;
                    processes[i].state = FINISHED;
                    processes[i].FT = current_time;

                    // Calculate turnaround time and wait time
                    int TA = processes[i].FT - processes[i].AT;
                    processes[i].wait_time = TA - processes[i].RT;

                    logProcessState(current_time, processes[i].original_pid, "finished", &processes[i]);

                    if (kill(*running_pid, SIGTERM) == -1 && errno != ESRCH)
                    {
                        perror("HPF kill failed");
                    }

                    *running_pid = -1; // No process is running now
                    running_idx = -1;
                }
                break;
            }
        }
    }

    // 2. Only schedule a new process if no process is currently running
    if (*running_pid == -1)
    {
        // Find highest priority ready process (lowest Pri number)
        int selected = -1;
        for (int i = 0; i < count; i++)
        {
            if (processes[i].state == READY &&
                processes[i].AT <= current_time &&
                processes[i].RemTime > 0)
            {
                if (selected == -1 ||
                    processes[i].Pri < processes[selected].Pri ||
                    (processes[i].Pri == processes[selected].Pri &&
                     processes[i].AT < processes[selected].AT))
                {
                    selected = i;
                }
            }
        }

        // 3. Schedule the selected process
        if (selected != -1)
        {
            // Calculate wait time
            if (processes[selected].last_run_time != -1)
            {
                processes[selected].wait_time += (current_time - max(processes[selected].AT,
                                                                     processes[selected].last_run_time));
            }
            else
            {
                processes[selected].wait_time = current_time - processes[selected].AT;
            }

            pid_t pid = fork();
            if (pid == -1)
            {
                perror("HPF fork failed");
                exit(1);
            }
            else if (pid == 0)
            {
                // Child process
                char rt_str[16], id_str[16];
                snprintf(rt_str, sizeof(rt_str), "%d", processes[selected].RemTime);
                snprintf(id_str, sizeof(id_str), "%d", processes[selected].original_pid);
                execl("./process.out", "./process.out", rt_str, id_str, NULL);
                perror("HPF execl failed");
                exit(1);
            }
            else
            {
                // Parent process
                processes[selected].pid = pid;
                processes[selected].state = RUNNING;

                if (processes[selected].ST == 0)
                {
                    processes[selected].ST = current_time;
                }

                processes[selected].last_run_time = current_time;
                *running_pid = pid;

                const char *state = (processes[selected].ST == current_time) ? "started" : "resumed";
                logProcessState(current_time, processes[selected].original_pid, state, &processes[selected]);
            }
        }
    }
}

void logProcessState(int time, int pid, const char *state, struct prcss *p)
{
    FILE *log = fopen("scheduler.log", "a");
    if (!log)
        return;
    lockFile(log);

    if (strcmp(state, "finished") == 0)
    {
        int TA = time - p->AT;
        float WTA = (float)TA / p->RT;
        fprintf(log, "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA %.2f\n",
                time, pid, state, p->AT, p->RT, 0, p->wait_time, TA, WTA);
    }
    else if (strcmp(state, "started") == 0 || strcmp(state, "resumed") == 0)
    {
        fprintf(log, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, pid, state, p->AT, p->RT, p->RemTime, p->wait_time);
    }
    else if (strcmp(state, "stopped") == 0 || strcmp(state, "preempted") == 0)
    {
        fprintf(log, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, pid, state, p->AT, p->RT, p->RemTime, p->wait_time);
    }
    else
    {                     // arrived
        p->wait_time = 0; // Initialize wait time when process arrives
        fprintf(log, "At time %d process %d %s arr %d total %d remain %d\n",
                time, pid, state, p->AT, p->RT, p->RemTime);
    }
    fflush(log);
    unlockFile(log);

    fclose(log);
}

void calculatePerformance(struct prcss processes[], int count)
{
    if (count == 0)
        return;

    float total_WTA = 0, total_wait = 0, std_wta = 0;
    float wtas[100] = {0};
    float total_runtime = 0;
    int min_AT = processes[0].AT;
    int max_FT = processes[0].FT;

    for (int i = 0; i < count; i++)
    {
        if (processes[i].AT < min_AT)
            min_AT = processes[i].AT;
        if (processes[i].FT > max_FT)
            max_FT = processes[i].FT;
        total_runtime += processes[i].RT;
    }

    float total_simulation_time = max(1, max_FT - min_AT);

    for (int i = 0; i < count; i++)
    {
        int TA = processes[i].FT - processes[i].AT;
        float WTA = (float)TA / processes[i].RT;
        int wait = max(0, TA - processes[i].RT);

        total_WTA += WTA;
        total_wait += wait;
        wtas[i] = WTA;
    }

    float avg_wta = total_WTA / count;
    float avg_wait = total_wait / count;
    float cpu_utilization = min(100.0, (total_runtime / total_simulation_time) * 100);

    for (int i = 0; i < count; i++)
    {
        std_wta += pow(wtas[i] - avg_wta, 2);
    }
    std_wta = sqrt(std_wta / count);

    FILE *perf = fopen("scheduler.perf", "w");
    if (!perf)
        return;
    fprintf(perf, "CPU utilization = %.2f%%\n", cpu_utilization);
    fprintf(perf, "Avg WTA = %.2f\n", avg_wta);
    fprintf(perf, "Avg Waiting = %.2f\n", avg_wait);
    fprintf(perf, "Std WTA = %.2f\n", std_wta);
    fclose(perf);
}