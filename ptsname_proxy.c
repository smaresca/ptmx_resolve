/*
 * Copyright 2013 
 *  Steven Maresca <steve@zentific.com>
 *  Zentific LLC
 *
 * ptmx_resolve:
 *  Given a PID, map each /proc/$PID/fd/$FD to corresponding
 *  /dev/pts/$PTS device node (if any)
 *
 *  Portions excerpted from the neercs project
 *  Copyright (c) 2008-2010 Pascal Terjan <pterjan@linuxfr.org>
*/

#define _XOPEN_SOURCE 500       /* getsid() */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <limits.h>

#include <linux/kdev_t.h>
#include <linux/major.h>

#include "ptmx_resolve.h"
#include "mytrace.h"

#define MAX_PTYS 4096
/* this should be reasonable for most realistic scenarios 
 *  and is the kernel default
 *  https://lkml.org/lkml/2012/1/2/151
 */
int ptsname_list_all(long pid, int **pts_ids, int *num_ids) {
    char fdstr[1024];
    struct mytrace *parent, *child;
    int fd = 0;
    int ret = -1;
    struct stat stat_buf;
    DIR *fddir;
    struct dirent *fddirent;

    if(!pts_ids || !num_ids){
        fprintf(stderr, "%s - invalid params: pts_ids & num_ids must not be"
                " NULL\n", __FUNCTION__);
        return -1;
    }

    parent = mytrace_attach(pid);
    if (!parent) {
        fprintf(stderr, "%s - cannot access process %ld\n", __FUNCTION__, pid);
        return -1;
    }

    child = mytrace_fork(parent);

    *pts_ids = calloc(MAX_PTYS, sizeof(int));

    snprintf(fdstr, sizeof(fdstr), "/proc/%ld/fd", pid);
    fddir = opendir(fdstr);

    /* Look for file descriptors that are PTYs */
    while ((fddirent = readdir(fddir)) && *num_ids < MAX_PTYS) {
        fd = atoi(fddirent->d_name);

        snprintf(fdstr, sizeof(fdstr), "/proc/%ld/fd/%s", pid, fddirent->d_name);

        if (lstat(fdstr, &stat_buf) < 0)
            continue;

        char linkname[PATH_MAX+1] = {0};
        int rlnk = readlink(fdstr, linkname, PATH_MAX);
        linkname[PATH_MAX-1] = '\0';

        //if (!S_ISCHR(stat_buf.st_mode)
        //    || MAJOR(stat_buf.st_rdev) != UNIX98_PTY_SLAVE_MAJOR)
        //    continue;

        if(!linkname || !strstr("/dev/ptmx", linkname) || strlen(linkname) == 0) continue;

        debug("found %s for %d for pid %li\n", linkname, fd, pid);

        int pts_number = -1;
        ret = mytrace_TIOCGPTN(child, fd, &pts_number);
        if (ret < 0) {
            perror("mytrace_TIOCGPTN");
        } else {
            (*pts_ids)[*num_ids] = pts_number;
            *num_ids += 1;
        }
    }
    closedir(fddir);

wrap_up:
    mytrace_detach(parent);
    waitpid(pid, NULL, 0);      

    return ret;
}

int ptsname_by_fd(long pid, int target_fd, int *pts_id) {
    char fdstr[1024];
    struct mytrace *parent, *child;
    int ret = 0;
    struct stat stat_buf;
    char linkname[PATH_MAX+1] = {0};
    int pts_number = -1;

    parent = mytrace_attach(pid);
    if (!parent) {
        fprintf(stderr, "%s - cannot access process %ld\n", __FUNCTION__, pid);
        return -1;
    }

    child = mytrace_fork(parent);

    snprintf(fdstr, sizeof(fdstr), "/proc/%ld/fd", pid);

    /* Inspect requested file descriptor, ensuring it is a PTY */
    snprintf(fdstr, sizeof(fdstr), "/proc/%ld/fd/%d", pid, target_fd);

    if (lstat(fdstr, &stat_buf) < 0){
        ret = -1;
        goto wrap_up;
    }

    int rlnk = readlink(fdstr, linkname, PATH_MAX);
    linkname[PATH_MAX-1] = '\0';

    //if (!S_ISCHR(stat_buf.st_mode)
    //    || MAJOR(stat_buf.st_rdev) != UNIX98_PTY_SLAVE_MAJOR)
    //    continue;

    if(!linkname || !strstr("/dev/ptmx", linkname) || strlen(linkname) == 0) {
        ret = -1;
        goto wrap_up;
    }

    debug("found %s for %d for pid %li\n", linkname, target_fd, pid);

    ret = mytrace_TIOCGPTN(child, target_fd, &pts_number);
    if (ret < 0) {
        perror("mytrace_TIOCGPTN");
    } else {
        *pts_id = pts_number;
    }

wrap_up:
    mytrace_detach(parent);
    waitpid(pid, NULL, 0);      

    return ret; 
}
