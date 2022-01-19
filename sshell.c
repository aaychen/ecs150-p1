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
#include <errno.h>

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
        cmd cmd[NUM_CMDS_MAX];
        int num_cmds;
        bool has_redirection;
        bool error_to_file;
        bool error_to_pipe[NUM_PIPES_MAX];
        char *outfile;
} cmdline;

/** De-allocate memory to avoid memory leaks 
 *  @param cmdline struct cmdline with dynamic memory used
 */
void cleanup(cmdline cmdline) {
        for (int i = 0; i < cmdline.num_cmds; i++) {
                for (int j = 0; j < cmdline.cmd[i].num_args; j++) {
                        if (cmdline.cmd[i].args[j]) {
                                free(cmdline.cmd[i].args[j]);
                                cmdline.cmd[i].args[j] = NULL;
                        }
                }
        }
        if (cmdline.outfile) {
                free(cmdline.outfile);
                cmdline.outfile = NULL;
        }
        return;
}

/** Handles cannot open output file error. If error occurs, this function will print the error message.
 *  @param fname the name of the file to check access/permissions for
 *  @return 0 if no error, 1 if error occurs
 */
int has_access_error(const char *fname) {
        int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1 && errno == EACCES) { // if no permission to open file
                fprintf(stderr, "Error: cannot open output file\n");
                return 1;
        }
        close(fd);
        return 0;
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
                if (strlen(cmdline) == 0) continue; // handle empty command line
                // Reset struct members to avoid double free when deallocating memory
                for (int i = 0; i < NUM_CMDS_MAX; i++) {
                        c.cmd[i].num_args = 0;
                }
                for (int i = 0; i < NUM_PIPES_MAX; i++) {
                        c.error_to_pipe[i] = false;
                }
                c.num_cmds = 0;
                c.has_redirection = false;
                c.error_to_file = false;
                c.outfile = NULL;

                int args_indx = -1;
                int cmd_indx = -1;
                int num_pipe = -1;
                char prev_char = ' ';
                bool parse_error = false;
                for (size_t i = 0; i < strlen(cmdline); i++) {
                        char ch = cmdline[i];
                        if (ch == '>') {
                                c.has_redirection = true;
                        } else if (prev_char == '>' && ch == '&') {
                                c.error_to_file = true;
                        } else if (prev_char == '|' && ch == '&') {
                                c.error_to_pipe[num_pipe] = true;
                        } else if (ch == '|') {
                                if (c.has_redirection) { // check redirection location
                                        parse_error = true;
                                        if (c.outfile == NULL) { // check if output file given first
                                                fprintf(stderr, "Error: no output file\n");
                                                break;
                                        }
                                        fprintf(stderr, "Error: mislocated output redirection\n");
                                        break;
                                }
                                num_pipe++;
                                if (num_pipe > cmd_indx) {
                                        parse_error = true;
                                        fprintf(stderr, "Error: missing command\n");
                                        break;
                                }
                        } else if (num_pipe == cmd_indx && cmd_indx != -1 && !isspace(ch)) { // pipe sign was read in -> new command
                                c.cmd[cmd_indx].args[++args_indx] = NULL; // append NULL to argument list of previous command
                                // New command
                                args_indx = -1;
                                c.num_cmds++;
                                c.cmd[++cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                c.cmd[cmd_indx].num_args++;
                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        } else if (c.has_redirection) {
                                if (!isspace(ch)) { // redirection symbol was read in -> rest of command line refers output file
                                        if (cmd_indx == -1) {
                                                parse_error = true;
                                                fprintf(stderr, "Error: missing command\n");
                                                break;
                                        }
                                        if (prev_char == '>' || isspace(prev_char)) {
                                                c.outfile = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        }
                                        strncat(c.outfile, &ch, 1);
                                } else if (c.outfile != NULL && has_access_error(c.outfile)){ // check output file permissions
                                        parse_error = true;
                                        break;
                                }
                        }
                        else if (!isspace(ch)) { // tokens are either first command or arguments
                                if (cmd_indx == -1) {
                                        c.num_cmds++;
                                        cmd_indx++;
                                }
                                if (isspace(prev_char)) {
                                        c.cmd[cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        c.cmd[cmd_indx].num_args++;
                                } 
                                if (c.cmd[cmd_indx].num_args > ARG_MAX) { // check number of arguments
                                        parse_error = true;
                                        fprintf(stderr, "Error: too many process arguments\n");
                                        break;
                                }
                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        }
                        prev_char = ch;                     
                }
                if (!parse_error && num_pipe == cmd_indx) {
                        parse_error = true;
                        fprintf(stderr, "Error: missing command\n");
                }
                if (!parse_error && c.has_redirection) { // check output file
                        if (c.outfile == NULL) { // if no output file given
                                parse_error = true;
                                fprintf(stderr, "Error: no output file\n");
                        } else if (has_access_error(c.outfile)){ // check output file permissions
                                parse_error = true;
                        }
                }
                if (parse_error) {
                        cleanup(c);
                        continue;
                }
                c.cmd[cmd_indx].args[++args_indx] = NULL; // append NULL to argument list of last command
                

                /* Builtin command */
                if (!strcmp(c.cmd[cmd_indx].args[0], "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        cleanup(c);
                        break;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "pwd")) {
                        char *dir_path = getcwd(NULL, 0);
                        fprintf(stdout, "%s\n", dir_path);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        free(dir_path);
                        cleanup(c);
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "cd")) {
                        if (chdir(c.cmd[cmd_indx].args[1]) == -1) {
                                fprintf(stderr, "Error: cannot cd into directory\n");
                                retval = 1;
                        }
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        cleanup(c);
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "sls")) {
                        DIR *dir_stream = opendir(".");
                        if (dir_stream == NULL) {
                                fprintf(stderr, "Error: cannot open directory\n");
                                retval = 1;
                        } else {
                                struct dirent *item;
                                struct stat item_stat;
                                while ((item = readdir(dir_stream)) != NULL) {
                                        if (item->d_name && item->d_name[0] != '.') { // ignore hidden file entries
                                                stat(item->d_name, &item_stat);
                                                fprintf(stdout, "%s (%lld bytes)\n", item->d_name, (long long) item_stat.st_size);
                                        }
                                }
                                closedir(dir_stream);
                        }
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
                        cleanup(c);
                        continue;
                }

                /* Pipeline commands (regular commands) */
                if (cmd_indx >= 0) {
                        int child_pid;
                        int children_pid[cmd_indx];
                        int children_exit[cmd_indx];
                        int fd[2];
                        int prev_read_pipe = STDIN_FILENO;
                        for (int i = 0; i <= cmd_indx; i++) {
                                pipe(fd);
                                child_pid = fork();
                                if (child_pid == 0) { // child process
                                        if (prev_read_pipe != STDIN_FILENO) { // if not first command
                                                dup2(prev_read_pipe, STDIN_FILENO);
                                                close(prev_read_pipe);
                                        }
                                        if (i != cmd_indx) { // if not last command
                                                dup2(fd[1], STDOUT_FILENO);
                                                if (c.error_to_pipe[i]) dup2(fd[1], STDERR_FILENO);
                                        } else if (c.has_redirection) { // if have redirection to file
                                                        int outfile_fd = open(c.outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                                        dup2(outfile_fd, STDOUT_FILENO);
                                                        if (c.error_to_file) dup2(outfile_fd, STDERR_FILENO);
                                                        close(outfile_fd);
                                        }
                                        close(fd[0]);
                                        close(fd[1]);
                                        execvp(c.cmd[i].args[0], c.cmd[i].args);
                                        fprintf(stderr, "Error: command not found\n");
                                        exit(1);
                                } else if (child_pid > 0) { // parent process
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
                        }
                        fprintf(stderr, "\n");
                }
                cleanup(c);
        }
        return EXIT_SUCCESS;
}
