/*
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
* serio.c
*
* Serial I/O functions
*
*/



#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "types.h"
#include "serio.h"
#include "notify.h"

#define TRUE 1
#define FALSE 0
#define ERROR -1

#define SERIO_MAGIC	0x4C9A8DBF

enum {MS_OK, MS_FAULT};

/*
* Private function to free all memory used in a seriostuff_t type
*/

static void free_seriostuff(serioStuffPtr_t serio)
{
	if(serio && (serio->magic == SERIO_MAGIC)){
		if(serio->line)
			free(serio->line);

		if(serio->path)
			free(serio->path);
		serio->magic = 0;
		free(serio);
	}
}


/*
* Private function to do open on a node and set it up for serial I/O
*/


static int node_open(serioStuffPtr_t serio)
{
	struct termios termios;

	/* 
	 * Open the serio tty device.
	 */

	serio->fd=open(serio->path, O_RDWR | O_NOCTTY | O_NDELAY);

	if(serio->fd == -1) {
		return FALSE;
	}
	
	/* Set the options on the port. */
	
	/* We don't want to block reads. */
	if(fcntl(serio->fd, F_SETFL, O_NONBLOCK) == -1) {
		close(serio->fd);
		return FALSE;
	}
	
	/* Get the current tty settings. */
	if(tcgetattr(serio->fd, &termios) != 0) {
		close(serio->fd);
		return FALSE;
	}
	
	/* Enable receiver. */
	termios.c_cflag |= CLOCAL | CREAD;
	
	/* Set to 8N1. */
	termios.c_cflag &= ~PARENB;
	termios.c_cflag &= ~CSTOPB;
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |=  CS8;
	
	/* Accept raw data. */
	termios.c_lflag &= ~(ICANON | ECHO | ISIG);
	termios.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONLRET | OFILL);
	termios.c_iflag &= ~(ICRNL | IXON | IXOFF | IMAXBEL);

	/* Set the speed of the port. */
	if(cfsetospeed(&termios, (speed_t) serio->brc) != 0) {
		close(serio->fd);
		return FALSE;
	}
	if(cfsetispeed(&termios, (speed_t) serio->brc) != 0) {
		close(serio->fd);
		return FALSE;
	}
	
	/* Save our modified settings back to the tty. */
	if(tcsetattr(serio->fd, TCSANOW, &termios) != 0) {
		close(serio->fd);
		return FALSE;
	}

	return TRUE;

}



/*
* Check baud rate for validity
*/

int serio_get_baud(unsigned br)
{	
	switch(br){
		case 1200:
			return B1200;
		case 2400:
			return B2400;
		case 4800:
			return B4800;
		case 9600:
			return B9600;
		case 19200:
			return B19200;
		case 38400:
			return B38400;
		case 57600:
			return B57600;
		case 115200:
			return B115200;
		default:
			return 0;
	}
}


/*
* Check the path name to the node to ensure it exists and is a character device
*/

Bool serio_check_node(char *path)
{
	struct stat s;

	/* Do sanity checks first */

	if(stat(path, &s) < 0){
		debug(DEBUG_UNEXPECTED, "Can't stat %s: %s", path, strerror(errno));
		return FALSE;
	}

	if(!S_ISCHR(s.st_mode)){
		debug(DEBUG_UNEXPECTED, "%s is not a character device", path);
		return FALSE;
	}

	if(access(path, R_OK|W_OK) < 0){
		debug(DEBUG_UNEXPECTED, "Permissions problem on: %s", path);
		return FALSE;
	}
	return TRUE;

}



/* 
 * Open the serial device. 
 *
 * Description of how to do the serial handling came from some mini serial
 * port programming howto.
 */



serioStuffPtr_t serio_open(const char *tty_name, unsigned baudrate) {
	serioStuffPtr_t serio;
	speed_t brc;


	if(!(brc = serio_get_baud(baudrate))){
		debug(DEBUG_UNEXPECTED, "Invalid baud rate: %u\n", baudrate);
		return NULL;
	}
	
	/* Allocate memory for our struct */
	if(!(serio = malloc(sizeof(serioStuff_t))))
		return NULL;

	/* Zero it */
	memset(serio, 0, sizeof(serioStuff_t));
	
	/* Add the magic number */

	serio->magic = SERIO_MAGIC;

	/* Allocate memory for line */
	if(!(serio->line = malloc(SERIO_MAX_LINE))){
		free_seriostuff(serio);
		return NULL;
	}
	/* Duplicate path name */
	if(!(serio->path = strdup(tty_name))){
		free_seriostuff(serio);
		return NULL;
	}


	serio->brc = (unsigned ) brc;

	/* Make sure the path is valid */

	if(!serio_check_node(serio->path)){
		free_seriostuff(serio);
		return NULL;
	}
	
	/* Open the port */

	if(!node_open(serio)){
		free_seriostuff(serio);
		return NULL;
	}
	
	
	return(serio);
}


/*
* Return the file descriptor
*/

int serio_fd(serioStuffPtr_t serio)
{
	return serio->fd;
}

/* Flush the input buffer */

int serio_flush_input(serioStuffPtr_t serio)
{

	return tcflush(serio->fd, TCIFLUSH);
}



/* Close the TTY port, and free the serio structure */

void serio_close(serioStuffPtr_t serio){

	if(serio->fd >= 0)
		close(serio->fd);
	free_seriostuff(serio);

}

/*
* Unbuffered write
*/

int serio_write(serioStuffPtr_t serio, const void *buffer, size_t count)
{

	return write(serio->fd, buffer, count);
}

/*
* Unbuffered read
*/

int serio_read(serioStuffPtr_t serio, void *buffer, size_t count)
{

	return read(serio->fd, buffer, count);
}


/*
* Non blocking line read
* Read bytes one at a time and build a line.
* Return 1 return detected, 0 if not at end of line, and -1 if error.
*/


int serio_nb_line_read(serioStuffPtr_t serio)
{
	char c;
	int res;


	do{
		res = serio_read(serio, &c, 1);
	

		if(res < 0){
			if((errno != EAGAIN) && (errno != EWOULDBLOCK)){
				debug(DEBUG_UNEXPECTED, "Read error on fd %d: %s", serio->fd, strerror(errno));
				serio->pos = 0;
				return ERROR;
			}
			return FALSE;
		}
		else if(res == 1){
			/* debug(DEBUG_ACTION,"Byte received"); */
			if(c != '\r'){
				if(serio->pos < (SERIO_MAX_LINE - 1))
					serio->line[serio->pos++] = c;
				else
					debug(DEBUG_UNEXPECTED,"End of line buffer reached!");
			}
			else{
				debug(DEBUG_ACTION, "Line received");
				serio->line[serio->pos] = 0;
				serio->pos = 0;
				return TRUE;
			}
		}
	} while(TRUE);

	return ERROR;
}

/*
* Non blocking line read
* Read bytes one at a time and build a line.
* Return 1 on cr detected, 0 if not at end of line, and -1 if error.
*/


int serio_nb_line_readcr(serioStuffPtr_t serio)
{
	char c;
	int res;

	do{
		res = serio_read(serio, &c, 1);	

		if(res < 0){
			if((errno != EAGAIN) && (errno != EWOULDBLOCK)){
				debug(DEBUG_UNEXPECTED, "Read error on fd %d: %s", serio->fd, strerror(errno));
				serio->pos = 0;
				return ERROR;
			}
			return FALSE;
		}
		else if(res == 1){
			/* debug(DEBUG_ACTION,"Byte received"); */
			if(c == '\r') /* Ignore return */
				continue;
			if(c != '\n'){
				if(serio->pos < (SERIO_MAX_LINE - 1))
					serio->line[serio->pos++] = c;
				else
					debug(DEBUG_UNEXPECTED,"End of line buffer reached!");

			}
			else{
				debug(DEBUG_ACTION, "Line received");
				serio->line[serio->pos] = 0;
				serio->pos = 0;
				return TRUE;
			}
		}
	} while(TRUE);

	return ERROR;
}


/*
* Return address of line buffer used for serio_nb_line_read
*/

char *serio_line(serioStuffPtr_t serio)
{
	return serio->line;
}

/*
* Printf to the com port
*/


int serio_printf(serioStuffPtr_t serio, const char *format, ...)
{
 	va_list ap;
	int res = 0;
    
	va_start(ap, format);

	if(serio->fd >= 0)
		res = vdprintf(serio->fd, format, ap);

	va_end(ap);

	return res;
}

