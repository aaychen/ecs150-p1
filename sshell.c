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
        int num_args;
        bool has_redirection;
        char *output_file;
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
                // Reset struct members to avoid double free when deallocating memory
                c.cmd = NULL;
                c.num_args = 0;
                c.has_redirection = false;
                c.output_file = NULL;
                int args_indx = -1;
                char prev_char = ' ';
                for (size_t i = 0; i < strlen(cmdline); i++) {
                        char ch = cmdline[i];
                        if (ch == '>') {
                                c.has_redirection = true;
                        } else if (c.has_redirection && !isspace(ch)) { // if redirection symbol read in, the rest of the command line should refer to output file
                                if (prev_char == '>' || isspace(prev_char)) {
                                        c.output_file = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                }
                                strncat(c.output_file, &ch, 1);
                        } else if (!isspace(ch)) { // if no redirection symbol, tokens are the command or arguments
                                if (isspace(prev_char)) {
                                        c.args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.num_args++;
                                        if (args_indx == 0) c.cmd = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                }
                                strncat(c.args[args_indx], &ch, 1);
                                if (args_indx == 0) strncat(c.cmd, &ch, 1);
                        }
                        prev_char = ch;                     
                }
                c.args[++args_indx] = NULL;

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
                        if (c.has_redirection) {
                                int fd = open(c.output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                        }
                        execvp(c.cmd, c.args);
                        fprintf(stderr, "Error: command not found\n");
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
        for (int i = 0; i < c.num_args; i++) {
                free(c.args[i]);
        }
        if (c.cmd) free(c.cmd);
        if (c.output_file) free(c.output_file);
}