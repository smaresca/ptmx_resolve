/*
 *  Copyright (c) 2013      Steven Maresca <steve@zentific.com>
 *                2008-2010 Sam Hocevar <sam@hocevar.net>
 *                All Rights Reserved
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What The Fuck You Want
 *  To Public License, Version 2, as published by Sam Hocevar. See
 *  http://sam.zoy.org/wtfpl/COPYING for more details.
 */

#include <termios.h>

struct mytrace;

struct mytrace* mytrace_attach(long int pid);
struct mytrace* mytrace_fork(struct mytrace *t);
int mytrace_detach(struct mytrace *t);
long mytrace_getpid(struct mytrace *t);

int mytrace_open(struct mytrace *t, char const *path, int mode);
int mytrace_write(struct mytrace *t, int fd, char const *data, size_t len);
int mytrace_close(struct mytrace *t, int fd);
int mytrace_dup2(struct mytrace *t, int oldfd, int newfd);
int mytrace_setpgid(struct mytrace *t, long pid, long pgid);
int mytrace_setsid(struct mytrace *t);
int mytrace_kill(struct mytrace *t, long pid, int sig);
int mytrace_exit(struct mytrace *t, int status);
int mytrace_exec(struct mytrace *t, char const *command);
int mytrace_tcgets(struct mytrace *t, int fd, struct termios *tos);
int mytrace_tcsets(struct mytrace *t, int fd, struct termios *tos);
int mytrace_sctty(struct mytrace *t, int fd);
int mytrace_TIOCGPTN(struct mytrace *t, int fd, int *pts);
