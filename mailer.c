/*
 * $Log: mailer.c,v $
 * Revision 1.5  2008/06/21 17:17:34  tjd
 * reindent everything
 *
 * Revision 1.4  1998/09/30 21:45:42  tjd
 * make mailer become leader of its own session and pgrp
 * bump version to 1.6
 *
 * Revision 1.3  1998/02/17 16:28:52  tjd
 * added skip_addrs function
 *
 * Revision 1.2  1996/05/02 22:39:41  tjd
 * removed all dependencies on useful.h
 *
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include "mailer_config.h"

/* mailer listfile message_file from@host [path to mpp] */

char *messagebody,*mailfrom,*myhostname;
int messagebody_size;
void readmessage(char *filename,char *mpp);
void do_list(char *fname);

void usage(char *prog)
{
    fprintf(stderr,
	"Usage: %s listfile[+line] message_file from@host [path to mpp] \n",
	prog);
    exit(1);
}

int main(int argc, char *argv[])
{
    int status;

    if(argc != 4 && argc != 5) usage(argv[0]);

    if(!(myhostname=strrchr((mailfrom=argv[3]),'@'))) usage(argv[0]);
    myhostname++;

    readmessage(argv[2], (argc==4 ? MPP : argv[4]));

    /* become a process group and session leader */
    switch(fork()) {
      case -1:
	perror("fork");
	exit(1);

      case 0:	/* child continues */
	break;

      default:	/* parent waits for the child */
	wait(&status);
	exit WEXITSTATUS(status);
    }

    if(setsid() == -1) {
	perror("setsid");
    }

    do_list(argv[1]);
    return 0;
}
