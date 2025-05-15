#include "oss.h"

int main(int argc, char *argv[]) {
    // Check arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <process_index> <shmid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    int procIndex = atoi(argv[1]);
    int shmid = atoi(argv[2]);
    
    // Attach to shared memory using the ID passed from parent
    SharedMemory *shm = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Get message queue
    int msqid = msgget(MSG_KEY, 0);
    if (msqid < 0) {
        perror("Failed to get message queue");
        shmdt(shm);
        exit(EXIT_FAILURE);
    }
    
    // Use procIndex to identify ourselves
    fprintf(stderr, "User process started with index: %d\n", procIndex);
    
    // Set up random number generator
    srand(getpid());
    
    // Get my process id from the PCB
    pid_t pid = getpid();
    
    // Main loop for memory requests
    int memoryReferences = 0;
    int terminationCheck = (rand() % 200) + 900;  // Check termination every 900-1100 refs
    
    while (1) {
        // Generate memory address to request
        int page = rand() % PAGES_PER_PROC;
        int offset = rand() % PAGE_SIZE;
        int address = (page * PAGE_SIZE) + offset;
        
        // Determine if it's a read or write (bias towards reads)
        bool isWrite = (rand() % 100) < 30;  // 30% chance of write
        
        // Prepare message
        Message msg;
        msg.mtype = REQUEST;
        msg.pid = pid;
        msg.address = address;
        msg.isWrite = isWrite;
        msg.terminated = false;
        
        // Send memory request to oss
        if (msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0) < 0) {
            perror("Failed to send request message");
            break;
        }
        
        // Wait for response from oss
        if (msgrcv(msqid, &msg, sizeof(Message) - sizeof(long), RESPONSE, 0) < 0) {
            perror("Failed to receive response message");
            break;
        }
        
        // Increment memory reference counter
        memoryReferences++;
        
        // Check if we should terminate
        if (memoryReferences >= terminationCheck) {
            if ((rand() % 100) < 30) {  // 30% chance to terminate
                // Send termination message
                msg.mtype = TERMINATE;
                msg.pid = pid;
                msg.terminated = true;
                
                if (msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0) < 0) {
                    perror("Failed to send termination message");
                }
                
                break;  // Exit the loop and terminate
            }
            
            // Reset termination check
            terminationCheck = memoryReferences + (rand() % 200) + 900;
        }
    }
    
    // Detach from shared memory
    shmdt(shm);
    
    return EXIT_SUCCESS;
}
