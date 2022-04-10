#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define QUANTUM 10000000 //10 milliseconds in nanoseconds

#define BILLION 1000000000UL //1 second in nanoseconds

pid_t childPid;

unsigned int *sharedNS;
unsigned int *sharedSecs;
int shmid_NS, shmid_Secs, shmid_PCT;
int queueSize = 0;

struct my_msgbuf {
   long mtype;
   char mtext[300];
};

struct PCB {
    unsigned int totalCPUTimeUsed;
    unsigned int totalTimeInSystem;
    unsigned int lastBurstTime;
    //Pid to be set with "getpid();"
    pid_t thisPid;
    int priority;
};

struct PCT {
    struct PCB ctrlTbl[18];
    int blocksInUse[18];
};

struct MLFQ {
    int RRQ[18];
};

void queueAdd(int p, int* q) {
    if (queueSize < 18) {
        for (int i = 0; i < 18; i++) {
            if (q[i] == 0) {
                q[i] = p;
                break;
            }
        }
        queueSize++;
    } else {
        printf("queueAdd invoked at max size!");
    }
}

void queueRemove(int p, int* q) {
    if (queueSize > 0) {
        for (int i = 18; i > 0; i--) {
            if (q[i] == p) {
                q[i] = 0;
                break;
            }
        }
        queueSize--;
    } else {
        printf("queueRemove invoked at minimum size!");
    }
}

int main(int argc, char *argv[])
{
    FILE *file;
    struct my_msgbuf buf;
    int msqid, status;

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyPCT = ftok("./child.c", 'r');
    key_t keyMsg = ftok("./child.c", 't');
    char iNum[3];
    int iInc = 0;

    struct timespec currentTime;
    unsigned long initialTimeNS, elapsedTimeNS;
    unsigned int randomTimeNS = 0, randomTimeSecs = 0;
    int initSwitch = 1;
    int scheduleSwitch = 0;

    const unsigned int maxTimeBetweenNewProcsNS = 300000000;
    const unsigned int maxTimeBetweenNewProcsSecs = 0;
    const int maxProcs = 50;
    const unsigned long maxTime = 3 * BILLION;
    //There should also be a constant representing the percentage of time a process is launched as a normal user process or a real-time one
    //and it should be weighted in favor of user processes
    //Should also keep track of total lines in the log file

    //Initialize Process Control Table and Multi-Level Feedback Queue
    struct PCT *procCtl = {0};
    struct MLFQ queue = {0};

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
    sharedNS = shmat(shmid_NS, NULL, 0);
    if (sharedNS == (void *) -1) {
        strcpy(report, ": shmatNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    sharedSecs = shmat(shmid_Secs, NULL, 0);
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

    //Reset system clock
    *sharedSecs = 0;
    *sharedNS = 0;

    /********************************************************************************

    Start doing things here

    *********************************************************************************/

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": msgget");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    /*--d-ie0i-09eiodqiiopqmoemdpoeqdmdjn91j0-0j4
    Steps for ROUND ROBIN
    - Add process 1 to the queue
    - Run process 1 for given quantum
    - Once the quantum ends, go to the next process, if there is one
        - How do you PAUSE a process after it has used its time quantum???
            - ??? (send a message? But then how will it receive it? Maybe there's a Signal I can use?)
    - If the process didn't finish by the time the quantum was used up, re-add it to the end of the queue
        - How do I know whether the process finished successfully or not?
            - Message send-back?
    dij1d9j8&%*($&)#*&)3o1idnjondkni(*&@)(%%hUjfnw*/

    /*************
    
    Generate new processes at random intervals

    **************/
    while (((*sharedSecs * BILLION) + *sharedNS) < maxTime && iInc < 18)
    {
        //Check for processes in queue
        for (int p = 0; p < 18; p++) {
            if (procCtl->blocksInUse[p] > 0) {
                scheduleSwitch = 1;
            }
        }

        /* START THE CL0CK */
        //Get the random time interval (only if it is not already set)
        if (initSwitch == 1) {
            randomTimeSecs = rand() % (maxTimeBetweenNewProcsSecs + 1);
            randomTimeNS = rand() % maxTimeBetweenNewProcsNS;

            clock_gettime(CLOCK_MONOTONIC, &currentTime);
            initialTimeNS = (currentTime.tv_sec * BILLION) + currentTime.tv_nsec;

            initSwitch = 0;
        }

        //Count the time
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        elapsedTimeNS = ((currentTime.tv_sec * BILLION) + currentTime.tv_nsec) - initialTimeNS;

        //If the clock has hit the appropriate time, make a new process
        if (((randomTimeSecs * BILLION) + (randomTimeNS)) < elapsedTimeNS) {
            //Add time to the clock
            if ((*sharedNS + elapsedTimeNS) > BILLION) {
                *sharedNS += elapsedTimeNS;
                while (*sharedNS > BILLION) {
                    *sharedSecs += 1;
                    *sharedNS -= BILLION;
                }
            } else {
                *sharedNS += elapsedTimeNS;
            }

            //printf("ELAPSED TIME WAS... %li.%09li\n", elapsedTimeNS / BILLION, elapsedTimeNS);
            printf("OSS creating new process at clock time %li:%09li\n", (long)*sharedSecs, (long)*sharedNS);
            
            //Reset elapsed time and initial time
            elapsedTimeNS = 0;
            initSwitch = 1;

            //Make message
            char msgToSnd[300];
            sprintf(msgToSnd,"Johnson's %i Bagels", iInc);
            buf.mtype = iInc + 1;
            strcpy(buf.mtext, msgToSnd);

            //Send message
            if (msgsnd(msqid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
                strcpy(report, ": msgsnd");
                message = strcat(title, report);
                perror(message);
                return 1;
            }

            //Create the user process
            childPid = fork();
            if (childPid == -1) {
                strcpy(report, ": childPid");
                message = strcat(title, report);
                perror(message);
                return 1;
            }

            //Allocate block, add to queue, and execute process
            if (childPid == 0) {
                sprintf(iNum, "%i", iInc + 1);
                procCtl->blocksInUse[iInc] = 1;
                execl("./child", iNum, NULL);
            } else {
                //Add to queue
                queueAdd(iInc + 1, queue.RRQ);

                //Increment process number
                iInc++;
            }
        }

        //Receive message that child finished
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), 33, IPC_NOWAIT) == -1)
        {
            if (errno != ENOMSG) {   
                strcpy(report, ": C-msgrcv");
                message = strcat(title, report);
                perror(message);
                return 1;
            }
        } else {
            printf("OSS received message: %s\n", buf.mtext);

            //Remove from queue
            queueRemove(atoi(buf.mtext), queue.RRQ);
            //iInc--;
        }
    }

    printf("\n");
    printf("Values in RRQ:\n");
    for (int z = 0; z < 18; z++) {
        printf("%i ", queue.RRQ[z]);
    }
    printf("\n");

    //Shutdown
    kill(childPid, SIGTERM);
    waitpid(childPid, &status, 0);

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        strcpy(report, ": msgctl");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        strcpy(report, ": shmdtNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    if (shmdt(sharedSecs) == -1) {
        strcpy(report, ": shmdtSecs");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    if (shmdt(procCtl) == -1) {
        strcpy(report, ": shmdtPDT");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Remove shared memory
    if (shmctl(shmid_NS, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    if (shmctl(shmid_Secs, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlSecs");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    if (shmctl(shmid_PCT, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlPDT");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    return 0;
}