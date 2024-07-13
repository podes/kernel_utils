
#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>


#define BUFFER_LEN    4096
static char final_buffer[ BUFFER_LEN ];
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static int log_level = LOG_ERR;

outer_log_fn outlog_fn = NULL;

static inline int
printk_get_level(const char *buffer)
{
    if (buffer[0] == KERN_SOH_ASCII && buffer[1]) {
        switch (buffer[1]) {
        case '0' ... '7':
        case 'c':   /* KERN_CONT */
            return buffer[1];
        }
    }
    return 0;
}

int
util_printk(const char *fmt, ...)
{
    const char *text = fmt;
    int kern_level;
    int level = LOG_INFO;
    va_list args;
    int r;

    while ((kern_level = printk_get_level(text)) != 0) {
        switch (kern_level) {
        case '0' ... '7':
            if (level == -1)
                    level = kern_level - '0';
                break;
        case 'c':   /* KERN_CONT */
                //lflags |= LOG_CONT;
                ;
       }

       text += 2;
    }

    va_start(args, fmt);                                                        
    r = util_vbsdlog(level, text, args);
    va_end(args); 
    return r;
}

void
util_panic(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    util_vbsdlog(LOG_EMERG, fmt, ap);
    va_end(ap);
    abort();
}

/* ------------------------------------------------------------ */

static int
do_vbsdlog( int n, const char * fmt, va_list arglist )
{
    static const char * const messages[] =
    {
        "EMERG", "ALERT", "CRIT", "ERR", "WARNING",
        "NOTICE", "INFO", "DEBUG"
    };

    int r0, r, tz_offset;
    struct timeval tv;
    struct tm tm;

    fflush(stdout);
    pthread_mutex_lock( & log_lock );
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    tz_offset = (int)(tm.tm_gmtoff/(60*60));
    r0 = snprintf(final_buffer, sizeof(final_buffer),
        "%04d-%02d-%02dT%02d:%02d:%02d.%06ld%+03d:00 [%s] ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec, tz_offset,
        messages[n]);
    r = vsnprintf(final_buffer+r0, sizeof(final_buffer)-r0, fmt, arglist);
    if (r >= 1) {
        r += r0;
        if (final_buffer[r-1] != '\n') {
            if (r + 2 <= (int)sizeof(final_buffer)) {
                final_buffer[r] = '\n';
                final_buffer[r+1] = '\0';
            } else {
                final_buffer[r-1] = '\n';
            }
        }
        fprintf(stderr, "%s", final_buffer);
    }
    pthread_mutex_unlock( & log_lock );
    return r;
}

int
util_do_vbsdlog( int n, const char * fmt, va_list arglist )
{
    return do_vbsdlog( n, fmt, arglist );
}

int
util_do_bsdlog(int n, const char * fmt, ...)
{
    va_list   arglist;
    int       ret;

    va_start( arglist, fmt );
    ret = ( do_vbsdlog( n, fmt, arglist ) );
    va_end( arglist );
    return ret;
}

void
util_set_loglevel(int level)
{
    log_level = level;
}

int
util_get_loglevel(void)
{
    return log_level;
}
