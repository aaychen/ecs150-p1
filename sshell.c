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
        char *outfile;
        bool error_to_pipe[NUM_PIPES_MAX];
        bool error_to_file;
} cmdline;

/** Reset struct members to avoid double free when deallocating memory
 * @param cmdline struct cmdline with initialized members
 */
void reset(cmdline *cmdline) {
        for (int i = 0; i < NUM_CMDS_MAX; i++) {
                cmdline->cmd[i].num_args = 0;
        }
        cmdline->num_cmds = 0;
        cmdline->has_redirection = false;
        cmdline->outfile = NULL;
        for (int i = 0; i < NUM_PIPES_MAX; i++) {
                cmdline->error_to_pipe[i] = false;
        }
        cmdline->error_to_file = false;
}

/** De-allocate memory to avoid memory leaks 
 *  @param cmdline struct cmdline with dynamic memory used
 */
void cleanup(cmdline *cmdline) {
        for (int i = 0; i < cmdline->num_cmds; i++) {
                for (int j = 0; j < cmdline->cmd[i].num_args; j++) {
                        if (cmdline->cmd[i].args[j]) {
                                free(cmdline->cmd[i].args[j]);
                                cmdline->cmd[i].args[j] = NULL;
                        }
                }
        }

        if (cmdline->outfile) {
                free(cmdline->outfile);
                cmdline->outfile = NULL;
        }
}

/** Handle failure of library functions
 *  @param
 */
void fail(char *func) {
        perror(func);
        exit(1); 
}

/** Display parsing error message on stderr
 *  @param
 */
void parsing_error_message(bool *parsing_error, char *error) {
        *parsing_error = true;
        fprintf(stderr, "Error: %s\n", error);
}

/** Handle cannot open output file error. If error occurs, this function will print the error message.
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

/* Builtin command */
// exit()
void sshell_exit(char *cmdline, int retval) {
        fprintf(stderr, "Bye...\n");
        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
}
// pwd()
void sshell_pwd(char *cmdline, int retval) {
        char *dir_path = getcwd(NULL, 0);
        fprintf(stdout, "%s\n", dir_path);
        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
        free(dir_path);
}
// cd()
void sshell_cd(char *input, cmdline c, int cmd_indx, int retval) {
        if (chdir(c.cmd[cmd_indx].args[1]) == -1) {
                fprintf(stderr, "Error: cannot cd into directory\n");
                retval = 1;
        }
        fprintf(stderr, "+ completed '%s' [%d]\n", input, retval);
}
// sls()
void sls(char *cmdline, int retval) {
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
}

/** Display completion message on stderr with return values of completed commands
 *  @param
 */
void completion_message(char *cmdline, int cmd_indx, int *children_pid, int *children_exit) {
        int child_pid;
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

                /* Parse command line (will throw errors when encountered incorrect commandline) */
                if (strlen(cmdline) == 0) continue; // empty command line
                reset(&c);
                int args_indx = -1;
                int cmd_indx = -1;
                int num_pipe = -1;
                char prev_char = ' ';
                bool parsing_error = false;
                for (size_t i = 0; i < strlen(cmdline); i++) { // iterate through each character of commandline
                        char ch = cmdline[i];
                        if (ch == '>') {
                                c.has_redirection = true;
                        } else if (prev_char == '>' && ch == '&') {
                                c.error_to_file = true;
                        } else if (prev_char == '|' && ch == '&') {
                                c.error_to_pipe[num_pipe] = true;
                        } else if (ch == '|') {
                                if (c.has_redirection) { // check redirection location
                                        if (c.outfile == NULL) { // check if output file given first
                                                fprintf(stderr, "Error: no output file\n");
                                                break;
                                        }
                                        parsing_error_message(&parsing_error, "mislocated output redirection");
                                        break;
                                }

                                num_pipe++;
                                if (num_pipe > cmd_indx) {
                                        parsing_error_message(&parsing_error, "missing command");
                                        break;
                                }
                        } else if (num_pipe == cmd_indx && cmd_indx != -1 && !isspace(ch)) { // pipe sign was read in -> new command
                                c.cmd[cmd_indx].args[++args_indx] = NULL; // append NULL to argument list of previous command
                                
                                // New command
                                args_indx = -1;
                                c.num_cmds++;
                                c.cmd[++cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                if (!c.cmd[cmd_indx].args[args_indx]) fail("calloc");
                                c.cmd[cmd_indx].num_args++;
                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        } else if (c.has_redirection) {
                                if (!isspace(ch)) { // redirection symbol was read in -> rest of command line refers output file
                                        if (cmd_indx == -1) {
                                                parsing_error_message(&parsing_error, "missing command");
                                                break;
                                        }

                                        if (prev_char == '>' || isspace(prev_char)) {
                                                c.outfile = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                                if (!c.outfile) fail("calloc");
                                        }
                                        strncat(c.outfile, &ch, 1);
                                } else if (c.outfile != NULL && has_access_error(c.outfile)){ // check output file permissions
                                        parsing_error = true;
                                        break;
                                }
                        }
                        else if (!isspace(ch)) { // tokens are either first command or arguments
                                if (cmd_indx == -1) { // first command
                                        c.num_cmds++;
                                        cmd_indx++;
                                }

                                if (isspace(prev_char)) {
                                        c.cmd[cmd_indx].args[++args_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                                        if (!c.cmd[cmd_indx].args[args_indx]) fail("calloc");
                                        c.cmd[cmd_indx].num_args++;
                                } 

                                if (c.cmd[cmd_indx].num_args > ARG_MAX) { // check number of arguments
                                        parsing_error_message(&parsing_error, "too many process arguments");
                                        break;
                                }

                                strncat(c.cmd[cmd_indx].args[args_indx], &ch, 1);
                        }
                        prev_char = ch;                     
                }
                c.cmd[cmd_indx].args[++args_indx] = NULL; // append NULL to argument list of last command
                // Handle parsing error
                if (!parsing_error && num_pipe == cmd_indx) {
                        parsing_error_message(&parsing_error, "missing command");
                }
                if (!parsing_error && c.has_redirection) { // check output file
                        if (c.outfile == NULL) { // if no output file given
                                parsing_error_message(&parsing_error, "no output file");
                        } else if (has_access_error(c.outfile)){ // check output file permissions
                                parsing_error = true;
                        }
                }
                if (parsing_error) {
                        cleanup(&c);
                        continue;
                }
                
                /* Builtin command */
                if (!strcmp(c.cmd[cmd_indx].args[0], "exit")) {
                        sshell_exit(cmdline, retval);
                        cleanup(&c);
                        break;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "pwd")) {
                        sshell_pwd(cmdline, retval);
                        cleanup(&c);
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "cd")) {
                        sshell_cd(cmdline, c, cmd_indx, retval);
                        cleanup(&c);
                        continue;
                } else if (!strcmp(c.cmd[cmd_indx].args[0], "sls")) {
                        sls(cmdline, retval);
                        cleanup(&c);
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
                                if (child_pid < 0) {
                                        fail("fork");
                                }
                                
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
                                } else { // parent process
                                        if (prev_read_pipe != STDIN_FILENO) { // if not first command
                                                close(prev_read_pipe);
                                        }
                                        close(fd[1]);
                                        prev_read_pipe = fd[0];
                                        children_pid[i] = child_pid;
                                }
                        }
                        close(fd[0]);
                        completion_message(cmdline, cmd_indx, children_pid, children_exit);
                }
                cleanup(&c);
        }
        return EXIT_SUCCESS;
}
