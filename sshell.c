#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define CMDLINE_MAX 512
#define ARG_MAX 16
#define TOKEN_LEN_MAX 32

typedef struct cmdline {
        char *cmd;
        char *args[ARG_MAX];
} cmdline;

int main(void) {
        cmdline c;
        char cmdline[CMDLINE_MAX];
        char *exec_args[ARG_MAX + 2];

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
                /* Get command */
                char *token;
                int i = 0;
                token = strtok(cmdline, " ");
                if (token != NULL) {
                        c.cmd = exec_args[i++] = token;
                        token = strtok(NULL, " ");
                }
                /* Get all arguments */
                while (token != NULL) {
                        exec_args[i++] = token;
                        // c.args[i++] = token;
                        token = strtok(NULL, " ");
                }
                exec_args[i] = NULL;

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
                        if (chdir(exec_args[1]) == -1) {
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
                        execvp(c.cmd, exec_args);
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
        }
        return EXIT_SUCCESS;
}