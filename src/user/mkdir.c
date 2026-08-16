#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        puts("usage: mkdir [path]");
        return -1;
    }

    char* path = argv[1];
    int err;

    if ((err = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) < 0) {
        if (err == -EROFS) {
            puts("directory is readonly");
        } else {
            puts("failed to create dir");
        }
    }

    return 0;
}
