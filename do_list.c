/* 
 * $Log: do_list.c,v $
 * Revision 1.3  1995/12/28 18:06:58  tjd
 * added timeout at end of loop waiting for children
 *
 * Revision 1.2  1995/12/27 17:50:06  tjd
 * added WIFSIGNALED check
 *
 * Revision 1.1  1995/12/14 15:39:06  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include "useful.h"
#include "mailer_config.h"
#include "userlist.h"

int deliver(char *hostname,userlist users[]);
void bounce_user(char *addr,bounce_reason why);

static char curhost[MAX_HOSTNAME_LEN+1];  /* +1 for null */
static char buf[BUFFER_LEN+1];		  /* +1 for temp newline (null) */
static userlist users[ADDRS_PER_BUF+1];	  /* +1 for null marker at end */

static int numchildren=0;

static void do_delivery();
static void handle_child();

#ifdef STATUS
static int numforks=0,numprocessed=0,numfailed=0;

static void do_status()
{
	static FILE *sf=NULL;
	static time_t start=0;
	char timebuf[80];
	time_t now,diff;
	int len,h,m,s,wait_timeout;

	if(sf==NULL)
	{
		if(!(sf=fopen("mailer.status","w")))
		{
			perror("Can't open mailer.status for writing");
			exit(1);
		}
	}
	now=time(NULL);
	if(start==0) start=now;

	/* get rid of newline from ctime() */

	strcpy(timebuf,ctime(&now));
	if((len=strlen(timebuf)))
		timebuf[len-1]='\0';

	if(numprocessed==0)
	{
		fprintf(sf,"%s: Starting list processing.\n",timebuf);
	}
	else
	{
		diff=now-start;

		h=diff/3600;
		m=(diff%3600)/60;
		s=(diff%60);

		fprintf(sf,"%s: p=%d d=%d f=%d (%2.1f%%) ET %02dh%02dm%02ds (%d/hr)\n",
		timebuf,numforks,numprocessed,numfailed,
		((float)numfailed/(float)numprocessed)*100.0,
		h,m,s,(int)(numprocessed/((float)diff/3600.0)));
	}
	fflush(sf);
}
#endif

void do_list(char *fname)
{
	FILE *f;

	int inbuf,tmplen;
	char *current,*start,*next,*user,*tmphost,*p;

#if 0
	struct sigaction sa;

	sa.sa_handler=handle_child;
	sigfillset(&sa.sa_mask);
	sa.sa_flags=SA_RESTART;
	sa.sa_restorer=NULL;

	if(sigaction(SIGCHLD,sa,NULL) == -1)
	{
		perror("sigaction");
		exit(1);
	}
#endif

	if(!(f=fopen(fname,"r")))
	{
		perror("Can't open list file");
		exit(1);
	}

	next=buf;
	inbuf=0;

#ifdef STATUS
	do_status();
#endif

	while(fgets(next,MAX_ADDR_LEN+2,f))	/* newline + null */
	{
		++numprocessed;
#ifdef DEBUG
		fprintf(stderr,"Address %d: %s",numprocessed,next);
#endif
#ifdef STATUS
		if(!(numprocessed % STATUS))
		{
			do_status();
		}
#endif
		/* this is tricky.  everything is really in buf.
		 * next points to where we just read the address.
		 */

		current=start=next;

		/* find the end of the address */
		p=next+strlen(next)-1;

		/* see if we have a newline at the end, and replace it
		 * with a null.  If we don't have a newline the address
		 * was too long, so we skip it.
		 */

		if(*p != '\n') {
			char c;
			bounce_user(start,long_addr);
			numfailed++;
			while((c=fgetc(f)) != '\n') { 
				if(feof(f))
				break;
			}
			next=current;
			continue;
		}

		while(isspace(*p) && p>=next) --p;	/* strip whitespace */

		++p;
		if(p==next) {				/* blank line */
			numprocessed--;
			continue;
		}
		*p='\0';
		next=p+1;
		while(isspace(*start)) ++start;

		/* check for source routed path */
		if(*start=='@')
		{
			user=start;
			tmphost=start+1;	/* discard @ */

			if(!(p=strchr(start,':')))
			{
				bounce_user(start,bad_addr);
				numfailed++;
				next=current;
				continue;
			}
			/* path continues either until the : or a , */

			p=strpbrk(start,",:");	/* can't return NULL */
			tmplen=p-tmphost;	/* save length of host part */
		}
		else
		{
			/* user@host:
			 * find the '@' to split into user/host.
			 * tmphost points at the host part.
			 */

			user=start;

			if(!(tmphost=strrchr(start,'@')))
			{
				bounce_user(start,bad_addr);
				numfailed++;
				next=current;
				continue;
			}

			++tmphost;
			tmplen=(next-tmphost)-1;
		}

#ifdef DEBUG
		fprintf(stderr,"\tuser=%s host=%s\n",user,tmphost);
#endif

		if(tmplen>MAX_HOSTNAME_LEN)
		{
			bounce_user(start,long_host);
			numfailed++;
			next=current;
			continue;
		}

		if(!inbuf)	/* new host */
		{
			strncpy(curhost,tmphost,tmplen);
			curhost[tmplen]='\0';
		}
		else
		{
			if(strncasecmp(curhost,tmphost,tmplen))
			{
				char *save;
				int savelen,hostpos,userpos;

				/* new host, deliver and save this one */
				users[inbuf].addr=NULL;
				save=start;
				savelen=next-start;
				hostpos=tmphost-start;
				userpos=user-start;
				
				do_delivery();

				memmove(buf,save,savelen+1);
				strncpy(curhost,buf+hostpos,tmplen);
				curhost[tmplen]='\0';
				users[0].addr=buf+userpos;
				inbuf=1;
				next=buf+savelen;
				continue;
			}
		}

		users[inbuf++].addr=user;

		if(inbuf == ADDRS_PER_BUF ||
		   (next-buf) >= (BUFFER_LEN-MAX_ADDR_LEN) ||
		   feof(f))
		{
			users[inbuf].addr=NULL;
			do_delivery();
			inbuf=0;
			next=buf;
		}
	}

	/* out of the loop, we're done!!!  feof() above won't catch this. */
	if(inbuf)
	{
		users[inbuf].addr=NULL;
		do_delivery();
	}

	wait_timeout=0;
	while(numchildren && ++wait_timeout < 720) /* wait an hour */
	{
		sleep(5);
		handle_child();
	}
#ifdef STATUS
	do_status();
#endif

	if(numchildren)
		fprintf(stderr,"WARNING: %d children did not exit!\n",numchildren);
}

static void do_delivery()
{
#ifdef DEBUG
	fprintf(stderr,"do_delivery (%s)\n",curhost);
#endif
	while(numchildren >= MAX_CHILD)
	{
		sleep(1);
		handle_child();
	}

#ifndef NO_FORK
retryfork:
	switch(fork())
	{
		case -1:
#ifdef ERROR_MESSAGES
			perror("fork");
#endif
			goto retryfork;
		case 0:
			exit(deliver(curhost,users));

		default:
#ifdef STATUS
			numforks++;
#endif
			numchildren++;
	}
#else
#ifdef STATUS
	numfailed+=
#endif
		deliver(curhost,users);
#endif
}

static void handle_child()
{
	int w,status;
	
#ifdef DEBUG_FORK
	fprintf(stderr,"handle_child(): num=%d\n",numchildren);
#endif
	while((w=waitpid(-1,&status,WNOHANG)) > 0)
	{	
		numchildren--; 
#ifdef DEBUG_FORK
		fprintf(stderr,"got a child, num=%d\n",numchildren);
#endif
#ifdef STATUS
		if(WIFEXITED(status))
			numfailed+=WEXITSTATUS(status);
#endif
		if(WIFSIGNALED(status))
			fprintf(stderr,"Warning: child exited on signal %d\n",WTERMSIG(status));
	}
#ifdef ERROR_MESSAGES
	if(w==-1 && errno != ECHILD) perror("waitpid");
#endif
}
