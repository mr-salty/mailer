/*
 * $Log: readmessage.c,v $
 * Revision 1.3  1996/02/15 04:09:05  tjd
 * no longer exec mpp through a pipe, but we use a temp file instead
 * this in preparation for mmap()ing the message
 *
 * Revision 1.2  1996/01/01 22:40:43  tjd
 * fixed read() logic for messages > pipe capacity
 *
 * Revision 1.1  1995/12/14  15:23:30  tjd
 * Initial revision
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "mailer_config.h"

extern char *messagebody,*myhostname;
extern int messagebody_size;

static void failmpp(char *message)
{
	fprintf(stderr,"mpp failed: %s\n",message);
	exit(1);
}

void readmessage(char *filename,char *mpp)
{
	char cmd[PATH_MAX];
	char outfname[80];
	struct stat sbuf;
	int fd;

	sprintf(outfname,"/tmp/mpp.%d",getpid());
	sprintf(cmd,"%s %s %s %s",mpp,filename,outfname,myhostname);

	if(system(cmd))
		failmpp("exec");

	if((fd=open(outfname,O_RDONLY,0)) == -1)
		failmpp("open");

	if(stat(outfname,&sbuf) == -1)
		failmpp("stat");

	if((messagebody_size=sbuf.st_size)==0) failmpp("size==0");

	/* mmap() here for other architectures! */
	if(!(messagebody=malloc(messagebody_size+1))) {
		perror("malloc");
		failmpp("malloc");
	}

	if(read(fd,messagebody,messagebody_size) != messagebody_size)
		failmpp("read");

	close(fd);
	unlink(outfname);

	messagebody[messagebody_size]='\0';

	if(strcmp(messagebody+messagebody_size-5,"\r\n.\r\n"))
		failmpp("trailer");
}
