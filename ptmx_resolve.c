/*
 * Copyright 2013 
 *  Steven Maresca <steve@zentific.com>
 *  Zentific LLC
 *
 * ptmx_resolve:
 *  Given a PID, map each /proc/$PID/fd/$FD to corresponding
 *  /dev/pts/$PTS device node (if any)
 *
 * Error handing and a real build system may happen someday.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#include <errno.h>
#include "ptmx_resolve.h"

int main(int argc, char **argv) {
    long pid = -1;
    int pts_id = -1;
    int target_fd = -1; 

    if(argc < 2){
        goto err;
    }

    pid = strtol(argv[1], NULL, 10);

    if(errno) goto err;

    if (argv[2]) {
        target_fd = strtol(argv[2], NULL, 10);
        
        if(errno) goto err;

        int ret = ptsname_by_fd(pid, target_fd, &pts_id);

        printf("target_pid=%ld target_fd=%d pts=/dev/pts/%d\n",
                pid, target_fd, pts_id);
        
        return ret;
    }

    /* catchall case: list all /dev/pts paths discovered */
    int num_ids = 0;
    int *ids_array = NULL;
    int ret = ptsname_list_all(pid, &ids_array, &num_ids);

    printf("There were %d /dev/pts devices discovered for pid=%ld\n", num_ids, pid);
    for(;num_ids > 0; num_ids--){
        printf("target_pid=%ld pts=/dev/pts/%d\n", pid, ids_array[num_ids-1]);
    }
    free(ids_array);

    return ret;

err:
    printf("Usage: ptmx_resolve $PID [<optional> target file descriptor ID]\n");
    exit(1);
}
