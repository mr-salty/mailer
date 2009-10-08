/* 
 * $Id$
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include "mailer_config.h"
#include "userlist.h"

static char curhost[MAX_HOSTNAME_LEN+1]; /* +1 for null */
static char buf[BUFFER_LEN+1];	     /* +1 for temp newline (null) */
static userlist users[ADDRS_PER_BUF+1];  /* +1 for null marker at end */

static int numchildren=0,delivery_rate=TARGET_RATE;
static int numforks=0,numprocessed=0,numfailed=0;
static int real_numprocessed=0,skip_addrs = 0;
static int list_line = 0, batch_id = 0, batch_size = 0;
static int max_child,min_child,target_rate;

int deliver(char *hostname,userlist users[],flags_t in_flags);
void readmessage();
void bounce_user(char *addr,bounce_reason why,int statcode);
void handle_sig(int sig),signal_backend(int sig);
static void process_message();
static int parse_address(FILE *f, char **abuf, char **start, char **host);
static void do_delivery(flags_t flags),do_status();
static void setup_signals(),schedule();
static int handle_child();
static int read_config_file(char *filename);
static int get_config_entry(char *host, flags_t *flags);

extern char *messagebody;
static int reread_message = 0;

#define MAX_CHUNKS 100
MessageChunk message[MAX_CHUNKS];
size_t message_chunks = 0;

#if defined(USE_IDTAGS)

#if defined(TWEAK_MSGID)
static char *idptr;
#endif /* TWEAK_MSGID */

#if defined(TWEAK_RCVHDR)
static char *r_idptr;
#endif /* TWEAK_RCVHDR */

#if defined(TWEAK_FROMADDR)
extern char *mailfrom;
static char *f_idptr, *f_addrptr;
static int f_addrlen;
#endif	/* TWEAK_FROMADDR */

#if defined(TWEAK_BODY)
static char *b_idptr;
char *g_body_idptr;	/* used in deliver() */
#endif /* TWEAK_BODY */

#endif	/* USE_IDTAGS */

void do_list(char *fname)
{
    FILE *f;

    int inbuf,hostlen,curhostlen,wait_timeout,fd,next_status=STATUS;
    char *next,*addr,*host;
    int addrs_per_buf = ADDRS_PER_BUF;
    flags_t flags = FL_DEFAULT;
    char *pskip;

    /* check for a +number_of_lines_to_skip in the filename */
    pskip=strrchr(fname,'+');
    if(pskip != NULL) {
	*pskip='\0';
	skip_addrs=atoi(++pskip);
	if(skip_addrs <= 0) {
	    skip_addrs=0;
	}
    }

    if(!(f=fopen(fname,"r")))
    {
	perror("Can't open list file");
	exit(1);
    }

    read_config_file(CONFIG_FILE);

    if((fd=open(BOUNCE_FILE,O_CREAT|O_TRUNC|O_WRONLY,0666)) == -1)
    {
	perror("Can't create bounce file");
	exit(1);
    }
    close(fd);

    do_status();
    setup_signals();
    process_message();

    /* main loop.
     * next holds next empty buffer position
     * addr points to start of current address
     * host points to start of hostname of current address
     * hostlen is the length of the hostname
     */
    next=buf;
    inbuf=0;
    curhostlen=0;	/* forces us into new host on the first iteration */

    while((hostlen=parse_address(f,&next,&addr,&host)))
    {
	if (reread_message)
	{
	    reread_message = 0;
	    fprintf(stderr, "Rereading message via SIGHUP\n");
	    do_status();
	    readmessage();
	    process_message();
	}

	if(real_numprocessed >= next_status)
	{
	    do_status();
	    next_status+=STATUS;
	}

	if((hostlen != curhostlen) || \
	   strncasecmp(curhost,host,hostlen))
	{
	    /* we have a new host */

	    if(inbuf) {
		/* deliver what we have */
		users[inbuf].addr=NULL;
		batch_size=inbuf;
		do_delivery(flags);

		/* copy current entry to the start of the buffer
		 * and update the pointers.
		 */
		memmove(buf,addr,(next-addr));
		next=buf+(next-addr);
		host=buf+(host-addr);
		addr = buf;
		inbuf=0;
	    }

	    /* set the current host and batchsize */
	    strncpy(curhost,host,hostlen);
	    curhostlen = hostlen;
	    curhost[curhostlen]='\0';
	    addrs_per_buf=get_config_entry(curhost, &flags);
#ifdef SINGLE_RECIPIENT
		    addrs_per_buf = 1;
#endif
	}

	if(inbuf == 0) {
	    batch_id = list_line;
	}

	users[inbuf++].addr=addr;

	if(inbuf == addrs_per_buf ||
	   (next-buf) >= (BUFFER_LEN-MAX_ADDR_LEN) ||
	   feof(f))
	{
	    users[inbuf].addr=NULL;
	    batch_size=inbuf;
	    do_delivery(flags);
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
	batch_size=inbuf;
	do_delivery(flags);
    }

    do_status();

    /* loop and wait for the children to exit.  */
    wait_timeout=0;
    while(numchildren && ++wait_timeout < (END_WAIT_TIMEOUT / 5)) 
    {
	sleep(5);
	handle_child();
    }
    do_status();

    fclose(f);

    if(numchildren) {
	fprintf(stderr,"WARNING: %d children did not exit!\n",
		numchildren);

	/* call signal_backend as if we got SIGTERM - this will make
	 * it clean up.
	 */
	signal_backend(SIGTERM);
	/* doesn't return */
    }
}

/*
 * post-processing on the messagebody to find places to insert tags, etc.
 */
static void process_message()
{
#if defined(USE_IDTAGS)
    char *last_idptr = messagebody;
    char *ptr;
    /* mpp puts '\xff'00000.000 where we need to insert id tags */

#if defined(TWEAK_RCVHDR)
    r_idptr=index(last_idptr, '\xff');
    last_idptr=r_idptr+1;
#endif /* TWEAK_RCVHDR */

#if defined(TWEAK_MSGID)
    idptr=index(last_idptr,'\xff');
    last_idptr=idptr+1;
#endif /* TWEAK_MSGID */

#if defined(TWEAK_FROMADDR)
    f_idptr=index(last_idptr, '\xff');
    last_idptr=f_idptr+1;

    /* find the start and length of the whole address */
    for(f_addrptr=(f_idptr) ; *(f_addrptr-1) != '<'; --f_addrptr) {} 
    f_addrlen=index(f_addrptr,'>') - f_addrptr;
#endif	/* TWEAK_FROMADDR */

#if defined(TWEAK_BODY)
    b_idptr=index(last_idptr, '\xff');
    last_idptr=b_idptr+1;

    /* find the start of the last line */
    for(g_body_idptr=b_idptr;*(g_body_idptr-1) != '\n'; --g_body_idptr) {}
#endif /* TWEAK_BODY */
#endif	/* USE_IDTAGS */

    message_chunks = 0;
    ptr = messagebody;
#ifdef SINGLE_RECIPIENT
    {
	char* endptr = messagebody;
	while ((endptr = strstr(ptr, ADDRESS_TOKEN)))
	{
	    message[message_chunks].ptr = ptr;
	    message[message_chunks].len = endptr - ptr;
	    if (!strncmp(endptr - 4, "To: ", 4))
	    {
		message[message_chunks].action = ACTION_TO_ADDR;
	    }
	    else
	    {
		message[message_chunks].action = ACTION_ENCODED_TO_ADDR;
	    }

	    if (++message_chunks == MAX_CHUNKS) {
		fprintf(stderr, "Too many message chunks");
		exit(-1);
	    }
	    ptr = endptr + sizeof(ADDRESS_TOKEN) - 1;
	}
	/* Code below handles the last chunk */
    }
#endif

    /* Also gets executed if !SINGLE_RECIPIENT */
    message[message_chunks].ptr = ptr;
    message[message_chunks].len = strlen(ptr);
    message[message_chunks].action = ACTION_NONE;
    ++message_chunks;
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

	++list_line;
	++numprocessed;
	++real_numprocessed;
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
	    real_numprocessed--;
	    continue;
	}

	if(numprocessed <= skip_addrs) {
	    real_numprocessed=0;
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

static void do_delivery(flags_t flags)
{
    int i;
    char idtag[11];

#if defined(USE_IDTAGS)
#if defined(TWEAK_FROMADDR)
    char msg_from[MAX_ADDR_LEN+1];
#endif /* TWEAK_FROMADDR */

    if(batch_id > 999999) batch_id=999999;	/* should never happen */
    if(batch_size > 999) batch_size=999;	/* should never happen */
    sprintf(idtag,"%06d.%03d", batch_id, batch_size);

#if defined(TWEAK_MSGID)
    memcpy(idptr, idtag, 10);
#endif	/* TWEAK_MSGID */

#if defined(TWEAK_RCVHDR)
    memcpy(r_idptr, idtag, 10);
#endif	/* TWEAK_RCVHDR */

#if defined(TWEAK_FROMADDR)
    memcpy(f_idptr, idtag, 10);
    strncpy(msg_from, f_addrptr, f_addrlen);
    msg_from[f_addrlen]='\0';
#endif	/* TWEAK_FROMADDR */

#if defined(TWEAK_BODY)
    /* memcpy(b_idptr, idtag, 6); */
    /* TJD XXX reverse it for obscurity, position only */
    for (i = 0; i < 6; ++i) {
	b_idptr[i] = idtag[5-i];
    }
    
#endif /* TWEAK_BODY */
#endif /* USE_IDTAGS */

#ifndef NO_FORK
    schedule();	/* blocks until we can start another */

retryfork:
    switch(fork())
    {
      case -1:
#ifdef ERROR_MESSAGES
	perror("fork");
#endif
	sleep(3);
	handle_child();
	goto retryfork;
      case 0:
#if defined(OPEN_MAX)
	for(i=0;i<OPEN_MAX;++i) close(i);
#else
	{
	    struct rlimit rl;
	    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
	    {
		perror("getrlimit(RLIMIT_NOFILE), using 1024");
		rl.rlim_max = 1024;
	    }
	    for(i=0;i<rl.rlim_max;++i) close(i);

	}
#endif

#if defined(USE_IDTAGS) && defined(TWEAK_FROMADDR)
	/* make deliver() use our tweaked from address */
	mailfrom = msg_from;
#endif
	if (batch_size != 1)
	{
	    /* can't have a URL unless one addr */
	    FLAG_UNSET(flags,FL_URL_BODY);
	}
	exit(deliver(curhost,users,flags));

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
	    fprintf(stderr,"Warning: child exited on signal %d\n",
		    WTERMSIG(status));
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
    static int child_limit=0;
    static int mc_factor=1;	
    int numexited;

    if(child_limit == 0) {
	child_limit = min_child;
    }

    while(numchildren >= child_limit)
    {
	sleep(1);
	numexited=handle_child();

	/* mc_factor is a scheduling parameter that controls how
	 * likely we are to start a new child.  We try to
	 * keep within 5% of our target.
	 */

	if(delivery_rate < (int)(0.95 * target_rate))
	    mc_factor=2;
	else if(delivery_rate > (int)(1.05 * target_rate))
	    mc_factor=0;
	else
	    mc_factor=1;

	/* we want an average of 1 child every 2 seconds, so we
	 * try to make that happen.
	 * 97/06/16: this is no longer true.  we now have 25k children.
	 * this probably needs re-written to deal with higher rates.
	 * right now 25k in 28k seconds is close to 1 per second.
	 */

	if(numexited==0) {
	    child_limit++;
	    if(child_limit > max_child) child_limit=max_child;
	}
	else if(numexited <= mc_factor) {
	    /* do nothing */
	}
	else {
	    child_limit--;
	    if(child_limit < min_child) child_limit=min_child;
	}
    }
}

static void do_status()
{
    static FILE *sf=NULL;
    static time_t start=0;
    char timebuf[80];
    time_t now,diff;
    int h,m,s;

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

    strftime(timebuf, sizeof(timebuf), "%Y/%m/%d %H:%M:%S",
	     localtime(&now));

    if(real_numprocessed==0)
    {
	fprintf(sf,"%s: Starting list processing.\n",timebuf);
    }
    else
    {
	diff=now-start;
	if(diff==0) diff=1;	/* avoid div by zero */

	h=diff/3600;
	m=(diff%3600)/60;
	s=(diff%60);

	fprintf(sf,"%s: p=%d n=%d d=%d f=%d/%2.1f%% t=%02d:%02d:%02d r=%d\n",
		timebuf,numforks,numchildren,numprocessed,numfailed,
		((float)numfailed/(float)real_numprocessed)*100.0,
		h,m,s,
		delivery_rate=(int)(real_numprocessed/((float)diff/3600.0)));
    }
    fflush(sf);
}

void signal_backend(int sig)
{
    if(sig == SIGINT || sig == SIGTERM || sig == SIGHUP) {
	/* user requested this, so wait for children to terminate */
	int wait_timeout=0;

	fprintf(stderr,"Waiting 30 seconds for children to exit...\n");
	while(numchildren && ++wait_timeout < 6) /* wait 30 seconds */
	{
	    sleep(5);
	    handle_child();
	}

	if(numchildren) {
	    fprintf(stderr,"Sending signal %d to all children...\n", sig);
	    signal(sig,SIG_IGN);
	    kill(0, sig);
	    sleep(10);
	    handle_child();

	    if(numchildren) {
		do_status();
		fprintf(stderr,"Giving up! sending SIGKILL!\n");
		kill(0, SIGKILL);
		sleep(5);
		handle_child();
		fprintf(stderr,"Strange, I'm still here!\n");
		if(numchildren) {
		    fprintf(stderr,"%d children did not exit.\n",
			    numchildren);
		}
	    }
	}
    }

    do_status();
    exit(1);
}

void handle_sighup()
{
    reread_message = 1;
}

static void setup_signals()
{
    int i;

    /* catch all signals not explicitly named */

    /* should be NSIG but linux gives an error when you use NSIG */
    for(i=1;i<32;++i)
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

/*
 * config file stuff
 */


typedef struct _config_entry {
    char *host;
    flags_t flags;
    short batch;
    struct _config_entry *next;
} config_entry;

static config_entry *head = NULL;


/*
 * read_config_file(filename)
 *
 * reads the specified config file for config, debug, and batchsize info
 *
 * also sets the globals max_child, min_child, target_rate
 */

#define CONFIG_BUFLEN	(MAX_ADDR_LEN + 80)
static int read_config_file(char *filename)
{
    FILE *fp;
    char buf[CONFIG_BUFLEN];
    config_entry *new_entry;
    char *ptr,*hostp,*batchp;
    flags_t flags;
    int batch;

    /* set defaults for scheduler parameters */
    max_child = MAX_CHILD;
    min_child = MIN_CHILD;
    target_rate = TARGET_RATE;

    fp = fopen(filename,"r");
    if(fp == (FILE *) NULL) {
	return -1;
    }

    buf[CONFIG_BUFLEN - 2] ='\n';

    while(fgets(buf, CONFIG_BUFLEN, fp) != NULL) {

	if(buf[CONFIG_BUFLEN - 2] != '\n') {
	    int c;
	    /* line is too long, argh */
	    fprintf(stderr,
		    "Warning: long line '%s'... in config file ignored!\n",
		    buf);

	    do {
		c=getc(fp);
	    } while((c != '\n') && c != EOF);

	    buf[CONFIG_BUFLEN-2]='\n';
	    continue;
	}

	/* skip leading whitespace */
	ptr=buf;
	while(isspace(*ptr) && *ptr) { ++ptr; }

	if(*ptr == '#' || !*ptr) {
	    continue;	/* comment or blank line */
	}

	/* found hostname */
	hostp=ptr;

	while(!isspace(*ptr) && *ptr) { ++ptr; }
	if(!*ptr) {
	    /* just a hostname with nothing else, ignore it */
	    continue;
	}
	*ptr='\0';
	++ptr;


	/* find batch size or flags.
	 * setting batch to BATCH_DEFAULT means use the default
	 */

	while(isspace(*ptr) && *ptr) { ++ptr; }

	/* see if the batchsize exists */
	batch=BATCH_DEFAULT;

	if(isdigit(*ptr) || *ptr == '-') {
	    batchp=ptr;

	    while(!isspace(*ptr) && *ptr) { ++ptr; }
	    if(*ptr) {
		*ptr='\0';
		++ptr;
	    }

	    batch=atoi(batchp);
	    if(batch <= 0) { batch = BATCH_DEFAULT; }
	}

	/* now look for flags */
	flags=FL_DEFAULT;

	while(*ptr) {
	    switch(*ptr) {
	      case 'd':
		FLAG_SET(flags,FL_DEBUG);
		break;

	      case 'D':
		FLAG_UNSET(flags,FL_DEBUG);
		break;

	      case 'i':
		FLAG_SET(flags,FL_IDTAG_MSGID);
		break;

	      case 'I':
		FLAG_UNSET(flags,FL_IDTAG_MSGID);
		break;

	      case 'r':
		FLAG_SET(flags,FL_IDTAG_RECV);
		break;

	      case 'R':
		FLAG_UNSET(flags,FL_IDTAG_RECV);
		break;

	      case 'f':
		FLAG_SET(flags,FL_IDTAG_FROM);
		break;

	      case 'F':
		FLAG_UNSET(flags,FL_IDTAG_FROM);
		break;

	      case 'b':
		FLAG_SET(flags,FL_IDTAG_BODY);
		break;

	      case 'B':
		FLAG_UNSET(flags,FL_IDTAG_BODY);
		break;

	      case 'u':
		FLAG_SET(flags,FL_URL_BODY);
		break;

	      case 'U':
		FLAG_UNSET(flags,FL_URL_BODY);
		break;
	    }
	    ++ptr;
	}

	/* special cases (parameters) */
	if(!strcasecmp(hostp,"min_child")) {
	    min_child = ((batch > 0) ? batch : MIN_CHILD);
	} else if(!strcasecmp(hostp,"max_child")) {
	    max_child = ((batch > 0) ? batch : MAX_CHILD);
	} else if(!strcasecmp(hostp,"target_rate")) {
	    target_rate = ((batch > 0) ? batch : TARGET_RATE);
	} else {
	    /* make a new record for this host */

	    if(!(new_entry = malloc(sizeof(config_entry)))) {
		perror("malloc (new_entry)");
		continue;
	    }
	    if(!(new_entry->host = malloc(strlen(hostp)+1))) {
		perror("malloc (new_entry->host)");
		free(new_entry);
		continue;
	    }
	    strcpy(new_entry->host, hostp);
	    new_entry->flags = flags;
	    new_entry->batch = (short) batch;
	    new_entry->next = head;
	    head = new_entry;
	}

	continue;
    }
    fclose(fp);
    return 0;
}


/*
 * config_lookup_host()
 * looks up the host in the config file info
 */

static int get_config_entry(char *host, flags_t *flags)
{
    config_entry *e;
    int batch;

    for(e=head; e != NULL; e=e->next) {
	char *p1=e->host, *p2=host;

	if(e->host[0] == '*') {
	    /* wildcard.  offset makes it so we try to match the tail of
	     * e->host, skipping the star.
	     */
	    int offset = strlen(host) - (strlen(e->host) - 1);
	    if(offset < 0) {
		/* too short to possibly match */
		continue;
	    }
	    ++p1;		/* skip the star */
	    p2 += offset;
	}

	if(!strcasecmp(p1,p2)) {
	    break;
	}
    }
    if(e==NULL) {
	/* didn't find an entry */
	batch = BATCH_DEFAULT;
	*flags = FL_DEFAULT;
    } else {
	batch = e->batch;
	*flags = e->flags;
    }

    if(batch <= 0 || batch > ADDRS_PER_BUF) {
	batch = ADDRS_PER_BUF;	/* default batchsize */
    }

#if defined(DEBUG_SMTP_ALL)
    FLAG_SET(*flags,F_DEBUG);
#endif
#if !defined(DEBUG_SMTP)
    FLAG_UNSET(*flags,F_DEBUG);
#endif

    return batch;
}
