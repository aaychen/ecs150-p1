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

### Parsing of the command line

`sshell` receives user input from the terminal and parses it to create a command-line data structure called `struct cmdline` at the start of its execution. This command-line data structure is a single object that holds a variety of information, such as the number of commands entered, whether output redirection is set, the output file's name, and if standard error is forwarded to either a file or another command in a pipeline. Finally, the command-line data structure also includes a data structure called `struct cmd` to represent individual commands. This data structure holds information on each command's arguments and how many there are. Having two data structures allows us to split down the information-dense commandline into manageable parts.

The program iterates through each character of the command line, looking for the information needed to launch it and addressing any parsing problems it encounters.

### Launching of the command line

After the command-line structure has been created, `sshell` executes the parsed commands. The commands are categorized into two types: built-in commands and standard commands. Only standard commands can be used as part of a pipeline and with output redirection.

Through a series of if-else statements, the program first determines whether the command is one of the four built-in commands (`exit()`, `pwd()`, `cd()`, and `sls()`).

If the command is not built in, `sshell` will execute it using `exec()`. If the program is given a series of commands in a pipeline, it runs the commands concurrently with a for loop; for each command, it forks a child process to execute the command. Before prompting for the next command line, the parent process (the shell) waits for all of the child processes to finish. If output redirection is specified, standard output will be redirected to the output file. Standard error will be forwarded to the output file or piped into the next command together with standard output if specified in the command line.