/* 
 * $Log: do_list.c,v $
 * Revision 1.22  1996/05/05 19:06:38  tjd
 * fixed off-by-one error when showing status.
 *
 * Revision 1.21  1996/05/05 19:05:35  tjd
 * errant logic when copying for a new host caused 'curhost' to get
 * corrupted.  fixed.
 *
 * Revision 1.20  1996/05/04 21:24:23  tjd
 * small fixup for addresses ending in :
 *
 * Revision 1.19  1996/05/04 20:50:17  tjd
 * major code restructuring:
 * moved parsing code to parse_address
 * moved scheduler to schedule()
 * misc fixups.
 *
 * Revision 1.18  1996/05/04 18:41:27  tjd
 * STATUS is no longer optional, it must be defined.
 * also minor cleanups.
 *
 * Revision 1.17  1996/05/02 22:39:41  tjd
 * removed all dependencies on useful.h
 *
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

#include "mailer_config.h"
#include "userlist.h"

static char curhost[MAX_HOSTNAME_LEN+1];  /* +1 for null */
static char buf[BUFFER_LEN+1];		  /* +1 for temp newline (null) */
static userlist users[ADDRS_PER_BUF+1];	  /* +1 for null marker at end */

static int numchildren=0,delivery_rate=TARGET_RATE;
static int numforks=0,numprocessed=0,numfailed=0;

int deliver(char *hostname,userlist users[]);
void bounce_user(char *addr,bounce_reason why,int statcode);
void handle_sig(int sig),signal_backend();
static int parse_address(FILE *f, char **abuf, char **start, char **host);
static void do_delivery(),do_status(),setup_signals(),schedule();
static int handle_child();

void do_list(char *fname)
{
	FILE *f;

	int inbuf,hostlen,wait_timeout,fd,next_status=STATUS;
	char *next,*addr,*host;

	if(!(f=fopen(fname,"r")))
	{
		perror("Can't open list file");
		exit(1);
	}

	if((fd=open(BOUNCE_FILE,O_CREAT|O_TRUNC|O_WRONLY,0666)) == -1)
	{
		perror("Can't create bounce file");
		exit(1);
	}
	close(fd);

	do_status();
	setup_signals();

	/* main loop.
	 * next holds next empty buffer position
	 * addr points to start of current address
	 * host points to start of hostname of current address
	 * hostlen is the length of the hostname
	 */
	next=buf;
	inbuf=0;

	while((hostlen=parse_address(f,&next,&addr,&host)))
	{
		if(numprocessed >= next_status)
		{
			do_status();
			next_status+=STATUS;
		}

		if(!inbuf)	/* empty buffer */
		{
			strncpy(curhost,host,hostlen);
			curhost[hostlen]='\0';
		}
		else
		{
			if(strncasecmp(curhost,host,hostlen))
			{	
				/* new host.  deliver what we have,
				 * then copy current entry to start of buffer.
				 */

				users[inbuf].addr=NULL;
				do_delivery();

				strncpy(curhost,host,hostlen);
				curhost[hostlen]='\0';
				memmove(buf,addr,(next-addr));
				next=buf+(next-addr);
				users[0].addr=buf;
				inbuf=1;
				continue;
			}
		}

		users[inbuf++].addr=addr;

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

	/* out of the loop, we're done with the list.
         * however, we could still have some addresses to deliver to.
	 */

	if(inbuf)
	{
		users[inbuf].addr=NULL;
		do_delivery();
	}

	do_status();

	/* loop and wait for the children to exit.  */
	wait_timeout=0;
	while(numchildren && ++wait_timeout < 720) /* wait an hour */
	{
		sleep(5);
		handle_child();
	}
	do_status();

	if(numchildren)
		fprintf(stderr,"WARNING: %d children did not exit!\n",numchildren);
	fclose(f);
}

/* reads the file and parses the addresses.
 * input: file pointer, pointer to buffer.
 * returns: 0 if no more addresses, non-zero otherwise
 * also returns:	pointer to start of address,
 * 			pointer to hostname
 *			length of hostname (return value)
 * and advances abuf past NULL at end of current address.
 */

static int parse_address(FILE *f, char **abuf, char **start, char **host)
{
	char *p,*a_buf,*a_start,*a_end,*a_host;	/* temporary pointers */
	int hostlen;

	a_buf=*abuf;
	while(1)
	{
		if(!fgets(a_buf,MAX_ADDR_LEN+2,f))	/* no more addresses */
			return 0;

		++numprocessed;
		a_start=a_buf;

		/* find the end of the address */
		p=a_buf+strlen(a_buf)-1;

		/* see if we have a newline at the end, and replace it
		 * with a null.  If we don't have a newline the address
		 * was too long, so we skip it.
		 */

		if(*p != '\n') {
			char c;
			bounce_user(a_start,long_addr,0);
			numfailed++;
			while((c=fgetc(f)) != '\n') { 
				if(feof(f))
					return 0;	
			}
			continue;
		}

		/* strip trailing whitespace */
		while(isspace(*p) && p>=a_buf) --p;

		++p;
		if(p==a_buf) {				/* blank line */
			numprocessed--;
			continue;
		}

		/* strip leading whitespace */
		*p='\0';
		a_end=p;
		while(isspace(*a_start)) ++a_start;

		/* at this point:
		 * a_start points to the start of the address
		 * a_end points to the null at the end of the address
		 * we parse the address, and set a_host to point to the
		 * hostname, hostlen to be its length.
		 */

		/* check for source routed path */
		if(*a_start=='@')
		{
			a_host=a_start+1;	/* discard @ */

			if(!(p=strchr(a_start,':'))) /* check for : */
			{
				bounce_user(a_start,bad_addr,0);
				numfailed++;
				continue;
			}
			/* path continues either until the : or a , */

			p=strpbrk(a_start,",:");	/* can't return NULL */
			hostlen=p-a_host;	/* save length of host part */
		}
		else
		{
			/* user@host:
			 * find the '@' to split into user/host.
			 * host points at the host part.
			 */

			if(!(a_host=strrchr(a_start,'@')))
			{
				bounce_user(a_start,bad_addr,1);
				numfailed++;
				continue;
			}

			++a_host;
			hostlen=a_end-a_host;
		}

		if(hostlen>MAX_HOSTNAME_LEN)
		{
			bounce_user(a_start,long_host,hostlen);
			numfailed++;
			continue;
		}
		if(!hostlen || *(a_end-1) == ':')
		{
			bounce_user(a_start,bad_addr,2);
			numfailed++;
			continue;
		}
		break;	/* got a valid address */
	}
	/* copy return values and exit */
	*start=a_start; *host=a_host; *abuf=a_end+1;
	return hostlen;
}

/* forks and calls deliver() to deliver the messages.
 */

static void do_delivery()
{
#ifndef NO_FORK
	schedule();	/* blocks until we can start another */

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
			numforks++;
			numchildren++;
	}
#else /* NO_FORK */
	numfailed+=deliver(curhost,users);
#endif /* NO_FORK */
}

/* waits for children and returns the number that have exited */
static int handle_child()
{
	int w,status,numexited=0;

	while((w=waitpid(-1,&status,WNOHANG)) > 0)
	{	
		numchildren--; numexited++;
		if(WIFEXITED(status))
			numfailed+=WEXITSTATUS(status);
#ifdef ERROR_MESSAGES
		if(WIFSIGNALED(status))
			fprintf(stderr,"Warning: child exited on signal %d\n",WTERMSIG(status));
#endif
	}
#ifdef ERROR_MESSAGES
	if(w==-1 && errno != ECHILD) perror("waitpid");
#endif
	return numexited;
}

/* returns when it's ok to start another child process.
 */

static void schedule()
{
	static int child_limit=MIN_CHILD;
	static int mc_factor=1;	
	int numexited;

	while(numchildren >= child_limit)
	{
		sleep(2);
		numexited=handle_child();

		/* mc_factor is a scheduling parameter that controls how
		 * likely we are to start a new child.  We try to
		 * keep within 5% of our target.
		 */

		if(delivery_rate < (int)(0.95 * TARGET_RATE))
			mc_factor=2;
		else if(delivery_rate > (int)(1.05 * TARGET_RATE))
			mc_factor=0;
		else
			mc_factor=1;

		/* we want an average of 1 child every 2 seconds, so we
		 * try to make that happen.
		 */

		if(numexited==0) {
			child_limit++;
			if(child_limit > MAX_CHILD) child_limit=MAX_CHILD;
		}
		else if(numexited <= mc_factor) {
			/* do nothing */
			}
		else {
			child_limit--;
			if(child_limit < MIN_CHILD) child_limit=MIN_CHILD;
		}
	}
}

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

void signal_backend()
{
	do_status();
	exit(1);
}

static void setup_signals()
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
