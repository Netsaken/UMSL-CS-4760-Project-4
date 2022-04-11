#include <errno.h>
#include <signal.h>
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
struct PCT *procCtl = {0};
int shmid_NS, shmid_Secs, shmid_PCT;
int msqid;
int queueSize = 0;

static void handle_sig(int sig) {
    int errsave, status;
    errsave = errno;
    //Print message
    printf("Program interrupted. Shutting down...\n");

    //End children
    kill(childPid, SIGTERM);
    waitpid(childPid, &status, 0);

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        perror("./oss: sigShmdtNS");
    }

    if (shmdt(sharedSecs) == -1) {
        perror("./oss: sigShmdtSecs");
    }

    if (shmdt(procCtl) == -1) {
        perror("./oss: sigShmdtPCT");
    }

    //Remove shared memory
    if (shmctl(shmid_NS, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlNS");
    }

    if (shmctl(shmid_Secs, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlSecs");
    }

    if (shmctl(shmid_PCT, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlPCT");
    }

    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        perror("./oss: sigMsgctl");
    }

    printf("Cleanup complete. Have a nice day!\n");

    //Exit program
    errno = errsave;
    exit(0);
}

static int setupinterrupt(void) {
    struct sigaction act;
    act.sa_handler = handle_sig;
    act.sa_flags = 0;
    return (sigemptyset(&act.sa_mask) || sigaction(SIGALRM, &act, NULL));
}

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
    int iValue;
};

struct PCT {
    struct PCB ctrlTbl[18];
    int blocksInUse[18];
};

struct MLFQ {
    int RRQ[18];
};

//Add to the back of the queue
void queueAdd(int p, struct MLFQ *q) {
    if (queueSize < 18) {
        for (int i = 0; i < 18; i++) {
            if (q->RRQ[i] == 0) {
                q->RRQ[i] = p;
                break;
            }
        }
        queueSize++;
        //printf("Check...1\n");
    } else {
        printf("queueAdd invoked at max size!");
    }
}

//Remove from the front of the queue, then shift all values 1 space to the left
void queueRemove(struct MLFQ *q) {
    if (queueSize > 0) {
        for (int i = 0; i < 17; i++) {
            q->RRQ[i] = q->RRQ[i + 1];
        }
        q->RRQ[17] = 0;
        queueSize--;
        //printf("Check...2\n");
    } else {
        printf("queueRemove invoked at minimum size!");
    }
}

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_sig);
    signal(SIGABRT, handle_sig);

    FILE *file;
    struct my_msgbuf buf;

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyPCT = ftok("./child.c", 'r');
    key_t keyMsg = ftok("./child.c", 't');
    char iNum[3];
    int iInc = 0;

    struct timespec currentTime;
    unsigned long initialTimeNS, elapsedTimeNS, initialQuantumTimeNS, elapsedQuantumTimeNS;
    unsigned int randomTimeNS = 0, randomTimeSecs = 0;
    int initSwitch = 1, initSwitchQuantum = 1;
    int iStore, scheduleSwitch = 0;

    const unsigned int maxTimeBetweenNewProcsNS = 300000000;
    const unsigned int maxTimeBetweenNewProcsSecs = 0;
    const int maxProcs = 50;
    const unsigned long maxTime = 3 * BILLION;
    //There should also be a constant representing the percentage of time a process is launched as a normal user process or a real-time one
    //and it should be weighted in favor of user processes
    //Should also keep track of total lines in the log file

    //Initialize Process Control Table and Multi-Level Feedback Queue
    struct MLFQ queue = {0};

    //Format "perror"
    char* title = argv[0];
    char report[30] = ": shm";
    char* message;

    //Set up interrupts
    if (setupinterrupt() == -1) {
        strcpy(report, ": setupinterrupt");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Get shared memory
    shmid_NS = shmget(keyNS, sizeof(sharedNS), IPC_CREAT | 0666);
    if (shmid_NS == -1) {
        strcpy(report, ": shmgetNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    shmid_Secs = shmget(keySecs, sizeof(sharedSecs), IPC_CREAT | 0666);
    if (shmid_Secs == -1) {
        strcpy(report, ": shmgetSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    shmid_PCT = shmget(keyPCT, sizeof(procCtl), IPC_CREAT | 0666);
    if (shmid_PCT == -1) {
        strcpy(report, ": shmgetPCT");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Attach shared memory
    sharedNS = shmat(shmid_NS, NULL, 0);
    if (sharedNS == (void *) -1) {
        strcpy(report, ": shmatNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    sharedSecs = shmat(shmid_Secs, NULL, 0);
    if (sharedSecs == (void *) -1) {
        strcpy(report, ": shmatSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    procCtl = shmat(shmid_PCT, NULL, 0);
    if (procCtl == (void *) -1) {
        strcpy(report, ": shmatPCT");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Reset system clock
    *sharedSecs = 0;
    *sharedNS = 0;

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": msgget");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    /********************************************************************************

    Start doing things here

    *********************************************************************************/
    /*--d-ie0i-09eiodqiiopqmoemdpoeqdmdjn91j0-0j4
    Steps for ROUND ROBIN
    - Run process 1 for given quantum
    - Once the quantum ends, go to the next process, if there is one
    - If the process didn't finish by the time the quantum was used up, re-add it to the end of the queue
    dij1d9j8&%*($&)#*&)3o1idnjondkni(*&@)(%%hUjfnw*/
    
    //Run program until maximum time is reached, and wait for the child processes to finish
    while ((((*sharedSecs * BILLION) + *sharedNS) < maxTime && iInc < 18) || scheduleSwitch == 1)
    {
        /* START THE CL0CK */
        //Get the random time interval (only if it is not already set)
        if (initSwitch == 1) {
            randomTimeSecs = rand() % (maxTimeBetweenNewProcsSecs + 1);
            randomTimeNS = rand() % maxTimeBetweenNewProcsNS;

            clock_gettime(CLOCK_MONOTONIC, &currentTime);
            initialTimeNS = (currentTime.tv_sec * BILLION) + currentTime.tv_nsec;

            initSwitch = 0;
        }

        //Get the time interval for the quantum separately
        if (initSwitchQuantum == 1) {
            clock_gettime(CLOCK_MONOTONIC, &currentTime);
            initialQuantumTimeNS = (currentTime.tv_sec * BILLION) + currentTime.tv_nsec;
            initSwitchQuantum = 0;
        }

        //Count the time
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        elapsedTimeNS = ((currentTime.tv_sec * BILLION) + currentTime.tv_nsec) - initialTimeNS;
        elapsedQuantumTimeNS = ((currentTime.tv_sec * BILLION) + currentTime.tv_nsec) - initialTimeNS;

        /********************************************************************************************************************
        If the clock has hit the random time, make a new process
        *********************************************************************************************************************/
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
            //Print status to file
            printf("OSS creating new process at clock time %li:%09li\n", (long)*sharedSecs, (long)*sharedNS);
            
            //Reset elapsed time and initial time
            elapsedTimeNS = 0;
            initSwitch = 1;

            //Create the user process
            childPid = fork();
            if (childPid == -1) {
                strcpy(report, ": childPid");
                message = strcat(title, report);
                perror(message);
                abort();
            }

            //Allocate block, add to queue, and execute process
            if (childPid == 0) {
                sprintf(iNum, "%i", iInc);
                execl("./child", iNum, NULL);
            } else {
                //Add to queue
                queueAdd(iInc + 1, &queue);
                
                //Specify block in use
                //printf("ALLOCATING BLOCKSINUSE: %i\n", iInc + 1);
                procCtl->blocksInUse[iInc] = 1;
                
                //Increment process number
                iInc++;
            }

            //Check for processes in queue
            for (int p = 0; p < 18; p++) {
                if (procCtl->blocksInUse[p] > 0) {
                    scheduleSwitch = 1;
                    break;
                } else {
                    scheduleSwitch = 0;
                }
            }
        }

        /********************************************************************************************************************
        Run appropriate process for the given time quantum, each time quantum
        *********************************************************************************************************************/
        if (elapsedQuantumTimeNS >= QUANTUM && scheduleSwitch == 1) {
            //Reset elapsed time
            elapsedQuantumTimeNS = 0;

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

            //Stop current process so we can run the next one
            // printf("PID: %i\n", procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid);
            //printf("iValue is: %i\n", procCtl->ctrlTbl[queue.RRQ[0] - 1].iValue);
            if (procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid != 0) {
                kill(procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid, SIGSTOP);
                iStore = procCtl->ctrlTbl[queue.RRQ[0] - 1].iValue;
                printf("Value of iStore: %i\n", iStore);
                queueRemove(&queue);
                queueAdd(iStore, &queue);
            }

            //printf("ELAPSED Quantum TIME WAS... %li.%09li\n", elapsedQuantumTimeNS / BILLION, elapsedQuantumTimeNS);
            // printf("Values in RRQ (before process run): \n");
            // for (int f = 0; f < 18; f++) {
            //     printf("%i ", queue.RRQ[f]);
            // }
            // printf("\n");
            //Print status to file
            printf("OSS running process %i at clock time %li:%09li\n", queue.RRQ[0], (long)*sharedSecs, (long)*sharedNS);

            //printf("The PID I'm trying to access is: %i\n", procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid);
            if (procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid != 0) {
                //printf("SIGCONT RAN. PID = %i\n", procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid);
                kill(procCtl->ctrlTbl[queue.RRQ[0] - 1].thisPid, SIGCONT);
            }

            //Make message
            char msgToSnd[300];
            sprintf(msgToSnd,"Johnson's %i Bagels", queue.RRQ[0]);
            buf.mtype = queue.RRQ[0];
            strcpy(buf.mtext, msgToSnd);

            //Send message
            if (msgsnd(msqid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
                strcpy(report, ": msgsnd");
                message = strcat(title, report);
                perror(message);
                abort();
            }
        }

        /********************************************************************************************************************
        Receive message that child finished and deallocate space
        *********************************************************************************************************************/
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), 33, IPC_NOWAIT) == -1)
        {
            if (errno != ENOMSG) {   
                strcpy(report, ": C-msgrcv");
                message = strcat(title, report);
                perror(message);
                abort();
            }
        } else {
            //Print status to file
            printf("OSS received message: %s\n", buf.mtext);

            //Remove from queue
            queueRemove(&queue);

            //Deallocate blocksinuse
            //printf("DEALLOCATING BLOCKSINUSE: %i\n", atoi(buf.mtext));
            procCtl->blocksInUse[atoi(buf.mtext) - 1] = 0;

            //Check for processes in queue
            for (int p = 0; p < 18; p++) {
                if (procCtl->blocksInUse[p] > 0) {
                    scheduleSwitch = 1;
                    break;
                } else {
                    scheduleSwitch = 0;
                }
            }
            //iInc--;
        }
    }

    // printf("ScheduleSwitch is at %i\n", scheduleSwitch);
    // printf("Values in RRQ:\n");
    // for (int z = 0; z < 18; z++) {
    //     printf("%i ", queue.RRQ[z]);
    // }
    // printf("\n");
    // printf("Values in BLOCKSINUSE: \n");
    // for (int f = 0; f < 18; f++) {
    //     printf("%i ", procCtl->blocksInUse[f]);
    // }

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        strcpy(report, ": msgctl");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        strcpy(report, ": shmdtNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    if (shmdt(sharedSecs) == -1) {
        strcpy(report, ": shmdtSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    if (shmdt(procCtl) == -1) {
        strcpy(report, ": shmdtPDT");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Remove shared memory
    if (shmctl(shmid_NS, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    if (shmctl(shmid_Secs, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    if (shmctl(shmid_PCT, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlPDT");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    return 0;
}