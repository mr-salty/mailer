/*
 * $Log: userlist.h,v $
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

typedef enum { ok=0, long_addr, bad_addr, long_host,
		 host_failure, unk_addr } bounce_reason;

#define BOUNCE_REASONS { "ok","addr too long","invalid address","host too long", "host failure", "user unknown" }

typedef struct {
	bounce_reason bounced;
	char *addr;
} userlist;
