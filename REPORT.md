# SSHELL: Simple Shell

## Summary

This program, `sshell`, accepts user input in the form of command lines and executes them. The shell has the following features:

1. Execution of user-supplied commands with optional arguments
2. Selection of common builtin commands
3. Redirection of the standard output of commands to files
4. Composition of commands via piping
5. Redirection of the standard error of commands to files or pipes
6. Simple ls-like builtin command

## Implementation

The program's implementation is divided into two stages:

1. Parsing the given command line to understand what the shell needs to do
2. Executing the command(s) specified in the command line

### Representation of the command line

`sshell` receives user input from the terminal and parses it to create a command-line data structure called `struct cmdline` at the start of its execution. 

An instance of `struct cmdline` holds a variety of information:
- The individual command(s) to be run
- Number of commands entered
- If output redirection is set, and the output file's name if applicable
- If standard error is forwarded to either a file or another command in a pipeline

The individual command(s) to be run are represented using a data structure called `struct cmd`. An instance of `struct cmd` represents one command, holding information about the command's arguments and how many there are. Having two data structures allows us to split down the information-dense command line into manageable parts.

### Parsing of the command line

After the program has read in user input, function `parsing cmdline()` uses a for loop to traverse through each character of the command line. Using if-else statements, it searches for information needed to build an instance of the command-line structure and addresses any parsing problems that arise.

`parsing cmdline()` makes use of three functions: `new_cmd()`, `read_outfile()`, and `read_cmd_args()`. `new cmd()` validates the start of new commands following the first command. `read outfile()` validates the start of an output file and reads in its name to construct `struct cmdline`. Finally, `read cmd args()` reads in both the commands (after `new cmd()` detects a new command) and the arguments for `struct cmdline`.

### Launching of the command line

After the command-line structure has been created, `sshell` executes the parsed commands. The commands are categorized into two types: built-in commands and standard commands. Only standard commands can be used as part of a pipeline and with output redirection.

Through a series of if-else statements, the program first determines whether the command is one of the four built-in commands: `exit()`, `pwd()`, `cd()`, and `sls()`.

If the command is not built in, function `pipeline()` will use `exec()` to run the singular command. If a pipeline of commands is given, `pipeline()` executes the commands concurrently with a for loop; for each command, it forks a child process to execute the command. The parent process (the shell) waits for all of the child processes to finish before prompting for the next command line. If output redirection is specified, standard output will be redirected to the output file. Standard error will be forwarded to the output file or piped into the next command together with standard output if specified in the command line.