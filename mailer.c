/*
 * $Log: mailer.c,v $
 * Revision 1.2  1996/05/02 22:39:41  tjd
 * removed all dependencies on useful.h
 *
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include <string.h>
#include "mailer_config.h"

/* mailer listfile message_file from@host [path to mpp] */

char *messagebody,*mailfrom,*myhostname;
int messagebody_size;
void readmessage(char *filename,char *mpp);
void do_list(char *fname);

void usage(char *prog)
{
	fprintf(stderr,"Usage: %s listfile message_file from@host [path to mpp] \n",prog);
	exit(1);
}

int main(int argc, char *argv[])
{
	if(argc != 4 && argc != 5) usage(argv[0]);

	if(!(myhostname=strrchr((mailfrom=argv[3]),'@'))) usage(argv[0]);
	myhostname++;

	readmessage(argv[2], (argc==4 ? MPP : argv[4]));

	do_list(argv[1]);
	return 0;
}
