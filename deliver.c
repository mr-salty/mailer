/*
 * $Log: deliver.c,v $
 * Revision 1.26  2000/03/08 14:04:35  tjd
 * roll back 1.25 change
 *
 * Revision 1.24  1999/09/07 20:16:58  tjd
 * fix handling of continued lines in responses
 * - atoi() was operating on the whole buffer instead of the ptr where we
 *   found the response code.
 *
 * fix handling of partial read() in responses:
 * - copy buffer data after the last newline to the start of the buffer and
 *   append to it when we read.  So i.e. "22" followed by "0 " will parse
 *   correctly as "220 ".
 * - make sure while we're doing this that we don't loop due to a line
 *   being too long for our buffer.
 *
 * Revision 1.23  1998/04/17 00:37:10  tjd
 * changed config file format
 * added config flags and associated definitions
 * changed tagging; added tagging on SMTP id and in the body
 * changed Message-Id to look like the other tags (no time(NULL))
 * removed NULL_RETURN_PATH (bad feature)
 *
 * Revision 1.22  1998/02/17 18:04:04  tjd
 * improved signal handling for INT/TERM/HUP, parent will wait for children
 * to exit and try to kill them if they don't
 *
 * Revision 1.21  1997/10/11 07:08:11  tjd
 * added support for mailer config file for debugging, batching, and setting
 * some parameters.
 * also fixed the computation of the batch_id
 *
 * Revision 1.20  1997/07/05 18:57:28  tjd
 * added selective debug facility
 *
 * Revision 1.19  1997/07/05 18:20:54  tjd
 * fixed misplaced paren when we call inet_addr()
 *
 * Revision 1.18  1996/05/02 22:52:59  tjd
 * removing dependencies on sendmail header files.
 * sendmail.h is now the only one and only needed by domain.c
 *
 * Revision 1.17  1996/05/02 22:04:13  tjd
 * more debug cleanups
 *
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <sys/types.h>
#ifndef ultrix
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <netdb.h>
#include <netinet/in.h>

#include "mailer_config.h"
#include "userlist.h"

int getmxrr(char *,char **,char,int *);
int bounce(userlist users[],bounce_reason fail_all);

#define SMTP_PORT 	25
#define TMPBUFLEN	(MAX_ADDR_LEN+80)

static char buf[TMPBUFLEN];
static int last_status;		/* status of last lookfor() */
extern char *messagebody,*myhostname,*mailfrom;
extern int messagebody_size;

#if defined(USE_IDTAGS) && defined(TWEAK_BODY)
extern char *g_body_idptr;
#endif /* USE_IDTAGS */

static int delivermessage(char *addr,char *hostname, userlist users[]);
static int lookfor(int s,int code,int alarmtime);
static int smtp_write(int s,int close_s,char *fmt, char *arg,int look,int timeout);

static int in_child=0;
extern void signal_backend(int sig);
static userlist *gl_users=NULL;
static flags_t flags;

#ifdef DEBUG_SMTP
static char *hostname2;	/* filled in by deliver() */
#endif

#include <stdarg.h>
void debug(char *format,...) {
#ifdef DEBUG_SMTP
    static FILE *fpLog = NULL;
    static int go_away = 0;
    char dlog[MAX_HOSTNAME_LEN+20];
    va_list ap;
    va_start(ap,format);

    if(go_away) {
	return;
    }

    if (fpLog == NULL) {
	if(!FLAG_ISSET(flags,FL_DEBUG)) {
	    go_away = 1;
	    return;
	}

	sprintf(dlog,"debug/D%s.%05d",hostname2,getpid());
	fpLog = fopen(dlog,"w");
	if(fpLog == NULL) {
	    go_away = 1;
	    return;
	}
    }
    vfprintf(fpLog,format,ap);

    va_end(ap);
#endif
}

void handle_sig(int sig)
{
	if(in_child)
	{
		debug("SMTP: Warning: pid %d caught signal %d, failing.\n",getpid(),sig);
		exit(bounce(gl_users,(caught_signal<<16) | sig));
	}
	else
	{
		fprintf(stderr,"FATAL: parent mailer caught signal %d, exiting.\n",sig);
		signal_backend(sig);
		exit(1);
	}
}

/* finds all the mx records and tries to deliver the message */

int deliver(char *hostname,userlist users[], flags_t in_flags)
{
	struct hostent *host;
	int nmx,rcode,i,p,deliver_status=0,taddr;
	time_t start_t;
	
	char *mxhosts[MAXMXHOSTS+1];
	in_child=1;
	gl_users=users;

	flags = in_flags;
#ifdef DEBUG_SMTP
	hostname2=hostname;
#endif
	debug("Hostname: %s\n",hostname);

	start_t=time(NULL);
	nmx=getmxrr(hostname,mxhosts,0,&rcode);
	debug("Number of MX records: %d\n",nmx);

	if (nmx<=0)
	{
		nmx=1;
		mxhosts[0]=hostname;
	}

	for(i=0;i<nmx;++i)
	{
		/* try to deliver to each host */
		debug("------------------------------------------------------------------------------\nSMTP: MX %d: %s\n",i,mxhosts[i]);

		if(!(host=gethostbyname(mxhosts[i])))
		{
			if((taddr=inet_addr(mxhosts[i])) != -1)
			{
				deliver_status=delivermessage((char *)&taddr,hostname,users);
			}
			else
			{
				debug("SMTP: gethostbyname() failed\n");
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

	debug("SMTP: Finished. Status=%d ET=%ds.\n",deliver_status, time(NULL)-start_t);
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
		debug("SMTP: socket: errno %d\n",errno);
		return -1;
	}

	if(signal(SIGALRM,handle_alarm)==SIG_ERR)
	{
		debug("SMTP: signal (connect alarm): errno %d\n",errno);
		return -1;
	}

	if(setjmp(jmpbuf))
	{
		debug("SMTP: Timed out during connect()\n");
		alarm(0);
		return -1;
	}
	alarm(CONNECT_TIMEOUT);

	debug("SMTP: trying %s\n",inet_ntoa(sock.sin_addr));
	if(connect(s,(struct sockaddr *)&sock,sizeof(struct sockaddr_in)) == -1)
	{
		alarm(0);
		debug("SMTP: connect() failed: errno %d\n",errno);
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
	if(smtp_write(s,1,"MAIL FROM:<%s>\r\n",mailfrom,250,SMTP_TIMEOUT_MAIL))
		return -1;

	rcpt_fail=0;

	for(p=0;users[p].addr;++p)
	{
		users[p].statcode=0;

		switch(smtp_write(s,0,"RCPT TO:<%s>\r\n",users[p].addr,251,SMTP_TIMEOUT_RCPT))
		{
	
			case -1:	return -1;

			case 1:
					debug("SMTP: failing %s, status %d\n",users[p].addr,last_status);
					users[p].statcode=(unk_addr<<16);
					users[p].statcode |= last_status;
					rcpt_fail++;
					break;
		}
	}
	if(p==rcpt_fail) return -2;	/* no valid recipients */

	if(smtp_write(s,1,"%s","DATA\r\n",354,SMTP_TIMEOUT_DATA))
		return -1;

#if defined(USE_IDTAGS) && defined(TWEAK_BODY)
	if(! FLAG_ISSET(flags,FL_IDTAG_BODY)) {
	    /* get rid of the ID tag in the body */
	    sprintf(g_body_idptr,".\r\n");
	}
#endif

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
			debug("SMTP: write: errno %d\n",errno);
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
	int len, r_len;
	char *ptr,*t;

	if(signal(SIGALRM,handle_alarm)==SIG_ERR)
	{
		debug("SMTP: signal (alarm): errno %d\n",errno);
		return 0;
	}

	if(setjmp(jmpbuf))
	{
		debug("SMTP: Timed out looking for %d\n",code);
		alarm(0);
		return 0;
	}

	/* initialize buffer to empty */
	buf[0]='\0';
	ptr = buf;
	len = 0;

restart:
	alarm(alarmtime);

	/* make sure we don't get stuck in a loop here */
	r_len = TMPBUFLEN - len - 1;
	if(r_len <= 0) {
	   debug("WARNING: read line too long for buffer, truncating.\n");
	   len = 0;
	   ptr = buf;
	   r_len = TMPBUFLEN - 1;
	}

	if((r_len=read(s, ptr, r_len)) <= 0)
	{
		alarm(0);
		debug("SMTP: read (SMTP connection): errno %d\n",errno);
		return 0;
	}
	alarm(0);

	/* update len to reflect the true buffer length, and reset ptr to
	 * the beginning of buf.
	 */

	len += r_len;
	ptr = buf;
	buf[len] = '\0';
	debug(" >>> %s", buf);

	while(!isdigit(ptr[0]) || !isdigit(ptr[1]) ||
	      !isdigit(ptr[2]) || ptr[3] != ' ')
	{
		if((t=strchr(ptr,'\n')) == NULL) {
			/* if there was anything after the last newline, copy
			 * it to the start of the buffer and set ptr to point
			 * at the end.  The read() above will append to this.
			 */
			len -= (ptr - buf);
			if(len > 0) {
			    memmove(buf, ptr, len);
			}
			ptr = buf + len;
			goto restart;
		} else {
			ptr=t+1;
		}
	}		

	last_status=atoi(ptr);

	debug("SMTP: got code %d\n",last_status);

	if(code == last_status || (code==251 && last_status==250) || (code==354 && last_status == 250)) /* special cases */
		return 1;
	else
	{
		debug("SMTP: expected %d got %d\n",code,last_status);
		return 0;
	}
}

static void handle_alarm(int dummy)
{
	longjmp(jmpbuf,1);
}
