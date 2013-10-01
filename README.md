ptmx_resolve
============

  For a given PID, resolve file descriptors in /proc/$PID/fd to their underlying /dev/pts/$X dynamically allocated pty

  Usage: ptmx_resolve $PID [<optional> target file descriptor ID]

  Elevated privileges is required.

Background:

  Pseudoterminals in use by $PID are difficult to resolve from a perspective outside of the running program.
  Within the actual process, these ptys are referenced only by file descriptors.  Such file descriptors
  correspond to a dynamically created path of the actual pseudoterminal, e.g., /dev/pts/12. These descriptors
  are obtained at runtime by opening /dev/ptmx, implicitly via getpt(3) / posix_openpt(3), or manually via
  open("/dev/ptmx", O_RDWR | O_NOCTTY). Within a running program, psuedoterminal file descriptors can be
  passed to ptsname(3) to obtain the corresponding device path, e.g. /dev/pts/12.

Motivation:

  Occasionally, one desires to connect to a dynamic pseudoterminal bound by a program. For example, it is often
  useful for advanced management of virtual machines to connect to the qemu monitor via a serial emulator such
  as screen or minicom. Unfortunately, from a context external to a running process, there is no mechanism to
  acquire the allocated pseudoterminal unless the program logs this information or otherwise makes it available.
  Indeed, the path to a pty is not accessible even via the data shown in /proc/$PID/fd for a running process.
  Furthermore, inspecting /proc/$PID/fd shows that these file descriptors are pseudo-symlinks to /dev/ptmx,
  rather than revealing the device path in /dev/pts.
  
  This rather godawful little utility exists to solve this problem.
  
How it works:

  This utility :
    1) finds file descriptors of a process that are pseudotermianl candidates (i.e., they
      reference /dev/ptmx when the /proc/$PID/fd/$FD symlink is resolved), then 
    2) attaches to a running process using ptrace()
    3) creates a child process that can be used sacrificially to obtain access to the parent's file descriptors
    3) injects a system call to obtain the path in /dev/pts, in essence performing the ioctl used internally
      by ptsname()
    4) restores process state and resumes the program
    
Final comments:

  Please forgive me for this abomination. It should not be used in any production context, ever...most especially
  because of process-thread equivalence and the fairly braindead nature of ptrace().
  
  It does work, however. 
  
  That said, if someone finds a better way, please for the love of god let me know what it is.
  
  
Copyright 2013
Author Steve Maresca <steve@zentific.com>

Thanks go to Sam Hocevar et al and neercs for some of the helpful tracing abstraction around ptrace().
