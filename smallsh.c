#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMDLENGTH 2049
#define MAXARGS 512
#define MAXBGPIDS 1024

int backgroundOn = 1;
int bgPids[MAXBGPIDS];
int bgCount = 0;

int lastExitStatus = 0;
int lastTermSignal = 0;
int lastWasSignaled = 0;

// background process management
void addBgPid(pid_t pid) {
    if (bgCount < MAXBGPIDS) bgPids[bgCount++] = pid;
}

void removeBgPid(pid_t pid) {
    for (int i = 0; i < bgCount; i++) {
        if (bgPids[i] == pid) {
            bgPids[i] = bgPids[--bgCount];
            return;
        }
    }
}

// signal handler for SIGTSTP
void catchSIGTSTP(int signo) {
    if (backgroundOn) {
        const char* msg = "\nEntering foreground-only mode (& is now ignored)\n";
        write(1, msg, strlen(msg));
        backgroundOn = 0;
    } else {
        const char* msg = "\nExiting foreground-only mode\n";
        write(1, msg, strlen(msg));
        backgroundOn = 1;
    }
}

// print last status
void printStatus() {
    if (lastWasSignaled) {
        printf("terminated by signal %d\n", lastTermSignal);
    } else {
        printf("exit value %d\n", lastExitStatus);
    }
    fflush(stdout);
}

// reap background processes
void reapBackground() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("background pid %d is done: ", pid);
        if (WIFEXITED(status)) {
            printf("exit value %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("terminated by signal %d\n", WTERMSIG(status));
        }
        fflush(stdout);
        removeBgPid(pid);
    }
}

// get user input
void getInput(char* input[], char inFile[], char outFile[], int* background, int pid) {
    char arr[CMDLENGTH];
    *background = 0;
    inFile[0] = '\0';
    outFile[0] = '\0';

    // initialize input array
    for (int i = 0; i < MAXARGS; i++) input[i] = NULL;

    // prompt and read input
    printf(": ");
    fflush(stdout);
    if (fgets(arr, CMDLENGTH, stdin) == NULL) {
        input[0] = NULL;
        return;
    }

    // remove trailing newline
    arr[strcspn(arr, "\n")] = '\0';
    if (arr[0] == '\0' || arr[0] == '#') {
        input[0] = NULL;
        return;
    }

    // expand $$
    char expanded[CMDLENGTH] = "";
    char pidStr[20];
    sprintf(pidStr, "%d", pid);
    for (int i = 0; arr[i] != '\0'; ) {
        if (arr[i] == '$' && arr[i+1] == '$') {
            strcat(expanded, pidStr);
            i += 2;
        } else {
            strncat(expanded, &arr[i], 1);
            i++;
        }
    }
    strcpy(arr, expanded);

    // tokenize input
    const char s[2] = " ";
    char* saveptr;
    char* token = strtok_r(arr, s, &saveptr);
    int i = 0;

    while (token) {
        // handle &
        if (strcmp(token, "&") == 0) {
            *background = 1;

        // handle input redirection
        } else if (strcmp(token, "<") == 0) {
            token = strtok_r(NULL, s, &saveptr);
            if (token) strcpy(inFile, token);

        // handle output redirection
        } else if (strcmp(token, ">") == 0) {
            token = strtok_r(NULL, s, &saveptr);
            if (token) strcpy(outFile, token);

        // regular argument
        } else {
            input[i++] = strdup(token);
        }
        token = strtok_r(NULL, s, &saveptr);
    }

    // null terminate input array
    input[i] = NULL;
}

// execute non built-in command
void execCommand(char* input[], char inFile[], char outFile[], int background) {
    pid_t spawnpid = fork();
    int status;

    switch (spawnpid) {
        // fork failed
        case -1:
            printf("fork failed\n");
            fflush(stdout);
            break;

        // child process
        case 0: {
            // ignore SIGTSTP for all children
            struct sigaction sa_ignore;
            sa_ignore.sa_handler = SIG_IGN;
            sigfillset(&sa_ignore.sa_mask);
            sa_ignore.sa_flags = 0;
            sigaction(SIGTSTP, &sa_ignore, NULL);

            // SIGINT default for foreground, ignore for background
            struct sigaction sa_int;
            sa_int.sa_handler = (!background || !backgroundOn) ? SIG_DFL : SIG_IGN;
            sigfillset(&sa_int.sa_mask);
            sa_int.sa_flags = 0;
            sigaction(SIGINT, &sa_int, NULL);

            // input redirection
            if (inFile[0] != '\0') {
                int fd = open(inFile, O_RDONLY);
                if (fd == -1) {
                    printf("%s: cannot open input file\n", inFile);
                    fflush(stdout);
                    exit(1);
                }
                dup2(fd, 0);
                close(fd);
            } else if (background && backgroundOn) {
                int fd = open("/dev/null", O_RDONLY);
                if (fd != -1) { dup2(fd, 0); close(fd); }
            }

            // output redirection
            if (outFile[0] != '\0') {
                int fd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    printf("%s: cannot open output file\n", outFile);
                    fflush(stdout);
                    exit(1);
                }
                dup2(fd, 1);
                close(fd);
            } else if (background && backgroundOn) {
                int fd = open("/dev/null", O_WRONLY);
                if (fd != -1) { dup2(fd, 1); dup2(fd, 2); close(fd); }
            }

            // execute command
            execvp(input[0], input);
            printf("%s: no such file or directory\n", input[0]);
            fflush(stdout);
            exit(1);
        }

        // parent process
        default:
            // background process
            if (background && backgroundOn) {
                addBgPid(spawnpid);
                printf("background pid is %d\n", spawnpid);
                fflush(stdout);

            // foreground process
            } else {
                waitpid(spawnpid, &status, 0);

                // update last status
                if (WIFEXITED(status)) {
                    lastWasSignaled = 0;
                    lastExitStatus = WEXITSTATUS(status);

                // terminated by signal
                } else if (WIFSIGNALED(status)) {
                    lastWasSignaled = 1;
                    lastTermSignal = WTERMSIG(status);
                    printf("terminated by signal %d\n", lastTermSignal);
                    fflush(stdout);
                }
            }
            break;
    }
}

int main() {
    pid_t shellPid = getpid();
    char* input[MAXARGS];
    char inFile[256];
    char outFile[256];
    int background;
    int run = 1;

    // ignore SIGINT in shell
    struct sigaction sa_int = {0};
    sa_int.sa_handler = SIG_IGN;
    sigfillset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    // handle SIGTSTP
    struct sigaction sa_tstp = {0};
    sa_tstp.sa_handler = catchSIGTSTP;
    sigfillset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = 0;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    while (run) {
        // reap any completed background processes
        reapBackground();

        // get user input
        getInput(input, inFile, outFile, &background, shellPid);

        // skip empty input and comments
        if (input[0] == NULL) continue;

        // exit command
        if (strcmp(input[0], "exit") == 0) {
            // kill any remaining background processes
            for (int i = 0; i < bgCount; i++) kill(bgPids[i], SIGKILL);

            // free any allocated memory in input[]
            for (int i = 0; input[i]; i++) free(input[i]);
            break;

        // cd command
        } else if (strcmp(input[0], "cd") == 0) {
            if (input[1] != NULL) {
                if (chdir(input[1]) == -1) {
                    printf("cd: no such file or directory\n");
                    fflush(stdout);
                }
            } else chdir(getenv("HOME"));

        // status command
        } else if (strcmp(input[0], "status") == 0) {
            printStatus();

        // other commands
        } else {
            int bg = background && backgroundOn;
            execCommand(input, inFile, outFile, bg);
        } 

        // reap any completed background processes again
        reapBackground();

        // free input array after each command
        for (int i = 0; input[i]; i++) {
            free(input[i]);
            input[i] = NULL;
        }
        background = 0;
        inFile[0] = '\0';
        outFile[0] = '\0';
    }

    // cleanup any remaining background processes
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);

    return 0;
}
