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

typedef struct cmd {
        char *args[ARG_MAX + 2];
        int num_args;
} cmd;

typedef struct cmdline {
        cmd cmd[4];
        bool has_redirection;
        char *output_file;
} cmdline;

void cleanup(cmd c);

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
                for (int i = 0; i < 4; i ++) {
                        c.cmd[i].num_args = 0;
                }
                // c.cmd = NULL;
                // c.num_args = 0;
                c.has_redirection = false;
                c.output_file = NULL;
                int args_indx = -1;
                int cmd_indx = -1;
                char prev_char = ' ';
                int num_pipe = -1;
                for (size_t i = 0; i < strlen(cmdline); i++) {
                        char ch = cmdline[i];
                        if (ch == '|') {
                                num_pipe++;
                        } else if (num_pipe == cmd_indx && cmd_indx != -1 && !isspace(ch)) { // pipe sign was read, so new command
                                args_indx = -1;
                                c.cmd[++cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                c.cmd[cmd_indx].num_args++;
                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        } else if (ch == '>') {
                                c.has_redirection = true;
                        } else if (c.has_redirection && !isspace(ch)) { // if redirection symbol read in, the rest of the command line should refer to output file
                                if (prev_char == '>' || isspace(prev_char)) {
                                        c.output_file = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                }
                                strncat(c.output_file, &ch, 1);
                        } else if (!isspace(ch)) { // tokens are command or arguments
                                if (cmd_indx == -1) { // first command
                                        c.cmd[++cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.cmd[cmd_indx].num_args++;
                                        // if (args_indx == 0) c.cmd = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                } else if (isspace(prev_char)) { // arguments
                                        c.cmd[cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.cmd[cmd_indx].num_args++;
                                }
                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                                // if (args_indx == 0) strncat(c.cmd, &ch, 1);
                        }
                        prev_char = ch;                     
                }
                for (int i = 0; i <= cmd_indx; i ++) {
                        c.cmd[i].args[++args_indx] = NULL;
                }

                /* Builtin command */
                if (!strcmp(c.cmd[0].args[0], "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        break;
                } else if (!strcmp(c.cmd[0].args[0], "pwd")) {
                        char *path = getcwd(NULL, 0);
                        fprintf(stdout, "%s\n", path);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "cd")) {
                        if (chdir(c.cmd[cmd_indx].args[1]) == -1) {
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
                        execvp(c.cmd[cmd_indx].args[0], c.cmd[cmd_indx].args);
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

                for (int i = 0; i < cmd_indx; i++) {
                        cleanup(c.cmd[i]);
                }
                if (c.output_file) free(c.output_file);
        }
        // cleanup(c);
        return EXIT_SUCCESS;
}

/* De-allocate memory */
void cleanup(cmd cmd) {
        for (int i = 0; i < cmd.num_args; i++) {
                free(cmd.args[i]);
        }
}