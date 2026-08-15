#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    /* int pid = fork(); */

    /* printf("pid = %d\n", pid); */

    /* if (pid == 0) { */
    /* puts("hello from child"); */
    /* exit(0); */
    /* } else { */
    /* puts("parent waiting for child..."); */

    /* int status; */

    /* wait4(pid, &status, 0, NULL); */
    /* } */

    puts("hello from musl.c!");
    printf("argc = %d\n", argc);

    return 0;

    /*
    DIR* dir = opendir("/dev");

    if (dir != NULL) {
        printf("dir = %d\n", dir);

        struct dirent* entry;

        while (1) {
            entry = readdir(dir);

            if (entry == NULL) {
                break;
            }

            printf("%s\n", entry->d_name);
        }

    } else {
        puts("Failed to opendir('/dev')");
    }

    sleep(1);

    return 0;
    */
}
