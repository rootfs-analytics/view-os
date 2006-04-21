/*   This is part of um-ViewOS
 *   The user-mode implementation of OSVIEW -- A Process with a View
 *
 *   scmap: map for system call wrappers
 *   
 *   Copyright 2005 Renzo Davoli University of Bologna - Italy
 *   
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *   $Id$
 *
 */   

#include <unistd.h>
#include <asm/unistd.h>
#include <linux/net.h>
#include "defs.h"
#include "scmap.h"
#include "uid16to32.h"

int scmap_scmapsize;
int scmap_sockmapsize;

serfunt choice_path, choice_link, choice_fd, choice_socket, choice_link2;
serfunt always_umnone, choice_mount, choice_sc;
wrapinfun wrap_in_getcwd, wrap_in_chdir, wrap_in_fchdir;
wrapinfun wrap_in_open, wrap_in_read, wrap_in_write, wrap_in_close;
wrapinfun wrap_in_select, wrap_in_poll, wrap_in_ioctl;
wrapinfun wrap_in_readv, wrap_in_writev;
wrapinfun wrap_in_stat, wrap_in_fstat;
wrapinfun wrap_in_stat64, wrap_in_fstat64;
wrapinfun wrap_in_getxattr;
wrapinfun wrap_in_readlink, wrap_in_getdents, wrap_in_access;
wrapinfun wrap_in_fcntl, wrap_in_notsupp, wrap_in_llseek, wrap_in_lseek;
wrapinfun wrap_in_mkdir, wrap_in_unlink, wrap_in_chown, wrap_in_fchown;
wrapinfun wrap_in_chmod, wrap_in_fchmod, wrap_in_dup, wrap_in_fsync;
wrapinfun wrap_in_link, wrap_in_symlink, wrap_in_pread, wrap_in_pwrite;
wrapinfun wrap_in_utime, wrap_in_mount, wrap_in_umount;
wrapinfun wrap_in_umask, wrap_in_chroot;
wrapinfun wrap_in_umask, wrap_in_execve;

wrapoutfun wrap_out_open, wrap_out_std, wrap_out_close, wrap_out_chdir;
wrapoutfun wrap_out_dup, wrap_out_select, wrap_out_poll, wrap_out_fcntl;
wrapoutfun wrap_out_execve;

#ifdef PIVOTING_TEST
wrapinfun wrap_in_getpid;
wrapoutfun wrap_out_getpid;
#endif

/* we should keep this structure unique. the indexes can be used to forward
 * the call on a different computer.*/

#define __NR_doesnotexist -1
#if defined(__x86_64__)
#define __NR__newselect __NR_doesnotexist
#define __NR_umount __NR_doesnotexist
#define __NR_stat64 __NR_doesnotexist
#define __NR_lstat64 __NR_doesnotexist
#define __NR_fstat64 __NR_doesnotexist
#define __NR_chown32 __NR_doesnotexist
#define __NR_lchown32 __NR_doesnotexist
#define __NR_fchown32 __NR_doesnotexist
#define __NR_fcntl64 __NR_doesnotexist
#define __NR__llseek __NR_doesnotexist
#endif

struct sc_map scmap[]={
	/*{__NR_execve,	always_umnone,	wrap_in_execve,	NULL,	0,	3},*/
  	{__NR_execve,	choice_path,	wrap_in_execve,	wrap_out_execve,	0,	3, SOC_NONE},
	{__NR_chdir,	choice_path,	wrap_in_chdir,	wrap_out_chdir, ALWAYS,	1, SOC_FILE},
	{__NR_fchdir,	choice_fd,	wrap_in_fchdir,	wrap_out_chdir, ALWAYS,	1, SOC_FILE},
	{__NR_getcwd,	always_umnone, wrap_in_getcwd,	wrap_out_std,	ALWAYS,	2, SOC_NONE},
	{__NR_open,	choice_path,	wrap_in_open,	wrap_out_open,	ALWAYS,	3, SOC_FILE},
	{__NR_creat,	choice_path,	wrap_in_open,	wrap_out_open,	ALWAYS,	2, SOC_FILE},
	{__NR_close,	choice_fd,	wrap_in_close,	wrap_out_close,	ALWAYS,	1, SOC_FILE|SOC_NET},
	{__NR_select,	always_umnone,	wrap_in_select,	wrap_out_select,ALWAYS,	5, SOC_FILE|SOC_NET},
	{__NR_poll,	always_umnone,	wrap_in_poll,	wrap_out_poll,  ALWAYS,	3, SOC_FILE|SOC_NET},
	{__NR__newselect,always_umnone,	wrap_in_select,	wrap_out_select,ALWAYS,	5, SOC_FILE|SOC_NET},
	{__NR_umask,	always_umnone,	wrap_in_umask,  wrap_out_std,	ALWAYS,	1, SOC_FILE|SOC_NET},
	{__NR_chroot,	always_umnone,	wrap_in_chroot, wrap_out_std,	ALWAYS,	1, SOC_FILE|SOC_NET},
	{__NR_dup,	choice_fd,	wrap_in_dup,	wrap_out_dup,	ALWAYS,	1, SOC_FILE|SOC_NET},
	{__NR_dup2,	choice_fd,	wrap_in_dup,	wrap_out_dup,	ALWAYS,	2, SOC_FILE|SOC_NET},
	{__NR_mount,	choice_mount,	wrap_in_mount,	wrap_out_std,	0,	5, SOC_FILE},
	{__NR_umount,	choice_path,	wrap_in_umount,	wrap_out_std,	0,	1, SOC_FILE},
	{__NR_umount2,	choice_path,	wrap_in_umount,	wrap_out_std,	0,	2, SOC_FILE},
	{__NR_ioctl,	choice_fd,	wrap_in_ioctl,	wrap_out_std, 	0,	3, SOC_FILE},
	{__NR_read,	choice_fd,	wrap_in_read,	wrap_out_std,	0,	3, SOC_FILE|SOC_NET},
	{__NR_write,	choice_fd,	wrap_in_write,	wrap_out_std,	0,	3, SOC_FILE|SOC_NET},
	{__NR_readv,	choice_fd,	wrap_in_readv,	wrap_out_std,	0,	3, SOC_FILE|SOC_NET},
	{__NR_writev,	choice_fd,	wrap_in_writev,	wrap_out_std,	0,	3, SOC_FILE|SOC_NET},
	{__NR_stat,	choice_path,	wrap_in_stat,	wrap_out_std,	0,	2, SOC_FILE|SOC_NET},
	{__NR_lstat,	choice_link,	wrap_in_stat,	wrap_out_std,	0,	2, SOC_FILE|SOC_NET},
	{__NR_fstat,	choice_fd,	wrap_in_fstat,	wrap_out_std,	0,	2, SOC_FILE|SOC_NET},
	{__NR_stat64,	choice_path,	wrap_in_stat64,	wrap_out_std,	0,	2, SOC_FILE|SOC_NET},
	{__NR_lstat64,	choice_link,	wrap_in_stat64,	wrap_out_std,	0,	2, SOC_FILE|SOC_NET},
	{__NR_fstat64,	choice_fd,	wrap_in_fstat64,wrap_out_std,	0,	2, SOC_FILE|SOC_NET},
	{__NR_chown,	choice_path,	wrap_in_chown, wrap_out_std,	0,	3, SOC_FILE|SOC_UID},
	{__NR_lchown,	choice_link,	wrap_in_chown, wrap_out_std,	0,	3, SOC_FILE|SOC_UID},
	{__NR_fchown,	choice_fd,	wrap_in_fchown, wrap_out_std,	0,	3, SOC_FILE|SOC_UID},
	{__NR_chown32,	choice_path,	wrap_in_chown, wrap_out_std,	0,	3, SOC_FILE|SOC_UID},
	{__NR_lchown32,	choice_link,	wrap_in_chown, wrap_out_std,	0,	3, SOC_FILE|SOC_UID},
	{__NR_fchown32,	choice_fd,	wrap_in_fchown, wrap_out_std,	0,	3, SOC_FILE|SOC_UID},
	{__NR_chmod,	choice_path,	wrap_in_chmod, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_fchmod,	choice_fd,	wrap_in_fchmod, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_getxattr,	choice_path,	wrap_in_getxattr, wrap_out_std,	0,	4, SOC_FILE},
	{__NR_lgetxattr,choice_link,	wrap_in_notsupp, wrap_out_std,	0,	4, SOC_FILE},
	{__NR_fgetxattr,choice_fd,	wrap_in_notsupp, wrap_out_std,	0,	4, SOC_FILE},
	{__NR_readlink,	choice_link,	wrap_in_readlink,wrap_out_std,	0,	3, SOC_FILE},
	{__NR_getdents,	choice_fd,	wrap_in_getdents,wrap_out_std,	0,	3, SOC_FILE},
	{__NR_getdents64,choice_fd,	wrap_in_getdents,wrap_out_std,	0,	3, SOC_FILE},
	{__NR_access,	choice_path,	wrap_in_access, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_fcntl,	choice_fd,	wrap_in_fcntl, wrap_out_fcntl,	0,	3, SOC_FILE},
	{__NR_fcntl64,	choice_fd,	wrap_in_fcntl, wrap_out_fcntl,	0,	3, SOC_FILE},
	{__NR_lseek,	choice_fd,	wrap_in_lseek, wrap_out_std,	0,	3, SOC_FILE},
	{__NR__llseek,	choice_fd,	wrap_in_llseek, wrap_out_std,	0,	5, SOC_FILE},
	{__NR_mkdir,	choice_link,	wrap_in_mkdir, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_rmdir,	choice_path,	wrap_in_unlink, wrap_out_std,	0,	1, SOC_FILE},
	{__NR_link,	choice_link2,	wrap_in_link, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_symlink,	choice_link2,	wrap_in_symlink, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_rename,	choice_link2,	wrap_in_symlink, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_unlink,	choice_path,	wrap_in_unlink, wrap_out_std,	0,	1, SOC_FILE},
	{__NR_statfs,	choice_path,	wrap_in_notsupp, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_fstatfs,	choice_fd,	wrap_in_notsupp, wrap_out_std,	0,	2, SOC_FILE},
	{__NR_utime,	choice_path,	wrap_in_utime, wrap_out_std,	0,	2, SOC_FILE|SOC_TIME},
	{__NR_utimes,	choice_path,	wrap_in_utime, wrap_out_std,	0,	2, SOC_FILE|SOC_TIME},
	{__NR_fsync,	choice_fd,	wrap_in_fsync, wrap_out_std,	0,	1, SOC_FILE},
	{__NR_fdatasync,choice_fd,	wrap_in_fsync, wrap_out_std,	0,	1, SOC_FILE},
#ifdef __NR_pread64
	{__NR_pread64,	choice_fd,	wrap_in_pread, 	wrap_out_std,	0,	5, SOC_FILE},
#else
	{__NR_pread,	choice_fd,	wrap_in_pread, 	wrap_out_std,	0,	5, SOC_FILE},
#endif
#ifdef __NR_pwrite64
	{__NR_pwrite64,	choice_fd,	wrap_in_pwrite, wrap_out_std,	0,	5, SOC_FILE},
#else
	{__NR_pwrite,	choice_fd,	wrap_in_pwrite, wrap_out_std,	0,	5, SOC_FILE},
#endif
#if 0
	/* TO DO! */
	/* time related calls */
	{__NR_time,	choice_sc,	wrap_in_time, wrap_out_std,	0,	1, SOC_TIME},
	{__NR_gettimeofday, choice_sc,	wrap_in_gettimeofday, wrap_out_std,	0,	2, SOC_TIME},
	{__NR_settimeofday, choice_sc,	wrap_in_settimeofday, wrap_out_std,	0,	2, SOC_TIME},
	{__NR_adjtimex, choice_sc,	wrap_in_adjtimex, wrap_out_std,	0,	1, SOC_TIME},
	{__NR_clock_gettime, choice_sc,	wrap_in_clock_gettime, wrap_out_std,	0,	2, SOC_TIME},
	{__NR_clock_settime, choice_sc,	wrap_in_clock_settime, wrap_out_std,	0,	2, SOC_TIME},
	{__NR_clock_getres, choice_sc,	wrap_in_clock_getres, wrap_out_std,	0,	2, SOC_TIME},

	/* user mgmt calls */
	{__NR_getuid,	choice_sc,	wrap_in_id_g1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_setuid,	choice_sc,	wrap_in_id_s1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_geteuid,	choice_sc,	wrap_in_id_g1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_setfsuid,	choice_sc,	wrap_in_id_s1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_setreuid,	choice_sc,	wrap_in_id_s2, wrap_out_std, 	0,	2, SOC_UID},
	{__NR_getresuid, choice_sc,	wrap_in_id_g3, wrap_out_std, 	0,	3, SOC_UID},
	{__NR_setresuid, choice_sc,	wrap_in_id_s3, wrap_out_std, 	0,	3, SOC_UID},
	{__NR_getgid,	choice_sc,	wrap_in_id_g1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_setgid,	choice_sc,	wrap_in_id_s1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_getegid,	choice_sc,	wrap_in_id_g1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_setfsgid,	choice_sc,	wrap_in_id_s1, wrap_out_std, 	0,	1, SOC_UID},
	{__NR_setregid,	choice_sc,	wrap_in_id_s2, wrap_out_std, 	0,	2, SOC_UID},
	{__NR_getresgid, choice_sc,	wrap_in_id_g3, wrap_out_std, 	0,	3, SOC_UID},
	{__NR_setresgid, choice_sc,	wrap_in_id_s3, wrap_out_std, 	0,	3, SOC_UID},
	  
	/* priority related calls */
	{__NR_nice,	choice_sc,	wrap_in_nice,  wrap_out_std,	0,	1, SOC_PRIO},
	{__NR_getpriority, choice_sc,	wrap_in_getpriority, wrap_out_std, 0,	2, SOC_PRIO},
	{__NR_setpriority, choice_sc,	wrap_in_setpriority, wrap_out_std, 0,	3, SOC_PRIO},

	/* process id related */
	{__NR_getpid,	choice_sc,	wrap_in_getpid,  wrap_out_std,	0,	0, SOC_PID},
	{__NR_getppid,	choice_sc,	wrap_in_getpid,  wrap_out_std,	0,	0, SOC_PID},
	{__NR_getpgrp,	choice_sc,	wrap_in_getpid,  wrap_out_std,	0,	0, SOC_PID},
	{__NR_setpgrp,	choice_sc,	wrap_in_setpid,  wrap_out_std,	0,	0, SOC_PID},
	{__NR_getpgid,	choice_sc,	wrap_in_getpid_1, wrap_out_std,	0,	1, SOC_PID},
	{__NR_setpgid,	choice_sc,	wrap_in_setpid_2, wrap_out_std,	0,	2, SOC_PID},
	{__NR_getsid,	choice_sc,	wrap_in_setpid_1, wrap_out_std,	0,	1, SOC_PID},
	{__NR_setsid,	choice_sc,	wrap_in_setpid,  wrap_out_std,	0,	0, SOC_PID},

	/* host id */
	{__NR_uname,	choice_sc,	wrap_in_uname,  wrap_out_std,	0,	1, SOC_HOSTID},
	{__NR_gethostname, choice_sc,	wrap_in_gethostname,  wrap_out_std,	0,	2, SOC_HOSTID},
	{__NR_sethostname, choice_sc,	wrap_in_sethostname,  wrap_out_std,	0,	2, SOC_HOSTID},
	{__NR_getdomainname, choice_sc,	wrap_in_gethostname,  wrap_out_std,	0,	2, SOC_HOSTID},
	{__NR_setdomainname, choice_sc,	wrap_in_sethostname,  wrap_out_std,	0,	2, SOC_HOSTID},

	{__NR_sysctl, choice_sysctl, wrap_in_sysctl, wrap_out_sysctl, 0, 2, 0}
	/* this is a trip */
	<__NR_ptrace, always_umnone, wrap_in_ptrace, wrap_out_ptrace, 0, 4, 0}
#endif
#ifdef PIVOTING_TEST
	{__NR_getpid,	always_umnone,	wrap_in_getpid, wrap_out_getpid,ALWAYS,	0, SOC_PID},
#endif
};

#define SIZESCMAP (sizeof(scmap)/sizeof(struct sc_map))

intfunt wrap_in_socket, wrap_out_socket;
intfunt wrap_in_bind_connect, wrap_in_listen, wrap_in_getsock, wrap_in_send;
intfunt wrap_in_recv, wrap_in_shutdown, wrap_in_setsockopt, wrap_in_getsockopt;
intfunt wrap_in_sendmsg, wrap_in_recvmsg, wrap_in_accept;
intfunt wrap_in_sendto, wrap_in_recvfrom;

struct sc_map sockmap[]={
	{0,			NULL,		NULL,			NULL,	0,	0, SOC_NET},
/* 1*/	{SYS_SOCKET,    	choice_socket, 	wrap_in_socket,		wrap_out_socket,	0,	3, SOC_NET}, 
/* 2*/	{SYS_BIND,      	choice_fd,	wrap_in_bind_connect,	wrap_out_std,	0,	3, SOC_NET},
/* 3*/	{SYS_CONNECT,   	choice_fd,	wrap_in_bind_connect,	wrap_out_std,	0,	3, SOC_NET},
/* 4*/	{SYS_LISTEN,    	choice_fd,	wrap_in_listen,		wrap_out_std,	0,	2, SOC_NET},
/* 5*/	{SYS_ACCEPT,    	choice_fd,	wrap_in_accept,		wrap_out_socket,	CB_R,	3, SOC_NET},
/* 6*/	{SYS_GETSOCKNAME,       choice_fd,	wrap_in_getsock,	wrap_out_std,	0,	3, SOC_NET},
/* 7*/	{SYS_GETPEERNAME,       choice_fd,	wrap_in_getsock,	wrap_out_std,	0,	3, SOC_NET},
/* 8*/	{SYS_SOCKETPAIR,        always_umnone,		NULL, 			NULL,	0,	4, SOC_NET}, /* not used */
/* 9*/	{SYS_SEND,      	choice_fd,	wrap_in_send,		wrap_out_std,	0,	4, SOC_NET},
/*10*/	{SYS_RECV,      	choice_fd,	wrap_in_recv,		wrap_out_std,	CB_R,	4, SOC_NET},
/*11*/	{SYS_SENDTO,    	choice_fd,	wrap_in_sendto,		wrap_out_std,	0,	6, SOC_NET},
/*12*/	{SYS_RECVFROM,  	choice_fd,	wrap_in_recvfrom,	wrap_out_std,	CB_R,	6, SOC_NET},
/*13*/	{SYS_SHUTDOWN,  	choice_fd,	wrap_in_shutdown,	wrap_out_std,	0,	2, SOC_NET},
/*14*/	{SYS_SETSOCKOPT,        choice_fd,	wrap_in_setsockopt,	wrap_out_std,	0,	5, SOC_NET},
/*15*/	{SYS_GETSOCKOPT,        choice_fd,	wrap_in_getsockopt,	wrap_out_std,	0,	5, SOC_NET},
/*16*/	{SYS_SENDMSG,   	choice_fd,	wrap_in_sendmsg,	wrap_out_std,	0,	3, SOC_NET},
/*17*/	{SYS_RECVMSG,   	choice_fd,	wrap_in_recvmsg,	wrap_out_std,	CB_R,	3, SOC_NET}
};
#define SIZESOCKMAP (sizeof(sockmap)/sizeof(struct sc_map))

static short scremap[MAXSC];
static short scuremap[MAXUSC];

void init_scmap()
{
	register int i;

	for (i=0; i<SIZESCMAP; i++) {
		int scno=scmap[i].scno;
		if (scno > 0 && scno < MAXSC)
			scremap[scno]=i;
		else if (scno >= BASEUSC && scno < BASEUSC+MAXUSC)
			scuremap[scno-BASEUSC]=i;
	}
	scmap_scmapsize = SIZESCMAP;
	scmap_sockmapsize = SIZESOCKMAP;
}

int uscno(int scno)
{
	if (scno > 0 && scno < MAXSC)
		return scremap[scno];
	else if (scno >= BASEUSC && scno < BASEUSC+MAXUSC)
		return scuremap[scno-BASEUSC];
	else
		return -1;
}

// vim: ts=8