/*
 * $Log: deliver.c,v $
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>

#include "sendmail.h"
#include "mailer_config.h"
#include "userlist.h"

int getmxrr(char *,char **,bool,int *);
int bounce(userlist users[],bounce_reason fail_all);

#define SMTP_PORT 	25
#define TMPBUFLEN	(MAX_ADDR_LEN+80)

static char buf[TMPBUFLEN];
extern char *messagebody,*myhostname,*mailfrom;
extern int messagebody_size;

#ifdef ERROR_MESSAGES
static char *hostname2;
#endif

static int delivermessage(char *addr,char *hostname, userlist users[]);
static int lookfor(int s,int code,int alarmtime);
static int smtp_write(int s,int close_s,char *fmt, char *arg,int look,int timeout);

/* finds all the mx records and tries to deliver the message */

int deliver(char *hostname,userlist users[])
{
	struct hostent *host;
	int nmx,rcode,i,p,deliver_status=0;
	
	char *mxhosts[MAXMXHOSTS+1];
#ifdef ERROR_MESSAGES
	hostname2=hostname;
#endif
	
	nmx=getmxrr(hostname,mxhosts,FALSE,&rcode);

	if (nmx<=0)
	{
		nmx=1;
		mxhosts[0]=hostname;
	}

	for(i=0;i<nmx;++i)
	{
		/* try to deliver to each host */

		if(!(host=gethostbyname(mxhosts[i])))
		{
#ifdef ERROR_MESSAGES
			sprintf(buf,"gethostbyname(%s)",mxhosts[i]);
			perror(buf);
#endif
			deliver_status=-1;
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

	switch(deliver_status)
	{
		case -1:	return bounce(users,host_failure);
		case -2:	return bounce(users,unk_addr);
		case 0:		return 0;
		default:	return bounce(users,0);	
	}
}



#ifdef ERROR_MESSAGES
void hperror(char *msg)
{
	sprintf(buf,"%s: %s",hostname2,msg);
	perror(buf); 
}
#endif


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

	sock.sin_family=AF_INET;
	sock.sin_port=htons(SMTP_PORT);
	bcopy(addr,&sock.sin_addr,sizeof sock.sin_addr);

	if((s=socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
#ifdef ERROR_MESSAGES
		hperror("socket");
#endif
		return -1;
	}

	if(connect(s,(struct sockaddr *)&sock,sizeof(struct sockaddr_in)) == -1)
	{
#ifdef ERROR_MESSAGES
		hperror("connect");
#endif
		close(s);
		return -1;
	}

	if(smtp_write(s,1,NULL,NULL,220,SMTP_TIMEOUT_WELCOME)) 
		return -1;

	if(smtp_write(s,1,"HELO %s\r\n",myhostname,250,SMTP_TIMEOUT_HELO))
		return -1;

	if(smtp_write(s,1,"MAIL FROM:<%s>\r\n",mailfrom,250,SMTP_TIMEOUT_MAIL))
		return -1;

	rcpt_fail=0;

	for(p=0;users[p].addr;++p)
	{
		users[p].bounced=0;

		switch(smtp_write(s,0,"RCPT TO:<%s>\r\n",users[p].addr,251,SMTP_TIMEOUT_RCPT))
		{
	
			case -1:	return -1;

			case 1:
					users[p].bounced=unk_addr;
					rcpt_fail++;
		}
	}
	if(p==rcpt_fail) return -2;	/* no valid recipients */

	if(smtp_write(s,1,"%s\r\n","DATA",354,SMTP_TIMEOUT_DATA))
		return -1;

	if(smtp_write(s,1,"%s",messagebody,250,SMTP_TIMEOUT_END))
		return -1;

	/* The message is now committed, methinks.  But we wait anyways */

	if(smtp_write(s,1,"%s\r\n","QUIT",221,SMTP_TIMEOUT_END))
		return rcpt_fail;

	close(s);

	return rcpt_fail;
}

/* returns:	 0 for ok
 *		-1 if the write failed		(always close s)
 *		 1 if the lookfor failed	(close s if close_s is true)
 *
 * if fmt is NULL we don't do the write, just lookfor.
 */

static int smtp_write(int s,int close_s,char *fmt, char *arg,int look,int timeout)
{

	if(fmt != NULL)
	{
		sprintf(buf,fmt,arg);

		if(write(s,buf,strlen(buf)) == -1)
		{
#ifdef ERROR_MESSAGES
			hperror("write");
#endif
			close(s);
			return -1;
		}
	}

	if(!lookfor(s,look,timeout)) {
#ifdef ERROR_MESSAGES
		fprintf(stderr,"%s: No %d response to %s",hostname2,look,buf);
#endif
		if(close_s) close(s);
		return 1;
	}
	return 0;
}

jmp_buf jmpbuf; /* for alarm */
static void handle_alarm(int dummy);

static int lookfor(int s,int code,int alarmtime)
{
	int len,retcode;
	char *ptr,*t;

	buf[0]='\0';

restart:
	if(signal(SIGALRM,handle_alarm)==SIG_ERR)
	{
#ifdef ERROR_MESSAGES
		hperror("signal (alarm)");
#endif
		return 0;
	}

	if(setjmp(jmpbuf))
	{
#ifdef DEBUG
		fprintf(stderr,"Timed out looking for %d\n",code);
#endif
		alarm(0);
		return 0;
	}
	alarm(alarmtime);
	
	if((len=read(s,buf,TMPBUFLEN-1))<=0)
	{
		alarm(0);
#ifdef ERROR_MESSAGES
		hperror("read (SMTP connection)");
#endif
		return 0;
	}
	alarm(0);

	ptr=buf;
	buf[len]='\0';

#ifdef DEBUG_SMTP
	fprintf(stderr,"SMTP: %s\n",buf);
#endif

	while(ptr[3] != ' ')
	{
		if((t=strchr(ptr,'\n')) == NULL)
			goto restart;
		else {
			ptr=t+1;
		}
	}		

	retcode=atoi(buf);

#ifdef DEBUG_SMTP
	fprintf(stderr,"SMTP: got code %d\n",retcode);
#endif

	if(code == retcode || (code==251 && retcode==250)) /* special case */
		return 1;
	else
	{
#ifdef DEBUG
		fprintf(stderr,"Error, expected %d got %d\n",code,retcode);
#endif
		return 0;
	}
}

static void handle_alarm(int dummy)
{
	longjmp(jmpbuf,1);
}
