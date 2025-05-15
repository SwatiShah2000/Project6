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

#define SHM_KEY 0x1234
#define MSG_KEY 0x5678
#define PERMS 0644

#define MAX_PROC 18
#define TOTAL_PROC 100
#define MEMORY_SIZE 131072
#define PAGE_SIZE 1024
#define TOTAL_FRAMES 128
#define PAGES_PER_PROC 32

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
    bool occupied;
    int pid;
    int page;
    bool dirtyBit;
    unsigned int lastRefSec;
    unsigned int lastRefNano;
} FrameTableEntry;

// Page table entry
typedef struct {
    int frame;
} PageTableEntry;

// Process control block
typedef struct {
    int pid;
    int state;
    int pageTable[PAGES_PER_PROC];
    int totalMemoryAccesses;
    int pageFaults;
    unsigned int startSec;
    unsigned int startNano;
} PCB;

// Message structure for memory requests
typedef struct {
    long mtype;
    int pid;
    int address;
    bool isWrite;
    bool terminated;
} Message;

// Shared memory structure
typedef struct {
    SimClock clock;
    PCB processes[MAX_PROC];
    FrameTableEntry frameTable[TOTAL_FRAMES];
    int activeProcesses;
} SharedMemory;

// Function prototypes
void incrementClock(SimClock *clock, unsigned int nanoseconds);
void displayMemoryMap(FILE *logfile);
int findLRUFrame();
void initSharedMemory(SharedMemory *shm);
void cleanupResources(int shmid, int msqid);

#endif
