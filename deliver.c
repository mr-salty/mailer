/*
 * $Id$
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
extern MessageChunk message[];
extern size_t message_chunks;

#if defined(USE_IDTAGS) && defined(TWEAK_BODY)
extern char *g_body_idptr;
#endif /* USE_IDTAGS */

static int delivermessage(char *addr,char *hostname, userlist users[]);
static int lookfor(int s,int code,int alarmtime);
static int smtp_write(int s,int close_s,char *fmt, char *arg,int look,
		      int timeout);

static int in_child=0;
extern void signal_backend(int sig);
extern void handle_sighup();
static userlist *gl_users=NULL;
static flags_t flags;

#ifdef DEBUG_SMTP
static char *hostname2;	/* filled in by deliver() */
#endif

static time_t start_t; /* filled in at start of deliver(), used by debug() */

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
    fprintf(fpLog, "%ld ", time(NULL) - start_t);
    vfprintf(fpLog,format,ap);

    va_end(ap);
#endif
}

void handle_sig(int sig)
{
    if(in_child)
    {
	debug("SMTP: Warning: pid %d caught signal %d, failing.\n",
	      getpid(), sig);
	exit(bounce(gl_users,(caught_signal<<16) | sig));
    }
    else
    {
	if (sig == SIGHUP)
	{
	    handle_sighup();
	}
	else
	{
	    fprintf(stderr,
		    "FATAL: parent mailer caught signal %d, exiting.\n",
		    sig);
	    signal_backend(sig);
	    exit(1);
	}
    }
}

/* finds all the mx records and tries to deliver the message */

int deliver(char *hostname,userlist users[], flags_t in_flags)
{
    struct hostent *host;
    int nmx,rcode,i,p,deliver_status=0,taddr;

    char *mxhosts[MAXMXHOSTS+1];
    start_t=time(NULL);
    in_child=1;
    gl_users=users;

    flags = in_flags;
#ifdef DEBUG_SMTP
    hostname2=hostname;
#endif
    debug("Hostname: %s\n",hostname);

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
		if (time(NULL) - start_t >= TRANSACTION_TIMEOUT)
		{
		    debug("SMTP: Transaction timeout.\n");
		    deliver_status = -1;
		}
		else
		{
		    deliver_status=delivermessage((char *)&taddr,
						  hostname, users);
		}
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
		if (time(NULL) - start_t >= TRANSACTION_TIMEOUT)
		{
		    debug("SMTP: Transaction timeout.\n");
		    deliver_status = -1;
		    break;
		}

		if((deliver_status=delivermessage(host->h_addr_list[p],
						  hostname, users)) != -1)
		    break;
	    }
	}
	if(deliver_status != -1) break;
    }

    debug("SMTP: Finished. Status=%d ET=%ds.\n", deliver_status,
	  time(NULL)-start_t);
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

int urlencode(char* s, char* addr)
{
    static char *hexdigits = "0123456789abcdef";
    char *p;
    int len = 0;
    for (p = addr; *p; ++p)
    {
	if (isalnum(*p))
	{
	    s[len++] = *p;
	}
	else
	{
	    s[len++] = '%';
	    s[len++] = hexdigits[(*p >> 4) & 0xf];
	    s[len++] = hexdigits[(*p) & 0xf];
	}
    }
    s[len] = '\0';
    return len;
}

void put_url(char* s, char* addr)
{
#if 0
    /* puts a URL to unsubscribe in the body */
    s += sprintf(s, "\r\nTo unsubscribe: http://wordsmith.org/unsub?");
    urlencode(s, addr);
#else
    /* just puts a message in the body */
    s += sprintf(s, "\r\nThis message was sent to \"%s\".", addr);
#endif
    sprintf(s, "\r\n.\r\n");
}

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
    int connect_timeout = CONNECT_TIMEOUT;
    if (!strcasecmp(hostname, "hotmail.com"))
    {
	connect_timeout = 10;
    }

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
	close(s);
	return -1;
    }

    if(sigsetjmp(jmpbuf, 1))
    {
	debug("SMTP: Timed out during connect()\n");
	alarm(0);
	close(s);
	return -1;
    }
    alarm(connect_timeout);

    debug("SMTP: trying %s timeout %d\n",inet_ntoa(sock.sin_addr),
	  connect_timeout);
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

    switch(smtp_write(s,1,"HELO %s\r\n",myhostname,250,SMTP_TIMEOUT_HELO))
    {
      case -1:
	return -1;

      case 1:
	for(p=0;users[p].addr;++p)
	{
	    debug("SMTP: failing %s, status %d\n",users[p].addr,last_status);
	    users[p].statcode= (host_failure<<16);
	    users[p].statcode |= last_status;
	    rcpt_fail++;
	}
	return -2;	/* no valid recipients */

    }

    switch(smtp_write(s, 1, "MAIL FROM:<%s>\r\n", mailfrom, 250,
		      SMTP_TIMEOUT_MAIL))
    {
      case -1:
	return -1;

      case 1:
	if (last_status == 421)
	{
	    for(p=0;users[p].addr;++p)
	    {
		debug("SMTP: failing %s, status %d\n",
		      users[p].addr, last_status);
		users[p].statcode= (host_failure<<16);
		users[p].statcode |= last_status;
		rcpt_fail++;
	    }
	    return -2;	/* no valid recipients */
	}
	else
	{
	    return -1;
	}

    }

    rcpt_fail=0;

    for(p=0;users[p].addr;++p)
    {
	users[p].statcode=0;

	switch(smtp_write(s, 0, "RCPT TO:<%s>\r\n",users[p].addr, 251,
			  SMTP_TIMEOUT_RCPT))
	{

	  case -1:	return -1;

	  case 1:
			debug("SMTP: failing %s, status %d\n",
			      users[p].addr, last_status);
			users[p].statcode = (unk_addr<<16);
			users[p].statcode |= last_status;
			rcpt_fail++;
			break;
	}
    }
    if(p==rcpt_fail) return -2;	/* no valid recipients */

    if(smtp_write(s,1,"%s","DATA\r\n",354,SMTP_TIMEOUT_DATA))
	return -1;

#if defined(USE_IDTAGS) && defined(TWEAK_BODY)
    if(FLAG_ISSET(flags,FL_URL_BODY)) {
	put_url(g_body_idptr, users[0].addr);
    } else if(! FLAG_ISSET(flags,FL_IDTAG_BODY)) {
	/* get rid of the ID tag in the body */
	sprintf(g_body_idptr,".\r\n");
    }
#endif

    switch(smtp_write(s,1,users[0].addr,NULL,250,SMTP_TIMEOUT_END))
    {
      case -1:
	return -1;

      case 1:
	if (last_status == 554)
	{
	    for(p=0;users[p].addr;++p)
	    {
		debug("SMTP: failing %s, status %d\n",users[p].addr,
		      last_status);
		users[p].statcode = (unk_addr<<16);
		users[p].statcode |= last_status;
		rcpt_fail++;
	    }
	    return -2;	/* no valid recipients */
	}
	else
	{
	    return -1;
	}
    }


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
 * if arg is NULL this is the message body and we process it in chunks.
 *	fmt contains the address in this case.
 */

static int smtp_write(int s, int close_s, char *fmt, char *arg, int look,
		      int timeout)
{
    if(fmt != NULL)
    {
	if(arg != NULL) {
	    int len = sprintf(buf,fmt,arg);
	    debug("%s",buf);
#ifndef NO_DELIVERY
	    if(write(s,buf,len) == -1)
	    {
		debug("SMTP: write: errno %d\n",errno);
		close(s);
		return -1;
	    }
#endif /* NO_DELIVERY */
	} else {
	    size_t idx;
	    debug("SMTP: [Message body omitted]\n");
	    for (idx = 0; idx < message_chunks; ++idx)
	    {
#ifndef NO_DELIVERY
		if (write(s,message[idx].ptr, message[idx].len) == -1)
		{
		    debug("SMTP: write: errno %d\n",errno);
		    close(s);
		    return -1;
		}

		switch (message[idx].action)
		{
		  case ACTION_NONE:
		    break;

		  case ACTION_TO_ADDR:
		    if (write(s, fmt, strlen(fmt)) == -1)
		    {
		       	debug("SMTP: write: errno %d\n",errno);
			close(s);
			return -1;
		    }
		    break;

		  case ACTION_ENCODED_TO_ADDR: {
		    char tmpbuf[3 * MAX_ADDR_LEN];
		    int len = urlencode(tmpbuf, fmt);
		    if (write(s, tmpbuf, len) == -1)
		    {
		       	debug("SMTP: write: errno %d\n",errno);
			close(s);
			return -1;
		    }
		    break;
		    }
		}
	    }
	}
#endif /* NO_DELIVERY */
    }

#ifndef NO_DELIVERY
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

    if(sigsetjmp(jmpbuf, 1))
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

    if(code == last_status || (code==251 && last_status==250) ||
       (code==354 && last_status == 250)) /* special cases */
	return 1;
    else
    {
	debug("SMTP: expected %d got %d\n",code,last_status);
	return 0;
    }
}

static void handle_alarm(int dummy)
{
    siglongjmp(jmpbuf,1);
}
