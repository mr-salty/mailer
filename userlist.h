/*
 * $Id$
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
