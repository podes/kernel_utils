
#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include <stdarg.h>
#include <sys/syslog.h>
#include <stdio.h>
#include "ratelimit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KERN_SOH    	"\001"      /* ASCII Start Of Header */
#define KERN_SOH_ASCII  '\001'

#define KERN_EMERG  KERN_SOH "0"    /* system is unusable */
#define KERN_ALERT  KERN_SOH "1"    /* action must be taken immediately */
#define KERN_CRIT   KERN_SOH "2"    /* critical conditions */
#define KERN_ERR    KERN_SOH "3"    /* error conditions */
#define KERN_WARNING    KERN_SOH "4"    /* warning conditions */
#define KERN_NOTICE KERN_SOH "5"    /* normal but significant condition */
#define KERN_INFO   KERN_SOH "6"    /* informational */
#define KERN_DEBUG  KERN_SOH "7"    /* debug-level messages */
#define KERN_DEFAULT    ""      /* the default kernel loglevel */

/*
 * Annotation for a "continued" line of log printout (only done after a
 * line that had no enclosing \n). Only to be used by core/arch code
 * during early bootup (a continued line is not SMP-safe otherwise).
 */
#define KERN_CONT   KERN_SOH "c"

typedef void (*outer_log_fn)(const char* s);
extern outer_log_fn outlog_fn;

#define util_bsdlog(level, fmt, ...)                    \
    ({                                                  \
        int ret = 0;                                    \
        if (level <= util_get_loglevel()) {             \
            if (!outlog_fn) {                           \
                ret = util_do_bsdlog(level, (fmt), ##__VA_ARGS__);  \
            } else {                                    \
                char fin_buffer[256];            \
                snprintf(fin_buffer, 255, (fmt), ##__VA_ARGS__);    \
                outlog_fn(fin_buffer);                  \
            }                                           \
        }                                               \
        ret;                                           \
    })

#define util_vbsdlog(level, fmt, args)                  \
    ({                                                  \
        int ret = 0;                                    \
        if (level <= util_get_loglevel())               \
            ret = util_do_vbsdlog(level, fmt, args);    \
        ret;                                            \
    })

#define util_bsdlog_ratelimited(level, fmt, ...)            \
    ({                                                      \
        static struct timeval lastover;                     \
        static struct timeval overinterval = { 0, 1000 };   \
                                                            \
        if (util_ratecheck(&lastover, &overinterval))       \
            util_bsdlog(level, fmt, ##__VA_ARGS__);         \
    })

#define util_vbsdlog_ratelimited(level, fmt, args)          \
    ({                                                      \
        static struct timeval lastover;                     \
        static struct timeval overinterval = { 0, 1000 };   \
                                                            \
        if (util_ratecheck(&lastover, &overinterval))       \
            util_vbsdlog(level, fmt, args);                 \
    })

int util_get_loglevel(void);
void util_set_loglevel(int level);
int util_do_bsdlog(int level, const char * fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
int util_do_vbsdlog(int level, const char * fmt, va_list arglist);
int util_printk(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
void util_panic(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));

#define printk_ratelimited(fmt, ...)                    \
({                                                      \
    static struct timeval lastover;                     \
    static struct timeval overinterval = { 0, 1000 };   \
                                                        \
    if (util_ratecheck(&lastover, &overinterval))       \
        util_printk(fmt, ##__VA_ARGS__);                \
})

#define printk  util_printk
#define panic   util_panic

#define pr_alert(fmt, ...) util_bsdlog(LOG_ALERT, (fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...) util_bsdlog(LOG_CRIT, (fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) util_bsdlog(LOG_ERR, (fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) util_bsdlog(LOG_WARNING, (fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...) util_bsdlog(LOG_NOTICE, (fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) util_bsdlog(LOG_INFO, (fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...) util_bsdlog(LOG_DEBUG, (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
