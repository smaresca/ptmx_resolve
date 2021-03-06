/*
 *  Copyright (c) 2013      Steven Maresca <steve@zentific.com>
 *                2008-2010 Pascal Terjan <pterjan@linuxfr.org>
 *                2008-2010 Sam Hocevar <sam@hocevar.net>
 *                All Rights Reserved
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What The Fuck You Want
 *  To Public License, Version 2, as published by Sam Hocevar. See
 *  http://sam.zoy.org/wtfpl/COPYING for more details.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>

#include "ptmx_resolve.h"
#include "mytrace.h"

static int memcpy_from_target(struct mytrace *t,
                              char *dest, long src, size_t n);
static int memcpy_into_target(struct mytrace *t,
                              long dest, char const *src, size_t n);
static long remote_syscall(struct mytrace *t, long call,
                           long arg1, long arg2, long arg3);
#   if defined DEBUG
static void print_registers(pid_t pid);
#   else
#       define print_registers(x) do {} while(0)
#   endif

#define X(x) #x
#define STRINGIFY(x) X(x)

#define SYSCALL_X86     0x80cd  /* CD 80 = int $0x80 */
#define SYSCALL_X86_NEW 0xf3eb  /* EB F3 = jmp <__kernel_vsyscall+0x3> */
#define SYSENTER        0x340f  /* 0F 34 = sysenter */
#define SYSCALL_AMD64   0x050fL /* 0F 05 = syscall */

#if defined __x86_64__
#   define RAX rax
#   define RBX rbx
#   define RCX rcx
#   define RDX rdx
#   define RSP rsp
#   define RBP rbp
#   define RIP rip
#   define RDI rdi
#   define RSI rsi
#   define FMT "%016lx"
#else
#   define RAX eax
#   define RBX ebx
#   define RCX ecx
#   define RDX edx
#   define RSP esp
#   define RBP ebp
#   define RIP eip
#   define RDI edi
#   define RSI esi
#   define FMT "%08lx"
#endif

#define MYCALL_OPEN     0
#define MYCALL_CLOSE    1
#define MYCALL_WRITE    2
#define MYCALL_DUP2     3
#define MYCALL_SETPGID  4
#define MYCALL_SETSID   5
#define MYCALL_KILL     6
#define MYCALL_FORK     7
#define MYCALL_EXIT     8
#define MYCALL_EXECVE   9
#define MYCALL_IOCTL   10

#if defined __x86_64__
/* from unistd_32.h on an amd64 system */
int syscalls32[] = { 5, 6, 4, 63, 57, 66, 37, 2, 1, 11, 54 };

int syscalls64[] =
#else
int syscalls32[] =
#endif
{ SYS_open, SYS_close, SYS_write, SYS_dup2, SYS_setpgid, SYS_setsid,
    SYS_kill, SYS_fork, SYS_exit, SYS_execve, SYS_ioctl
};

char const *syscallnames[] =
    { "open", "close", "write", "dup2", "setpgid", "setsid", "kill", "fork",
    "exit", "execve", "ioctl"
};

struct mytrace
{
    pid_t pid, child;
};

struct mytrace *mytrace_attach(long int pid)
{
    struct mytrace *t;
    int status;

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0)
    {
        perror("PTRACE_ATTACH (attach)");
        return NULL;
    }
    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return NULL;
    }
    if (!WIFSTOPPED(status))
    {
        fprintf(stderr, "traced process was not stopped\n");
        ptrace(PTRACE_DETACH, pid, 0, 0);
        return NULL;
    }

    t = malloc(sizeof(struct mytrace));
    t->pid = pid;
    t->child = 0;

    return t;
}

struct mytrace *mytrace_fork(struct mytrace *t)
{
    struct mytrace *child;

    ptrace(PTRACE_SETOPTIONS, t->pid, NULL, PTRACE_O_TRACEFORK);
    remote_syscall(t, MYCALL_FORK, 0, 0, 0);
    waitpid(t->child, NULL, 0);

    child = malloc(sizeof(struct mytrace));
    child->pid = t->child;
    child->child = 0;

    return child;
}

int mytrace_detach(struct mytrace *t)
{
    ptrace(PTRACE_DETACH, t->pid, 0, 0);
    free(t);

    return 0;
}

long mytrace_getpid(struct mytrace *t)
{
    return t->pid;
}

int mytrace_open(struct mytrace *t, char const *path, int mode)
{
    char backup_data[4096];
    struct user_regs_struct regs;
    size_t size = strlen(path) + 1;
    int ret, err;

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (open)\n");
        return -1;
    }

    /* Backup the data that we will use */
    if (memcpy_from_target(t, backup_data, regs.RSP, size) < 0)
        return -1;

    memcpy_into_target(t, regs.RSP, path, size);

    ret = remote_syscall(t, MYCALL_OPEN, regs.RSP, O_RDWR, 0755);
    err = errno;

    /* Restore the data */
    memcpy_into_target(t, regs.RSP, backup_data, size);

    errno = err;
    return ret;
}

int mytrace_close(struct mytrace *t, int fd)
{
    return remote_syscall(t, MYCALL_CLOSE, fd, 0, 0);
}

int mytrace_write(struct mytrace *t, int fd, char const *data, size_t len)
{
    struct user_regs_struct regs;
    char *backup_data;
    int ret, err;

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (write)\n");
        return -1;
    }

    backup_data = malloc(len);

    /* Backup the data that we will use */
    if (memcpy_from_target(t, backup_data, regs.RSP, len) < 0)
        return -1;

    memcpy_into_target(t, regs.RSP, data, len);

    ret = remote_syscall(t, MYCALL_WRITE, fd, regs.RSP, len);
    err = errno;

    /* Restore the data */
    memcpy_into_target(t, regs.RSP, backup_data, len);

    errno = err;
    return ret;
}

int mytrace_dup2(struct mytrace *t, int oldfd, int newfd)
{
    return remote_syscall(t, MYCALL_DUP2, oldfd, newfd, 0);
}

int mytrace_setpgid(struct mytrace *t, long pid, long pgid)
{
    return remote_syscall(t, MYCALL_SETPGID, pid, pgid, 0);
}

int mytrace_setsid(struct mytrace *t)
{
    return remote_syscall(t, MYCALL_SETSID, 0, 0, 0);
}

int mytrace_kill(struct mytrace *t, long pid, int sig)
{
    return remote_syscall(t, MYCALL_KILL, pid, sig, 0);
}

int mytrace_exit(struct mytrace *t, int status)
{
    ptrace(PTRACE_SETOPTIONS, t->pid, NULL, PTRACE_O_TRACEEXIT);
    return remote_syscall(t, MYCALL_EXIT, status, 0, 0);
}

int mytrace_exec(struct mytrace *t, char const *command)
{
    struct user_regs_struct regs;
    char *env, *p;
    long p2, envaddr, argvaddr, envptraddr;
    char envpath[PATH_MAX + 1];
    ssize_t envsize = 16 * 1024;
    int ret, fd, l, l2;
    char *nullp = NULL;
    ssize_t r;

    ptrace(PTRACE_SETOPTIONS, t->pid, NULL, PTRACE_O_TRACEEXEC);

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (exec)\n");
        return -1;
    }

    debug("PTRACE_GETREGS done");
    env = malloc(envsize);
    if (!env)
        return -1;

    snprintf(envpath, PATH_MAX, "/proc/%d/environ", t->pid);

    fd = open(envpath, O_RDONLY);
    if (fd == -1)
        return -1;
    r = read(fd, env, envsize);
    close(fd);
    if (r == -1)
        return -1;
    while (r == envsize)
    {
        free(env);
        env = malloc(envsize);
        if (!env)
            return -1;
        fd = open(envpath, O_RDONLY);
        r = read(fd, env, envsize);
        close(fd);
        if (r == -1)
            return -1;
    }
    envsize = r;
    l2 = sizeof(char *);        /* Size of a pointer */
    p2 = regs.RSP;

    /* First argument is the command string */
    l = strlen(command) + 1;
    memcpy_into_target(t, p2, command, l);
    p2 += l;

    /* Second argument is argv */
    argvaddr = p2;
    /* argv[0] is a pointer to the command string */
    memcpy_into_target(t, p2, (char *)&regs.RSP, l2);
    p2 += l2;
    /* Then follows a NULL pointer */
    memcpy_into_target(t, p2, (char *)&nullp, l2);
    p2 += l2;

    /* Third argument is the environment */
    /* First, copy all the strings */
    memcpy_into_target(t, p2, env, envsize);
    envaddr = p2;
    p2 += envsize;
    /* Then write an array of pointers to the strings */
    envptraddr = p2;
    p = env;
    while (p < env + envsize)
    {
        long diffp = p - env + envaddr;
        memcpy_into_target(t, p2, (char *)&diffp, l2);
        p2 += l2;
        p += strlen(p) + 1;
    }
    /* And have a NULL pointer at the end of the array */
    memcpy_into_target(t, p2, (char *)&nullp, l2);
    free(env);

    ret = remote_syscall(t, MYCALL_EXECVE, regs.RSP, argvaddr, envptraddr);

    return ret;
}

/* Added 2013-09-17 by S. Maresca */
int mytrace_TIOCGPTN(struct mytrace *t, int fd, int *pts)
{
    char backup_data[4096];
    struct user_regs_struct regs;
    size_t size = sizeof(int);
    int ret, err;

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (TIOCGPTN)\n");
        return -1;
    }

    /* Backup the data that we will use */
    if (memcpy_from_target(t, backup_data, regs.RSP, size) < 0)
        return -1;

    ret = remote_syscall(t, MYCALL_IOCTL, fd, TIOCGPTN, regs.RSP);
    err = errno;

    memcpy_from_target(t, (char *)pts, regs.RSP, size);

    /* Restore the data */
    memcpy_into_target(t, regs.RSP, backup_data, size);

    errno = err;
    return ret;
}
int mytrace_tcgets(struct mytrace *t, int fd, struct termios *tos)
{
    char backup_data[4096];
    struct user_regs_struct regs;
    size_t size = sizeof(struct termios);
    int ret, err;

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (tcgets)\n");
        return -1;
    }

    /* Backup the data that we will use */
    if (memcpy_from_target(t, backup_data, regs.RSP, size) < 0)
        return -1;

    ret = remote_syscall(t, MYCALL_IOCTL, fd, TCGETS, regs.RSP);
    err = errno;

    memcpy_from_target(t, (char *)tos, regs.RSP, size);

    /* Restore the data */
    memcpy_into_target(t, regs.RSP, backup_data, size);

    errno = err;
    return ret;
}

int mytrace_tcsets(struct mytrace *t, int fd, struct termios *tos)
{
    char backup_data[4096];
    struct user_regs_struct regs;
    size_t size = sizeof(struct termios);
    int ret, err;

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (tcsets)\n");
        return -1;
    }

    /* Backup the data that we will use */
    if (memcpy_from_target(t, backup_data, regs.RSP, size) < 0)
        return -1;

    memcpy_into_target(t, regs.RSP, (char *)tos, size);

    ret = remote_syscall(t, MYCALL_IOCTL, fd, TCSETS, regs.RSP);
    err = errno;

    /* Restore the data */
    memcpy_into_target(t, regs.RSP, backup_data, size);

    errno = err;
    return ret;
}

int mytrace_sctty(struct mytrace *t, int fd)
{
    ptrace(PTRACE_SETOPTIONS, t->pid, NULL, PTRACE_O_TRACEEXIT);
    return remote_syscall(t, MYCALL_IOCTL, fd, TIOCSCTTY, 0);
}

/*
 * XXX: the following functions are local
 */

static int memcpy_from_target(struct mytrace *t,
                              char *dest, long src, size_t n)
{
    static int const align = sizeof(long) - 1;

    while (n)
    {
        long data;
        size_t todo = sizeof(long) - (src & align);

        if (n < todo)
            todo = n;

        data = ptrace(PTRACE_PEEKTEXT, t->pid, src - (src & align), 0);
        if (errno)
        {
            perror("ptrace_peektext (memcpy_from_target)");
            return -1;
        }
        memcpy(dest, (char *)&data + (src & align), todo);

        dest += todo;
        src += todo;
        n -= todo;
    }

    return 0;
}

static int memcpy_into_target(struct mytrace *t,
                              long dest, char const *src, size_t n)
{
    static int const align = sizeof(long) - 1;

    while (n)
    {
        long data;
        size_t todo = sizeof(long) - (dest & align);

        if (n < todo)
            todo = n;
        if (todo != sizeof(long))
        {
            data = ptrace(PTRACE_PEEKTEXT, t->pid, dest - (dest & align), 0);
            if (errno)
            {
                perror("ptrace_peektext (memcpy_into_target)");
                return -1;
            }
        }

        memcpy((char *)&data + (dest & align), src, todo);
        if (ptrace(PTRACE_POKETEXT, t->pid, dest - (dest & align), data) < 0)
        {
            perror("ptrace_poketext (memcpy_into_target)");
            return -1;
        }

        src += todo;
        dest += todo;
        n -= todo;
    }

    return 0;
}

static long remote_syscall(struct mytrace *t, long call,
                           long arg1, long arg2, long arg3)
{
    /* Method for remote syscall: - wait until the traced application exits
       from a syscall - save registers - rewind eip/rip to point on the
       syscall instruction - single step: execute syscall instruction -
       retrieve resulting registers - restore registers */
    struct user_regs_struct regs, oldregs;
    long oinst;
    int bits;
    int offset = 2;

    if (call < 0
        || call >= (long)(sizeof(syscallnames) / sizeof(*syscallnames)))
    {
        fprintf(stderr, "unknown remote syscall %li\n", call);
        return -1;
    }

    debug("remote syscall %s(0x%lx, 0x%lx, 0x%lx)",
          syscallnames[call], arg1, arg2, arg3);

#if defined __x86_64__
    bits = 64;
#else
    bits = 32;
#endif

    for (;;)
    {
        if (ptrace(PTRACE_GETREGS, t->pid, NULL, &oldregs) < 0)
        {
            perror("PTRACE_GETREGS (syscall 1)\n");
            return -1;
        }

        oinst = ptrace(PTRACE_PEEKTEXT, t->pid, oldregs.RIP - 2, 0) & 0xffff;

#if defined __x86_64__
        if (oinst == SYSCALL_AMD64)
            break;
#endif
        if (oinst == SYSCALL_X86 || oinst == SYSCALL_X86_NEW)
        {
            bits = 32;
            break;
        }

        if (ptrace(PTRACE_SYSCALL, t->pid, NULL, 0) < 0)
        {
            perror("ptrace_syscall (1)");
            return -1;
        }
        waitpid(t->pid, NULL, 0);
        if (ptrace(PTRACE_SYSCALL, t->pid, NULL, 0) < 0)
        {
            perror("ptrace_syscall (2)");
            return -1;
        }
        waitpid(t->pid, NULL, 0);
    }

    print_registers(t->pid);

    if (oinst == SYSCALL_X86_NEW)
    {
        /* Get back to sysenter */
        while ((ptrace(PTRACE_PEEKTEXT, t->pid, oldregs.RIP - offset, 0) &
                0xffff) != 0x340f)
            offset++;
        oldregs.RBP = oldregs.RSP;
    }

    regs = oldregs;
    regs.RIP = regs.RIP - offset;
#if defined __x86_64__
    if (bits == 64)
    {
        regs.RAX = syscalls64[call];
        regs.RDI = arg1;
        regs.RSI = arg2;
        regs.RDX = arg3;
    }
    else
#endif
    {
        regs.RAX = syscalls32[call];
        regs.RBX = arg1;
        regs.RCX = arg2;
        regs.RDX = arg3;
    }

    if (ptrace(PTRACE_SETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_SETREGS (syscall 1)\n");
        return -1;
    }

    for (;;)
    {
        int status;

        print_registers(t->pid);

        if (ptrace(PTRACE_SINGLESTEP, t->pid, NULL, NULL) < 0)
        {
            perror("PTRACE_SINGLESTEP (syscall)\n");
            return -1;
        }
        waitpid(t->pid, &status, 0);

        if (WIFEXITED(status))
            return 0;

        if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP)
            continue;

        /* Fuck Linux: there is no macro for this */
        switch ((status >> 16) & 0xffff)
        {
        case PTRACE_EVENT_FORK:
            if (ptrace(PTRACE_GETEVENTMSG, t->pid, 0, &t->child) < 0)
            {
                perror("PTRACE_GETEVENTMSG (syscall)\n");
                return -1;
            }
            debug("PTRACE_GETEVENTMSG %d", t->child);
            continue;
        case PTRACE_EVENT_EXIT:
            debug("PTRACE_EVENT_EXIT");
            /* The process is about to exit, don't do anything else */
            return 0;
        case PTRACE_EVENT_EXEC:
            debug("PTRACE_EVENT_EXEC");
            return 0;
        }

        break;
    }

    print_registers(t->pid);

    if (ptrace(PTRACE_GETREGS, t->pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (syscall 2)\n");
        return -1;
    }

    if (ptrace(PTRACE_SETREGS, t->pid, NULL, &oldregs) < 0)
    {
        perror("PTRACE_SETREGS (syscall 2)\n");
        return -1;
    }
    print_registers(t->pid);

    debug("syscall %s returned %ld", syscallnames[call], regs.RAX);

    if ((long)regs.RAX < 0)
    {
        errno = -(long)regs.RAX;
        perror("syscall");
        return -1;
    }

    return regs.RAX;
}

/* For debugging purposes only. Prints register and stack information. */
#if defined DEBUG
static void print_registers(pid_t pid)
{
    union
    {
        long int l;
        unsigned char data[sizeof(long int)];
    } inst;
    struct user_regs_struct regs;
    int i;

    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0)
    {
        perror("PTRACE_GETREGS (syscall 2)");
        exit(errno);
    }

    fprintf(stderr, "  / %s: " FMT "   ", STRINGIFY(RAX), regs.RAX);
    fprintf(stderr, "%s: " FMT "\n", STRINGIFY(RBX), regs.RBX);
    fprintf(stderr, "  | %s: " FMT "   ", STRINGIFY(RCX), regs.RCX);
    fprintf(stderr, "%s: " FMT "\n", STRINGIFY(RDX), regs.RDX);
    fprintf(stderr, "  | %s: " FMT "   ", STRINGIFY(RDI), regs.RDI);
    fprintf(stderr, "%s: " FMT "\n", STRINGIFY(RSI), regs.RSI);
    fprintf(stderr, "  | %s: " FMT "   ", STRINGIFY(RSP), regs.RSP);
    fprintf(stderr, "%s: " FMT "\n", STRINGIFY(RIP), regs.RIP);

    inst.l = ptrace(PTRACE_PEEKTEXT, pid, regs.RIP - 4, 0);
    fprintf(stderr, "  | code: ... %02x %02x %02x %02x <---> ",
            inst.data[0], inst.data[1], inst.data[2], inst.data[3]);
    inst.l = ptrace(PTRACE_PEEKTEXT, pid, regs.RIP, 0);
    fprintf(stderr, "%02x %02x %02x %02x ...\n",
            inst.data[0], inst.data[1], inst.data[2], inst.data[3]);

    fprintf(stderr, "  \\ stack: ... ");
    for (i = -16; i < 24; i += sizeof(long))
    {
        inst.l = ptrace(PTRACE_PEEKDATA, pid, regs.RSP + i, 0);
#if defined __x86_64__
        fprintf(stderr, "%02x %02x %02x %02x %02x %02x %02x %02x ",
                inst.data[0], inst.data[1], inst.data[2], inst.data[3],
                inst.data[4], inst.data[5], inst.data[6], inst.data[7]);
#else
        fprintf(stderr, "%02x %02x %02x %02x ",
                inst.data[0], inst.data[1], inst.data[2], inst.data[3]);
#endif
        if (i == 0)
            fprintf(stderr, "[%s] ", STRINGIFY(RSP));
    }
    fprintf(stderr, "...\n");
}
#endif /* DEBUG */
