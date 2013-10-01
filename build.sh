#!/bin/bash

gcc -o ptmx_resolve ptmx_resolve.c ptsname_proxy.c  mytrace.c 
#gcc -o ptmx_resolve ptmx_resolve.c ptsname_proxy.c  mytrace.c -DDEBUG=1
