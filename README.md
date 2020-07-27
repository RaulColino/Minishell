# Minishell

Unix command-line interpreter implemented in C

## How it works

When the input is executed, it is checked whether we have any commands after parsing the line. If there are none, the first iteration of the main program loop ends. If there is one, it checks if it's an internal command of the Shell. Note: An internal command _exit_ has been implemented to facilitate the exit of the program.

If the input is not an internal command, a child process is created and it is checked if several commands or only one have been entered.  If only one has been entered, it is checked if the command exists and if so, it is executed using execv(). 

When several commands need to be executed, an array is created with the appropriate pipes and, through a loop, a process is created that redirects its output to the pipe. For each cycle the parent thread waits and redirects the normal data to the reading end of the the corresponding pipe. Before executing each command, file redirections are made if necessary, for example, an intermediate command from among a sequence of commands connected with pipes the file redirections. Before executing each command, file redirects have also been done if necessary, for example, an intermediate command from a sequence of commands connected with pipes could not make the file redirections.

The structure of the program is made in this way so that a separate process is started for processing the line that performs the redirects and you do not have to worry about resetting them since the modification on the file descriptor table is on a copy of it that is deleted at the end of the process.

I tried to implement the functionality to run processes in the background. When a line is executed in the background, the PID and the line executed are displayed. The background processes are saved in a specific array for them. In this way, Jobs can be called to consult the array and show the processes. However, I have had problems implementing the _fg_ command since the processes of the commands executed in the background with _execv()_ have been configured to ignore the SIGINT and SIGQUIT signals. 
