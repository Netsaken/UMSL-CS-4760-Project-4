#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>

struct my_msgbuf {
   long mtype;
   char mtext[300];
};

int main(int argc, char *argv[])
{
    struct my_msgbuf buf;

    int msqid;
    int i = atoi(argv[0]);

    key_t keyMsg = ftok("./child.c", 't');

    //Format "perror"
    char* title = argv[0];
    char report[30] = ": shm";
    char* message;

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": C-msgget");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Recieve messages
    if (msgrcv(msqid, &buf, sizeof(buf.mtext), 0, 0) == -1) {
        strcpy(report, ": C-msgrcv");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    printf("Received message: %s\n", buf.mtext);

    return 0;
}
