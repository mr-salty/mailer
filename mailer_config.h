/*
 * $Id: mailer_config.h,v 1.19 2004/02/09 15:53:40 tjd Exp $
 */

/* debug stuff */

#define STATUS	100		/* status every n messages */

#define DEBUG_SMTP		/* SMTP deliver() debugging */
#undef DEBUG_SMTP_ALL		/* debug ALL SMTP sessions (highly unrecommended!) */

#undef NO_FORK			/* useful to debug deliver() */
#undef NO_DELIVERY		/* fake delivery if defined */
#undef ERROR_MESSAGES		/* generate error messages */

/* general params */
#define HEADER_HEADER	"AWAD"
#define MPP		"mpp"

/* bounces file name */
#define BOUNCE_FILE	"mailer.bounces"

/* configuration file name */
#define CONFIG_FILE	"mailer.config"

/* flags specified in config file */
typedef short flags_t;

#define FL_NONE		(0x00)
#define FL_DEBUG	(0x01)
#define FL_URL_BODY	(0x02)  /* add 'unsubscribe' url to the body */

/* these require USE_IDTAGS to be defined to have any effect */
#define FL_IDTAG_MSGID	(0x10)	/* unimplemented (on), see TWEAK_MSGID */
#define FL_IDTAG_RECV	(0x20)	/* unimplemented (on), see TWEAK_RCVHDR */
#define FL_IDTAG_FROM	(0x40)	/* unimplemented, see TWEAK_FROMADDR */
#define FL_IDTAG_BODY	(0x80)	/* works if TWEAK_BODY is defined */

/* default flags */
#define FL_DEFAULT	(FL_IDTAG_MSGID|FL_IDTAG_RECV|FL_URL_BODY)

/* macros for setting/clearing/querying flags */
#define FLAGS_CLEAR(flags)	(flags = 0)
#define FLAG_SET(flags,flag)	(flags |= flag)
#define FLAG_UNSET(flags,flag)	(flags &= ~flag)
#define FLAG_ISSET(flags,flag)	(flags & flag)

#define BATCH_DEFAULT	(-1)

/* use ID tags? */
#define USE_IDTAGS

#ifdef USE_IDTAGS
/* this will embed the current address number in the message-id */
#define TWEAK_MSGID

/* this will embed the id tag in the Received: header SMTP id */
#define TWEAK_RCVHDR

/* this will embed the message-id in the from address if batchsize==1 */
/* #define TWEAK_FROMADDR */

/* this will embed the message-id in the body if FL_IDTAG_BODY is set */
#define TWEAK_BODY
#endif				/* USE_IDTAGS */

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
#define CONNECT_TIMEOUT		30	/* timeout for tcp connect() */
#define SMTP_TIMEOUT_WELCOME	30
#define SMTP_TIMEOUT_HELO	30	/* not defined in 1123 */
#define SMTP_TIMEOUT_MAIL	30
#define SMTP_TIMEOUT_RCPT	30
#define SMTP_TIMEOUT_DATA	120
#define SMTP_TIMEOUT_END	600

/* how long (in sec) to wait for children to finish at the end of processing */
#define END_WAIT_TIMEOUT	(20 * 60)

#ifdef sun
#define memmove(D,S,L)	bcopy(S,D,L)
#endif
