#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#define CMDLINE_MAX 512
#define ARG_MAX 16
#define TOKEN_LEN_MAX 32
#define NUM_CMDS_MAX 4
#define NUM_PIPES_MAX 3

typedef struct cmd {
        char *args[ARG_MAX + 2];
        int num_args;
} cmd;

typedef struct cmdline {
        cmd cmd[4];
        bool has_redirection;
        bool error_to_file;
        bool error_to_pipe[NUM_PIPES_MAX];
        char *output_file;
} cmdline;

/* De-allocate memory */
void cleanup(cmd cmd) {
        for (int i = 0; i < cmd.num_args; i++) {
                free(cmd.args[i]);
        }
}

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
                for (int i = 0; i < NUM_CMDS_MAX; i++) {
                        c.cmd[i].num_args = 0;
                }
                for (int i = 0; i < NUM_PIPES_MAX; i++) {
                        c.error_to_pipe[i] = false;
                }
                c.has_redirection = false;
                c.error_to_file = false;
                c.output_file = NULL;

                int args_indx = -1;
                int cmd_indx = -1;
                int num_pipe = -1;
                char prev_char = ' ';
                for (size_t i = 0; i < strlen(cmdline); i++) {
                        char ch = cmdline[i];
                        if (ch == '>') {
                                c.has_redirection = true;
                        } else if (prev_char == '>' && ch == '&') {
                                c.error_to_file = true;
                        } else if (prev_char == '|' && ch == '&') {
                                c.error_to_pipe[num_pipe] = true;
                        } else if (ch == '|') {
                                num_pipe++;
                        } else if (num_pipe == cmd_indx && cmd_indx != -1 && !isspace(ch)) { // pipe sign was read, so new command
                                c.cmd[cmd_indx].args[++args_indx] = NULL; // append NULL to args list for previous command
                                // new command
                                args_indx = -1;
                                c.cmd[++cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                c.cmd[cmd_indx].num_args++;

                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        } else if (c.has_redirection && !isspace(ch)) { // if redirection symbol read in, the rest of the command line should refer to output file
                                if (prev_char == '>' || isspace(prev_char)) {
                                        c.output_file = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                }
                                strncat(c.output_file, &ch, 1);
                        } else if (!isspace(ch)) { // tokens are command or arguments
                                if (cmd_indx == -1) { // first command
                                        c.cmd[++cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.cmd[cmd_indx].num_args++;
                                } else if (isspace(prev_char)) { // arguments
                                        c.cmd[cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.cmd[cmd_indx].num_args++;
                                }
                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        }
                        prev_char = ch;                     
                }
                c.cmd[cmd_indx].args[++args_indx] = NULL; // append NULL to args list for last command
                

                /* Builtin command */
                if (!strcmp(c.cmd[0].args[0], "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        break;
                } else if (!strcmp(c.cmd[0].args[0], "pwd")) {
                        char *dir_path = getcwd(NULL, 0);
                        fprintf(stdout, "%s\n", dir_path);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        // cleanup();
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "cd")) {
                        if (chdir(c.cmd[cmd_indx].args[1]) == -1) {
                                fprintf(stderr, "Error: cannot cd into directory\n");
                                retval = 1;
                        }
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        // cleanup();
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "sls")) {
                        DIR *dir_stream = opendir(".");
                        struct dirent *item;
                        struct stat item_stat;
                        while ((item = readdir(dir_stream)) != NULL) {
                                if (item->d_name && item->d_name[0] != '.') { // ignore hidden file entries
                                        stat(item->d_name, &item_stat);
                                        fprintf(stdout, "%s (%lld bytes)\n", item->d_name, (long long) item_stat.st_size);
                                }
                        }
                        closedir(dir_stream);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        // cleanup();
                        continue;
                }

                // Pipeline commands (regular commands)
                if (cmd_indx >= 0) {
                        int child_pid;
                        int children_pid[cmd_indx];
                        int children_exit[cmd_indx];
                        int fd[2];
                        int prev_read_pipe = STDIN_FILENO;
                        for (int i = 0; i <= cmd_indx; i++) {
                                pipe(fd);
                                child_pid = fork();
                                if (child_pid == 0) { // Child process
                                        dup2(prev_read_pipe, STDIN_FILENO);
                                        close(prev_read_pipe);
                                        if (i != cmd_indx) { // if not last command
                                                dup2(fd[1], STDOUT_FILENO);
                                                if (c.error_to_pipe[i]) dup2(fd[1], STDERR_FILENO);
                                        } else if (c.has_redirection) {
                                                int outfile_fd = open(c.output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                                dup2(outfile_fd, STDOUT_FILENO);
                                                if (c.error_to_file) dup2(outfile_fd, STDERR_FILENO);
                                                close(outfile_fd);
                                        }
                                        close(fd[1]);
                                        execvp(c.cmd[i].args[0], c.cmd[i].args);
                                        fprintf(stderr, "Error: command not found\n");
                                        exit(1);
                                } else if (child_pid > 0) { // Parent process
                                        if (prev_read_pipe != STDIN_FILENO) { // if not first command
                                                close(prev_read_pipe);
                                        }
                                        close(fd[1]);
                                        prev_read_pipe = fd[0];
                                        children_pid[i] = child_pid;
                                } else {
                                        perror("fork");
                                        exit(1);   
                                }
                        }
                        close(fd[0]);
                        for (int i = 0; i <= cmd_indx; i++) {
                                int status;

                                child_pid = children_pid[i];
                                waitpid(child_pid, &status, 0);
                                children_exit[i] = WEXITSTATUS(status);
                        }
                        fprintf(stderr, "+ completed '%s' ", cmdline);
                        for (int i = 0; i <= cmd_indx; i++) {
                                fprintf(stderr, "[%d]", children_exit[i]);
                                cleanup(c.cmd[i]);
                        }
                        fprintf(stderr, "\n");
                        // continue;
                }
                if (c.output_file) free(c.output_file);
        }
        // cleanup();
        return EXIT_SUCCESS;
}
