/*
*    Serial I/O functions 
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
*    serial I/O definitions.
*
*
*/

#ifndef SERIO_H
#define SERIO_H

#include "types.h"

#define SERIO_MAX_LINE 1024


/* Typedefs. */
typedef struct seriostuff serioStuff_t;
typedef serioStuff_t * serioStuffPtr_t;

/* Structure to hold serio info. */
struct seriostuff {
	int fd;				/* File descriptor */
	int pos;			/* Position variable for non-blocking read fn's */
	unsigned brc;		/* baud rate constant */
	unsigned magic;	/* magic number */
	char *path;			/* path name to node file */
	char *line;			/* line buffer for non-blocking read fn's */
};

/* Prototypes. */
serioStuffPtr_t serio_open(const char *tty_name, unsigned baudrate);
void serio_close(serioStuffPtr_t serio);
Bool serio_check_node(char *path);
int serio_flush_input(serioStuffPtr_t serio);
int serio_fd(serioStuffPtr_t s);
int serio_get_baud(unsigned br);
int serio_write(serioStuffPtr_t serio, const void *buffer, size_t count);
int serio_read(serioStuffPtr_t serio, void *buffer, size_t count);
int serio_nb_line_read(serioStuffPtr_t serio);
int serio_nb_line_readcr(serioStuffPtr_t serio);
char *serio_line(serioStuffPtr_t serio);
int serio_printf(serioStuffPtr_t serio, const char *format, ...);

#endif
