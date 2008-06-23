/*
 * $Id$
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/file.h>
#include "mailer_config.h"
#include "userlist.h"

void bounce_user(char *addr,bounce_reason why,int statcode)
{
    char *reasons[]=BOUNCE_REASONS;
    char buf[MAX_ADDR_LEN+100];
    int fd;

    sprintf(buf,"%s [%03d %s]\n",addr,statcode,reasons[why]);

    if((fd=open(BOUNCE_FILE,O_WRONLY|O_APPEND,0666))==-1)
    {
#ifdef ERROR_MESSAGES
	perror("open (bounce file)");
#endif
	return;
    }

    if(flock(fd,LOCK_EX) == -1)
    {
#ifdef ERROR_MESSAGES
	perror("lock (bounce file)");
#endif
    }

    if(write(fd,buf,strlen(buf)) == -1)
    {
#ifdef ERROR_MESSAGES
	perror("write (bounce)");
#endif
    }

    flock(fd,LOCK_UN);
    close(fd);
}

/* returns # of failures */
int bounce(userlist users[],bounce_reason fail_all)
{
    int i,ret=0;

    for(i=0;users[i].addr;++i)
    {
	if(fail_all || users[i].statcode)
	{
	    bounce_user(users[i].addr,
		fail_all ? fail_all >> 16 : users[i].statcode >> 16,
		fail_all ? fail_all & 0xffff : users[i].statcode & 0xffff );
	    ret++;
	}
    }
#ifdef ERROR_MESSAGES
    if(!i)
	fprintf(stderr,"Warning: bounce() called with no users\n");
#endif

    return ret;
}
