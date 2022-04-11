#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

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
};

struct PCT {
    struct PCB ctrlTbl[18];
    int blocksInUse[18];
};

int main(int argc, char *argv[])
{
    struct my_msgbuf buf;
    unsigned int *sharedNS;
    unsigned int *sharedSecs;
    int shmid_NS, shmid_Secs, shmid_PCT;

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

    //Get Process Control Table
    struct PCT *procCtl;

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

    /********************************************************************************

    Start doing things here

    *********************************************************************************/

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": C-msgget");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Receive messages
    if (msgrcv(msqid, &buf, sizeof(buf.mtext), i + 1, 0) == -1) {
        strcpy(report, ": C-msgrcv");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    printf("Process %i received message: %s\n", i + 1, buf.mtext);
    //printf("Oh yeah, and the clock is at %li:%09li\n", (long) *sharedSecs, (long) *sharedNS);

    //Make message
    char msgToSnd[3];
    sprintf(msgToSnd,"%i", i + 1);
    buf.mtype = 33;
    strcpy(buf.mtext, msgToSnd);

    //Send message
    if (msgsnd(msqid, &buf, strlen(buf.mtext) + 1, 0) == -1) {
        strcpy(report, ": msgsnd");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

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

    return 0;
}