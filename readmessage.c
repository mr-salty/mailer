/*
 * $Id$
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "mailer_config.h"

extern char *messagebody,*mailfrom;
extern char *messagefile,*mpppath;
extern int messagebody_size;

static void failmpp(char *message)
{
    fprintf(stderr,"mpp failed: %s\n",message);
    exit(1);
}

void readmessage()
{
    char cmd[MAXPATHLEN];
    char outfname[80];
    struct stat sbuf;
    int fd;

    sprintf(outfname,"/tmp/mpp.%d",getpid());
    sprintf(cmd,"%s %s %s %s",mpppath,messagefile,outfname,mailfrom);

    if(system(cmd))
	failmpp("exec");

    if((fd=open(outfname,O_RDONLY,0)) == -1)
	failmpp("open");

    if(stat(outfname,&sbuf) == -1)
	failmpp("stat");

    if((messagebody_size=sbuf.st_size)==0) failmpp("size==0");

    /* mmap() here for other architectures! */
    /* allocate an extra 1k to hold the URL */
    if(!(messagebody=malloc(messagebody_size+1+1024))) {
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
