# Minishell

Unix command-line interpreter implemented in C

## How it works

Once the input is executed, it is first checked if after parsing the line we have any command.
If there are none, the first iteration of the main program loop ends. If there is one, it checks if it is an internal command of the Shell. Note: An internal command _exit_ has been implemented to facilitate the exit of the program.

If the input is not an internal command, a child process is created and checks if several commands or only one have been entered. If only one has been entered, it checks if the command exists and if it is the case, it is executed using an _execv()_.

In case of execution of several commands, an array is created with the necessary pipes and a process is created through a loop in each iteration that redirects its output to the pipe. The parent waits for each process and redirects the standard input to the read end of the corresponding pipe. Before executing each command, file redirects have also been done if necessary, for example, an intermediate command from a script connected with pipes could not do file redirects.

The structure of the program is made in this way so that an independent process is started for the processing of the line that performs the redirects. Thus you do not have to worry about re-establishing them since the modification on the table of file descriptors is on a copy of this, which is removed at the end of the process.

An attempt has been made to implement the functionality to run processes in the background. Running a line in the background shows the PID and the executed line. Background processes are saved in an array specific to them. In this way, the _jobs_ command can be executed to query the array and display the processes. However, I have had trouble implementing the _fg_ command since the command processes executed in the background with _execv()_ have been configured to ignore the SIGINT and SIGQUIT signals.
