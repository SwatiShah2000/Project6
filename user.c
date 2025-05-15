#include "oss.h"

int main(int argc, char *argv[]) {
    // Check arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <process_index>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int procIndex = atoi(argv[1]);

    // Get shared memory
    int shmid = shmget(SHM_KEY, sizeof(SharedMemory), 0);
    if (shmid < 0) {
        perror("Failed to get shared memory");
        exit(EXIT_FAILURE);
    }

    // Attach to shared memory
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

    // Set up random number generator
    srand(getpid());

    // Use procIndex to get our PCB entry and process ID
    pid_t pid = getpid();

    // Log that we're starting (optional, for debugging)
    fprintf(stderr, "User process %d (PID %d) started\n", procIndex, pid);

    // Main loop for memory requests
    int memoryReferences = 0;
    int terminationCheck = (rand() % 200) + 900;  // Check termination every 900-1100 refs

    while (1) {
        // Generate memory address to request
        int page = rand() % PAGES_PER_PROC;
        int offset = rand() % PAGE_SIZE;
        int address = (page * PAGE_SIZE) + offset;

        // Determine if it's a read or write (bias towards reads)
        bool isWrite = (rand() % 100) < 30;

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
                // Log that we're terminating (optional)
                fprintf(stderr, "User process %d (PID %d) terminating after %d references\n",
                        procIndex, pid, memoryReferences);

                msg.mtype = TERMINATE;
                msg.pid = pid;
                msg.terminated = true;

                if (msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0) < 0) {
                    perror("Failed to send termination message");
                }

                break;
            }

            terminationCheck = memoryReferences + (rand() % 200) + 900;
        }
    }

    // Detach from shared memory
    shmdt(shm);

    return EXIT_SUCCESS;
}
