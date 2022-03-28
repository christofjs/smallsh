// Author: Christof Schoenborn
// Course: CS344
// Description: Design a small shell that performs a subset of bash features

// include necessary packages
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>


// global vars
pid_t sigCaught = -2;
int redirInput = 0;
int redirOutput = 0;
int to_exit = 0;
int allow_bg = 0;
int statusVar = 0;
char sh_input[2049] = {'\0'};

// input command structure
struct sh_command {
    char *command;
    char *args[513];
    char *inputFile;
    char *outputFile;
    int background;
};

void clearCommand(struct sh_command *c) {
    /*
    Wipes user command's allocated memory to ensure clean write
    :param: user command struct
    */
    memset(c, 0, sizeof(struct sh_command));
}

void freeCommand(struct sh_command *c) {
    /*
    Checks for memory allocations and frees them if they exist
    :param: user command struct
    */
    if (c->command != NULL) {
        free(c->command);
    }
    if (c->inputFile != NULL) {
        free(c->inputFile);
    }
    if (c->outputFile != NULL) {
        free(c->outputFile);
    }
    int i;
    for (i=0; i < (sizeof(c->args) / sizeof(c->args[0])); i++) {
        if (c->args[i] != NULL) {
            free(c->args[i]);
        }
    }
}

char *insertPID(char *command) {
    /*
    convert any instances of $$ to the PID
    :param: command string
    :retrn: converted command string
    */
    char temppid[2049]; // to build new string
    char pid[7];        // hold pid

    // wipe both arrays
    memset(temppid, '\0', strlen(temppid));
    memset(pid, '\0', strlen(pid));

    // iterators
    int i;
    int j = 0;
    int k;

    // find pid
    sprintf(pid, "%d", getpid());
    fflush(stdout);
    // iterate command string
    for (i = 0; i < strlen(command) - 1; i++) {
        // check for $$
        if (command[i] == '$' && command[i + 1] == '$') {
            // replace $$ with PID
            for (k = 0; k < strlen(pid); k++) {
                temppid[j] = pid[k];
                j++;
            }
            i++;
        // copy char for char
        } else {
            temppid[j] = command[i];
            j++;
        }
    }
    // write PID string over $$ string
    memset(command, '\0', strlen(command));
    strcpy(command, temppid);
    return command;
}

int noComment(void) {
    /*
    This will ignore all processing and jump to a new command line when the user either hits enter immediately or types a line beginning with a '#'
    :retrn: no comment bool
    */
    // check for comment or blank line
    if (sh_input[0] == '#' || sh_input[0] == '\n') {
        return 0;
    }
    return 1;
}

int checkBuiltIns(char *temp, char *token, char *ptr) {
    /*
    Search the first token of the command string for built-in commands exit, cd, and status.
    Exit - leave the shell
    cd <directory>- change directory (no directory goes to HOME)
    status - return exit status of last exited program
    :param: user command string, first token, token pointer
    :retrn: command recognized bool
    */
    char workingDir[2049] = {'\0'};

    // handle exit command
    if (strncmp(token, "exit", 4) == 0) {
        printf("Goodbye!\n");
        fflush(stdout);
        to_exit = 1;
        return 1;

    // handle cd command
    } else if (strncmp(token, "cd", 2) == 0) {

        // change to home directory
        if (ptr[0] == '\000') {
            char *curDir = calloc(strlen(getenv("HOME") + 1), sizeof(char));
            curDir = getenv("HOME");

            // changes directory in if statement, enters on error
            if (chdir(curDir) != 0) {
                printf("Error finding home.\n");
                fflush(stdout);
                return 1;
            } else{
                // wipe workingDir to length of current contents, fill with cwd
                memset(workingDir, '\0', strlen(workingDir));
                getcwd(workingDir, sizeof(workingDir));
                printf("%s\n", workingDir);
                fflush(stdout);
            }
        // change to directory from root
        } else if (ptr[0] == '/') {

            // var to store dirname
            char *newDir = strtok(ptr, "\0");
            // changes directory in if statement, enters on error
            if (chdir(newDir) != 0) {
                // wipe workingDir to length of current contents, fill with cwd
                memset(workingDir, '\0', strlen(workingDir));
                getcwd(workingDir, sizeof(workingDir));
                printf("No such directory: %s\n%s\n", newDir, workingDir);
                fflush(stdout);
                return 1;
            } else{
                // wipe workingDir to length of current contents, fill with cwd
                memset(workingDir, '\0', strlen(workingDir));
                getcwd(workingDir, sizeof(workingDir));
                printf("%s\n", workingDir);
                fflush(stdout);
            }
        // change to new directory from relative path
        } else {

            // wipe workingDir to length of current contents, fill with cwd
            memset(workingDir, '\0', strlen(workingDir));
            getcwd(workingDir, sizeof(workingDir));
            char *curDir = calloc(strlen(workingDir) + strlen(ptr) + 2, sizeof(char));

            // add new directory onto current path
            strcpy(curDir, workingDir);
            strcat(curDir, "/");
            strcat(curDir, ptr);

            // changes directory in if statement, enters on error
            if (chdir(curDir) != 0) {
                // wipe workingDir to length of current contents, fill with cwd
                memset(workingDir, '\0', strlen(workingDir));
                getcwd(workingDir, sizeof(workingDir));
                printf("No such directory: %s\n%s\n", ptr, workingDir);
                fflush(stdout);
                free(curDir);
                return 1;
            } else{
                // wipe workingDir to length of current contents, fill with cwd
                memset(workingDir, '\0', strlen(workingDir));
                getcwd(workingDir, sizeof(workingDir));
                printf("%s\n", workingDir);
                fflush(stdout);
                free(curDir);
            }
        }
        return 1;
    // handle status command
    } else if (strncmp(token, "status", 6) == 0) {
        printf("Exited with value %i\n", statusVar);
        fflush(stdout);
        return 1;
    }
    return 0;
}

int parseInput(char *commandLine, struct sh_command *newCommand) {
    /*
    Read user input and separate it into vars in the sh_command struct
    :param: user input string, command struct
    :retrn: passing built-in bool
    // borrowed own code from previous movies.c and filesAndDirs.c
    // newline removal introduced by https://stackoverflow.com/questions/16677800/strtok-not-discarding-the-newline-character
    */

    // use temp array to prevent altering original input
    char temp[2049];
    // iterator for arg array
    int i = 0;

    // wipe temp to ensure clear memory block
    memset(temp, '\0', sizeof(temp));
    strcpy(temp, commandLine);

    // tokenizing pointer
    char *curptr = temp;

    // remove newline from fgets string
    char *removeThis = strchr(temp, '\n');
    if (removeThis != NULL) {
        *removeThis = '\0';
    }

    // change $$ to PID and fill temp array
    while (strstr(temp, "$$")) {
        insertPID(temp);
    }

    // collect first token
    char* token = strtok_r(curptr, " ", &curptr);

    // check for built-in functions
    if (checkBuiltIns(temp, token, curptr) == 1) {
        // special case handled, end parsing
        return 1;
    }

    // allocate and fill command memory
    newCommand->command = calloc(strlen(token) + 1, sizeof(char));
    strcpy(newCommand->command, token);
    // allocate and fill first arg with command
    newCommand->args[i] = calloc(strlen(token) + 1, sizeof(char));
    strcpy(newCommand->args[i], token);
    i++;

    // check each token for input, output, and background indications
    do {
        // set input indicated
        if (curptr[0] == '<' && curptr[1] == ' ') {
            // ignore special symbol
            token = strtok_r(curptr, " ", &curptr);
            // collect, allocate for, and store input location
            token = strtok_r(curptr, " ", &curptr);
            newCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(newCommand->inputFile, token);
            // toggle redir bool
            redirInput = 1;

        // set output indicated
        } else if (curptr[0] == '>' && curptr[1] == ' ') {
            // ignore special symbol
            token = strtok_r(curptr, " ", &curptr);
            // collect, allocate for, and store output location
            token = strtok_r(curptr, " ", &curptr);
            newCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(newCommand->outputFile, token);
            // toggle redir bool
            redirOutput = 1;

        // send to background indicated
        } else if (curptr[0] == '&' && curptr[1] == '\0') {
            // make it a background process
            if (allow_bg == 0) {
                newCommand->background = 1;
            }
            break;

        // end of command
        } else if (curptr[0] == '\0') {
            break;

        // no special symbols encountered, collect, allocate for, and store command arg
        } else {
            token = strtok_r(curptr, " ", &curptr);
            newCommand->args[i] = calloc(strlen(token) + 1, sizeof(char));
            strcpy(newCommand->args[i], token);
            i++;
        }
    // loop until curptr finds null char
    } while (curptr[0] != '\0');
    // indicate no built-ins encountered
    return 0;
}

void printCommand(struct sh_command *command) {
    /*
    Non-vital function for debugging purposes - prints command struct vars
    :param: command struct
    */
    printf("# DEBUG INFO #\n--- Command: %s\n", command->command);
    fflush(stdout);
    int i = 0;
    while (command->args[i] != NULL) {
        printf("--- arg: %s\n", command->args[i]);
        fflush(stdout);
        i++;
    }
    printf("--- inputFile: %s\n", command->inputFile);
    fflush(stdout);
    printf("--- outputFile: %s\n", command->outputFile);
    fflush(stdout);
    printf("--- background: %i\n", command->background);
    fflush(stdout);
    printf("##############");
    fflush(stdout);
}

void parSigStp(int signum) {
    /*
    Signal catching function - activated on receipt of SIGTSTP command, prevents running background
    :param: signal passed (unused)
    */
    // toggle allow-background bool, print message
    if (allow_bg == 0) {
        char *message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
        allow_bg = 1;
    // toggle allow-background bool and print message
    } else {
        char *message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        allow_bg = 0;
    }
}

int main(int argc, char *argv[]) {

    // signal handling for parent
    struct sigaction parIgnore_SIGINT = {0};
    // ignore other incoming signals
    sigfillset(&parIgnore_SIGINT.sa_mask);
    // send to terminating function
    parIgnore_SIGINT.sa_handler = SIG_IGN;
    // don't flag anything
    parIgnore_SIGINT.sa_flags = 0;
    // set handler on SIGINT
    sigaction(SIGINT, &parIgnore_SIGINT, NULL);

    // SIGSTP handling for foreground children
    struct sigaction catch_SIGTSTP = {0};
    // ignore other incoming signals
    sigfillset(&catch_SIGTSTP.sa_mask);
    // send to terminating function
    catch_SIGTSTP.sa_handler = parSigStp;
    // don't flag anything
    catch_SIGTSTP.sa_flags = 0;
    // set handler on catch_SIGTSTP
    sigaction(SIGTSTP, &catch_SIGTSTP, NULL);

    while (1) {

        pid_t checkAll;
        // -1 checks for any finished children, waitpid returns > 0 when found, WNOHANG doesn't wait
        while ((checkAll = waitpid(-1, &statusVar, WNOHANG)) > 0) {
            // exited normally
            if (WIFEXITED(statusVar)) {
                // fix statusVar format
                statusVar = WEXITSTATUS(statusVar);
                // make sure process ending isn't a previously failed foreground process
                if (checkAll != sigCaught) {
                    printf("Background process %d ended, exit value %d\n", checkAll, statusVar);
                    fflush(stdout);
                // foreground process ending late
                } else {
                    // reset catch value to something that can't be returned by waitpid
                    sigCaught = -2;
                }
            // terminated abnormally
            } else {
                // fix statusVar format
                statusVar = WTERMSIG(statusVar);
                printf("Background process %d ended, terminated with signal %d\n", checkAll, statusVar);
                fflush(stdout);
            }
        }

        // send empty prompt to terminal
        printf(": ");
        fflush(stdout);

        // allocate memory for command struct with main scope and clear it
        struct sh_command *newCommand = malloc(sizeof(struct sh_command));
        clearCommand(newCommand);
        newCommand->background = 0;

        // clear input buffer, reset redirect bools, and receive user command - 2049 chars
        memset(sh_input, '\0', sizeof(sh_input));
        redirOutput = 0;
        redirInput = 0;
        fgets(sh_input, 2049, stdin);

        // check for blankline/comment prior to parsing
        if (noComment() == 0) {
            continue;
        }

        // prevent segfault when catching SIGTSTP (i.e. fgets exited early)
        if (sh_input[0] == '\0') {
            free(newCommand);
            continue;
        }

        // caught a built-in
        if (parseInput(sh_input, newCommand) == 1){
            // look for exit bool
            if (to_exit == 1) {
                // terminate processes in process group
                killpg(0, SIGTERM);
                return EXIT_SUCCESS;
            }
            continue;
        }

        // below developed with guidance from http://faculty.cs.niu.edu/~hutchins/csci480/forkexm1.htm and Creating and Terminating Processes Exploration and https://stackoverflow.com/questions/7155810/example-of-waitpid-wnohang-and-sigchld and https://linux.die.net/man/2/waitpid

        // create child process and begin fork
        pid_t childPID;
        childPID = fork();

        // evaluate fork
        switch (childPID) {
            // failed fork
            case -1:
                perror("The fork was unsuccessful.\n");
                exit(1);
                break;
            // '{}' for '0' case establish a scope for the child process to allow struct declaration
            // successful fork
            case 0: {
                // guidance from Processes and I/O, and Signal Handling Explorations 

                // SIGTSTP handling for foreground children
                struct sigaction catch_SIGTSTP = {0};
                // ignore other incoming signals
                sigfillset(&catch_SIGTSTP.sa_mask);
                // send to terminating function
                catch_SIGTSTP.sa_handler = SIG_IGN;
                // don't flag anything
                catch_SIGTSTP.sa_flags = 0;
                // set handler on catch_SIGTSTP
                sigaction(SIGTSTP, &catch_SIGTSTP, NULL);

                // check input redirection bool
                if (redirInput == 1) {
                    // open file in readonly
                    int inRedir = open(newCommand->inputFile, O_RDONLY);
                    // failed open
                    if (inRedir == -1) {
                        printf("Cannot open %s for input\n", newCommand->inputFile);
                        fflush(stdout);
                        exit(1);
                    }
                    // change stdin to file path
                    int redirectDup = dup2(inRedir, 0);
                    // failed dup
                    if (redirectDup == -1) {
                        printf("Cannot redirect stdin to %s\n", newCommand->inputFile);
                        fflush(stdout);
                        exit(1);
                    }
                    // close input file
                    close(inRedir);
                }

                // check input redirection bool
                if (redirOutput == 1) {
                    // open file for writing: create if necessary, truncate if exists
                    int outRedir = open(newCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    // failed open
                    if (outRedir == -1) {
                        printf("Cannot open %s for output\n", newCommand->outputFile);
                        fflush(stdout);
                        exit(1);
                    }
                    // change stdout to file path via dup2
                    int redirOutDup = dup2(outRedir, 1);
                    // failed dup
                    if (redirOutDup == -1) {
                        printf("Cannot redirect stdout to %s\n", newCommand->outputFile);
                        fflush(stdout);
                        exit(1);
                    }
                    // close file
                    close(outRedir);
                }

                // check background process bool
                if (newCommand->background == 1) {
                    // SIGINT handling for background children
                    struct sigaction bgIgnore_SIGINT = {0};
                    // ignore other incoming signals
                    sigfillset(&bgIgnore_SIGINT.sa_mask);
                    // send to terminating function
                    bgIgnore_SIGINT.sa_handler = SIG_IGN;
                    // don't flag anything
                    bgIgnore_SIGINT.sa_flags = 0;
                    // set handler on SIGINT
                    sigaction(SIGINT, &bgIgnore_SIGINT, NULL);

                    // open null for writing
                    int bgRedir = open("/dev/null", O_WRONLY);
                    // failed open
                    if (bgRedir == -1) {
                        printf("Cannot open /dev/null for output\n");
                        fflush(stdout);
                        exit(1);
                    }

                    // change stdout to null for void dump
                    int redirBgDup = dup2(bgRedir, 1);
                    // failed dup
                    if (redirBgDup == -1) {
                        printf("Cannot redirect stdout to /dev/null\n");
                        fflush(stdout);
                        exit(1);
                    }

                    // close null for writing
                    close(bgRedir);

                // foreground process handling
                } else {
                    // SIGINT handling for foreground children
                    struct sigaction catch_SIGINT = {0};
                    // ignore other incoming signals
                    sigfillset(&catch_SIGINT.sa_mask);
                    // send to terminating function
                    catch_SIGINT.sa_handler = SIG_DFL;
                    // don't flag anything
                    catch_SIGINT.sa_flags = 0;
                    // set handler on SIGINT
                    sigaction(SIGINT, &catch_SIGINT, NULL);
                }
                execvp(newCommand->command, newCommand->args);
                // failed exec command
                printf("%s: no such file or directory\n", newCommand->command);
                fflush(stdout);
                exit(1);
            }
            // parent resumption
            default:
                // foreground: wait for child and store statusVar
                if (newCommand->background == 0) {
                    pid_t newPID;
                    newPID = waitpid(childPID, &statusVar, 0);
                    // catch a foreground process terminating unsuccessfully
                    if (newPID < 0) {
                        sigCaught = childPID;
                    }
                    // evaluate child's exit
                    if (WIFEXITED(statusVar)) {
                        // format statusVar if exited successfully
                        statusVar = WEXITSTATUS(statusVar);
                    } else {
                        // format statusVar if process terminated
                        statusVar = WTERMSIG(statusVar);
                        printf("Process %d ended, terminated with signal %d\n", newPID, statusVar);
                        fflush(stdout);
                    }
                } else {
                    // print background PID
                    printf("Background PID is %d\n", childPID);
                    fflush(stdout);
                }
                break;
        }
        // release allocated memory
        freeCommand(newCommand);
        free(newCommand);
    }
}