#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <ctype.h>
#include <fcntl.h>


#define CMDLINE_MAX 512
#define ARG_MAX 16
#define TOKEN_LEN_MAX 32

typedef struct cmdline {
        char *cmd;
        char *args[ARG_MAX + 2];
        int numArgs;
        bool hasRedirection;
        char *outputFileName;
} cmdline;

void cleanup(cmdline c);

int main(void) {
        cmdline c;
        char cmdline[CMDLINE_MAX];

        while (1) {
                char *nl;
                int retval = 0;

                /* Print prompt */
                printf("sshell$ ");
                fflush(stdout);

                /* Get command line */
                fgets(cmdline, CMDLINE_MAX, stdin);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", cmdline);
                        fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmdline, '\n');
                if (nl)
                        *nl = '\0';

                /* Parse command line */
                c.cmd = NULL;
                c.numArgs = 0;
                c.hasRedirection = false;
                c.outputFileName = NULL;
                int argsIndx = -1;
                char prevChar = ' ';
                for (size_t i = 0; i < strlen(cmdline); i++) {
                        char ch = cmdline[i];
                        if (ch == '>') {
                                c.hasRedirection = true;
                        }
                        else if (c.hasRedirection && !isspace(ch)) {
                                if (prevChar == '>' || isspace(prevChar)) {
                                        c.outputFileName = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                }
                                strncat(c.outputFileName, &ch, 1);
                        }
                        else if (!isspace(ch)) {
                                if (isspace(prevChar)) {
                                        c.args[++argsIndx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.numArgs++;
                                        if (argsIndx == 0) c.cmd = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                }
                                strncat(c.args[argsIndx], &ch, 1);
                                if (argsIndx == 0) strncat(c.cmd, &ch, 1);
                        }
                        prevChar = ch;                                                                                                                                                                                   
                }
                c.args[++argsIndx] = NULL;



                /* Builtin command */
                if (!strcmp(c.cmd, "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        break;
                } else if (!strcmp(c.cmd, "pwd")) {
                        char *path = getcwd(NULL, 0);
                        fprintf(stdout, "%s\n", path);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        continue;
                } else if (!strcmp(c.cmd, "cd")) {
                        if (chdir(c.args[1]) == -1) {
                                fprintf(stderr, "Error: cannot cd into directory\n");
                                retval = 1;
                        }
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        continue;
                }

                /* Regular command */
                int status;
                int pid = fork();
                if (pid == 0) { /* The shell creates a child process */
                        /* The child process runs the command line */
                        if (c.hasRedirection) {
                                int fd = open(c.outputFileName, O_RDWR | O_CREAT, 0644);
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                        }
                        execvp(c.cmd, c.args);
                        perror("execvp");
                        exit(1);
                } else if (pid > 0) {
                        /* The parent process (the shell) waits for the child process's completion & collects its exit status */
                        waitpid(-1, &status, 0);
                        retval = WEXITSTATUS(status);
                } else {
                     perror("fork");
                     exit(1);   
                }
                fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                cleanup(c);
        }
        cleanup(c);
        return EXIT_SUCCESS;
}

/* De-allocate memory */
void cleanup(cmdline c) {
        for (int i = 0; i < c.numArgs; i++) {
                free(c.args[i]);
        }
        if (c.cmd) free(c.cmd);
        if (c.outputFileName) free(c.outputFileName);
}