#include <stdio.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
    printf("Hello world!\n");
    execl("./child", "./child", NULL);
    return 0;
}