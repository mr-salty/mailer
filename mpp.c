/*
 * $Log: mpp.c,v $
 * Revision 1.12  1998/04/21 04:24:21  tjd
 * add time(NULL) back in to message-id, otherwise 2 mailings in the same
 * day will have duplicate ID's.  Now we just can't mail 2 in the same
 * second, which shouldn't be a problem...
 *
 * Revision 1.11  1998/04/17 00:37:10  tjd
 * changed config file format
 * added config flags and associated definitions
 * changed tagging; added tagging on SMTP id and in the body
 * changed Message-Id to look like the other tags (no time(NULL))
 * removed NULL_RETURN_PATH (bad feature)
 *
 * Revision 1.10  1997/11/24 22:44:01  tjd
 * slight formatting change with Message-Id
 *
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
#if defined(USE_IDTAGS)
	char idtag[MAX_ADDR_LEN+40];
#endif /* USE_IDTAGS */

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

#if defined(USE_IDTAGS)
	/* make the ID tags (used in message ID, optionally from and body)
	 * be careful about changing these (see do_list.c)
	 */

	sprintf(idtag,"%s.%d.%04d%02d%02d.%c%s", HEADER_HEADER, (int) utime,
		ltime->tm_year+1900, ltime->tm_mon+1, ltime->tm_mday,
		'\xff',"00000.000");
#endif /* USE_IDTAGS */

	/* received header */
	sprintf(p,"Received: from local (localhost)\r\n"
		  "\tby %s (mailer 1.4) with SMTP", host);
	p+=strlen(p);
#if defined(USE_IDTAGS) && defined(TWEAK_RCVHDR)
	sprintf(p," id <%s>", idtag);
	p+=strlen(p);
#endif
	sprintf(p,";\r\n\t%s\r\n",longdate);
	p+=strlen(p);
	sprintf(p,"Date: %s\r\n",longdate);
	p+=strlen(p);

	/* Message-Id: */
#if defined(USE_IDTAGS) && defined(TWEAK_MSGID)
	sprintf(p,"Message-Id: <%s@%s>\r\n",idtag,host);
#else
	sprintf(p,"Message-Id: <%s.%d@%s>\r\n",HEADER_HEADER,
		(int)time(NULL),host);
#endif

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

#if defined(USE_IDTAGS) && defined(TWEAK_FROMADDR)
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
			    sprintf(aptr," <%s+%s@%s>\r\n", user, idtag, host);
			}
#endif

			reqhdr |= newhdr;
		}
	
		if(line[0]=='.') { *p='.'; ++p; }
		sprintf(p,"%s",line);
		p+=strlen(p);
	}

#if defined(USE_IDTAGS) && defined(TWEAK_BODY)
	/* add this at the end of the message.  We'll strip it out later (in
	 * deliver() if we don't want it.
	 */

	sprintf(p,"(%s)\r\n",idtag);
	p+=strlen(p);
#endif

	/* EOM sentinel.  Don't change this without looking at deliver.c */
	sprintf(p-2,"\r\n.\r\n");
	p+=strlen(p);

	fprintf(o,"%s",message);
	fclose(o);
	fclose(f);
	return 0;
}
