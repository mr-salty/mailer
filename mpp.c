/*
 * $Log: mpp.c,v $
 * Revision 1.9  1997/11/24 22:39:54  tjd
 * made the From: header get tweaked by TWEAK_FROMADDR as well
 *
 * Revision 1.8  1997/11/24 03:29:53  tjd
 * bumped version to 1.2a (TWEAK_FROMADDR change)
 *
 * Revision 1.7  1997/11/24 00:37:32  tjd
 * small tweak to msgid, it now contains the batchsize as the last
 * component.
 *
 * Revision 1.6  1997/10/11 07:07:05  tjd
 * bumped version to 1.1a
 * also removed the 'rand' in the message id and replaced it with the date
 *
 * Revision 1.5  1997/08/14 16:01:52  tjd
 * added TWEAK_MSGID stuff
 *
 * Revision 1.4  1996/08/06 15:42:23  tjd
 * changed order of headers per 822 4.1
 *
 * Revision 1.3  1996/08/05 17:55:26  tjd
 * added Received: header on outgoing message
 *
 * Revision 1.2  1996/02/15 04:10:01  tjd
 * write output to temp file rather than stdout (pipe)
 * this in preparation for mmap()ing the message
 *
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

#include "mailer_config.h"

char *arpadate(char *);

/* mpp msgfile outfile hostname */
int main(int argc, char *argv[])
{
	FILE *f,*o;
	struct stat sbuf;
	char line[MAX_LINE_LEN+1];
	char *message,*p;
	char *longdate;
	char from_addr[MAX_ADDR_LEN + 1];
	char *user,*host;
	time_t utime;
	struct tm *ltime;
	int hdr,reqhdr;

	if(argc != 4)
	{
		fprintf(stderr,"Usage: mpp msgfile tmpfile from_addr\n");
		exit(1);
	}

	if(stat(argv[1],&sbuf)==-1)
	{
		perror("stat (message)");
		exit(1);
	}

	if(!(message=malloc((sbuf.st_size*2)+512)))
	{
		perror("malloc (message)");
		exit(1);
	}

	if(!(f=fopen(argv[1],"r")))
	{
		perror("fopen (message)");
		exit(1);
	}

	if(!(o=fopen(argv[2],"w")))
	{
		perror("fopen (temp file)");
		exit(1);
	}

	/* break from_addr into user@host */
	strcpy(from_addr, argv[3]);
	host=rindex(from_addr,'@');
	if(host == (char *) NULL) {
	    fprintf(stderr,"Couldn't parse %s into user@host\n",from_addr);
	    exit(1);
	}
	*host='\0';
	++host;
	user=from_addr;

	p=message;

#define H_FROM	1
#define H_TO	2
#define H_SUBJ	4
#define H_REQ	(H_FROM|H_TO|H_SUBJ)

	hdr=1;
	reqhdr=0;

	/* per RFC822 4.1: preferred order of headers is: "Return-Path",
	 * "Received", "Date", "From", "Subject", "Sender", "To"
	 */

	longdate=arpadate(NULL);
	utime=time(NULL);
	ltime = localtime(&utime);
	sprintf(p,"Received: from local (localhost)\r\n"
		  "          by %s (mailer 1.3a) with SMTP;\r\n"
		  "          %s\r\n",host,longdate);
	p+=strlen(p);
	sprintf(p,"Date: %s\r\n",longdate);
	p+=strlen(p);
	sprintf(p,"Message-Id: <%s.%d.%04d%02d%02d%s@%s>\r\n",
			HEADER_HEADER,(int)time(NULL),
			ltime->tm_year+1900, ltime->tm_mon+1, ltime->tm_mday,
#ifdef TWEAK_MSGID
			".\xff""00000.000",
#else
			"",
#endif
			host);

	p+=strlen(p);

	while(fgets(line,MAX_LINE_LEN+1,f))
	{
		int l;

		l=strlen(line);
		
		while(l>0 && (line[l-1] == '\n' || line[l-1] == '\r'))
			line[--l]='\0';

		if(l > MAX_LINE_LEN-2)
                {
                        fprintf(stderr,"Warning: line too long (>%d characters) in message, truncated.\n",MAX_LINE_LEN);
                }

		if(l==0 && hdr) {	/* blank line, end of headers */
			hdr=0;

			if(reqhdr != H_REQ)
			{
				fprintf(stderr,"Missing required header(s) from message: %s%s%s\n",
					(reqhdr & H_FROM) ? "" : "From: ",
					(reqhdr & H_TO) ? "" : "To: ",
					(reqhdr & H_SUBJ) ? "" : "Subject: ");

				exit(1);
			}
		}

		/* Add cr/lf */

		line[l++]='\r'; line[l++]='\n'; line[l]='\0';

		if(hdr)
		{
			char *t;
			int i,tl,upcase,discard,newhdr;

			if(!(t=strchr(line,':')))
			{
				fprintf(stderr,"Bad header: %s",line);
				exit(1);
			}
			tl=t-line;

			upcase=1;
			line[0]=toupper(line[0]);
			for(i=0;i<tl;++i)
			{ 
				line[i]=(upcase ? toupper (line[i])
					        : tolower(line[i]));

				upcase=(line[i]=='-');
			}

			discard=newhdr=0;

			if(!strncmp(line,"From: ",tl)) 	       newhdr = H_FROM;
			else if(!strncmp(line,"To: ",tl))      newhdr = H_TO;
			else if(!strncmp(line,"Subject: ",tl)) newhdr = H_SUBJ;
			else if(!strncmp(line,"Date: ",tl))    discard = 1;
			else if(!strncasecmp(line,"Message-Id: ",tl))
							      discard = 1;

			if((reqhdr & newhdr) || discard)
			{
				fprintf(stderr,"Warning: discarding extra header: %s",line);			continue;
			}

#if defined(TWEAK_FROMADDR)
			if(newhdr == H_FROM) {
			    char *aptr;
			    aptr=index(line,'<');
			    if(aptr == (char *) NULL) {
				aptr=index(line,'\r');
			    } else {
				--aptr;
			    }
			    /* be careful that this looks like the message-id
			     * above!
			     */
			    sprintf(aptr," <%s+%04d%02d%02d%s@%s>\r\n", user,
				ltime->tm_year+1900, ltime->tm_mon+1,
				ltime->tm_mday,
				".\xff""00000.000", host);

			}
#endif

			reqhdr |= newhdr;
		}
	
		if(line[0]=='.') { *p='.'; ++p; }
		sprintf(p,"%s",line);
		p+=strlen(p);
	}

	/* EOM sentinel */
	sprintf(p-2,"\r\n.\r\n");
	p+=strlen(p);

	fprintf(o,"%s",message);
	fclose(o);
	fclose(f);
	return 0;
}
