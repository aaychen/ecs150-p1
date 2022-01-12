#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define CMDLINE_MAX 512

int main(void)
{
        char cmd[CMDLINE_MAX];
        char *args[] = {cmd, NULL};

        while (1) {
                char *nl;
                int retval;

                /* Print prompt */
                printf("sshell$ ");
                fflush(stdout);

                /* Get command line */
                fgets(cmd, CMDLINE_MAX, stdin);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", cmd);
                        fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmd, '\n');
                if (nl)
                        *nl = '\0';

                /* Builtin command */
                if (!strcmp(cmd, "exit")) {
                        fprintf(stderr, "Bye...\n");
                        break;
                }

                /* Regular command */
                // retval = system(cmd);
                int status;
                if (!fork()) {
                        execvp(cmd, args);
                        perror("execvp");
                        exit(1);
                } else {
                        waitpid(-1, &status, 0);
                        retval = WEXITSTATUS(status);
                }
                fprintf(stderr, "+ completed '%s' [%d]\n", cmd, retval);
        }

        return EXIT_SUCCESS;
}