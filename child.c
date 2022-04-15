#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define QUANTUM 10000000
#define BILLION 1000000000UL //1 second in nanoseconds
#define CHANCE_TO_TERMINATE 20
#define BLOCK_CHANCE 5

unsigned int *sharedNS;
unsigned int *sharedSecs;
struct PCT *procCtl = {0};

struct my_msgbuf {
   long mtype;
   char mtext[300];
};

struct PCB {
    unsigned int totalCPUTimeUsed;
    unsigned int totalTimeInSystem;
    unsigned int lastTimeUsed;
    //Pid to be set with "getpid();"
    pid_t thisPid;
    int priority;
    int iValue;
};

struct PCT {
    struct PCB ctrlTbl[18];
    int blocksInUse[18];
};

void endProcess(struct my_msgbuf buf, int msqid, int i) {
    //Make message
    char msgToSnd[3];
    sprintf(msgToSnd,"%i", i + 1);
    buf.mtype = 33;
    strcpy(buf.mtext, msgToSnd);

    //Send message
    if (msgsnd(msqid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
        perror("./child: endMsg");
        exit(1);
    }

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        perror("./child: endShmdtNS");
        exit(1);
    }

    if (shmdt(sharedSecs) == -1) {
        perror("./child: endShmdtSecs");
        exit(1);
    }

    if (shmdt(procCtl) == -1) {
        perror("./child: endShmdtPCT");
        exit(1);
    }

    exit(0);
}

int main(int argc, char *argv[])
{
    int terminateCheck = 0, blockedCheck = 0;

    struct my_msgbuf buf;
    int shmid_NS, shmid_Secs, shmid_PCT;
    unsigned int initialSharedSecs, initialSharedNS, interTImeNS, r, s;
    unsigned long blockTimerEnd = 0, blockTimerNS = 0;
    float p;

    int msqid;
    int i = atoi(argv[0]);

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyPCT = ftok("./child.c", 'r');
    key_t keyMsg = ftok("./child.c", 't');

    //Format "perror"
    char* title = argv[0];
    char report[30] = ": shm";
    char* message;

    //Get shared memory
    shmid_NS = shmget(keyNS, sizeof(sharedNS), IPC_CREAT | 0666);
    if (shmid_NS == -1) {
        strcpy(report, ": shmgetNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    shmid_Secs = shmget(keySecs, sizeof(sharedSecs), IPC_CREAT | 0666);
    if (shmid_Secs == -1) {
        strcpy(report, ": shmgetSecs");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    shmid_PCT = shmget(keyPCT, sizeof(procCtl), IPC_CREAT | 0666);
    if (shmid_PCT == -1) {
        strcpy(report, ": shmgetPCT");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Attach shared memory
    sharedNS = (unsigned int*)shmat(shmid_NS, NULL, 0);
    if (sharedNS == (void *) -1) {
        strcpy(report, ": shmatNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    sharedSecs = (unsigned int*)shmat(shmid_Secs, NULL, 0);
    if (sharedSecs == (void *) -1) {
        strcpy(report, ": shmatSecs");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    procCtl = shmat(shmid_PCT, NULL, 0);
    if (procCtl == (void *) -1) {
        strcpy(report, ": shmatPCT");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": C-msgget");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    /********************************************************************************

    Start doing things here

    *********************************************************************************/

    //Set PID in PCB
    procCtl->ctrlTbl[i].thisPid = getpid();
    procCtl->ctrlTbl[i].iValue = i + 1;

    //Get current shared time
    initialSharedSecs = *sharedSecs;
    initialSharedNS = *sharedNS;

    //Initialize RNG
    srand((getpid() * 3) % 50);

    //Loop until OSS termination or endProcess() termination
    while (1)
    {
        //Wait to receive messages
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), i + 1, 0) == -1) {
            strcpy(report, ": C-msgrcv");
            message = strcat(title, report);
            perror(message);
            return 1;
        }

        // Start/reset in-CPU time
        interTImeNS = ((*sharedSecs * BILLION) + *sharedNS);

        //If already Blocked, notify OSS and go back to start until Block timer ends (and add to Total time)
        if (blockedCheck == 1) {
            if (blockTimerEnd > ((*sharedSecs * BILLION) + *sharedNS)) {
                // Send Blocked message
                char msgToSnd[3];
                sprintf(msgToSnd, "%i", i + 1);
                buf.mtype = 34;
                strcpy(buf.mtext, msgToSnd);

                if (msgsnd(msqid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
                    strcpy(report, ": msgsnd");
                    message = strcat(title, report);
                    perror(message);
                    return 1;
                }

                procCtl->ctrlTbl[i].totalTimeInSystem += ((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS));
                continue;
            } else {
                blockedCheck = 0;
            }
        }

        //Check for termination
        if ((rand() % 100) < CHANCE_TO_TERMINATE) {
            terminateCheck = 1;
        }

        //Check for I/O block
        if ((rand() % 100) < BLOCK_CHANCE) {
            blockedCheck = 1;
        }

        //If terminating, use random amount of quantum first
        if (terminateCheck == 1) {
            //Spin
            while ((((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS))) < (rand() % QUANTUM)) {}

            //Log time (caused seg fault)
            // procCtl->ctrlTbl[i].totalCPUTimeUsed += interTImeNS - (((initialSharedSecs * BILLION) + initialSharedNS));
            // procCtl->ctrlTbl[i].totalTimeInSystem += ((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS));

            //Exit
            endProcess(buf, msqid, i);
        }

        // If getting blocked, wait for an event that lasts a random number of seconds
        if (blockedCheck == 1) {
            // Generate random values for r.s
            r = rand() % 6;
            s = rand() % 1001;
            p = (rand() % 99) + 1;
            p = p / 100;

            blockTimerNS = (r * BILLION) + (s / 10000);

            //For "3 indicates that the process gets preempted after using p of its assigned quantum" from project specs (???)
            if (r == 3) {
                //Spin for designated percentage of time quantum
                while ((((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS))) < (p * QUANTUM)) {}
                
                //Log time (caused seg fault)
                // procCtl->ctrlTbl[i].totalCPUTimeUsed += interTImeNS - (((initialSharedSecs * BILLION) + initialSharedNS));
                // procCtl->ctrlTbl[i].totalTimeInSystem += ((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS));
                continue;
            } else {
                blockTimerEnd = blockTimerNS + ((*sharedSecs * BILLION) + *sharedNS);
            }

            // Send Blocked message
            char msgToSnd[3];
            sprintf(msgToSnd, "%i", i + 1);
            buf.mtype = 34;
            strcpy(buf.mtext, msgToSnd);

            if (msgsnd(msqid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
                strcpy(report, ": msgsnd");
                message = strcat(title, report);
                perror(message);
                return 1;
            }
        } else {
            //If all else fails, simply spin for whole quantum
            while ((((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS))) < QUANTUM) {}
        }

        //Log time
        procCtl->ctrlTbl[i].totalCPUTimeUsed += interTImeNS - (((initialSharedSecs * BILLION) + initialSharedNS));
        procCtl->ctrlTbl[i].totalTimeInSystem += ((*sharedSecs * BILLION) + *sharedNS) - (((initialSharedSecs * BILLION) + initialSharedNS));
    }

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}