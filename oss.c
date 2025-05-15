// oss.c - Operating System Simulator
#include "oss.h"
#include <getopt.h>
#include <sys/time.h>
#include <limits.h>

// Global variables
SharedMemory *shm;
int shmid, msqid;
FILE *logfile = NULL;
char logfileName[256] = "oss.log";
int totalProcesses = 0;
bool verbose = false;

// Function to handle interrupts and clean up
void interruptHandler(int sig) {
    printf("\nInterrupt received. Cleaning up and terminating...\n");
    cleanupResources(shmid, msqid);
    if (logfile != NULL) fclose(logfile);
    exit(EXIT_SUCCESS);
}

// Function to increment the simulated clock
void incrementClock(SimClock *clock, unsigned int nanoseconds) {
    clock->nanoseconds += nanoseconds;
    if (clock->nanoseconds >= 1000000000) {
        clock->seconds += clock->nanoseconds / 1000000000;
        clock->nanoseconds %= 1000000000;
    }
}

// Function to initialize shared memory
void initSharedMemory(SharedMemory *shm) {
    // Initialize clock
    shm->clock.seconds = 0;
    shm->clock.nanoseconds = 0;
    
    // Initialize process table
    for (int i = 0; i < MAX_PROC; i++) {
        shm->processes[i].pid = 0;
        shm->processes[i].state = UNUSED;
        shm->processes[i].totalMemoryAccesses = 0;
        shm->processes[i].pageFaults = 0;
        
        // Initialize page table for each process
        for (int j = 0; j < PAGES_PER_PROC; j++) {
            shm->processes[i].pageTable[j] = -1;  // -1 indicates not in memory
        }
    }
    
    // Initialize frame table
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        shm->frameTable[i].occupied = false;
        shm->frameTable[i].pid = -1;
        shm->frameTable[i].page = -1;
        shm->frameTable[i].dirtyBit = false;
        shm->frameTable[i].lastRefSec = 0;
        shm->frameTable[i].lastRefNano = 0;
    }
    
    shm->activeProcesses = 0;
}

// Function to find an unused PCB entry
int findUnusedPCB() {
    for (int i = 0; i < MAX_PROC; i++) {
        if (shm->processes[i].state == UNUSED) {
            return i;
        }
    }
    return -1;  // No unused PCB found
}

// Function to find an empty frame
int findEmptyFrame() {
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (!shm->frameTable[i].occupied) {
            return i;
        }
    }
    return -1;  // No empty frame found
}

// Function to find the LRU frame
int findLRUFrame() {
    unsigned int minSec = UINT_MAX;
    unsigned int minNano = UINT_MAX;
    int lruFrame = -1;
    
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (shm->frameTable[i].occupied) {
            if (shm->frameTable[i].lastRefSec < minSec || 
                (shm->frameTable[i].lastRefSec == minSec && 
                 shm->frameTable[i].lastRefNano < minNano)) {
                minSec = shm->frameTable[i].lastRefSec;
                minNano = shm->frameTable[i].lastRefNano;
                lruFrame = i;
            }
        }
    }
    
    return lruFrame;
}

// Function to display memory map
void displayMemoryMap(FILE *logfile) {
    fprintf(logfile, "Current memory layout at time %u:%u is:\n", 
            shm->clock.seconds, shm->clock.nanoseconds);
    fprintf(stdout, "Current memory layout at time %u:%u is:\n", 
            shm->clock.seconds, shm->clock.nanoseconds);
    
    fprintf(logfile, "%-8s %-10s %-10s %-10s %-10s\n", 
            "Frame", "Occupied", "DirtyBit", "LastRefS", "LastRefNano");
    fprintf(stdout, "%-8s %-10s %-10s %-10s %-10s\n", 
            "Frame", "Occupied", "DirtyBit", "LastRefS", "LastRefNano");
    
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        fprintf(logfile, "Frame %-3d: %-10s %-10d %-10u %-10u\n", 
                i, 
                shm->frameTable[i].occupied ? "Yes" : "No", 
                shm->frameTable[i].dirtyBit ? 1 : 0,
                shm->frameTable[i].lastRefSec,
                shm->frameTable[i].lastRefNano);
        fprintf(stdout, "Frame %-3d: %-10s %-10d %-10u %-10u\n", 
                i, 
                shm->frameTable[i].occupied ? "Yes" : "No", 
                shm->frameTable[i].dirtyBit ? 1 : 0,
                shm->frameTable[i].lastRefSec,
                shm->frameTable[i].lastRefNano);
    }
    
    // Display page tables for each active process
    for (int i = 0; i < MAX_PROC; i++) {
        if (shm->processes[i].state != UNUSED) {
            fprintf(logfile, "P%d page table: [ ", i);
            fprintf(stdout, "P%d page table: [ ", i);
            
            for (int j = 0; j < PAGES_PER_PROC; j++) {
                fprintf(logfile, "%d ", shm->processes[i].pageTable[j]);
                fprintf(stdout, "%d ", shm->processes[i].pageTable[j]);
            }
            
            fprintf(logfile, "]\n");
            fprintf(stdout, "]\n");
        }
    }
    fprintf(logfile, "\n");
    fprintf(stdout, "\n");
}

// Function to handle page fault
void handlePageFault(int procIndex, int page, bool isWrite) {
    int frameIndex;
    
    // Find an empty frame or use LRU
    frameIndex = findEmptyFrame();
    if (frameIndex == -1) {
        // No empty frame, use LRU
        frameIndex = findLRUFrame();
        
        // Get the process and page using this frame
        int oldPid = shm->frameTable[frameIndex].pid;
        int oldPage = shm->frameTable[frameIndex].page;
        
        // Find the PCB index for this process
        int oldProcIndex = -1;
        for (int i = 0; i < MAX_PROC; i++) {
            if (shm->processes[i].pid == oldPid && shm->processes[i].state != UNUSED) {
                oldProcIndex = i;
                break;
            }
        }
        
        if (oldProcIndex != -1) {
            // Update the page table of the process that was using this frame
            shm->processes[oldProcIndex].pageTable[oldPage] = -1;
        }
        
        fprintf(logfile, "oss: Clearing frame %d and swapping in p%d page %d\n", 
                frameIndex, procIndex, page);
        fprintf(stdout, "oss: Clearing frame %d and swapping in p%d page %d\n", 
                frameIndex, procIndex, page);
        
        // Check if the frame is dirty
        if (shm->frameTable[frameIndex].dirtyBit) {
            fprintf(logfile, "oss: Dirty bit of frame %d set, adding additional time to the clock\n", 
                    frameIndex);
            fprintf(stdout, "oss: Dirty bit of frame %d set, adding additional time to the clock\n", 
                    frameIndex);
            
            // Extra time for writing back a dirty page (additional to the normal page fault time)
            incrementClock(&shm->clock, 10000000);  // 10ms extra for writing back
        }
    }
    
    // Update frame table
    shm->frameTable[frameIndex].occupied = true;
    shm->frameTable[frameIndex].pid = shm->processes[procIndex].pid;
    shm->frameTable[frameIndex].page = page;
    shm->frameTable[frameIndex].dirtyBit = isWrite;  // Set dirty bit if it's a write
    shm->frameTable[frameIndex].lastRefSec = shm->clock.seconds;
    shm->frameTable[frameIndex].lastRefNano = shm->clock.nanoseconds;
    
    // Update page table
    shm->processes[procIndex].pageTable[page] = frameIndex;
    
    // Increment page fault counter
    shm->processes[procIndex].pageFaults++;
    
    // Simulate the time for page fault (disk read)
    incrementClock(&shm->clock, 14000000);  // 14ms for disk read
}

// Function to handle memory request
void handleMemoryRequest(int procIndex, int address, bool isWrite) {
    // Extract page number and offset
    int page = address / PAGE_SIZE;
    // Offset is not used, but calculated for completeness
    // int offset = address % PAGE_SIZE;
    
    // Check if the page is in memory
    int frameIndex = shm->processes[procIndex].pageTable[page];
    
    if (frameIndex == -1) {
        // Page fault
        fprintf(logfile, "oss: P%d requesting %s of address %d at time %u:%u\n", 
                procIndex, isWrite ? "write" : "read", address, 
                shm->clock.seconds, shm->clock.nanoseconds);
        fprintf(stdout, "oss: P%d requesting %s of address %d at time %u:%u\n", 
                procIndex, isWrite ? "write" : "read", address, 
                shm->clock.seconds, shm->clock.nanoseconds);
        
        fprintf(logfile, "oss: Address %d is not in a frame, pagefault\n", address);
        fprintf(stdout, "oss: Address %d is not in a frame, pagefault\n", address);
        
        // Handle page fault
        handlePageFault(procIndex, page, isWrite);
        
        // Get the updated frame index
        frameIndex = shm->processes[procIndex].pageTable[page];
    } else {
        // Page hit, update LRU timestamp
        shm->frameTable[frameIndex].lastRefSec = shm->clock.seconds;
        shm->frameTable[frameIndex].lastRefNano = shm->clock.nanoseconds;
        
        // Update dirty bit if it's a write
        if (isWrite) {
            shm->frameTable[frameIndex].dirtyBit = true;
        }
        
        fprintf(logfile, "oss: P%d requesting %s of address %d at time %u:%u\n", 
                procIndex, isWrite ? "write" : "read", address, 
                shm->clock.seconds, shm->clock.nanoseconds);
        fprintf(stdout, "oss: P%d requesting %s of address %d at time %u:%u\n", 
                procIndex, isWrite ? "write" : "read", address, 
                shm->clock.seconds, shm->clock.nanoseconds);
        
        fprintf(logfile, "oss: Address %d in frame %d, %s at time %u:%u\n", 
                address, frameIndex, 
                isWrite ? "writing data to frame" : "giving data to P", 
                shm->clock.seconds, shm->clock.nanoseconds);
        fprintf(stdout, "oss: Address %d in frame %d, %s at time %u:%u\n", 
                address, frameIndex, 
                isWrite ? "writing data to frame" : "giving data to P", 
                shm->clock.seconds, shm->clock.nanoseconds);
        
        // Increment clock for memory access
        incrementClock(&shm->clock, 100);  // 100ns for memory access
    }
    
    // Increment memory access counter
    shm->processes[procIndex].totalMemoryAccesses++;
}

// Function to terminate a process
void terminateProcess(int procIndex) {
    // Calculate process statistics
    double effectiveAccessTime = 0.0;
    if (shm->processes[procIndex].totalMemoryAccesses > 0) {
        effectiveAccessTime = (double)shm->processes[procIndex].pageFaults / 
                             shm->processes[procIndex].totalMemoryAccesses;
    }
    
    fprintf(logfile, "oss: Process P%d terminating at time %u:%u\n", 
            procIndex, shm->clock.seconds, shm->clock.nanoseconds);
    fprintf(stdout, "oss: Process P%d terminating at time %u:%u\n", 
            procIndex, shm->clock.seconds, shm->clock.nanoseconds);
    
    fprintf(logfile, "oss: Process P%d statistics:\n", procIndex);
    fprintf(stdout, "oss: Process P%d statistics:\n", procIndex);
    
    fprintf(logfile, "      Total memory accesses: %d\n", 
            shm->processes[procIndex].totalMemoryAccesses);
    fprintf(stdout, "      Total memory accesses: %d\n", 
            shm->processes[procIndex].totalMemoryAccesses);
    
    fprintf(logfile, "      Total page faults: %d\n", 
            shm->processes[procIndex].pageFaults);
    fprintf(stdout, "      Total page faults: %d\n", 
            shm->processes[procIndex].pageFaults);
    
    fprintf(logfile, "      Effective memory access time: %.6f\n", 
            effectiveAccessTime);
    fprintf(stdout, "      Effective memory access time: %.6f\n", 
            effectiveAccessTime);
    
    // Free all frames used by this process
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (shm->frameTable[i].occupied && 
            shm->frameTable[i].pid == shm->processes[procIndex].pid) {
            shm->frameTable[i].occupied = false;
            shm->frameTable[i].pid = -1;
            shm->frameTable[i].page = -1;
            shm->frameTable[i].dirtyBit = false;
        }
    }
    
    // Mark the process as terminated
    shm->processes[procIndex].state = TERMINATED;
    shm->activeProcesses--;
}

// Function to clean up resources
void cleanupResources(int shmid, int msqid) {
    // Detach and remove shared memory
    if (shmid > 0) {
        shmdt(shm);
        shmctl(shmid, IPC_RMID, NULL);
    }
    
    // Remove message queue
    if (msqid > 0) {
        msgctl(msqid, IPC_RMID, NULL);
    }
}

int main(int argc, char *argv[]) {
    // Default values
    int opt;
    int maxProcesses = 100;
    int maxConcurrent = 18;
    int launchInterval = 1000;  // Default to 1000ms
    struct timeval startTime, currentTime;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "hn:s:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: oss [-h] [-n proc] [-s simul] [-i interval] [-f logfile]\n");
                printf("Options:\n");
                printf("  -h            : Show this help message\n");
                printf("  -n proc       : Total number of processes to launch (default: 100)\n");
                printf("  -s simul      : Maximum number of concurrent processes (default: 18)\n");
                printf("  -i interval   : Interval in ms to launch processes (default: 1000)\n");
                printf("  -f logfile    : Log file name (default: oss.log)\n");
                exit(EXIT_SUCCESS);
            case 'n':
                maxProcesses = atoi(optarg);
                if (maxProcesses <= 0) {
                    fprintf(stderr, "Invalid number of processes\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 's':
                maxConcurrent = atoi(optarg);
                if (maxConcurrent <= 0 || maxConcurrent > MAX_PROC) {
                    fprintf(stderr, "Invalid number of concurrent processes (max is %d)\n", MAX_PROC);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'i':
                launchInterval = atoi(optarg);
                if (launchInterval <= 0) {
                    fprintf(stderr, "Invalid launch interval\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                strncpy(logfileName, optarg, 255);
                break;
            default:
                fprintf(stderr, "Invalid option. Use -h for help.\n");
                exit(EXIT_FAILURE);
        }
    }
    
    // Open log file
    logfile = fopen(logfileName, "w");
    if (logfile == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    // Set up signal handlers
    signal(SIGINT, interruptHandler);
    signal(SIGTERM, interruptHandler);
    
    // Print size of SharedMemory for diagnostics
    fprintf(stdout, "Size of SharedMemory: %lu bytes\n", sizeof(SharedMemory));
    
    // Create shared memory - use IPC_PRIVATE to ensure unique ID
    shmid = shmget(IPC_PRIVATE, sizeof(SharedMemory), IPC_CREAT | PERMS);
    if (shmid < 0) {
        perror("Failed to create shared memory");
        fclose(logfile);
        exit(EXIT_FAILURE);
    }
    
    fprintf(stdout, "Created shared memory with ID: %d\n", shmid);
    
    // Attach to shared memory
    shm = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("Failed to attach to shared memory");
        cleanupResources(shmid, -1);
        fclose(logfile);
        exit(EXIT_FAILURE);
    }
    
    // Create message queue
    msqid = msgget(MSG_KEY, IPC_CREAT | PERMS);
    if (msqid < 0) {
        perror("Failed to create message queue");
        cleanupResources(shmid, -1);
        fclose(logfile);
        exit(EXIT_FAILURE);
    }
    
    // Initialize shared memory
    initSharedMemory(shm);
    
    // Record start time
    gettimeofday(&startTime, NULL);
    
    // Main loop
    unsigned int nextLaunchTime = 0;
    unsigned int lastMapTime = 0;
    int processesSoFar = 0;
    
    fprintf(logfile, "oss: Starting simulation with max %d processes, %d concurrent\n", 
            maxProcesses, maxConcurrent);
    fprintf(stdout, "oss: Starting simulation with max %d processes, %d concurrent\n", 
            maxProcesses, maxConcurrent);
    
    while (processesSoFar < maxProcesses || shm->activeProcesses > 0) {
        // Check if we should exit due to time limit (5 seconds)
        gettimeofday(&currentTime, NULL);
        if ((currentTime.tv_sec - startTime.tv_sec) >= 5) {
            fprintf(logfile, "oss: Time limit reached. Terminating...\n");
            fprintf(stdout, "oss: Time limit reached. Terminating...\n");
            break;
        }
        
        // Check if we should launch a new process
        if (processesSoFar < maxProcesses && 
            shm->activeProcesses < maxConcurrent && 
            shm->clock.nanoseconds >= nextLaunchTime) {
            
            // Find an unused PCB entry
            int procIndex = findUnusedPCB();
            if (procIndex != -1) {
                pid_t pid = fork();
                
                if (pid < 0) {
                    perror("Failed to fork");
                    continue;
                } else if (pid == 0) {
                    // Child process
                    char procIndexStr[10], shmidStr[10];
                    sprintf(procIndexStr, "%d", procIndex);
                    sprintf(shmidStr, "%d", shmid);
                    
                    execl("./user", "./user", procIndexStr, shmidStr, NULL);
                    perror("Failed to exec user process");
                    exit(EXIT_FAILURE);
                } else {
                    // Parent process
                    shm->processes[procIndex].pid = pid;
                    shm->processes[procIndex].state = RUNNING;
                    shm->processes[procIndex].startSec = shm->clock.seconds;
                    shm->processes[procIndex].startNano = shm->clock.nanoseconds;
                    shm->activeProcesses++;
                    processesSoFar++;
                    
                    fprintf(logfile, "oss: Process P%d created at time %u:%u\n", 
                            procIndex, shm->clock.seconds, shm->clock.nanoseconds);
                    fprintf(stdout, "oss: Process P%d created at time %u:%u\n", 
                            procIndex, shm->clock.seconds, shm->clock.nanoseconds);
                    
                    // Set next launch time
                    nextLaunchTime = shm->clock.nanoseconds + (launchInterval * 1000000);
                    if (nextLaunchTime >= 1000000000) {
                        nextLaunchTime %= 1000000000;
                    }
                }
            }
        }
        
        // Display memory map every second of simulated time
        if (shm->clock.seconds > lastMapTime) {
            displayMemoryMap(logfile);
            lastMapTime = shm->clock.seconds;
        }
        
        // Process messages from user processes
        Message msg;
        if (msgrcv(msqid, &msg, sizeof(Message) - sizeof(long), 0, IPC_NOWAIT) >= 0) {
            // Find the process in the PCB
            int procIndex = -1;
            for (int i = 0; i < MAX_PROC; i++) {
                if (shm->processes[i].pid == msg.pid && shm->processes[i].state != UNUSED) {
                    procIndex = i;
                    break;
                }
            }
            
            if (procIndex != -1) {
                if (msg.terminated) {
                    // Process is terminating
                    terminateProcess(procIndex);
                } else {
                    // Process is requesting memory access
                    handleMemoryRequest(procIndex, msg.address, msg.isWrite);
                    
                    // Send response back to the process
                    msg.mtype = RESPONSE;
                    if (msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0) < 0) {
                        perror("Failed to send response message");
                    }
                }
            }
        }
        
        // Increment the clock
        incrementClock(&shm->clock, 1000);  // Increment by 1000ns each iteration
    }
    
    // Print final statistics
    fprintf(logfile, "\nFinal Statistics:\n");
    fprintf(stdout, "\nFinal Statistics:\n");
    
    int totalMemoryAccesses = 0;
    int totalPageFaults = 0;
    
    for (int i = 0; i < MAX_PROC; i++) {
        if (shm->processes[i].state != UNUSED) {
            totalMemoryAccesses += shm->processes[i].totalMemoryAccesses;
            totalPageFaults += shm->processes[i].pageFaults;
        }
    }
    
    fprintf(logfile, "Total processes: %d\n", processesSoFar);
    fprintf(stdout, "Total processes: %d\n", processesSoFar);
    
    fprintf(logfile, "Total memory accesses: %d\n", totalMemoryAccesses);
    fprintf(stdout, "Total memory accesses: %d\n", totalMemoryAccesses);
    
    fprintf(logfile, "Total page faults: %d\n", totalPageFaults);
    fprintf(stdout, "Total page faults: %d\n", totalPageFaults);
    
    double accessesPerSecond = 0.0;
    if (shm->clock.seconds > 0 || shm->clock.nanoseconds > 0) {
        double totalSeconds = shm->clock.seconds + (shm->clock.nanoseconds / 1000000000.0);
        accessesPerSecond = totalMemoryAccesses / totalSeconds;
    }
    
    fprintf(logfile, "Memory accesses per second: %.2f\n", accessesPerSecond);
    fprintf(stdout, "Memory accesses per second: %.2f\n", accessesPerSecond);
    
    double faultsPerAccess = 0.0;
    if (totalMemoryAccesses > 0) {
        faultsPerAccess = (double)totalPageFaults / totalMemoryAccesses;
    }
    
    fprintf(logfile, "Page faults per memory access: %.6f\n", faultsPerAccess);
    fprintf(stdout, "Page faults per memory access: %.6f\n", faultsPerAccess);
    
    // Clean up resources
    cleanupResources(shmid, msqid);
    fclose(logfile);
    
    return EXIT_SUCCESS;
}
