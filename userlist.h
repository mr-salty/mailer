/*
 * $Log: userlist.h,v $
 * Revision 1.6  2008/06/23 18:27:08  tjd
 * add support for YOUR-EMAIL-ADDRESS - encoded/unencoded email address insertion
 *
 * Revision 1.5  2008/06/21 17:17:34  tjd
 * reindent everything
 *
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

typedef enum
{
    ok = 0,
    long_addr,
    bad_addr,
    long_host, /* from do_list */
    host_failure,
    caught_signal,
    unk_addr
} bounce_reason;

#define BOUNCE_REASONS { \
    "ok", \
    "addr too long", \
    "invalid address", \
    "host too long", \
    "host failure", \
    "died on signal", \
    "user unknown" \
}

typedef struct {
    int statcode;
    char *addr;
} userlist;

/*
 * this doesn't really belong here but i didn't want to make a new header...
 *
 * we split the message into chunks where we need to insert something
 * between chunks.
 */
#define ADDRESS_TOKEN "YOUR-EMAIL-ADDRESS"

typedef enum {
    ACTION_NONE,
    ACTION_TO_ADDR,
    ACTION_ENCODED_TO_ADDR
} Action;

typedef struct {
    char *ptr;
    size_t len;
    Action action;
} MessageChunk;
