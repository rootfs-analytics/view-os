/*   This is part of um-ViewOS
 *   The user-mode implementation of OSVIEW -- A Process with a View
 *
 *   um_plusio: io wrappers (second part)
 *   
 *   Copyright 2005 Renzo Davoli University of Bologna - Italy
 *   Modified 2005 Ludovico Gardenghi
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
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/statfs.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <linux/net.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <utime.h>
#include "defs.h"
#include "gdebug.h"
#include "umproc.h"
#include "services.h"
#include "um_services.h"
#include "sctab.h"
#include "scmap.h"
#include "utils.h"
#include "uid16to32.h"

#define umNULL ((int) NULL)

int wrap_in_mkdir(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	int mode;
	mode=getargn(1,pc);
	pc->retval = um_syscall(pc->path,mode & ~ (pc->fdfs->mask));
	pc->erno=errno;
	return SC_FAKE;
}

int wrap_in_unlink(int sc_number,struct pcb *pc,
		                char sercode, sysfun um_syscall)
{
	pc->retval = um_syscall(pc->path);
	pc->erno=errno;
	return SC_FAKE;
}

int wrap_in_chown(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	unsigned int owner,group;
	owner=getargn(1,pc);
	group=getargn(2,pc);
#if __NR_chown != __NR_chown32
	if (sc_number == __NR_chown || sc_number == __NR_lchown) {
		owner=id16to32(owner);
		group=id16to32(group);
	}
#endif
	pc->retval = um_syscall(pc->path,owner,group);
	pc->erno=errno;
	return SC_FAKE;
}

int wrap_in_fchown(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	int sfd=fd2sfd(pc->fds,pc->arg0);
	if (sfd < 0) {
		pc->retval= -1;
		pc->erno= EBADF;
		return SC_FAKE;
	} else {
		unsigned int owner,group;
		owner=getargn(1,pc);
		group=getargn(2,pc);
#if __NR_fchown != __NR_fchown32
	if (sc_number == __NR_fchown) {
		owner=id16to32(owner);
		group=id16to32(group);
	}
#endif
		pc->retval = um_syscall(sfd,owner,group);
		pc->erno=errno;
		return SC_FAKE;
	}
}

int wrap_in_chmod(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	int mode;
	mode=getargn(1,pc);
	pc->retval = um_syscall(pc->path,mode);
	pc->erno=errno;
	return SC_FAKE;
}

int wrap_in_fchmod(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	int sfd=fd2sfd(pc->fds,pc->arg0);
	if (sfd < 0) {
		pc->retval= -1;
		pc->erno= EBADF;
		return SC_FAKE;
	} else {
		int mode;
		mode=getargn(1,pc);
		pc->retval = um_syscall(sfd,mode);
		pc->erno=errno;
		return SC_FAKE;
	}
}

/* DUP & DUP2.
 * Always processed in any case.
 * if the dup fd refers an open file it must be closed (if it is managed by a service 
 * module the close request must be forwarded to that module).
 */

/* DUP management: dup gets executed both by the process (the fifo is
 * dup-ped when the file is virtual) and umview records the operation 
 * DUP does not exist for modules */
int wrap_in_dup(int sc_number,struct pcb *pc,
		service_t sercode, sysfun um_syscall)
{
	int sfd;
	if (sc_number == __NR_dup2) {
		pc->arg1=getargn(1,pc);
	} else
		pc->arg1= -1;
	sfd=fd2sfd(pc->fds,pc->arg0);
	GDEBUG(4, "DUP %d %d sfd %d %s",pc->arg0,pc->arg1,sfd,fd_getpath(pc->fds,pc->arg0));
	if (pc->arg1 == um_mmap_secret || (sfd < 0 && sercode != UM_NONE)) {
		pc->retval= -1;
		pc->erno= EBADF;
		return SC_FAKE;
	} else {
		pc->retval=fd2lfd(pc->fds,pc->arg0);
		pc->erno = 0;
		/*if (pc->retval < 0) {
			pc->retval= -1;
			pc->erno= EBADF;
			return SC_FAKE;
		} else { */
		lfd_dup(pc->retval);
		return SC_CALLONXIT;
		/*}*/
	}
}

int wrap_out_dup(int sc_number,struct pcb *pc)
{
	/* TODO Dup is incomplete (20061028: (rd) why? what's missing? */
	//if (pc->retval >= 0) {
	/* SC_CALLONXIT both for UM_NONE and module managed files.
	 * FAKE only when the module gave an error */
	if (pc->behavior == SC_CALLONXIT) {
		int fd=getrv(pc);
		if (fd >= 0) {
			/* DUP2 case, the previous fd has been closed, umview must
			 * update its lfd table */
			if (pc->arg1 != -1)
				lfd_deregister_n_close(pc->fds,pc->arg1);
			lfd_register(pc->fds,fd,pc->retval);
		} else {
			lfd_close(pc->retval);
		}
	} else {
		putrv(pc->retval,pc);
		puterrno(pc->erno,pc);
	}
	return STD_BEHAVIOR;
}

int wrap_in_fcntl(int sc_number,struct pcb *pc,
		service_t sercode, sysfun um_syscall)
{
	int cmd=getargn(1,pc);
	unsigned long arg=getargn(2,pc);
	int sfd=fd2sfd(pc->fds,pc->arg0);
	pc->arg1=cmd;
	//printf("wrap_in_fcntl %d %d %d %d \n",pc->arg0,sfd,cmd,fd2lfd(pc->fds,pc->arg0));
	if (sfd < 0) {
		pc->retval= -1;
		pc->erno= EBADF;
		return SC_FAKE;
	} else {
		if (cmd == F_GETLK || cmd == F_SETLK || cmd == F_SETLKW) {
			/* LOCKING unsupported yet XXX */
			fprint2("Locking unsupported\n");
			pc->retval= -1;
			pc->erno= EBADF;
			return SC_FAKE;
		} else if (cmd == F_DUPFD) {
			pc->retval=fd2lfd(pc->fds,pc->arg0);
			lfd_dup(pc->retval);
			return SC_CALLONXIT;
		} else {
			pc->retval = um_syscall(sfd,cmd,arg);
			//pc->erno= pc->retval<0?errno:0;
			pc->erno= errno;
			return SC_FAKE;
		}
	}
}


int wrap_out_fcntl(int sc_number,struct pcb *pc)
{
	int fd;
	switch (pc->arg1) {
		case F_DUPFD:
			fd=getrv(pc);
			//printf("F_DUPFD %d->%d\n",pc->retval,fd);
			if (fd>=0)
				lfd_register(pc->fds,fd,pc->retval);
			else
				lfd_close(pc->retval);
			break;
		case F_SETFD:
			/* take care of FD_CLOEXEC flag value XXX */
		default:
			//printf("fcntl returns %d %d\n",pc->retval,pc->erno);
			putrv(pc->retval,pc);
			puterrno(pc->erno,pc);
			break;
	}
	return STD_BEHAVIOR;
}

int wrap_in_fsync(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	int sfd=fd2sfd(pc->fds,pc->arg0);
	if (sfd < 0) {
		pc->retval= -1;
		pc->erno= EBADF;
		return SC_FAKE;
	} else {
		pc->retval = um_syscall(sfd);
		pc->erno=errno;
	}
	return SC_FAKE;
}

int wrap_in_link(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	struct stat64 sourcest;
	char *source=um_abspath(pc->arg0,pc,&sourcest,0);

	if (source==um_patherror) {
		pc->retval= -1;
		pc->erno= ENOENT;
	} else {
		/* inter module file hard link are unsupported! */
		int ser2=service_check(CHECKPATH,source,0);
		if (ser2 != sercode) {
			pc->retval= -1;
			pc->erno= EXDEV;
		} else {
			pc->retval=um_syscall(source,pc->path);
			pc->erno= errno;
		}
		free(source);
	}
	return SC_FAKE;
}

int wrap_in_symlink(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	char *source=um_getpath(pc->arg0,pc);
	if (source==um_patherror) {
		pc->retval= -1;
		pc->erno= ENOENT;
	} else {
		pc->retval=um_syscall(source,pc->path);
		pc->erno= errno;
		free(source);
	}
	return SC_FAKE;
}

/* UTIME & UTIMES wrap in function */
int wrap_in_utime(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	unsigned long argaddr=getargn(1,pc);
	int argsize;
	char *larg;
	if (argaddr == umNULL) 
		larg=NULL;
	else {
		if (sc_number == __NR_utime)
		/* UTIME */
			argsize=sizeof(struct utimbuf);
		else
		/* UTIMES */
			argsize=2*sizeof(struct timeval);
		larg=alloca(argsize);
		umoven(pc,argaddr,argsize,larg);
	}
	pc->retval = um_syscall(pc->path,larg);
	pc->erno=errno;
	return SC_FAKE;
}

/* MOUNT */
int wrap_in_mount(int sc_number,struct pcb *pc,
		                char sercode, sysfun um_syscall)
{
	char *source;
	char filesystemtype[PATH_MAX];
	char data[PATH_MAX];
	char *datax=data;
	unsigned long argaddr=pc->arg0;
	unsigned int fstype=getargn(2,pc);
	unsigned int mountflags=getargn(3,pc);
	unsigned long pdata=getargn(4,pc);
	struct stat64 imagestat;
	epoch_t nestepoch;
	umovestr(pc,fstype,PATH_MAX,filesystemtype);
	source = um_abspath(argaddr,pc,&imagestat,0);
	/*service_check(CHECKPATH,source,1); *//* ??? what is this for? */
	nestepoch=um_setepoch(0);
	um_setepoch(nestepoch+1);
	/* maybe the source is not a path at all.
	 * source is not converted to an absolute path if it is not a path
	 * it is simply copied "as is" */
	if (source==um_patherror) {
		source=malloc(PATH_MAX);
		assert(source);
		umovestr(pc,argaddr,PATH_MAX,source);
	} 
	if (pdata != umNULL)
		umovestr(pc,pdata,PATH_MAX,data);
	else
		datax=NULL;
	pc->retval = um_syscall(source,pc->path,filesystemtype,mountflags,datax);
	pc->erno=errno;
	free(source);
	um_setepoch(nestepoch);
	return SC_FAKE;
}

int wrap_in_umount(int sc_number,struct pcb *pc,
		                char sercode, sysfun um_syscall)
{
	unsigned int flags=0;
	pc->retval = um_syscall(pc->path,flags);
	pc->erno=errno;
	return SC_FAKE;
}

int wrap_in_umount2(int sc_number,struct pcb *pc,
		                char sercode, sysfun um_syscall)
{
	unsigned int flags=0;
	// flags is defined as int in umount manpage.
	flags= (int) getargn(1,pc);
	pc->retval = um_syscall(pc->path,flags);
	pc->erno=errno;
	return SC_FAKE;
}

#if (defined(__powerpc__) && !defined(__powerpc64__)) || (defined (MIPS) && !defined(__mips64))
#define PALIGN 1
#else
#define PALIGN 0
#endif

int wrap_in_truncate(int sc_number,struct pcb *pc,
		                char sercode, sysfun um_syscall)
{
	__off64_t off;
#if (__NR_truncate64 != __NR_doesnotexist)
	if (sc_number == __NR_truncate64) 
		off=LONG_LONG(getargn(1+PALIGN,pc), getargn(2+PALIGN,pc));
	else
#endif
		off=getargn(1,pc);
	pc->retval=um_syscall(pc->path,off);
	pc->erno=errno;
	return SC_FAKE;
}

int wrap_in_ftruncate(int sc_number,struct pcb *pc,
		                char sercode, sysfun um_syscall)
{
	__off64_t off;
	int sfd=fd2sfd(pc->fds,pc->arg0);
#if (__NR_ftruncate64 != __NR_doesnotexist)
	if (sc_number == __NR_ftruncate64) 
		off=LONG_LONG(getargn(1+PALIGN,pc), getargn(2+PALIGN,pc));
	else
#endif
		off=getargn(1,pc);
	pc->retval = um_syscall(sfd,off);
	pc->erno=errno;
	return SC_FAKE;
}

static void statfs264(struct statfs *fs,struct statfs64 *fs64)
{
	fs->f_type = fs64->f_type;
	fs->f_bsize = fs64->f_bsize;
	fs->f_blocks = fs64->f_blocks;
	fs->f_bfree = fs64->f_bfree;
	fs->f_bavail = fs64->f_bavail;
	fs->f_files = fs64->f_files;
	fs->f_ffree = fs64->f_ffree;
	fs->f_fsid = fs64->f_fsid;
	fs->f_namelen = fs64->f_namelen;
	fs->f_frsize = fs64->f_frsize;
}

int wrap_in_statfs(int sc_number,struct pcb *pc,
		                    char sercode, sysfun um_syscall)
{
	struct statfs64 sfs64;
	long pbuf=getargn(1,pc);

	pc->retval = um_syscall(pc->path,&sfs64);
	pc->erno=errno;
	if (pc->retval >= 0) {
		struct statfs sfs;
		statfs264(&sfs,&sfs64);
		ustoren(pc,pbuf,sizeof(struct statfs),(char *)&sfs);
	}
	return SC_FAKE;
}

int wrap_in_fstatfs(int sc_number,struct pcb *pc,
		                    char sercode, sysfun um_syscall)
{
	struct statfs64 sfs64;
	long pbuf=getargn(1,pc);
	int sfd=fd2sfd(pc->fds,pc->arg0);
	if (sfd < 0) {
		pc->retval= -1;
		pc->erno= EBADF;
	} else {
		pc->retval = um_syscall(sfd,&sfs64);
		pc->erno=errno;
		if (pc->retval >= 0) {
			struct statfs sfs;
			statfs264(&sfs,&sfs64);
			ustoren(pc,pbuf,sizeof(struct statfs),(char *)&sfs);
		}
	}
	return SC_FAKE;
}

#if (__NR_statfs64 != __NR_doesnotexist)
int wrap_in_statfs64(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	struct statfs64 sfs64;
	long size=getargn(1,pc);
	long pbuf=getargn(2,pc);

	if (size != sizeof(sfs64)) {
		pc->retval= -1;
		pc->erno= EINVAL;
	}
	else {
		pc->retval = um_syscall(pc->path,&sfs64);
		pc->erno=errno;
		if (pc->retval >= 0) 
			ustoren(pc,pbuf,sizeof(struct statfs64),(char *)&sfs64);
	}
	return SC_FAKE;
}

int wrap_in_fstatfs64(int sc_number,struct pcb *pc,
		char sercode, sysfun um_syscall)
{
	struct statfs64 sfs64;
	long size=getargn(1,pc);
	long pbuf=getargn(2,pc);
	int sfd=fd2sfd(pc->fds,pc->arg0);
	
	if (sfd < 0) {
		pc->retval= -1;
		pc->erno= EBADF;
	} else if (size != sizeof(sfs64)) {
		pc->retval= -1;
		pc->erno= EINVAL;
	} else {
		pc->retval = um_syscall(sfd,&sfs64);
		pc->erno=errno;
		if (pc->retval >=0) 
			ustoren(pc,pbuf,sizeof(struct statfs64),(char *)&sfs64);
	}
	return SC_FAKE;
}
#endif