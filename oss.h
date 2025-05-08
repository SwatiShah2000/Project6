// oss.h - Header file for shared structures and constants
#ifndef OSS_H
#define OSS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#define SHM_KEY 0x1234        // Key for shared memory
#define MSG_KEY 0x5678        // Key for message queue
#define PERMS 0644            // Permissions

#define MAX_PROC 18           // Maximum concurrent processes
#define TOTAL_PROC 100        // Total process limit
#define MEMORY_SIZE 131072    // 128K total memory
#define PAGE_SIZE 1024        // 1K page size
#define TOTAL_FRAMES 128      // Total frames in memory
#define PAGES_PER_PROC 32     // Each process has 32K of memory, so 32 pages

// Process states
#define UNUSED 0
#define RUNNING 1
#define BLOCKED 2
#define TERMINATED 3

// Message types
#define REQUEST 1
#define RESPONSE 2
#define TERMINATE 3

// Shared clock structure
typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

// Frame table entry
typedef struct {
    bool occupied;            // Is the frame occupied?
    int pid;                  // Process ID using this frame
    int page;                 // Page number stored in this frame
    bool dirtyBit;            // Has the page been written to?
    unsigned int lastRefSec;  // Last reference time (seconds)
    unsigned int lastRefNano; // Last reference time (nanoseconds)
} FrameTableEntry;

// Page table entry
typedef struct {
    int frame;                // Frame number where this page is stored (-1 if not in memory)
} PageTableEntry;

// Process control block
typedef struct {
    int pid;                  // Process ID
    int state;                // Process state
    int pageTable[PAGES_PER_PROC]; // Page table for the process (stores frame number or -1)
    int totalMemoryAccesses;  // Total memory accesses by this process
    int pageFaults;           // Total page faults
    unsigned int startSec;    // Process start time
    unsigned int startNano;   // Process start time
} PCB;

// Message structure for memory requests
typedef struct {
    long mtype;               // Message type
    int pid;                  // Process ID
    int address;              // Memory address
    bool isWrite;             // Read or write operation
    bool terminated;          // Is the process terminating?
} Message;

// Shared memory structure
typedef struct {
    SimClock clock;
    PCB processes[MAX_PROC];
    FrameTableEntry frameTable[TOTAL_FRAMES];
    int activeProcesses;      // Number of active processes
} SharedMemory;

// Function prototypes
void incrementClock(SimClock *clock, unsigned int nanoseconds);
void displayMemoryMap(FILE *logfile);
int findLRUFrame();
void initSharedMemory(SharedMemory *shm);
void cleanupResources(int shmid, int msqid);

#endif // OSS_H
