#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    char* path = "./";

    if (argc > 1) {
        path = argv[1];
    }

    DIR* dir = opendir(path);

    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }

    return 0;
}
