/*
 * $Log: userlist.h,v $
 * Revision 1.4  1996/01/02 04:30:43  tjd
 * added SMTP status code to bounce messages
 *
 * Revision 1.3  1996/01/02  00:29:11  tjd
 * added signal handling code
 *
 * Revision 1.2  1995/12/31  20:14:42  tjd
 * added 550/551/generic user unknown indication
 *
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

typedef enum { ok=0, long_addr, bad_addr, long_host, /* from do_list */
		 host_failure, caught_signal,
		 unk_addr } bounce_reason;

#define BOUNCE_REASONS { "ok","addr too long","invalid address","host too long", "host failure", "died on signal", "user unknown" }

typedef struct {
	int statcode;
	char *addr;
} userlist;
