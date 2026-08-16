#include <stdio.h>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        puts("usage: cat [path]");
        return 0;
    }

    const char* path = argv[1];

    FILE* file = fopen(path, "r");

    if (!file) {
        puts("file not found!");
        return 0;
    }

    char buffer[1024];
    int bytes = fread(buffer, 1, sizeof(buffer), file);

    buffer[bytes] = '\0';

    printf("%s\n", buffer);

    return 0;
}
