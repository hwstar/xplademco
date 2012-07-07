/*
*    xplademco - an AD2USB to xPL bridge
*    Copyright (C) 2012  Stephen A. Rodgers
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*   
*
*
*/

#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <xPL.h>
#include "serio.h"
#include "notify.h"
#include "confread.h"

#define SHORT_OPTIONS "c:d:f:hi:l:np:s:v"


#define WS_SIZE 256
#define DEF_INSTANCE_ID		"ademco"
#define DEF_COM_PORT		"/dev/tty-ademco"
#define DEF_PID_FILE		"/var/run/xplademco.pid"
#define DEF_CFG_FILE		"/etc/xplademco.conf"

/* Config override flags */
enum { CO_PID_FILE = 1, CO_COM_PORT = 2, CO_INSTANCE_ID= 4, CO_INTERFACE = 8, CO_LOG_FILE = 0x10 };

char *progName;
int debugLvl = 0; 
Bool noBackground = FALSE;
uint32_t pollRate = 5;
uint32_t configOverride = 0;

static Bool lineReceived = FALSE;
static seriostuff_t *serioStuff = NULL;
static xPL_ServicePtr xplrcsService = NULL;
static xPL_MessagePtr xplrcsStatusMessage = NULL;
static xPL_MessagePtr xplrcsTriggerMessage = NULL;
static ConfigEntry_t *configEntry;

static char comPort[WS_SIZE] = DEF_COM_PORT;
static char interface[WS_SIZE] = "";
static char logPath[WS_SIZE] = "";
static char instanceID[WS_SIZE] = DEF_INSTANCE_ID;
static char pidFile[WS_SIZE] = DEF_PID_FILE;
static char configFile[WS_SIZE] = DEF_CFG_FILE;


/* Commandline options. */

static struct option longOptions[] = {
  {"com-port", 1, 0, 'p'},
  {"config",1, 0, 'c'},
  {"debug", 1, 0, 'd'},
  {"help", 0, 0, 'h'},
  {"instance", 1, 0, 's'},
  {"interface", 1, 0, 'i'},
  {"log", 1, 0, 'l'},
  {"no-background", 0, 0, 'n'},
  {"pid-file", 0, 0, 'f'},
  {"version", 0, 0, 'v'},
  {0, 0, 0, 0}
};

/* 
 * Get the pid from a pidfile.  Returns the pid or -1 if it couldn't get the
 * pid (either not there, stale, or not accesible).
 */

static pid_t pid_read(char *filename) {
	FILE *file;
	pid_t pid;
	
	/* Get the pid from the file. */
	file=fopen(filename, "r");
	if(!file) {
		return(-1);
	}
	if(fscanf(file, "%d", &pid) != 1) {
		fclose(file);
		return(-1);
	}
	if(fclose(file) != 0) {
		return(-1);
	}
	
	/* Check that a process is running on this pid. */
	if(kill(pid, 0) != 0) {
		
		/* It might just be bad permissions, check to be sure. */
		if(errno == ESRCH) {
			return(-1);
		}
	}
	
	/* Return this pid. */
	return(pid);
}


/* 
 * Write the pid into a pid file.  Returns zero if it worked, non-zero
 * otherwise.
 */
static int pid_write(char *filename, pid_t pid) {
	FILE *file;
	
	/* Create the file. */
	file=fopen(filename, "w");
	if(!file) {
		return -1;
	}
	
	/* Write the pid into the file. */
	(void) fprintf(file, "%d\n", pid);
	if(ferror(file) != 0) {
		(void) fclose(file);
		return -1;
	}
	
	/* Close the file. */
	if(fclose(file) != 0) {
		return -1;
	}
	
	/* We finished ok. */
	return 0;
}


/*
* When the user hits ^C, logically shutdown
* (including telling the network the service is ending)
*/

static void shutdownHandler(int onSignal)
{
	xPL_setServiceEnabled(xplrcsService, FALSE);
	xPL_releaseService(xplrcsService);
	xPL_shutdown();
	unlink(pidFile);
	exit(0);
}


/*
* Our Listener 
*/



static void xPLListener(xPL_MessagePtr theMessage, xPL_ObjectPtr userValue)
{

	String ws;


	if(!xPL_isBroadcastMessage(theMessage)){ /* If not a broadcast message */
		if(xPL_MESSAGE_COMMAND == xPL_getMessageType(theMessage)){ /* If the message is a command */
			const String const type = xPL_getSchemaType(theMessage);
			const String const class = xPL_getSchemaClass(theMessage);
			
			debug(DEBUG_EXPECTED,"Non-broadcast message received: type=%s, class=%s", type, class);
			
			/* Allocate a working string */

			if(!(ws = malloc(WS_SIZE)))
				fatal("Cannot allocate work string in xPLListener");
			ws[0] = 0;

			free(ws);
		}

	}
}



/*
* Serial I/O handler (Callback from xPL)
*/

static void serioHandler(int fd, int revents, int userValue)
{
	static Bool firstTime = TRUE;
	String line;
	char newStatBits[21];
	static char oldStatBits[21];

	
	
	/* Do non-blocking line read */
	if(serio_nb_line_readcr(serioStuff)){
		lineReceived=TRUE;
		/* Got a line */
		line = serio_line(serioStuff);
		if(line[0] == '['){ /* Parse the status bits */
			strncpy(newStatBits, line+1, 20);
			newStatBits[20] = 0;
			if(firstTime){ /* Set new and old the same on first time */
				firstTime = FALSE;
				strcpy(oldStatBits, newStatBits);
			}
			if(strcmp(newStatBits, oldStatBits)){
				strcpy(oldStatBits, newStatBits);
				debug(DEBUG_EXPECTED,"New Status bits: %s", newStatBits);
			}
		}
		else if(line[0] == '!'){ /* Other events */
			if(!strncmp(line + 1, "EXP", 3)){ /* Expander event ? */
				debug(DEBUG_EXPECTED,"Expander event: %s",line + 5);
			}
		}

	} /* End serio_nb_line_read */
}


/*
* Our tick handler. 
* 
*/

static void tickHandler(int userVal, xPL_ObjectPtr obj)
{
	static short pollCtr = 0;


	pollCtr++;

	debug(DEBUG_ACTION, "TICK: %d", pollCtr);
	/* Process clock tick update checking */
}


/*
* Show help
*/

void showHelp(void)
{
	printf("'%s' is a daemon that bridges the xPL protocol to an ademco panel using an ad2usb adapter\n", progName);
	printf("via a USB or RS-232 interface\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", progName);
	printf("\n");
	printf("  -c, --config-file PATH  Set the path to the config file\n");
	printf("  -d, --debug LEVEL       Set the debug level, 0 is off, the\n");
	printf("                          compiled-in default is %d and the max\n", debugLvl);
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -f, --pid-file PATH     Set new pid file path, default is: %s\n", pidFile);
	printf("  -h, --help              Shows this\n");
	printf("  -i, --interface NAME    Set the broadcast interface (e.g. eth0)\n");
	printf("  -l, --log  PATH         Path name to debug log file when daemonized\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -p, --com-port PORT     Set the communications port (default is %s)\n", comPort);
	printf("  -s, --instance ID       Set instance id. Default is %s", instanceID);
	printf("  -v, --version           Display program version\n");
	printf("\n");
 	printf("Report bugs to <%s>\n\n", EMAIL);
	return;

}


/*
* main
*/


int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	String p;

	/* Set the program name */
	progName=argv[0];

	/* Parse the arguments. */
	while((optchar=getopt_long(argc, argv, SHORT_OPTIONS, longOptions, &longindex)) != EOF) {
		
		/* Handle each argument. */
		switch(optchar) {
			
			/* Was it a long option? */
			case 0:
				
				/* Hrmm, something we don't know about? */
				fatal("Unhandled long getopt option '%s'", longOptions[longindex].name);
			
			/* If it was an error, exit right here. */
			case '?':
				exit(1);

			/* Was it a config file path ? */
			case 'c':
				strncpy(configFile, optarg, WS_SIZE - 1);
				configFile[WS_SIZE - 1] = 0;
				debug(DEBUG_ACTION,"New config file path is: %s", configFile);
				break;
		

		
			/* Was it a debug level set? */
			case 'd':

				/* Save the value. */
				debugLvl=atoi(optarg);
				if(debugLvl < 0 || debugLvl > DEBUG_MAX) {
					fatal("Invalid debug level");
				}

				break;

			/* Was it a pid file switch? */
			case 'f':
				strncpy(pidFile, optarg, WS_SIZE - 1);
				logPath[WS_SIZE - 1] = 0;
				debug(DEBUG_ACTION,"New pid file path is: %s", pidFile);
				configOverride |= CO_PID_FILE;
				break;

			
			/* Was it a help request? */
			case 'h':
				showHelp();
				exit(0);

			/* Specify interface to broadcast on */
			case 'i': 
				strncpy(interface, optarg, WS_SIZE -1);
				interface[WS_SIZE - 1] = 0;
				xPL_setBroadcastInterface(interface);
				configOverride |= CO_INTERFACE;

				break;

			case 'l':
				/* Override log path*/
				strncpy(logPath, optarg, WS_SIZE - 1);
				logPath[WS_SIZE - 1] = 0;
				debug(DEBUG_ACTION,"New log path is: %s", logPath);
				configOverride |= CO_LOG_FILE;
				break;

			/* Was it a no-backgrounding request? */
			case 'n':

				/* Mark that we shouldn't background. */
				noBackground = TRUE;

				break;
			case 'p':
				/* Override com port*/
				strncpy(comPort, optarg, WS_SIZE - 1);
				comPort[WS_SIZE - 1] = 0;
				debug(DEBUG_ACTION,"New com port is: %s", comPort);
				configOverride |= CO_COM_PORT;
				break;

			/* Was it an instance ID ? */
			case 's':
				strncpy(instanceID, optarg, WS_SIZE);
				instanceID[WS_SIZE -1] = 0;
				debug(DEBUG_ACTION,"New instance ID is: %s", instanceID);
				configOverride |= CO_INSTANCE_ID;
				break;


			/* Was it a version request? */
			case 'v':
				printf("Version: %s\n", VERSION);
				exit(0);
	

			
			/* It was something weird.. */
			default:
				fatal("Unhandled getopt return value %d", optchar);
		}
	}

	
	/* If there were any extra arguments, we should complain. */

	if(optind < argc) {
		fatal("Extra argument on commandline, '%s'", argv[optind]);
	}

	/* Load the config file */
	if(!(configEntry =confreadScan(configFile, NULL)))
		exit(1);

	/* Parse the general section */

	/* Com port */
	if(!(configOverride & CO_COM_PORT)){
		if((p = confreadValueBySectKey(configEntry, "general", "com-port"))){
			strncpy(comPort, p, WS_SIZE);
			comPort[WS_SIZE - 1] = 0;
		}	
	}

	/* Log file */
	if(!(configOverride & CO_LOG_FILE)){
		if((p = confreadValueBySectKey(configEntry, "general", "log-file"))){
			strncpy(logPath, p, WS_SIZE);
			logPath[WS_SIZE - 1] = 0;
		}
	
	}



	/* Instance ID */
	if(!(configOverride & CO_INSTANCE_ID)){
		if((p =  confreadValueBySectKey(configEntry, "general", "instance-id"))){
			strncpy(instanceID, p, WS_SIZE);
			instanceID[WS_SIZE - 1] = 0;
		}	
	}


	/* Interface */
	if(!(configOverride & CO_INTERFACE)){
		if((p = confreadValueBySectKey(configEntry, "general", "interface"))){
			strncpy(interface, p, WS_SIZE);
			interface[WS_SIZE - 1] = 0;
		}	
	}

	printf("interface = %s\n", interface);
	exit(0);






	/* Turn on library debugging for level 5 */
	if(debugLvl >= 5)
		xPL_setDebugging(TRUE);

 	/* Make sure we are not already running (.pid file check). */
	if(pid_read(pidFile) != -1) {
		fatal("%s is already running", progName);
	}
  
	/* Fork into the background. */

	if(!noBackground) {
		int retval;
		debug(DEBUG_STATUS, "Forking into background");

    		/* 
		* If debugging is enabled, and we are daemonized, redirect the debug output to a log file if
    		* the path to the logfile is defined
		*/

		if((debugLvl) && (logPath[0]))                          
			notify_logpath(logPath);

		/* Fork and exit the parent */

		if((retval = fork())){
      			if(retval > 0)
				exit(0);  /* Exit parent */
			else
				fatal_with_reason(errno, "parent fork");
    		}



		/*
		* The child creates a new session leader
		* This divorces us from the controlling TTY
		*/

		if(setsid() == -1)
			fatal_with_reason(errno, "creating session leader with setsid");


		/*
		* Fork and exit the session leader, this prohibits
		* reattachment of a controlling TTY.
		*/

		if((retval = fork())){
			if(retval > 0)
        			exit(0); /* exit session leader */
			else
				fatal_with_reason(errno, "session leader fork");
		}

		/* 
		* Change to the root of all file systems to
		* prevent mount/unmount problems.
		*/

		if(chdir("/"))
			fatal_with_reason(errno, "chdir to /");

		/* set the desired umask bits */

		umask(022);
		
		/* Close STDIN, STDOUT, and STDERR */

		close(0);
		close(1);
		close(2);
		} 

	/* Start xPL up */
	if (!xPL_initialize(xPL_getParsedConnectionType())) {
		fatal("Unable to start xPL lib");
	}

	/* Initialze xplrcs service */

	/* Create a service and set our application version */
	xplrcsService = xPL_createService("hwstar", "xplademco", instanceID);
  	xPL_setServiceVersion(xplrcsService, VERSION);

	/*
	* Create a status message object
	*/

  	xplrcsStatusMessage = xPL_createBroadcastMessage(xplrcsService, xPL_MESSAGE_STATUS);
  
	/*
	* Create trigger message objects
	*/

	xplrcsTriggerMessage = xPL_createBroadcastMessage(xplrcsService, xPL_MESSAGE_TRIGGER);


  	/* Install signal traps for proper shutdown */
 	signal(SIGTERM, shutdownHandler);
 	signal(SIGINT, shutdownHandler);

	/* Initialize the COM port */
	
	if(!(serioStuff = serio_open(comPort, 115200)))
		fatal("Could not open com port: %s", comPort);


	/* Flush any partial commands */
	serio_printf(serioStuff, "\r");
	usleep(100000);
	serio_flush_input(serioStuff);

	/* Ask xPL to monitor our serial device */
	if(xPL_addIODevice(serioHandler, 1234, serio_fd(serioStuff), TRUE, FALSE, FALSE) == FALSE)
		fatal("Could not register serial I/O fd with xPL");

	/* Add 1 second tick service */
	xPL_addTimeoutHandler(tickHandler, 1, NULL);

  	/* And a listener for all xPL messages */
  	xPL_addMessageListener(xPLListener, NULL);


 	/* Enable the service */
  	xPL_setServiceEnabled(xplrcsService, TRUE);

	/* Update pid file */
	if(pid_write(pidFile, getpid()) != 0) {
		debug(DEBUG_UNEXPECTED, "Could not write pid file '%s'.", pidFile);
	}




 	/** Main Loop **/

	for (;;) {
		/* Let XPL run forever */
		xPL_processMessages(-1);
  	}

	exit(1);
}

