/* 
 * $Log: do_list.c,v $
 * Revision 1.16  1996/05/02 22:20:19  tjd
 * moved signal code to setup_signals()
 *
 * Revision 1.15  1996/05/02 22:12:59  tjd
 * removed some old signal code.
 *
 * Revision 1.14  1996/05/02 22:09:55  tjd
 * removed DEBUG, DEBUG_FORK
 *
 * Revision 1.13  1996/04/17 14:47:39  tjd
 * more scheduler tning; decreased tolerance band to 5%
 *
 * Revision 1.12  1996/04/16 14:58:58  tjd
 * tuned the scheduler; we now have a TARGET_RATE and the scheduler
 * dynamically adapts to try to meet it.
 *
 * Revision 1.11  1996/04/16 04:57:13  tjd
 * added a scheduler!
 *
 * Revision 1.10  1996/03/04 15:00:14  tjd
 * bracketed signal warnings with ERROR_MESSAGES
 *
 * Revision 1.9  1996/02/12 00:49:37  tjd
 * added some #includes to support open().
 *
 * Revision 1.8  1996/02/12 00:42:29  tjd
 * added check to create BOUNCE_FILE
 *
 * Revision 1.7  1996/01/02 04:30:43  tjd
 * added SMTP status code to bounce messages
 *
 * Revision 1.6  1996/01/02  00:29:11  tjd
 * added signal handling code
 *
 * Revision 1.5  1996/01/01  22:04:19  tjd
 * changed memmove() to bcopy() for sun
 *
 * Revision 1.4  1995/12/28  18:09:01  tjd
 * fixed placement of wait_timeout, argh.
 *
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
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include "useful.h"
#include "mailer_config.h"
#include "userlist.h"

int deliver(char *hostname,userlist users[]);
void bounce_user(char *addr,bounce_reason why,int statcode);
void handle_sig(int sig);

static char curhost[MAX_HOSTNAME_LEN+1];  /* +1 for null */
static char buf[BUFFER_LEN+1];		  /* +1 for temp newline (null) */
static userlist users[ADDRS_PER_BUF+1];	  /* +1 for null marker at end */

static int numchildren=0,child_limit=MIN_CHILD,delivery_rate=TARGET_RATE;

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
	int len,h,m,s;

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

		fprintf(sf,"%s: p=%d n=%d d=%d f=%d/%2.1f%% t=%02d:%02d:%02d r=%d\n",
		timebuf,numforks,numchildren,numprocessed,numfailed,
		((float)numfailed/(float)numprocessed)*100.0,
		h,m,s,delivery_rate=(int)(numprocessed/((float)diff/3600.0)));
	}
	fflush(sf);
}
#endif

void signal_backend()
{
#ifdef STATUS
	do_status();
#endif
	exit(1);
}

void setup_signals()
{
	int i;

	/* catch all signals not explicitly named */

	for(i=1;i<NSIG;++i)
	{
		switch(i) {
			case SIGKILL: case SIGSTOP: case SIGTSTP:
			case SIGCONT: case SIGALRM:
#ifdef SIGWINCH
			case SIGWINCH:
#endif
#ifdef SIGCHLD
			case SIGCHLD:
#else
#ifdef SIGCLD
			case SIGCLD:
#endif
#endif
					break;	/* from switch, cont loop */
			default:
				if((int)signal(i,handle_sig)==-1)
				{
					sprintf(buf,"signal(%d)",i);
					perror(buf);
				}
				break;
		}
	}
}

void do_list(char *fname)
{
	FILE *f;

	int inbuf,tmplen,wait_timeout,i;
	char *current,*start,*next,*user,*tmphost,*p;

	if(!(f=fopen(fname,"r")))
	{
		perror("Can't open list file");
		exit(1);
	}

	if((i=open(BOUNCE_FILE,O_CREAT|O_TRUNC|O_WRONLY,0666)) == -1)
	{
		perror("Can't create bounce file");
		exit(1);
	}
	close(i);

	next=buf;
	inbuf=0;

#ifdef STATUS
	do_status();
#endif
	setup_signals();

	while(fgets(next,MAX_ADDR_LEN+2,f))	/* newline + null */
	{
		++numprocessed;
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
			bounce_user(start,long_addr,0);
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
				bounce_user(start,bad_addr,0);
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
				bounce_user(start,bad_addr,1);
				numfailed++;
				next=current;
				continue;
			}

			++tmphost;
			tmplen=(next-tmphost)-1;
		}

		if(tmplen>MAX_HOSTNAME_LEN)
		{
			bounce_user(start,long_host,tmplen);
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
#ifdef sun
				bcopy(save,buf,savelen+1);
#else
				memmove(buf,save,savelen+1);
#endif
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

#ifdef STATUS
	do_status();
#endif

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
	while(numchildren >= child_limit)
	{
		int old_numch=numchildren;
		int mc_factor=1;

		sleep(2);
		handle_child();

		/* mc_factor is a scheduling parameter that controls how
		 * likely we are to start a new child.  We try to
		 * keep within 10% of our target.
		 */

		if(delivery_rate < (int)(0.95 * TARGET_RATE))
			mc_factor=2;
		else if(delivery_rate > (int)(1.05 * TARGET_RATE))
			mc_factor=0;
		else
			mc_factor=1;

		/* this is the "scheduler".
		 * we want an average of 1 child every 2 seconds, so we
		 * try to make that happen.
		 */
		if(old_numch==numchildren) {
			child_limit++;
			if(child_limit > MAX_CHILD) child_limit=MAX_CHILD;
		}
		else if(old_numch-mc_factor <= numchildren) {
			/* do nothing */
		}
		else {
			child_limit--;
			if(child_limit < MIN_CHILD) child_limit=MIN_CHILD;
		}
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

	while((w=waitpid(-1,&status,WNOHANG)) > 0)
	{	
		numchildren--; 
#ifdef STATUS
		if(WIFEXITED(status))
			numfailed+=WEXITSTATUS(status);
#endif
#ifdef ERROR_MESSAGES
		if(WIFSIGNALED(status))
			fprintf(stderr,"Warning: child exited on signal %d\n",WTERMSIG(status));
#endif
	}
#ifdef ERROR_MESSAGES
	if(w==-1 && errno != ECHILD) perror("waitpid");
#endif
}
