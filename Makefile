# Makefile for mailer
# $Id: Makefile,v 1.1 1996/05/11 18:33:53 tjd Exp $

CC=gcc
WFLAGS=-Wall -pedantic
OFLAGS=-O2 -fomit-frame-pointer # -fno-inline -fno-unroll-loops
GFLAGS=-g

CFLAGS=$(WFLAGS) $(OFLAGS)

# uncomment for debug
#CFLAGS=$(WFLAGS) $(GFLAGS)
#LDFLAGS=$(GFLAGS)

MLIBS=# -lresolv -l44bsd	# needed for lrdc5, new bind not in libc.

MAILER=mailer
MPP=mpp
PROGS=$(MAILER) $(MPP)

SRCS=	mailer.c do_list.c readmessage.c domain.c deliver.c mpp.c \
	arpadate.c bounce.c
HDRS=	conf.h sendmail.h mailer_config.h cdefs.h userlist.h
MOBJS=	mailer.o do_list.o readmessage.o domain.o deliver.o bounce.o
POBJS=	mpp.o arpadate.o

all: ${PROGS}

${MAILER}: ${MOBJS}
	$(CC) $(LDFLAGS) ${MOBJS} $(MLIBS) -o $(MAILER)

${MPP}: ${POBJS}
	$(CC) $(LDFLAGS) ${POBJS} -o $(MPP)

strip:	${PROGS}
	strip $(PROGS)

clean:
	rm -f *.o

realclean: clean
	rm -f $(PROGS)

checkout:
	co ${SRCS} ${HDRS}

tar:
	tar cvf mailer.tar RCS 
	gzip -9 mailer.tar
