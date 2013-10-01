#if defined DEBUG
#   include <stdio.h>
#   include <stdarg.h>
static inline void debug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "** neercs debug ** ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}
#else
#   define debug(format, ...) do {} while(0)
#endif

int ptsname_list_all(long pid, int **pts_ids, int *num_ids);
int ptsname_by_fd(long pid, int target_fd, int *pts_id);
