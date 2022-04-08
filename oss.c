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

#define TIME_QUANTUM 10
#define BILLION 1000000000

FILE *file;
pid_t childPid;

unsigned int *sharedNS;
unsigned int *sharedSecs;
int shmid_NS, shmid_Secs, shmid_PCT;

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
    //Priority to be set with "getpriority(PRIO_PROCESS, 0);"
    int priority;
};

struct PCT {
    struct PCB ctrlTbl[18];
    int blocksInUse[18];
};

int main(int argc, char *argv[])
{
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

    const unsigned int maxTimeBetweenNewProcsNS = 500000000;
    const unsigned int maxTimeBetweenNewProcsSecs = 3;
    const int maxProcs = 50;
    const unsigned int maxTime = 3;
    //There should also be a constant representing the percentage of time a process is launched as a normal user process or a real-time one
    //and it should be weighted in favor of user processes

    //Initialize Process Control Table
    struct PCT *procCtl;

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

    /*************
    
    Generate new processes by allocating and initializing a PCB (at random intervals)

    **************/
   
    while (iInc < 5) {
        //Reset elapsed time
        elapsedTimeNS = 0;

        //Choose a random interval
        randomTimeSecs = rand() % (maxTimeBetweenNewProcsSecs + 1);
        randomTimeNS = rand() % maxTimeBetweenNewProcsNS;

        //* START THE CL0CK */ Count time until interval
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        initialTimeNS = (currentTime.tv_sec * BILLION) + currentTime.tv_nsec;

        while (((randomTimeSecs * BILLION) + (randomTimeNS)) > elapsedTimeNS) {
            clock_gettime(CLOCK_MONOTONIC, &currentTime);
            //Use the full time, all converted to nanoseconds
            elapsedTimeNS = ((currentTime.tv_sec * BILLION) + currentTime.tv_nsec) - initialTimeNS;
        }

        //Add time to the clock (and handle overflow)
        if ((*sharedNS + elapsedTimeNS) > BILLION) {
            *sharedNS += elapsedTimeNS;
            while (*sharedNS > BILLION) {
                *sharedSecs += 1;
                *sharedNS -= BILLION;
            }
        } else {
            *sharedNS += elapsedTimeNS;
        }
        
        //printf("ELAPSED TIME WAS... %li.%09li\n", elapsedTimeNS / BILLION, elapsedTimeNS / 10);
        printf("OSS creating new process at clock time %li.%09li\n", (long) *sharedSecs, (long) *sharedNS);

        //Make message
        char *msgToSnd = "Johnson Bagels";
        buf.mtype = 1;
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

        //Allocate block and execute process
        if (childPid == 0) {
            sprintf(iNum, "%i", iInc);
            procCtl->blocksInUse[iInc] = 1;
            execl("./child", iNum, NULL);
        } else {
            // Increment process number
            iInc++;
            // Wait for child to finish
            // do {
            //     if ((childPid = waitpid(childPid, &status, WNOHANG)) == -1) {
            //         strcpy(report, ": waitPid");
            //         message = strcat(title, report);
            //         perror(message);
            //         return 1;
            //     }
            // } while (childPid == 0);
        }
    }

    sleep(maxTime);
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