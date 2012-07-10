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

/* Define these if not defined */

#ifndef VERSION
	#define VERSION "X.X.X"
#endif

#ifndef EMAIL
	#define EMAIL "hwstar@rodgers.sdcoxmail.com"
#endif

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

#define SHORT_OPTIONS "c:d:f:hi:np:s:u:v"


#define WS_SIZE 256
#define DEF_INSTANCE_ID		"ademco"
#define DEF_COM_PORT		"/dev/tty-ademco"
#define DEF_PID_FILE		"/var/run/xplademco.pid"
#define DEF_CFG_FILE		"/etc/xplademco.conf"

#define MALLOC_ERROR	malloc_error(__FILE__,__LINE__)

typedef struct state_bits stateBits_t;

struct state_bits {
	unsigned armed : 1;
	unsigned alarm : 1;
	unsigned acfail : 1;
	unsigned lowbatt : 1;
};


typedef struct zone_map zoneMap_t;
typedef zoneMap_t * zoneMapPtr_t;

struct zone_map {
	unsigned zone_num;
	uint32_t zone_name_hash;
	String zone_name;
	String zone_type;
	String alarm_type;
	zoneMapPtr_t next;
	zoneMapPtr_t prev;
};
	
typedef struct {
	const String ademco;
	const String xpl;
} lrrNameMap_t;


typedef struct exp_map expMap_t;
typedef expMap_t * expMapPtr_t;

struct exp_map {
	unsigned addr;
	unsigned channel;
	String zone;
	zoneMapPtr_t zone_entry;
	expMapPtr_t next;
	expMapPtr_t prev;
};
	



/* Config override flags */
enum { CO_PID_FILE = 1, CO_COM_PORT = 2, CO_INSTANCE_ID= 4, CO_INTERFACE = 8, CO_DEBUG_FILE = 0x10 };


char *progName;
int debugLvl = 0; 
Bool noBackground = FALSE;
uint32_t pollRate = 5;
uint32_t configOverride = 0;
unsigned zoneCount = 0;
Bool alarmLRR = FALSE;
stateBits_t stateBits = {0,0};

static Bool lineReceived = FALSE;
static seriostuff_t *serioStuff = NULL;
static xPL_ServicePtr xplService = NULL;
static xPL_MessagePtr xplStatusMessage = NULL;
static xPL_MessagePtr xplEventTriggerMessage = NULL;
static xPL_MessagePtr xplZoneTriggerMessage = NULL;
static ConfigEntry_t *configEntry = NULL;
static zoneMapPtr_t zoneMapHead = NULL;
static zoneMapPtr_t zoneMapTail = NULL;
static expMapPtr_t expMapHead = NULL;
static expMapPtr_t expMapTail = NULL;


static char comPort[WS_SIZE] = DEF_COM_PORT;
static char interface[WS_SIZE] = "";
static char debugFile[WS_SIZE] = "";
static char instanceID[WS_SIZE] = DEF_INSTANCE_ID;
static char pidFile[WS_SIZE] = DEF_PID_FILE;
static char configFile[WS_SIZE] = DEF_CFG_FILE;



/* Basic command list */

static const String const basicCommandList[] = {
	NULL
};


/* Request command list */

static const String const requestCommandList[] = {
	"gateinfo",
	"zonelist",
	"zoneinfo",
	"gatestat",
	NULL
};

/* Commandline options. */

static struct option longOptions[] = {
  {"com-port", 1, 0, 'p'},
  {"config",1, 0, 'c'},
  {"debug-level", 1, 0, 'd'},
  {"help", 0, 0, 'h'},
  {"instance", 1, 0, 's'},
  {"interface", 1, 0, 'i'},
  {"debug-file", 1, 0, 'u'},
  {"no-background", 0, 0, 'n'},
  {"pid-file", 0, 0, 'f'},
  {"version", 0, 0, 'v'},
  {0, 0, 0, 0}
};

/* Ad2usb to xPL event mapping */

lrrNameMap_t lrrNameMap[] = {
{"ACLOSS","ac-fail"},
{"LOWBAT","low-battery"},
{"OPEN","disarmed"},
{"ARM_AWAY","armed"},
{"ARM_STAY","armed-stay"},
{"AC_RESTORE","ac-restore"},
{"LOWBAT_RESTORE","battery-ok"},
{"ALARM_PANIC","alarm"},
{"ALARM_FIRE","alarm"},
{"ALARM_ENTRY","alarm"},
{"ALARM_AUX","alarm"},
{"ALARM_AUDIBLE","alarm"},
{"ALARM_SILENT","alarm"},
{"ALARM_PERIMETER","alarm"},
{NULL,NULL} };


/* 
 * Allocate a memory block and zero it out
 */

static void *mallocz(size_t size)
{
	void *m = malloc(size);
	if(m)
		memset(m, 0, size);
	return m;
}
 
/*
 * Malloc error handler
 */
 
static void malloc_error(String file, int line)
{
	fatal("Out of memory in file %s, at line %d");
}

/*
 * Convert a string to an unsigned int with bounds checking
 */

static Bool str2uns(String s, unsigned *num, unsigned min, unsigned max)
{
		long val;
		if((!num) || (!s)){
			debug(DEBUG_UNEXPECTED, "NULL pointer passed to str2uns");
			return FALSE;
		}
		val = strtol(s, NULL, 0);
		if((val < min) || (val > max))
			return FALSE;
		*num = (unsigned) val;
		return TRUE;
}



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
	xPL_setServiceEnabled(xplService, FALSE);
	xPL_releaseService(xplService);
	xPL_shutdown();
	unlink(pidFile);
	exit(0);
}

/*
* Match a command from a NULL-terminated list, return index to list entry
*/

static int matchCommand(const String const *commandList, const String const command)
{
	int i;

	for(i = 0; commandList[i]; i++){
		if(!strcmp(command, commandList[i]))
			break;
	}
	return i;	
}

/*
* Split string into pieces
*
* The string is copied, and the sep characters are replaced with nul's and a list pointers
* is built. 
*
* This function returns the number of arguments found.
*
* When the caller is finished with the list and the return value is non-zero he should free() the first entry.
*/

static int splitString(const String src, String *list, char sep, int limit)
{
		String p, q, srcCopy;
		int i;
		

		if((!src) || (!list) || (!limit))
			return 0;

		if(!(srcCopy = strdup(src)))
			MALLOC_ERROR;

		for(i = 0, q = srcCopy; (i < limit) && (p = strchr(q, sep)); i++, q = p + 1){
			*p = 0;
			list[i] = q;
		
		}
		if(i){ /* If at least 1 comma is found, get the last bit */
			list[i] = q;
			i++;
		}
		return i;
}

/*
 * Do a zone lookup
 */
 
zoneMapPtr_t zoneLookup(String s)
{
		uint32_t hash = confreadHash(s);
		zoneMapPtr_t zm = zoneMapHead;
		for(; zm; zm = zm->next){
			if((zm->zone_name_hash == hash) && (!strcmp(s, zm->zone_name)))
				break;
		}
		return zm;
}


/*
* Return Gateway info 
*/

static void doGateInfo()
{
	
	char ws[20];
	
	snprintf(ws, 20, "%u", zoneCount);

	xPL_setSchema(xplStatusMessage, "security", "gateinfo");

	xPL_clearMessageNamedValues(xplStatusMessage);

	xPL_setMessageNamedValue(xplStatusMessage, "protocol", "ECP");
	xPL_setMessageNamedValue(xplStatusMessage, "description", "ad2usb to xPL bridge");
	xPL_setMessageNamedValue(xplStatusMessage, "version", VERSION);
	xPL_setMessageNamedValue(xplStatusMessage, "author", "Stephen A. Rodgers");
	xPL_setMessageNamedValue(xplStatusMessage, "info-url", "http://xpl.ohnosec.org");
	xPL_setMessageNamedValue(xplStatusMessage, "zone-count", ws);

	if(!xPL_sendMessage(xplStatusMessage))
		debug(DEBUG_UNEXPECTED, "request.gateinfo transmission failed");
}

/*
* Return list of zones, one per name-value pair
*/

static void doZoneList()
{
	zoneMapPtr_t zm;

	xPL_setSchema(xplStatusMessage, "security", "zonelist");

	xPL_clearMessageNamedValues(xplStatusMessage);

	for(zm = zoneMapHead; zm; zm = zm->next){
		xPL_addMessageNamedValue(xplStatusMessage, "zone-list", zm->zone_name);
	}
	if(!xPL_sendMessage(xplStatusMessage))
		debug(DEBUG_UNEXPECTED, "request.zonelist transmission failed");
}


/*
 * Return zone info for a specific zone
 */

static void doZoneInfo(xPL_MessagePtr theMessage)
{
		const String zone = xPL_getMessageNamedValue(theMessage, "zone");
		zoneMapPtr_t zm;
		if(zone && (zm = zoneLookup(zone))){
			xPL_setSchema(xplStatusMessage, "security", "zoneinfo");
			
			/* Clear the message */
			xPL_clearMessageNamedValues(xplStatusMessage);
			
			/* Fill in the data */
			xPL_addMessageNamedValue(xplStatusMessage, "id", zone);
			xPL_addMessageNamedValue(xplStatusMessage, "zone-type", zm->zone_type);
			xPL_addMessageNamedValue(xplStatusMessage, "alarm-type", zm->alarm_type);
			xPL_addMessageNamedValue(xplStatusMessage, "area-count","0");
			/* Send the message */
			if(!xPL_sendMessage(xplStatusMessage))
				debug(DEBUG_UNEXPECTED, "request.zoneinfo transmission failed");
		}
}

/*
 * Return gateway status
 */

static void doGateStat()
{
		String status = "disarmed";
		
		xPL_setSchema(xplStatusMessage, "security", "gatestat");
		
		/* Clear the message */
		xPL_clearMessageNamedValues(xplStatusMessage);
		
		/* Fill in the data */
		xPL_addMessageNamedValue(xplStatusMessage, "ac-fail", stateBits.acfail ? "true" : "false");
		xPL_addMessageNamedValue(xplStatusMessage, "low-battery", stateBits.lowbatt ? "true" : "false");
		if(stateBits.alarm)
			status = "alarm";
		else if(stateBits.armed)
			status = "armed";
		xPL_addMessageNamedValue(xplStatusMessage, "status", status);		
		
		/* Send the message */
		if(!xPL_sendMessage(xplStatusMessage))
			debug(DEBUG_UNEXPECTED, "request.gatestat transmission failed");
}



/*
* Our Listener 
*/



static void xPLListener(xPL_MessagePtr theMessage, xPL_ObjectPtr userValue)
{

	if(!xPL_isBroadcastMessage(theMessage)){ /* If not a broadcast message */
		if(xPL_MESSAGE_COMMAND == xPL_getMessageType(theMessage)){ /* If the message is a command */
			const String type = xPL_getSchemaType(theMessage);
			const String class = xPL_getSchemaClass(theMessage);
			const String command =  xPL_getMessageNamedValue(theMessage, "command");
			const String request =  xPL_getMessageNamedValue(theMessage, "request");
		
			
			
			debug(DEBUG_EXPECTED,"Non-broadcast message received: type=%s, class=%s", type, class);
			
			/* Allocate a working string */


			if(!strcmp(class,"security")){
				
				if(!strcmp(type, "basic")){ /* Basic command schema */
					if(command){
/*
 * Note: I'm somewhat leery of implementing the arm, disarm, isolate, and bypass commands
 * as premises security could be compromised. Request messages seem be safer than commands,
 * at least to me.
 */
 
						switch(matchCommand(basicCommandList, command)){

							default:
								break;
						}
					}
				}
				else if(!strcmp(type, "request")){ /* Request command schema */
					if(request){
						switch(matchCommand(requestCommandList, request)){

							case 0: /* gateinfo */
								doGateInfo();
								break;

							case 1: /* zonelist */
								doZoneList();
								break;

							case 2: /* zoneinfo */
								doZoneInfo(theMessage);
								break;

							case 3: /* gatestat */
								doGateStat();
								break;

							default:
								break;
						}
								
					}
				}
			}
		}

	}
}


/* 
* Send LRR trigger message
*/


static void doLRRTrigger(String line)
{
	String plist[4];
	int i;

	plist[0] = NULL;

	/* Split the message */


	if(3 == splitString(line, plist, ',', 3)){
		
		/* If OPEN or CANCEL, clear the alarmLRR flag */
		if((!strcmp(plist[2], "OPEN")) || (!strcmp(plist[2], "CANCEL")))
			alarmLRR = FALSE;
			
		/* Try to find a match to an xPL equicalent reporting state */
		for(i = 0; lrrNameMap[i].ademco; i++){
			if(!strcmp(lrrNameMap[i].ademco, plist[2]))
				break;
		}
		if(lrrNameMap[i].ademco){ /* If match */
			xPL_clearMessageNamedValues(xplEventTriggerMessage);
			xPL_addMessageNamedValue(xplEventTriggerMessage, "event", lrrNameMap[i].xpl);
			xPL_sendMessage(xplEventTriggerMessage);
		
			/* Update the alarmLRR bit which reflects the status of all the alarms */
			if(!strcmp(lrrNameMap[i].xpl, "alarm"))
					alarmLRR = TRUE;
		}
	}
	if(plist[0])
		free(plist[0]);
}

/*
* Send an EXP trigger message
*/

static void doEXPTrigger(String line)
{
	String plist[4];
	int i;
	expMapPtr_t e;
	
	plist[0] = NULL;
	
	/* Do not send zone state changes if armed */
	if(stateBits.armed)
		return;
		
	/* Split the message */
	if(3 == splitString(line, plist, ',', 3)){
		for(e = expMapHead; e ; e = e->next){
			/* debug(DEBUG_EXPECTED,"plist[0]: %s, plist[1]: %s, e->addr: %d, e->channel: %d", plist[0], plist[1], e->addr, e->channel); */
			if((atoi(plist[0]) == e->addr)&&(atoi(plist[1]) == e->channel))
				break;
		}
		if(e){ /* If match */
			i = atoi(plist[2]);
			xPL_clearMessageNamedValues(xplEventTriggerMessage);
			xPL_addMessageNamedValue(xplEventTriggerMessage, "event", i ? "alert" : "normal");
			xPL_addMessageNamedValue(xplEventTriggerMessage, "zone", e->zone);
			xPL_sendMessage(xplEventTriggerMessage);
		}
	}
	if(plist[0])
		free(plist[0]);

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
			confreadStringCopy(newStatBits, line + 1, 21);
			if(firstTime){ /* Set new and old the same on first time */
				firstTime = FALSE;
				confreadStringCopy(oldStatBits, newStatBits, 21);
			}
			if(strcmp(newStatBits, oldStatBits)){
				confreadStringCopy(oldStatBits, newStatBits, 21);
				debug(DEBUG_EXPECTED,"New Status bits: %s", newStatBits);
			}
			
			/* If anything is armed */
			if((newStatBits[1] == '1') || (newStatBits[2] == '1') ||
			   (newStatBits[12] == '1') || (newStatBits[15] == '1'))
				stateBits.armed = 1;
			else
				stateBits.armed = 0;
				
			/* If any alarm including one sent from LRR */
			if((newStatBits[10] == '1') || (newStatBits[13] == '1') || alarmLRR)
				stateBits.alarm = 1;
			else
				stateBits.alarm = 0;
			
			/* If AC fail */	
			if(newStatBits[7] == '0')
				stateBits.acfail = 1;
			else
				stateBits.acfail = 0;
			
			/* If low battery */
			if(newStatBits[11] == '1')
					stateBits.lowbatt = 1;
			else
					stateBits.lowbatt = 0;
				
		}
		else if(line[0] == '!'){ /* Other events */
			String p = line + 5;
			if(!strncmp(line + 1, "EXP", 3)){ /* Expander event ? */
				debug(DEBUG_EXPECTED,"Expander event: %s", p);
				doEXPTrigger(p);
			}
			if(!strncmp(line + 1, "LRR", 3)){ /* Long Range radio event ? */
				debug(DEBUG_EXPECTED,"Long Range Radio event: %s", p);
				doLRRTrigger(p);
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
	static Bool firstTime = TRUE;

	/* Process clock tick update checking */
	if(firstTime){
		firstTime = FALSE;
		xPL_clearMessageNamedValues(xplEventTriggerMessage);
		xPL_addMessageNamedValue(xplEventTriggerMessage, "event", "ready");
		xPL_sendMessage(xplEventTriggerMessage);
	}

}


/*
* Show help
*/

static void showHelp(void)
{
	printf("'%s' is a daemon that bridges the xPL protocol to an ademco panel using an ad2usb adapter\n", progName);
	printf("via a USB or RS-232 interface\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", progName);
	printf("\n");
	printf("  -c, --config-file PATH  Set the path to the config file\n");
	printf("  -d, --debug-level LEVEL Set the debug level, 0 is off, the\n");
	printf("                          compiled-in default is %d and the max\n", debugLvl);
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -f, --pid-file PATH     Set new pid file path, default is: %s\n", pidFile);
	printf("  -h, --help              Shows this\n");
	printf("  -i, --interface NAME    Set the broadcast interface (e.g. eth0)\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -p, --com-port PORT     Set the communications port (default is %s)\n", comPort);
	printf("  -s, --instance ID       Set instance id. Default is %s", instanceID);
	printf("  -u, --debug-file PATH    Path name to debug file when daemonized\n");
	printf("  -v, --version           Display program version\n");
	printf("\n");
 	printf("Report bugs to <%s>\n\n", EMAIL);
	return;

}

/*
* Print syntax error message and exit
*/

static void syntax_error(KeyEntryPtr_t ke, String configFile, String message)
{
	if(ke && configFile && message)
		fatal("Syntax error in configuration file: %s on line %u: %s", configFile, confreadKeyLineNum(ke), message);
	else
		fatal("syntax_error() called without valid arguments");
}


/*
* main
*/


int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	String p;
	KeyEntryPtr_t e;
	zoneMapPtr_t zm;

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
				confreadStringCopy(configFile, optarg, WS_SIZE - 1);
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
				confreadStringCopy(pidFile, optarg, WS_SIZE - 1);
				debug(DEBUG_ACTION,"New pid file path is: %s", pidFile);
				configOverride |= CO_PID_FILE;
				break;

			
			/* Was it a help request? */
			case 'h':
				showHelp();
				exit(0);

			/* Specify interface to broadcast on */
			case 'i': 
				confreadStringCopy(interface, optarg, WS_SIZE -1);
				xPL_setBroadcastInterface(interface);
				configOverride |= CO_INTERFACE;

				break;

			case 'u':
				/* Override debug path*/
				confreadStringCopy(debugFile, optarg, WS_SIZE - 1);
				debug(DEBUG_ACTION,"New debug path is: %s", debugFile);
				configOverride |= CO_DEBUG_FILE;
				break;

			/* Was it a no-backgrounding request? */
			case 'n':

				/* Mark that we shouldn't background. */
				noBackground = TRUE;

				break;
			case 'p':
				/* Override com port*/
				confreadStringCopy(comPort, optarg, WS_SIZE - 1);
				debug(DEBUG_ACTION,"New com port is: %s", comPort);
				configOverride |= CO_COM_PORT;
				break;

			/* Was it an instance ID ? */
			case 's':
				confreadStringCopy(instanceID, optarg, WS_SIZE);
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
			confreadStringCopy(comPort, p, WS_SIZE);
		}	
	}

	/* Debug file */
	if(!(configOverride & CO_DEBUG_FILE)){
		if((p = confreadValueBySectKey(configEntry, "general", "debug-file"))){
			confreadStringCopy(debugFile, p, WS_SIZE);
		}
	
	}

	/* PID file */
	if(!(configOverride & CO_PID_FILE)){
		if((p = confreadValueBySectKey(configEntry, "general", "pid-file"))){
			confreadStringCopy(pidFile, p, WS_SIZE);
		}
	
	}

	/* Instance ID */
	if(!(configOverride & CO_INSTANCE_ID)){
		if((p =  confreadValueBySectKey(configEntry, "general", "instance-id"))){
			confreadStringCopy(instanceID, p, WS_SIZE);
		}	
	}


	/* Interface */
	if(!(configOverride & CO_INTERFACE)){
		if((p = confreadValueBySectKey(configEntry, "general", "interface"))){
			confreadStringCopy(interface, p, WS_SIZE);
		}	
	}

	/* Build Zone Map */
	
	if(!(e = confreadGetFirstKeyBySection(configEntry, "zone-map")))
		fatal("A valid zone-map section and at least one entry must be defined in the config file");
	for(; e; e = confreadGetNextKey(e)){
		String plist[3];
		const String key = confreadGetKey(e);
		const String value = confreadGetValue(e);
		/* Allocate a zone struct */
		if(!(zm = mallocz(sizeof(zoneMap_t))))
			MALLOC_ERROR;
			
		/* Get the zone number */
		if(!str2uns(key, &zm->zone_num, 1, 99))
			syntax_error(e, configFile,"invalid zone number");
			
		/* Get the parameters */
		if(3 != splitString(value, plist, ',', 3))
			syntax_error(e, configFile, "3 parameters required");
		if(!(zm->zone_name = strdup(plist[0])))
			MALLOC_ERROR;
		if(!(zm->zone_type = strdup(plist[1])))
			MALLOC_ERROR;
		if(!(zm->alarm_type = strdup(plist[2])))
			MALLOC_ERROR;
			
		/* Hash the zone name */
		zm->zone_name_hash = confreadHash(zm->zone_name);
		
			
		/* Free the split string */
		free(plist[0]);
		
		/* Insert the entry into the zone list */
		if(!zoneMapHead)
			zoneMapHead = zoneMapTail = zm;
		else{
			zm->prev = zoneMapTail;
			zoneMapTail->next = zm;
			zoneMapTail = zm;
		}
		zoneCount++;
	}

	/* EXP zone mapping */

	for(e =  confreadGetFirstKeyBySection(configEntry, "exp-map"); e; e = confreadGetNextKey(e)){
		expMapPtr_t emp;
		const String keyString = confreadGetKey(e);
		const String zone = confreadGetValue(e);
		String plist[3];
		unsigned expaddr, expchannel;

		/* Check the key and zone strings */
		if(!(keyString) || (!zone))
			syntax_error(e, configFile, "key or zone missing");


		/* Split the address and channel */
		plist[0] = NULL;
		if(2 != splitString(keyString, plist, ',', 2))
			syntax_error(e, configFile, "left hand side needs 2 numbers separated by a comma");

		/* Convert and check address */
		if(!str2uns(plist[0], &expaddr, 1, 99))
			syntax_error(e, configFile,"address is limited from 1 - 99");


		/* Convert and check channel */
		if(!str2uns(plist[1], &expchannel, 1, 99))
			syntax_error(e, configFile,"channel is limited from 1 - 99");
			

		/* debug(DEBUG_ACTION, "Address: %u, channel: %u, zone: %s", expaddr, expchannel, zone); */
		
		/* Look up zone to ensure it is defined */
		
		if(!(zm = zoneLookup(zone)))
			syntax_error(e, configFile, "Zone must be defined in zone-map section");
		

		/* Get memory for entry */
		if(!(emp = mallocz(sizeof(expMap_t))))
			MALLOC_ERROR;

		/* Initialize entry */
		emp->zone_entry = zm;
		emp->addr = expaddr;
		emp->channel = expchannel;
		if(!(emp->zone = strdup(zone)))
			MALLOC_ERROR;

		/* Insert into list */
		if(!expMapHead){
			expMapHead = expMapTail = emp;
		}
		else{
			expMapTail->next = emp;
			emp->prev = expMapTail;
			expMapTail = emp;
		}

		/* Free parameter string */
		if(plist[0])
			free(plist[0]);
	}


	/* Turn on library debugging for level 5 */
	if(debugLvl >= 5)
		xPL_setDebugging(TRUE);

 	/* Make sure we are not already running (.pid file check). */
	if(pid_read(pidFile) != -1) {
		fatal("%s is already running", progName);
	}

	/* Check to see the serial device exists before we fork */
	if(!serio_check_node(comPort))
		fatal("Serial device %s does not exist or its permissions are not allowing it to be used.", comPort);

	/* Fork into the background. */

	if(!noBackground) {
		int retval;
		debug(DEBUG_STATUS, "Forking into background");

    		/* 
		* If debugging is enabled, and we are daemonized, redirect the debug output to a log file if
    		* the path to the logfile is defined
		*/

		if((debugLvl) && (debugFile[0]))                          
			notify_logpath(debugFile);

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

	/* Initialize xplrcs service */

	/* Create a service and set our application version */
	xplService = xPL_createService("hwstar", "xplademco", instanceID);
  	xPL_setServiceVersion(xplService, VERSION);

	/*
	* Create a status message object
	*/

  	xplStatusMessage = xPL_createBroadcastMessage(xplService, xPL_MESSAGE_STATUS);
  
	/*
	* Create trigger message objects
	*/

	/* security.gateway */
	if(!(xplEventTriggerMessage = xPL_createBroadcastMessage(xplService, xPL_MESSAGE_TRIGGER)))
		fatal("Could not initialize security.gateway trigger");
	xPL_setSchema(xplEventTriggerMessage, "security", "gateway");

	/* security.zone */
	if(!(xplZoneTriggerMessage = xPL_createBroadcastMessage(xplService, xPL_MESSAGE_TRIGGER)))
		fatal("Could not initialize security.zone trigger");
	xPL_setSchema(xplZoneTriggerMessage, "security", "zone");


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
  	xPL_setServiceEnabled(xplService, TRUE);

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

