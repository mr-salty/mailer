/*
 * $Log: bounce.c,v $
 * Revision 1.3  1996/01/02 04:30:43  tjd
 * added SMTP status code to bounce messages
 *
 * Revision 1.2  1996/01/02  00:28:11  tjd
 * added defug code to check for null userlist
 *
 * Revision 1.1  1995/12/14  15:23:30  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include "userlist.h"

void bounce_user(char *addr,bounce_reason why,int statcode)
{
	char *reasons[]=BOUNCE_REASONS;

	fprintf(stderr,"BOUNCE_USER: %s [%03d %s]\n",addr,statcode,reasons[why]);
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
