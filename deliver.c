/*
 * $Log: deliver.c,v $
 * Revision 1.16  1996/05/02 19:07:00  tjd
 * rewrote all of the debug code.
 *
 * Revision 1.15  1996/04/15 16:36:42  tjd
 * added CONNECT_TIMEOUT for tcp connect() timeout
 * since linux's timeout is excruciatingly long...
 *
 * Revision 1.14  1996/03/21 19:27:43  tjd
 * added NULL_RETURN_PATH define
 *
 * Revision 1.13  1996/03/04 15:00:14  tjd
 * bracketed signal warnings with ERROR_MESSAGES
 *
 * Revision 1.12  1996/01/02 06:14:39  tjd
 * fix for braindead ultrix headers
 *
 * Revision 1.11  1996/01/02 04:30:43  tjd
 * added SMTP status code to bounce messages
 *
 * Revision 1.10  1996/01/02  03:57:42  tjd
 * call inet_addr if gethostbyname() fails
 *
 * Revision 1.9  1996/01/02  03:37:32  tjd
 * added more checks for valid SMTP response- digit digit digit space
 *
 * Revision 1.8  1996/01/02  01:19:33  tjd
 * missed a )
 *
 * Revision 1.7  1996/01/02  01:12:26  tjd
 * allowed 250 Ok response where 354 is expected to accomidate broken sendmails
 *
 * Revision 1.6  1996/01/02  00:34:47  tjd
 * minor change to smtp_write for DATA and QUIT
 *
 * Revision 1.5  1996/01/02  00:29:11  tjd
 * added signal handling code
 *
 * Revision 1.4  1995/12/31  20:14:42  tjd
 * added 550/551/generic user unknown indication
 *
 * Revision 1.3  1995/12/27 18:56:45  tjd
 * fixed problem where messagebody was copied into a buffer
 * that was probably too small to contain it.
 *
 * Revision 1.2  1995/12/27 18:05:44  tjd
 * added NO_DELIVERY
 *
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

#include <sys/types.h>
#ifndef ultrix
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <ctype.h>

#include "sendmail.h"
#include "mailer_config.h"
#include "userlist.h"

int getmxrr(char *,char **,bool,int *);
int bounce(userlist users[],bounce_reason fail_all);

#define SMTP_PORT 	25
#define TMPBUFLEN	(MAX_ADDR_LEN+80)

static char buf[TMPBUFLEN];
static int last_status;		/* status of last lookfor() */
extern char *messagebody,*myhostname,*mailfrom;
extern int messagebody_size;


static int delivermessage(char *addr,char *hostname, userlist users[]);
static int lookfor(int s,int code,int alarmtime);
static int smtp_write(int s,int close_s,char *fmt, char *arg,int look,int timeout);

static int in_child=0;
extern void signal_backend();
static userlist *gl_users=NULL;

#ifdef DEBUG_SMTP
static char *hostname2;	/* filled in by deliver() */
#endif

#include <stdarg.h>
void debug(char *format,...) {
#ifdef DEBUG_SMTP
    static FILE *fpLog = 0;
    char dlog[MAX_HOSTNAME_LEN+20];
    va_list ap;
    va_start(ap,format);

    if(!format) {
	if(fpLog) fclose(fpLog);
    }
    else {
	if (!fpLog) {
		sprintf(dlog,"debug/D%s.%05d",hostname2,getpid());
		fpLog = fopen(dlog,"w");
	}
    	if(fpLog) {
		vfprintf(fpLog,format,ap);
	}
    }
    va_end(ap);
#endif
}

void handle_sig(int sig)
{
	if(in_child)
	{
		debug("Warning: pid %d caught signal %d, failing.\n",getpid(),sig);
		debug(NULL);
		exit(bounce(gl_users,(caught_signal<<16) | sig));
	}
	else
	{
		fprintf(stderr,"FATAL: parent mailer caught signal %d, exiting.\n",sig);
		signal_backend();
		exit(1);
	}
}

/* finds all the mx records and tries to deliver the message */

int deliver(char *hostname,userlist users[])
{
	struct hostent *host;
	int nmx,rcode,i,p,deliver_status=0,taddr;
	
	char *mxhosts[MAXMXHOSTS+1];
	in_child=1;
	gl_users=users;
#ifdef DEBUG_SMTP
	hostname2=hostname;
#endif
	debug("Hostname: %s\n",hostname);

	nmx=getmxrr(hostname,mxhosts,FALSE,&rcode);
	debug("Number of MX records: %d\n",nmx);

	if (nmx<=0)
	{
		nmx=1;
		mxhosts[0]=hostname;
	}

	for(i=0;i<nmx;++i)
	{
		/* try to deliver to each host */
		debug("MX %d: %s\n",i,mxhosts[i]);

		if(!(host=gethostbyname(mxhosts[i])))
		{
			if((taddr=inet_addr(mxhosts[i]) != -1))
			{
				deliver_status=delivermessage((char *)&taddr,hostname,users);
			}
			else
			{
				debug("gethostbyname() failed\n");
				deliver_status=-1;
			}
		}
		else
		{
			for(p=0;host->h_addr_list[p];++p)
			{
				if((deliver_status=delivermessage(host->h_addr_list[p],hostname,users)) != -1)
					break;
			}
		}
		if(deliver_status != -1) break;
	}

	debug("Finished with status %d.\n",deliver_status);
	debug(NULL);
	switch(deliver_status)
	{
		case -1:	return bounce(users,(host_failure<<16));
		case 0:		return 0;
		case -2:	
		default:	return bounce(users,0);	
	}
}

jmp_buf jmpbuf; /* for alarm */
static void handle_alarm(int dummy);

/* called by above once we have a host address to try 
 *
 * return value is:
 *
 * -1: this MX failed, but keep trying
 * -2: don't try more MX's, no valid recipients
 * >=0: OK (number of failures)
 *
 */

static int delivermessage(char *addr,char *hostname, userlist users[])
{
	int s,p;
	struct sockaddr_in sock;
	int rcpt_fail;

#ifndef NO_DELIVERY
	sock.sin_family=AF_INET;
	sock.sin_port=htons(SMTP_PORT);
	bcopy(addr,&sock.sin_addr,sizeof sock.sin_addr);

	if((s=socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		debug("socket: errno=%d\n",errno);
		return -1;
	}

	if(signal(SIGALRM,handle_alarm)==SIG_ERR)
	{
		debug("signal (connect alarm): errno %d\n",errno);
		return -1;
	}

	if(setjmp(jmpbuf))
	{
		debug("Timed out during connect()\n");
		alarm(0);
		return -1;
	}
	alarm(CONNECT_TIMEOUT);

	if(connect(s,(struct sockaddr *)&sock,sizeof(struct sockaddr_in)) == -1)
	{
		alarm(0);
		debug("connect() failed: errno %d\n",errno);
		close(s);
		return -1;
	}
	alarm(0);
	debug("SMTP: Connected.\n");
#endif /* NO_DELIVERY */

	if(smtp_write(s,1,NULL,NULL,220,SMTP_TIMEOUT_WELCOME)) 
		return -1;

	if(smtp_write(s,1,"HELO %s\r\n",myhostname,250,SMTP_TIMEOUT_HELO))
		return -1;
#ifdef NULL_RETURN_PATH
	if(smtp_write(s,1,"MAIL FROM:<>\r\n",NULL,250,SMTP_TIMEOUT_MAIL))
#else
	if(smtp_write(s,1,"MAIL FROM:<%s>\r\n",mailfrom,250,SMTP_TIMEOUT_MAIL))
#endif /* NULL_RETURN_PATH */
		return -1;

	rcpt_fail=0;

	for(p=0;users[p].addr;++p)
	{
		users[p].statcode=0;

		switch(smtp_write(s,0,"RCPT TO:<%s>\r\n",users[p].addr,251,SMTP_TIMEOUT_RCPT))
		{
	
			case -1:	return -1;

			case 1:
					debug("failing %s with status %d\n",users[p].addr,last_status);
					users[p].statcode=(unk_addr<<16);
					users[p].statcode |= last_status;
					rcpt_fail++;
					break;
		}
	}
	if(p==rcpt_fail) return -2;	/* no valid recipients */

	if(smtp_write(s,1,"%s","DATA\r\n",354,SMTP_TIMEOUT_DATA))
		return -1;

	if(smtp_write(s,1,messagebody,NULL,250,SMTP_TIMEOUT_END))
		return -1;

	/* The message is now committed, methinks.  But we wait anyways */

	if(smtp_write(s,1,"%s","QUIT\r\n",221,SMTP_TIMEOUT_END))
		return rcpt_fail;

	close(s);

	return rcpt_fail;
}

/* returns:	 0 for ok
 *		-1 if the write failed		(always close s)
 *		 1 if the lookfor failed	(close s if close_s is true)
 *			[last_status will be set]
 *
 * if fmt is NULL we don't do the write, just lookfor.
 * if arg is NULL we don't sprintf, but use fmt as the pointer.
 */

static int smtp_write(int s,int close_s,char *fmt, char *arg,int look,int timeout)
{
	char *wbuf;

	if(fmt != NULL)
	{
		if(arg != NULL) {
			sprintf(buf,fmt,arg);
			wbuf=buf;
			debug("%s",wbuf);
		} else {
			wbuf=fmt;
			debug("SMTP: [Message body omitted]\n");
		}

#ifndef NO_DELIVERY
		if(write(s,wbuf,strlen(wbuf)) == -1)
		{
			debug("write: errno %d\n",errno);
			close(s);
			return -1;
		}
	}

	if(!lookfor(s,look,timeout)) {
		if(close_s) close(s);
		return 1;
#endif /* NO_DELIVERY */
	}
	return 0;
}

static int lookfor(int s,int code,int alarmtime)
{
	int len;
	char *ptr,*t;

	buf[0]='\0';

restart:
	if(signal(SIGALRM,handle_alarm)==SIG_ERR)
	{
		debug("signal (alarm): errnot %d\n",errno);
		return 0;
	}

	if(setjmp(jmpbuf))
	{
		debug("Timed out looking for %d\n",code);
		alarm(0);
		return 0;
	}
	alarm(alarmtime);
	
	if((len=read(s,buf,TMPBUFLEN-1))<=0)
	{
		alarm(0);
		debug("read (SMTP connection): errno %d\n",errno);
		return 0;
	}
	alarm(0);

	ptr=buf;
	buf[len]='\0';
	debug("%s",buf);

	while(!isdigit(ptr[0]) || !isdigit(ptr[1]) ||
	      !isdigit(ptr[2]) || ptr[3] != ' ')
	{
		if((t=strchr(ptr,'\n')) == NULL)
			goto restart;
		else {
			ptr=t+1;
		}
	}		

	last_status=atoi(buf);

	debug("SMTP: got code %d\n",last_status);

	if(code == last_status || (code==251 && last_status==250) || (code==354 && last_status == 250)) /* special cases */
		return 1;
	else
	{
		debug("Error: expected %d got %d\n",code,last_status);
		return 0;
	}
}

static void handle_alarm(int dummy)
{
	longjmp(jmpbuf,1);
}
