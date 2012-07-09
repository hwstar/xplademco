# Makefile

PACKAGE = xplademco
VERSION = 0.0.1
CONTACT = <hwstar@rodgers.sdcoxmail.com>

CC = gcc
CFLAGS = -O2 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'
#CFLAGS = -g3 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'

# Install paths for built executables

DAEMONDIR = /usr/local/bin

#.PHONY Targets

.PHONY: all, clean, install, dist

# Object file lists

OBJS = $(PACKAGE).o serio.o notify.o confread.o

#Dependencies

all: $(PACKAGE) 

$(PACKAGE).o: Makefile $(PACKAGE).c notify.h serio.h

#Rules

$(PACKAGE): $(OBJS)
	$(CC) $(CFLAGS) -o $(PACKAGE) $(OBJS) -lxPL

clean:
	-rm -f $(PACKAGE) *.o core

install:
	cp $(PACKAGE) $(DAEMONDIR)

dist:
	(cd ..; tar cvzf $(PACKAGE).tar.gz $(PACKAGE) --exclude *.o --exclude $(PACKAGE)/$(PACKAGE) --exclude .git --exclude .*.swp)

