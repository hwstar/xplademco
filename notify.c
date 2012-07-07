/*
*    xplrcs - an RCS RS-485 thermostat to xPL bridge
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
*    Notification helpers
*   
* 
* 
* Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
*
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include "notify.h"

#define LOGOUT (output == NULL ? stderr : output)

/* Program name */
extern char *progName;

/* Debug level. */
extern int debugLvl;

FILE *output = NULL;


/*
* Redirect logging and error output
*/

void notify_logpath(char *path)
{
  FILE *f;

  if(output != NULL)
    fclose(output);
    
  if((f = fopen(path,"w")) == NULL)
    fatal_with_reason(errno, "Can't open log file for writing");
  output = f;
}


/* Fatal error handler with strerror(errno) reason */

void fatal_with_reason(int error, char *message, ...)
{
    va_list ap;
    
    va_start(ap, message);

    fprintf(LOGOUT, "%s: ", progName);
    vfprintf(LOGOUT, message, ap);
    fprintf(LOGOUT, ": %s\n",strerror(error));

    va_end(ap);
    exit(1);
}


/* Fatal error handler. */
void fatal(char *message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print error message. */
	fprintf(LOGOUT,"%s: ",progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	
	/* Exit with an error code. */
	va_end(ap);
	exit(1);
}


/* Normal error handler. */
void error(char *message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print error message. */
	fprintf(LOGOUT,"%s: ",progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	
	va_end(ap);
	return;
}


/* Warning handler. */
void warn(char *message, ...) {
	va_list ap;
	va_start(ap, message);
	
	/* Print warning message. */
	fprintf(LOGOUT,"%s: warning: ",progName);
	vfprintf(LOGOUT,message,ap);
	fprintf(LOGOUT,"\n");
	
	va_end(ap);
	return;
}



/* Debugging error handler. */
void debug(int level, char *message, ...) {
	va_list ap;
	va_start(ap, message);
	time_t t;

	char timenow[32];
	int l;
 	
	/* We only do this code if we are at or above the debug level. */
	if(debugLvl >= level) {
		t = time(NULL);
		strncpy(timenow,ctime(&t), 31);
		timenow[31] = 0;
		l = strlen(timenow);
		if(l)
			timenow[l-1] = '\0';
      
		/* Print the error message. */
		fprintf(LOGOUT,"%s [ %s ] (debug): ", progName, timenow);
		vfprintf(LOGOUT, message, ap);
		fprintf(LOGOUT,"\n");
		if(output != NULL)  /* If we are writing to a log file, flush the debug output. */
			fflush(output);
		}
	va_end(ap);
}

/* Print a debug string with a buffer of bytes to print */

void debug_hexdump(int level, void *buf, int buflen, char *message, ...){
	int i;
	va_list ap;
	va_start(ap, message);

	if(debugLvl >= level) {
		fprintf(LOGOUT,"%s: (debug): ",progName);
		vfprintf(LOGOUT,message,ap);
		for(i = 0 ; i < buflen ; i++)
			fprintf(LOGOUT,"%02X ",((int) ((char *)buf)[i]) & 0xFF);
		fprintf(LOGOUT,"\n");
	}
}

