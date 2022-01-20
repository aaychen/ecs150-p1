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
        int num_pipes;
        bool error_to_pipe[NUM_PIPES_MAX];
        bool error_to_file;
} cmdline;

/* Reset struct members to avoid double free when deallocating memory */
void reset(cmdline *cmdline) {
        for (int i = 0; i < NUM_CMDS_MAX; i++) {
                cmdline->cmd[i].num_args = 0;
        }
        cmdline->num_cmds = 0;
        cmdline->has_redirection = false;
        cmdline->outfile = NULL;
        cmdline->num_pipes = 0;
        for (int i = 0; i < NUM_PIPES_MAX; i++) {
                cmdline->error_to_pipe[i] = false;
        }
        cmdline->error_to_file = false;
}

/* De-allocate memory to avoid memory leaks */
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

/* Handle failure of library functions */
void fail(char *func) {
        perror(func);
        exit(1); 
}

/* Display error message on stderr */
void error_message(char *error) {
        fprintf(stderr, "Error: %s\n", error);
}

/** 
 * Handle cannot open output file error. If error occurs, this function will print the error message.
 * @return 0 if no error, 1 if error occurs
 */
int has_access_error(const char *fname) {
        int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1 && errno == EACCES) { // if no permission to open file
                error_message("cannot open output file");
                return 1;
        }
        close(fd);
        return 0;
}

/* A new command after a pipe sign was detected */
void new_cmd(cmdline *cmdline, char ch) {
        int cmd_indx = cmdline->num_cmds;

        cmdline->cmd[cmd_indx].args[0] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
        cmdline->num_cmds++;
        if (!cmdline->cmd[cmd_indx].args[0]) fail("calloc");
        strncat(cmdline->cmd[cmd_indx].args[0], &ch, 1);
        cmdline->cmd[cmd_indx].num_args++;
}

/* Read in an output file's name */
void read_outfile(cmdline *cmdline, char prev_char, char ch) {
        if (prev_char == '>' || isspace(prev_char)) {
                cmdline->outfile = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                if (!cmdline->outfile) fail("calloc");
        }
        strncat(cmdline->outfile, &ch, 1);
}

/**
 * Read in commands or arguments
 * @return 0 if no parsing error for too many arguments, 1 otherwise
 */
int read_cmd_args(cmdline *cmdline, char prev_char, char ch) {
        int cmd_indx = cmdline->num_cmds - 1;
        if (cmdline->num_cmds == 0) { // first command
                cmdline->num_cmds++;
                cmd_indx++;
        } 
        int arg_indx = cmdline->cmd[cmd_indx].num_args - 1;
        
        if (isspace(prev_char)) {
                cmdline->cmd[cmd_indx].args[++arg_indx] = calloc(TOKEN_LEN_MAX + 1, sizeof(char));
                cmdline->cmd[cmd_indx].num_args++;
                if (!cmdline->cmd[cmd_indx].args[arg_indx]) fail("calloc");
        } 

        if (cmdline->cmd[cmd_indx].num_args > ARG_MAX) { // check number of arguments
                error_message("too many process arguments");
                return 1;
        }

        strncat(cmdline->cmd[cmd_indx].args[arg_indx], &ch, 1);
        return 0;
}

/**
 * Parse the given command line 
 * @return 0 if no parsing error occurs, 1 otherwise
 */
int parsing_cmdline(cmdline *c, char *cmdline) {
        char prev_char = ' ';
        for (size_t i = 0; i < strlen(cmdline); i++) { // iterate through each character of command line
                char ch = cmdline[i];
                if (ch == '>') {
                        c->has_redirection = true;
                } else if (prev_char == '>' && ch == '&') {
                        c->error_to_file = true;
                } else if (prev_char == '|' && ch == '&') {
                        c->error_to_pipe[c->num_pipes - 1] = true;
                } else if (ch == '|') {
                        if (c->has_redirection) { // check redirection location
                                if (c->outfile == NULL) { // check if output file given first
                                        error_message("no output file");
                                        return 1;
                                }
                                error_message("mislocated output redirection");
                                return 1;
                        }

                        c->num_pipes++;
                        if (c->num_pipes > c->num_cmds) {
                                error_message("missing command");
                                return 1;
                        }
                } else if (c->num_pipes == c->num_cmds && c->num_cmds != 0 && !isspace(ch)) { // pipe sign was read in -> new command
                        c->cmd[c->num_cmds - 1].args[c->cmd[c->num_cmds - 1].num_args++] = NULL; // append NULL to argument list of previous command
                        
                        new_cmd(c, ch); // new command
                } else if (c->has_redirection) {
                        if (!isspace(ch)) { // redirection symbol was read in -> rest of command line refers to output file
                                if (c->num_cmds == 0) {
                                        error_message("missing command");
                                        return 1;
                                }

                                read_outfile(c, prev_char, ch);
                        } else if (c->outfile != NULL && has_access_error(c->outfile)){ // check output file permissions
                                return 1;
                        }
                } else if (!isspace(ch)) { // tokens are either first command or arguments
                        if (read_cmd_args(c, prev_char, ch) == 1) return 1;
                }
                prev_char = ch;                     
        }
        c->cmd[c->num_cmds - 1].args[c->cmd[c->num_cmds - 1].num_args++] = NULL; // append NULL to argument list of last command
        return 0;
}

/* Run exit command (built in) */
void sshell_exit(char *cmdline, int retval) {
        fprintf(stderr, "Bye...\n");
        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
}

/* Run pwd command (built in) */
void sshell_pwd(char *cmdline, int retval) {
        char *dir_path = getcwd(NULL, 0);
        fprintf(stdout, "%s\n", dir_path);
        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
        free(dir_path);
}

/* Run cd command (built in) */
void sshell_cd(cmdline c, char *cmdline, int cmd_indx, int retval) {
        if (chdir(c.cmd[cmd_indx].args[1]) == -1) {
                error_message("cannot cd into directory");
                retval = 1;
        }
        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, retval);
}

/* Run sls command (built in) */
void sls(char *cmdline, int retval) {
        DIR *dir_stream = opendir(".");
        if (dir_stream == NULL) {
                error_message("cannot open directory");
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

/* Display completion message on stderr with return values of completed standard commands */
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

/* Execute either a singular standard command or multiple commands in a pipeline */
void pipeline(cmdline *c, char *cmdline) {
        int child_pid;
        int children_pid[c->num_cmds - 1];
        int children_exit[c->num_cmds - 1];
        int fd[2];
        int prev_read_pipe = STDIN_FILENO;
        for (int i = 0; i < c->num_cmds; i++) {
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
                        if (i != c->num_cmds - 1) { // if not last command
                                dup2(fd[1], STDOUT_FILENO);
                                if (c->error_to_pipe[i]) dup2(fd[1], STDERR_FILENO);
                        } else if (c->has_redirection) { // if have redirection to file
                                int outfile_fd = open(c->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                dup2(outfile_fd, STDOUT_FILENO);
                                if (c->error_to_file) dup2(outfile_fd, STDERR_FILENO);
                                close(outfile_fd);
                        }
                        close(fd[0]);
                        close(fd[1]);
                        execvp(c->cmd[i].args[0], c->cmd[i].args);
                        error_message("command not found");
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
        completion_message(cmdline, c->num_cmds - 1, children_pid, children_exit);
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
                if (nl) *nl = '\0';

                /* Parse command line (will throw errors when encountered incorrect command line) */
                if (strlen(cmdline) == 0) continue; // empty command line
                reset(&c);
                bool parsing_error = parsing_cmdline(&c, cmdline);
                // Handle parsing errors that were not detected when parsing the command line
                if (!parsing_error && c.num_pipes == c.num_cmds) {
                        error_message("missing command");
                        parsing_error = true;
                }
                if (!parsing_error && c.has_redirection) { // check output file
                        if (c.outfile == NULL) { // if no output file given
                                error_message("no output file");
                                parsing_error = true;
                        } else if (has_access_error(c.outfile)){ // check output file permissions
                                parsing_error = true;
                        }
                }
                if (parsing_error) {
                        cleanup(&c);
                        continue;
                }

                /* Builtin command */
                if (!strcmp(c.cmd[c.num_cmds - 1].args[0], "exit")) {
                        sshell_exit(cmdline, retval);
                        cleanup(&c);
                        break;
                } else if (!strcmp(c.cmd[c.num_cmds - 1].args[0], "pwd")) {
                        sshell_pwd(cmdline, retval);
                        cleanup(&c);
                        continue;
                } else if (!strcmp(c.cmd[c.num_cmds - 1].args[0], "cd")) {
                        sshell_cd(c, cmdline, c.num_cmds - 1, retval);
                        cleanup(&c);
                        continue;
                } else if (!strcmp(c.cmd[c.num_cmds - 1].args[0], "sls")) {
                        sls(cmdline, retval);
                        cleanup(&c);
                        continue;
                }

                /* Pipeline commands (regular commands) */
                if (c.num_cmds >= 1) {
                        pipeline(&c, cmdline);
                }
                cleanup(&c);
        }
        return EXIT_SUCCESS;
}