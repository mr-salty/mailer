/*
 * $Log: mpp.c,v $
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

/* mpp msgfile hostname */
int main(int argc, char *argv[])
{
	FILE *f;
	struct stat sbuf;
	char line[MAX_LINE_LEN+1];
	char *message,*p;
	int hdr,reqhdr;
	int len;

	if(argc != 3)
	{
		fprintf(stderr,"Usage: mpp msgfile from_hostname\n");
		exit(1);
	}

	if(stat(argv[1],&sbuf)==-1)
	{
		perror("stat (message)");
		exit(1);
	}

	if(!(message=malloc((sbuf.st_size*2)+256)))
	{
		perror("malloc (message)");
		exit(1);
	}

	if(!(f=fopen(argv[1],"r")))
	{
		perror("fopen (message)");
		exit(1);
	}

	p=message;

#define H_FROM	1
#define H_TO	2
#define H_SUBJ	4
#define H_REQ	(H_FROM|H_TO|H_SUBJ)

	hdr=1;
	reqhdr=0;

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
			sprintf(p,"Date: %s\r\n",arpadate(NULL));
			p+=strlen(p);
			sprintf(p,"Message-Id: <%s.%d.%d@%s>\r\n",
				HEADER_HEADER,(int)time(NULL),rand(),argv[2]);
			p+=strlen(p);
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

			reqhdr |= newhdr;
		}
	
		if(line[0]=='.') { *p='.'; ++p; }
		sprintf(p,"%s",line);
		p+=strlen(p);
	}

	/* EOM sentinel */
	sprintf(p-2,"\r\n.\r\n");
	p+=strlen(p);

	len=p-message;

	fwrite(&len,sizeof(int),1,stdout);
	printf("%s",message);
	return 1;
}
