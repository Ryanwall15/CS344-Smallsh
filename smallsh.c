#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>

bool fg_mode = false;

//Function to check for & symbol
//Will be used to check if process should run in background
bool ampersand (char* a) {
   int length = strlen(a);
   if(a[length-2] == '&') {
      return true;
   }

   return false;
}

//Reading 3.3
//https://oregonstate.instructure.com/courses/1662152/pages/3-dot-3-advanced-user-input-with-getline
void catchSIGINT(int signo) {
   char* message = "SIGINT. Use CTRL-Z to Stop.\n";
   write(STDOUT_FILENO, message, 28);
}

//Based on program description if the signal is sent
//I want to make it foreground only and set fg_mode to true
//If fg_mode is true and the signal is sent I want to turn that off
void catchSIGTSTP(int signo) {
   if (fg_mode == false) {
      fg_mode = true;
      char* message = "Entering foreground-only mode (& is now ignored)\n";
      write(STDOUT_FILENO, message, 50);
   }
   else if (fg_mode == true) {
      fg_mode = false;
      char* message = "Exiting foreground-only mode\n";
      write(STDOUT_FILENO, message, 30);
   }
}

void background_process(int* child_count) {
   int childExitMethod = -5;
   while (*child_count > 0) {
      //Check if any process has completed, return immediately with 0 if none have
      //Per 3.1 Lecture
      int childPID = waitpid(-1, &childExitMethod, WNOHANG); 

      if (childPID != 0) {

	 //If process terminates normally, WIFEXITED returns non-zero
	 //Get actual status by WEXITSTATUS(childExitMethod)
	 //Per 3.1 Lecture
	 if (WIFEXITED(childExitMethod) != 0) {
	    printf("Background PID %d is done: exit value %d\n", childPID, WEXITSTATUS(childExitMethod));
	    fflush(stdout);
	 }

	 //If process was terminated by a signal, then WIFSIGNALED returns non-zero
	 //Get actual status by WTERMSIG(childExitMethod)
	 //Per 3.1 lecture
	 else if (WIFSIGNALED(childExitMethod) != 0) {
	    printf("Background PID %d is done: terminated by singal %d\n", childPID, WTERMSIG(childExitMethod));
	    fflush(stdout);
	 }

	 //If this gets triggered, then it didn't exit normally or by signal
	 else {
	    printf("Backround PID %d is done, but didn't exit normally or by signal\n", childPID);
	    fflush(stdout);
	 }
	 (*child_count)--;
      }
      else {
	 break;
      }
   }
}


//Function for when we want to exit the shell
void exitShell() {
   kill(0, SIGTERM);
   exit(0);
}

int main() {

   //From Lecture and reading 3.3
   //Handling Control-C   
   struct sigaction SIGINT_action = {0};
   SIGINT_action.sa_handler = catchSIGINT;
   sigfillset(&SIGINT_action.sa_mask);
   SIGINT_action.sa_flags = SA_RESTART;
   sigaction(SIGINT, &SIGINT_action, NULL);

   //Handling Control-Z
   //Same format as Control-C but Control-z will throw SIGTSTP
   struct sigaction SIGTSTP_action = {0};
   SIGTSTP_action.sa_handler = catchSIGTSTP;
   sigfillset(&SIGTSTP_action.sa_mask);
   SIGTSTP_action.sa_flags = SA_RESTART;
   sigaction(SIGTSTP, &SIGTSTP_action, NULL);

   //Set up getline
   size_t bufferSize = 0;
   char* lineEntered = NULL;

   //Set up command line and arguments
   char commandLength[2048];
   char* arguments[512];

   int i;
   int exitStatus = 0;

   //Set up stdout and stdin redirection
   int stdout_redirected, stdout_index;
   int stdin_redirected, stdin_index;

   int num;
   int background;

   int num_process = 0;
   int *ptr_num_process = &num_process;

   while(1) {

      //Check background process
      background_process(ptr_num_process);

      printf(": ");
      fflush(stdout);
      getline(&lineEntered, &bufferSize, stdin);

      //Converting $$ into Process ID of the shell itself
      for(i=0; i<strlen(lineEntered); i++) {
	 if(lineEntered[i] == '$' && lineEntered[i+1] == '$') {
	    lineEntered[i] = '%';
	    lineEntered[i] = 'd';
	 }
      }

      //Store output 
      sprintf(commandLength, lineEntered, getpid());

      //Tokenize line and get commands
      //Have statements checking for each command
      /* if (strncmp(commandLength, "\n", 1) == 0) {
	 exitStatus = 0;
	 continue;
      }*/

      //Cd Command 
      //Tried tokenizing line and using strcmp but that didn't work 
      //So I did strncmp and that for some reason worked
      //I had to do that for all of these commands
      if (strncmp(commandLength, "cd", 2) == 0) {
	 char* target = strtok(NULL, " ");

	 if (target == NULL) {
	    target = getenv("HOME");
	    //chdir(target);
	 }

	 if (chdir(target) != 0) {
	    printf("Error changing directory to: %s\n", target);
	    fflush(stdout);
	    exitStatus = 1;
	 }
	 else {
	    exitStatus = 0;
	 }
      }

      //Exit command
      else if (strncmp(commandLength, "exit", 4) == 0) {
	 exitShell();
      }

      //Status command
      else if(strncmp(commandLength, "status", 6) == 0) {
	 printf("exit value %d\n", exitStatus);
	 fflush(stdout);
      }

      //Create new process (From Lectures)
      else {
	 pid_t spawnPid = -5;
	 int childExitStatus = -5;
	 int bg = 0;

	 //If the command line has an ampersand at the end and if we aren't ignoring the symbol
	 //then it can be run in the background 
	 if ((ampersand(commandLength) == true) && (fg_mode == false)) {
	    bg = 1;
	 }


	 spawnPid = fork();
	 switch (spawnPid) {
	    case -1: {
			perror("Hull Breach!\n"); exit(1); break; 
		     }

	    case 0:  {
			//Set up signals for child process
			//This is for Control-C
			//Let the hander equal to SIG_IGN because based on 
			//3.3 Lecture, this is the only way to tell processes
			//to ignore particular signals
			//https://stackoverflow.com/questions/12953350/ignore-sigint-signal-in-child-process
			SIGINT_action.sa_handler = SIG_IGN;
			sigfillset(&SIGINT_action.sa_mask);
			SIGINT_action.sa_flags = SA_RESTART;
			sigaction(SIGINT, &SIGINT_action, NULL);

			//Set up signal for Control-z
			//I don't want to ignore it so I set it up 
			//The same as above
			SIGTSTP_action.sa_handler = catchSIGTSTP;
			sigfillset(&SIGTSTP_action.sa_mask);
			SIGTSTP_action.sa_flags = SA_RESTART;
			sigaction(SIGTSTP, &SIGTSTP_action, NULL);


			//Set up tokenizer to go through command line
			char* token;
			token = strtok(commandLength, " "); //Tokenize command by spaces

			//While token isn't null
			while (token != NULL) {
			//check for <, >, &
			if (token[0] == '<' || token[0] == '>' || token[0] == '&') {

			//if < then redirect stdin
			if (token[0] == '<') {
			   stdin_redirected = 1; //set flag to 1
			   stdin_index = num;
			}

			//if > then reirect stdout
			if (token[0] == '>') {
			   stdout_redirected = 1; //set flag to 1
		  	   stdout_index = num;

			}

			}

			//else build the command array for stdin/stdout redirection
			else {
			   arguments[num] = malloc(sizeof(char));
			   strcpy(arguments[num], token);

			   for (i=0; i<sizeof(arguments[num]); i++) {
			      if (arguments[num][i] == '\n')
				 arguments[num][i] = '\0';
			   }
			   num++;

			}

			token = strtok(NULL, " ");

			}

			//From Lecture 3.4
			//Stdin redirection 
			if (stdin_redirected == 1) {
			   int sourceFD = open(arguments[stdin_index], O_RDONLY);
			   if (sourceFD == -1) { perror("Error: Can't oopen file\n"); exit(1); }
			   else {
			      dup2(sourceFD, 0);
			      free(arguments[stdin_index]);
			      num--;
			   }
			
			}

			//From lecture 3.4
			//Stdout redirection 
			if (stdout_redirected == 1) {
			   int targetFD = open(arguments[stdout_index], O_WRONLY | O_CREAT | O_TRUNC, 0644);
			   if (targetFD == -1) { perror("Error: Can't open file\n"); exit(2); }

			   else {
			      dup2(targetFD, 1); 
			      free(arguments[stdout_index]);
			      num--;
			   }
			}

			arguments[num] = NULL;

			//If statements for additional error checks
			if(strlen(arguments[0]) == 0)
			   exit(0);

			//Comment
			if (arguments[0][0] == '#')
			   exit(0);

			if (execvp(arguments[0], arguments)==-1) {
			   printf("%s: no such file or directory \n", arguments[0]);
			   fflush(stdout);
			   exit(1);
			}

			exit(0);
	
		     } //End of child

	    default: {
			if (bg == 0) {
			   //Block parent until specified child process terminates 
			   //Per 3.1 Lecture
			   waitpid(spawnPid, &childExitStatus, 0);
			   exitStatus = WEXITSTATUS(childExitStatus); //Getting actual status 

			   //If terminated by signal 
			   //Update exit status to that signal 
			   if (WIFSIGNALED(childExitStatus) != 0) {
			      printf("terminated by signal %d\n", WTERMSIG(childExitStatus));
			      fflush(stdout);
			      exitStatus = WTERMSIG(childExitStatus);
			   }
			}

			//Print background PID
			else {
			   printf("Background Pid is %d\n", spawnPid);
			   fflush(stdout);
			   num_process++;
			}
		     
		     } //End of parent
	 }
      }

   }

   return 0;
}


