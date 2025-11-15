/***********************************************************************
 * smallsh.c
 *
 * this implements a shell with the following features:
 *   - foreground & background commands
 *   - input/output redirection
 *   - custom signal handling for SIGINT and SIGTSTP
 *   - $$ expansion into the shell's PID
 *   - built-in commands: cd, status, exit
 * 
 * Author: Kaelee Duong
 * Course: CS344 - Operating Systems
 ***********************************************************************/

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

// tracks whether background mode is allowed
int backgroundOn = 1;

// array of background PIDs so they can be killed/reaped
int bgPids[MAXBGPIDS];
int bgCount = 0;

// tracks exit/termination info for "status"
int lastExitStatus = 0;
int lastTermSignal = 0;
int lastWasSignaled = 0;

/************************************************************
 * addBgPid - adds a background PID to an array for tracking 
 * 
 * arguments:
 * pid - the background PID to be added
 ************************************************************/
void addBgPid(pid_t pid) {
    if (bgCount < MAXBGPIDS) bgPids[bgCount++] = pid;
}

/************************************************************
 * addBgPid - removes a given background PID from the bgPids array
 * 
 * arguments:
 * pid - the background PID to be removed
 ************************************************************/
void removeBgPid(pid_t pid) {
    for (int i = 0; i < bgCount; i++) {
        if (bgPids[i] == pid) {
            bgPids[i] = bgPids[--bgCount];
            return;
        }
    }
}

/************************************************************
 * catchSIGTSTP - toggles foreground-only mode on SIGTSTP
 * - writes messages directly using write() to be async-signal-safe
 * - updates backgroundOn flag
 * 
 * arguments:
 * signo: signal number (unused)
 ************************************************************/
void catchSIGTSTP(int signo) {
    
    // if foreground-only mode is off, turn it on
    if (backgroundOn) {
        const char* msg = "\nEntering foreground-only mode (& is now ignored)\n";

        // use write() instead of printf() to be reentrant
        write(1, msg, strlen(msg));
        backgroundOn = 0;

    // if foreground-only mode is on, turn it off
    } else {
        const char* msg = "\nExiting foreground-only mode\n";

        // use write() instead of printf() to be reentrant
        write(1, msg, strlen(msg));
        backgroundOn = 1;
    }
}

/************************************************************
 * printStatus - prints exit/termination status of last foreground process
 * - used by "status" built-in command
 ************************************************************/
void printStatus() {

    // print based on how last process ended
    if (lastWasSignaled) {
        printf("terminated by signal %d\n", lastTermSignal);
    } else {
        printf("exit value %d\n", lastExitStatus);
    }
    fflush(stdout);
}

/************************************************************
 * reapBackground - checks for completed background processes
 * - uses waitpid() with WNOHANG to avoid blocking
 * - prints exit/termination status of completed processes
 ************************************************************/
void reapBackground() {
    int status;
    pid_t pid;

    // loop reaping all available finished background children
    // WNOHANG makes sure the shell does not block if there are no completed children
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

        printf("background pid %d is done: ", pid);

        // WIFEXITED checks if the child exited normally
        if (WIFEXITED(status)) {
            // The child called exit()
            printf("exit value %d\n", WEXITSTATUS(status));

        // WIFSIGNALED checks if the child was terminated by a signal
        } else if (WIFSIGNALED(status)) {
            // The child terminated bc of a signal
            printf("terminated by signal %d\n", WTERMSIG(status));
        }
        fflush(stdout);

        // remove PID from tracked background PIDs
        removeBgPid(pid);
    }
}

/************************************************************
 * getInput - reads and parses user input
 * - handles $$ expansion, input/output redirection,
 *  background indicator
 * 
 * arguments:
 * input[]: argv-style array of command + args
 * inFile: input redirection file ("" if none)
 * outFile: output redirection file ("" if none)
 * background: 1 if background process, 0 if foreground
 * pid: shell's PID for $$ expansion
 ************************************************************/
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

    // remove newline
    arr[strcspn(arr, "\n")] = '\0';

    // blank or comment line
    if (arr[0] == '\0' || arr[0] == '#') {
        input[0] = NULL;
        return;
    }

    // Expand $$
    char expanded[CMDLENGTH] = "";
    char pidStr[20];
    sprintf(pidStr, "%d", pid);

    for (int i = 0; arr[i] != '\0'; ) {

        // check for $$ sequence
        if (arr[i] == '$' && arr[i+1] == '$') {
            strcat(expanded, pidStr);
            i += 2;
        } else {
            strncat(expanded, &arr[i], 1);
            i++;
        }
    }
    strcpy(arr, expanded);

    // Tokenize input string
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

            // get next token as input file
            token = strtok_r(NULL, s, &saveptr);
            if (token) strcpy(inFile, token);

        // handle output redirection
        } else if (strcmp(token, ">") == 0) {

            // get next token as output file
            token = strtok_r(NULL, s, &saveptr);
            if (token) strcpy(outFile, token);

        // regular argument
        } else {
            input[i++] = strdup(token);
        }

        token = strtok_r(NULL, s, &saveptr);
    }

    // add null terminator to input array for execvp()
    input[i] = NULL;
}

/************************************************************
 * execCommand - executes non-built-in commands
 * - handles forking, redirection, and background/foreground
 * - updates status info for "status" built-in
 * 
 * arguments:
 * input[]: argv-style array of command + args
 * inFile: input redirection file ("" if none)
 * outFile: output redirection file ("" if none)
 * background: 1 if background process, 0 if foreground
 ************************************************************/
void execCommand(char* input[], char inFile[], char outFile[], int background) {

    // Fork a new process for the non – built-in command
    // fork() creates a new process by duplicating the calling process (child process)
    // returns the child's PID which is what is used to track it
    pid_t spawnpid = fork();
    int status;

    switch (spawnpid) {

        case -1:
            // fork() failure
            printf("fork failed\n");
            fflush(stdout);
            break;

        /* child process */
        case 0: {

            // update SIGTSTP to be ignored in child processes
            // SIGTSTP is only handled by the parent shell
            struct sigaction sa_ignore;
            sa_ignore.sa_handler = SIG_IGN;
            sigfillset(&sa_ignore.sa_mask);
            sa_ignore.sa_flags = 0;
            sigaction(SIGTSTP, &sa_ignore, NULL);

            // Child process SIGINT handling
            struct sigaction sa_int;

            // foreground processes: default SIGINT behavior
            // background processes: ignore SIGINT
            sa_int.sa_handler = (!background || !backgroundOn) ? SIG_DFL : SIG_IGN;
            sigfillset(&sa_int.sa_mask);
            sa_int.sa_flags = 0;
            sigaction(SIGINT, &sa_int, NULL);

            // Input redirection using open() and dup2()
            if (inFile[0] != '\0') {
                // open() opens a file descriptor for reading
                int fd = open(inFile, O_RDONLY);

                // Error handling for open()
                if (fd == -1) {
                    printf("%s: cannot open input file\n", inFile);
                    fflush(stdout);
                    exit(1);
                }

                // dup2() redirects STDIN to the opened file
                dup2(fd, 0); 
                close(fd);
            
            // No input file specified for background process
            } else if (background && backgroundOn) {
                // background tasks default to /dev/null input
                int fd = open("/dev/null", O_RDONLY);
                if (fd != -1) { dup2(fd, 0); close(fd); }
            }

            // Output redirection using open() and dup2()
            if (outFile[0] != '\0') {

                // open() opens or creates a file descriptor for output
                // 0644 sets read+write for owner, read for group+others
                int fd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                // Error handling for open()
                if (fd == -1) {
                    printf("%s: cannot open output file\n", outFile);
                    fflush(stdout);
                    exit(1);
                }

                // dup2() redirects STDOUT (fd 1) to the newly opened file
                dup2(fd, 1); 
                close(fd);

            } else if (background && backgroundOn) {
                // background tasks send both stdout + stderr to /dev/null
                // open() opens a file descriptor for /dev/null
                int fd = open("/dev/null", O_WRONLY);

                // dup2() redirects both stdout (fd 1) and stderr (fd 2) to /dev/null
                if (fd != -1) { dup2(fd, 1); dup2(fd, 2); close(fd); }
            }

            // execvp() replaces the child process with the requested command.
            // it will only return if there is an error.
            execvp(input[0], input);

            // only reached if execvp() failed
            printf("%s: no such file or directory\n", input[0]);
            fflush(stdout);
            exit(1);
        }

        /* parent process */
        default:

            if (background && backgroundOn) {
                // background
                // No waitpid() so the shell can continue running
                // Track background PIDs to reap later
                addBgPid(spawnpid);

                // Inform user of background PID
                printf("background pid is %d\n", spawnpid);
                fflush(stdout);

            } else {
                // foreground
                // waitpid() waits for the child to terminate before moving to the next process 
                waitpid(spawnpid, &status, 0);

                // Update status info for "status" built-in
                // WIFEXITED checks if the child exited normally 
                if (WIFEXITED(status)) {
                    lastWasSignaled = 0;
                    lastExitStatus = WEXITSTATUS(status);
                
                // WIFSIGNALED checks if the child was terminated by a signal
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

/* main loop */
int main() {
    pid_t shellPid = getpid();
    char* input[MAXARGS];
    char inFile[256];
    char outFile[256];
    int background;

    // set up shell ignores SIGINT. this will be updated for foreground child processes 
    // sigaction() is used to install signal handlers safely.
    struct sigaction sa_int = {0};
    sa_int.sa_handler = SIG_IGN;
    sigfillset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    // set up SIGTSTP to be handled by the parent 
    // sigaction() is used to install signal handlers safely.
    struct sigaction sa_tstp = {0};
    sa_tstp.sa_handler = catchSIGTSTP;
    sigfillset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = 0;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    while (1) {

        getInput(input, inFile, outFile, &background, shellPid);

        // skip empty input and comments
        if (input[0] == NULL) continue;

        /* built-in: exit */
        if (strcmp(input[0], "exit") == 0) {

            // kill all remaining background processes
            for (int i = 0; i < bgCount; i++)
                kill(bgPids[i], SIGKILL);

            // free arguments
            for (int i = 0; input[i]; i++) free(input[i]);

            break;
        }

        /* built-in: cd */
        else if (strcmp(input[0], "cd") == 0) {
            if (input[1] != NULL) {

                // change to specified directory and print error if it fails
                if (chdir(input[1]) == -1) {
                    printf("cd: no such file or directory\n");
                    fflush(stdout);
                }
            } else {
                chdir(getenv("HOME"));
            }
        }

        /* Built-in: status */
        else if (strcmp(input[0], "status") == 0) {
            printStatus();
        }

        /* all other commands */
        else {
            int bg = background && backgroundOn;
            execCommand(input, inFile, outFile, bg);
        }

        // cleanup completed background processes
        reapBackground(); 

        // free allocated input strings
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
