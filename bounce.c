/*
 * $Log: bounce.c,v $
 * Revision 1.2  1996/01/02 00:28:11  tjd
 * added defug code to check for null userlist
 *
 * Revision 1.1  1995/12/14  15:23:30  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include "userlist.h"

void bounce_user(char *addr,bounce_reason why)
{
	char *reasons[]=BOUNCE_REASONS;

	fprintf(stderr,"BOUNCE_USER: %s [%s]\n",addr,reasons[why]);
}

/* returns # of failures */
int bounce(userlist users[],bounce_reason fail_all)
{
	int i,ret=0;

	for(i=0;users[i].addr;++i)
	{
		if(fail_all || users[i].bounced)
		{
			bounce_user(users[i].addr,
				fail_all ? fail_all : users[i].bounced);
			ret++;
		}
	}
#ifdef ERROR_MESSAGES
	if(!i)
		fprintf(stderr,"Warning: bounce() called with no users\n");
#endif

	return ret;
}
