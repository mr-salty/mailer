/*
 * $Log: mailer_config.h,v $
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

/* debug stuff */

#define STATUS	250	/* status every n messages */

#undef DEBUG		/* general debugging */
#undef DEBUG_SMTP	/* SMTP replies from host (LOTS of output!!!) */
#undef NO_FORK		/* useful to debug deliver() */
#undef ERROR_MESSAGES 	/* generate error messages */

/* general params */
#define HEADER_HEADER	"AWAD"
#define MPP		"mpp"

/* list processing parameters */
#define MAX_ADDR_LEN    256	/* single address size limit: RFC821 */
#define MAX_LINE_LEN	1024	/* single message line limit: RFC821 */
#define MAX_HOSTNAME_LEN 64	/* hostname limit: RFC821 */
#define ADDRS_PER_BUF   100	/* max # of addresses per buffer: RFC821 */
#define BUFFER_LEN   	4096	/* single delivery attempt buffer */
#define MAX_CHILD	12	/* max # of deliver children */

/* SMTP: timeouts as defined in RFC1123 */
#define SMTP_TIMEOUT_WELCOME	300
#define SMTP_TIMEOUT_HELO	300	/* not defined in 1123 */
#define SMTP_TIMEOUT_MAIL	300
#define SMTP_TIMEOUT_RCPT	300
#define SMTP_TIMEOUT_DATA	120
#define SMTP_TIMEOUT_END	600
