#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    int pid = fork();

    if (pid == 0) {
        char* args[] = {"/bin/shell", NULL};
        execve("/bin/shell", args, NULL);
    }

    int status;
    wait(&status);

    puts("init: /bin/shell terminated");
    puts("init: pausing until signal received...");

    pause();

    return 0;
}
