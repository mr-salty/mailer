/*
 * $Log: readmessage.c,v $
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
#include <unistd.h>

#include "mailer_config.h"

extern char *messagebody,*myhostname;
extern int messagebody_size;

static void failmpp(char *message)
{
	fprintf(stderr,"Message failed mpp: %s\n",message);
	exit(1);
}

int small_popen_r(char *cmd[]);

void readmessage(char *filename,char *mpp)
{
	char *cmd[4];
	int fd,nread,tread;

	cmd[0]=mpp;
	cmd[1]=filename;
	cmd[2]=myhostname;
	cmd[3]=NULL;

	if((fd=small_popen_r(cmd))==-1)
	{
		fprintf(stderr,"Can't exec %s\n",mpp);
		exit(1);
	}

	messagebody_size=0;

	if(read(fd,&messagebody_size,sizeof(int)) != sizeof(int))
		failmpp("read(size)");

	if(messagebody_size==0) failmpp("size==0");

	if(!(messagebody=malloc(messagebody_size+1))) {
		perror("malloc");
		failmpp("malloc");
	}

	tread=0;

	while((nread=read(fd,messagebody+tread,messagebody_size-tread)))
	{
		if(nread==-1)
		{
			perror("read (pipe)");
			failmpp("read error (message)");
		}
		tread+=nread;
	}

	if(tread != messagebody_size)
	{
		failmpp("size mismatch");
	}

	close(fd);
	wait(NULL);

	messagebody[messagebody_size]='\0';

	if(strcmp(messagebody+messagebody_size-5,"\r\n.\r\n"))
		failmpp("message trailer");
}
