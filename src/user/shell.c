/**
 * Userspace shell for hltOS
 *
 * A simple interactive shell demonstrating syscall usage.
 * Compiled with musl libc for standard C library support.
 */

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

// ============================================================================
// Shell commands
// ============================================================================

void cmd_help(void)
{
    puts("Available commands:");
    puts("  help     - Show this help");
    puts("  pwd      - Print working directory");
    puts("  cd PATH  - Change directory");
    puts("  ls PATH  - List directory entries");
    puts("  cat PATH - Display contents of a file");
    puts("  pid      - Show process ID");
    puts("  mmap     - Test mmap syscall");
    puts("  mkdir    - Test mkdir syscall");
    puts("  fork     - Test forking this shell");
    puts("  exit     - Exit shell");
}

void cmd_pwd(void)
{
    char buf[256];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        puts(buf);
    } else {
        puts("getcwd failed");
    }
}

void cmd_cd(const char* path)
{
    if (chdir(path) != 0) {
        printf("cd: no such directory: %s\n", path);
    }
}

void cmd_ls(char* path)
{
    int pid = fork();

    if (pid == 0) {
        char* argv[] = {"ls", path, NULL};
        execve("/bin/ls", argv, NULL);
    } else {
        int status;
        wait4(pid, &status, 0, NULL);
    }
}

void cmd_cat(const char* path)
{
    FILE* file = fopen(path, "r");

    if (!file) {
        puts("file not found!");
        return;
    }

    char buffer[1024];

    int bytes = fread(buffer, 1, sizeof(buffer), file);

    buffer[bytes] = '\0';

    printf("%s\n", buffer);
}

void cmd_pid(void)
{
    printf("PID: %d\n", getpid());
}

void cmd_fork(void)
{
    int pid = fork();

    if (pid == 0) {
        syscall(59, "/bin/musl");
    } else {
        int status;
        wait(&status);
    }
}

void cmd_mmap(void)
{
    puts("Testing mmap...");

    size_t size = 4096;
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    printf("  mmap returned: %p\n", ptr);

    if (ptr == MAP_FAILED || ptr == NULL) {
        puts("  FAILED: mmap returned invalid pointer");
        return;
    }

    // Write a test pattern
    unsigned char* mem = (unsigned char*)ptr;
    for (size_t i = 0; i < size; i++) {
        mem[i] = (unsigned char)(i & 0xFF);
    }
    puts("  Wrote test pattern to memory");

    // Read back and verify
    int errors = 0;
    for (size_t i = 0; i < size; i++) {
        if (mem[i] != (unsigned char)(i & 0xFF)) {
            errors++;
        }
    }

    if (errors == 0) {
        puts("  SUCCESS: All bytes verified correctly");
    } else {
        printf("  FAILED: %d byte mismatches\n", errors);
    }
}

void cmd_mkdir(const char* path)
{
    int err;

    if ((err = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) < 0) {
        if (err == -EROFS) {
            puts("Directory is readonly");
        } else {
            puts("Failed to create dir");
        }
    }
}

// ============================================================================
// Command parser
// ============================================================================

void process_command(char* cmd)
{
    // Skip leading whitespace
    while (*cmd == ' ')
        cmd++;

    // Empty command
    if (*cmd == '\0')
        return;

    // Parse command and argument
    char* arg = cmd;
    while (*arg && *arg != ' ' && *arg != '\n')
        arg++;
    if (*arg == ' ') {
        *arg++ = '\0';
        while (*arg == ' ')
            arg++;
    }

    // Dispatch
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(arg);
    } else if (strcmp(cmd, "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(cmd, "cd") == 0) {
        if (*arg) {
            cmd_cd(arg);
        } else {
            cmd_cd("/");
        }
    } else if (strcmp(cmd, "pid") == 0) {
        cmd_pid();
    } else if (strcmp(cmd, "mmap") == 0) {
        cmd_mmap();
    } else if (strcmp(cmd, "exit") == 0) {
        exit(0);
    } else if (strcmp(cmd, "fork") == 0) {
        cmd_fork();
    } else if (strcmp(cmd, "ls") == 0) {
        if (*arg) {
            cmd_ls(arg);
        } else {
            cmd_ls("./");
        }
    } else if (strcmp(cmd, "cat") == 0) {
        if (*arg) {
            cmd_cat(arg);
        } else {
            printf("cat: missing file\n");
        }
    } else {
        printf("unknown command: '%s'\n", cmd);
    }
}

// ============================================================================
// Entry point
// ============================================================================

static const char* BANNER = "                ____   _____\n"
                            "  _     _ _    / __ \\ / ____|\n"
                            " | |__ | | |_ | |  | | (___\n"
                            " | '_ \\| | __|| |  | |\\___ \\\n"
                            " | | | | | |_ | |__| |____) |\n"
                            " |_| |_|_|\\__| \\____/|_____/\n"
                            "\n";

int main(void)
{
    fputs(BANNER, stdout);
    puts("Welcome to hltOS!");
    puts("Type 'help' for available commands.\n");

    char buf[256];

    while (1) {
        // Print prompt with cwd
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s", cwd);
        }
        printf(" $ ");
        fflush(stdout);

        // Read input
        ssize_t n = read(0, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';

            if (buf[n - 1] == '\n') {
                buf[n - 1] = '\0';
            }

            process_command(buf);
        }
    }
}
