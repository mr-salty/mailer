/*
 * $Log: mailer_config.h,v $
 * Revision 1.15  1997/10/11 07:08:11  tjd
 * added support for mailer config file for debugging, batching, and setting
 * some parameters.
 * also fixed the computation of the batch_id
 *
 * Revision 1.14  1997/08/14 15:10:13  tjd
 * added TWEAK_MSGID
 *
 * Revision 1.13  1996/05/04 20:51:04  tjd
 * but def for memmove() for sun at the end.
 *
 * Revision 1.12  1996/05/04 18:25:26  tjd
 * made DEBUG_SMTP defined by default.
 *
 * Revision 1.11  1996/05/02 22:52:59  tjd
 * removing dependencies on sendmail header files.
 * sendmail.h is now the only one and only needed by domain.c
 *
 * Revision 1.10  1996/05/02 22:09:55  tjd
 * removed DEBUG, DEBUG_FORK
 *
 * Revision 1.9  1996/04/23 03:56:11  tjd
 * mode ADDS_PER_BUF 64 to cater to msn.com's broken SMTP.
 * (see rfc821, 4.5.3. SIZES)
 *
 * Revision 1.8  1996/04/16 14:58:58  tjd
 * tuned the scheduler; we now have a TARGET_RATE and the scheduler
 * dynamically adapts to try to meet it.
 *
 * Revision 1.7  1996/04/16 04:57:32  tjd
 * added defines for the scheduler (MAX_CHILD,MIN_CHILD)
 *
 * Revision 1.6  1996/04/15 16:36:42  tjd
 * added CONNECT_TIMEOUT for tcp connect() timeout
 * since linux's timeout is excruciatingly long...
 *
 * Revision 1.5  1996/04/14 22:00:54  tjd
 * changed default MAX_CHILD to 30.
 *
 * Revision 1.4  1996/03/21 19:28:53  tjd
 * added NULL_RETURN_PATH define
 *
 * Revision 1.3  1996/02/12 00:36:34  tjd
 * added BOUNCE_FILE define
 *
 * Revision 1.2  1995/12/27 18:06:50  tjd
 * added NO_DELIVERY
 *
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

/* debug stuff */

#define STATUS	100	/* status every n messages */

#define DEBUG_SMTP	/* SMTP deliver() debugging */
#undef DEBUG_SMTP_ALL	/* debug ALL SMTP sessions (highly unrecommended!) */

#undef NO_FORK		/* useful to debug deliver() */
#undef NO_DELIVERY	/* fake delivery if defined */
#undef ERROR_MESSAGES 	/* generate error messages */

/* general params */
#define HEADER_HEADER	"AWAD"
#define MPP		"mpp"

/* bounces file name */
#define BOUNCE_FILE	"mailer.bounces"

/* configuration file name */
#define CONFIG_FILE	"mailer.config"

/* do we want bounce mail from remote SMTPs to be discarded at the source?
 * defining this will send MAIL FROM:<>.  Probably not a good idea.
 */
#undef NULL_RETURN_PATH

/* this will embed the current address number in the message-id */
#define TWEAK_MSGID

/* list processing parameters */
#define MAX_ADDR_LEN    256	/* single address size limit: RFC821 */
#define MAX_LINE_LEN	1024	/* single message line limit: RFC821 */
#define MAX_HOSTNAME_LEN 64	/* hostname limit: RFC821 */
#if 1
#define ADDRS_PER_BUF   100	/* max # of addresses per buffer: RFC821 */
#else
#define ADDRS_PER_BUF   64	/* msn.com won't take 100 though it should! */
#endif
#define BUFFER_LEN   	4096	/* single delivery attempt buffer */
#define MAXMXHOSTS	20	/* max # of MX records */

/* scheduler parameters.  these can be overridden in the config file. */
#define MAX_CHILD	90	/* max # of deliver children */
#define MIN_CHILD	15	/* min # of deliver children */
#define TARGET_RATE	10000	/* target rate in deliveries per hour */

/* SMTP: timeouts as defined in RFC1123 */
#define CONNECT_TIMEOUT		300	/* timeout for tcp connect() */
#define SMTP_TIMEOUT_WELCOME	300
#define SMTP_TIMEOUT_HELO	300	/* not defined in 1123 */
#define SMTP_TIMEOUT_MAIL	300
#define SMTP_TIMEOUT_RCPT	300
#define SMTP_TIMEOUT_DATA	120
#define SMTP_TIMEOUT_END	600

#ifdef sun
#define memmove(D,S,L)	bcopy(S,D,L)
#endif
