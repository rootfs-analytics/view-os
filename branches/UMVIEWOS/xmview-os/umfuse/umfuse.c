/*   This is part of um-ViewOS
 *   The user-mode implementation of OSVIEW -- A Process with a View
 *
 *   umviewos -> fuse gateway
 *   
 *   Copyright 2005 Renzo Davoli University of Bologna - Italy
 *   Modified 2005 Paolo Angelelli, Andrea Seraghiti
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
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <utime.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fuse/fuse.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include "module.h"
#include "libummod.h"
#include "umfusestd.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ipc.h>
#include <stdarg.h>
#include <math.h>
#include <values.h>


/* Enable Experimental code */
//#define __UMFUSE_EXPERIMENTAL__

#ifdef __UMFUSE_EXPERIMENTAL__
/* There are already some problems with dup. (e.g. output redirection)
 * TODO permission management and user management (who is the writer in the Virtual FS?)
 * TODO fuse parms management (e.g. RO file system do permit rmdir)
 */
#endif

/* Enable umfuse own debug output */

//#define __UMFUSE_DEBUG__ 1   /* it is better to enable it from makefile */
//#define __UMFUSE_DEBUG_LEVEL__ 0

#ifdef __UMFUSE_DEBUG__
#define PRINTDEBUG(level,args...) printdebug(level, __FILE__, __LINE__, __func__, args)
#else
#define PRINTDEBUG(level,args...)
#endif



struct fuse {
	char *filesystemtype;
	char *path;
	short pathlen;
	void *dlhandle;
	pthread_t thread;
	pthread_cond_t endloop;
	pthread_mutex_t endmutex;
	struct fuse_operations fops;	
	int inuse;
	unsigned long flags;
};

/* values for INUSE and thread synchro */
#define WAITING_FOR_LOOP -1
#define EXITING -2
#define FUSE_ABORT -3
/* horrible! the only way to have some context to allow multiple mount is
 * a global var: XXX new solution needed. This is not thread-scalable */

static int umfuse_current_context;
static struct fuse_context **fusetab=NULL;
static int fusetabmax=0;

static pthread_mutex_t condition_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  startloop  = PTHREAD_COND_INITIALIZER;

#define WORDLEN sizeof(int *)
#define WORDALIGN(X) (((X) + WORDLEN) & ~(WORDLEN-1))
#define SIZEDIRENT64NONAME (sizeof(__u64)+sizeof(__s64)+sizeof(unsigned short)+sizeof(unsigned char))
#define SIZEDIRENT32NONAME (sizeof(long)+sizeof(__kernel_off_t)+sizeof(unsigned short))

struct umfuse_dirent64 {
	__u64             d_ino;
	__s64             d_off;
	unsigned short  d_reclen;
	unsigned char   d_type;
	char            *d_name;
};

struct umdirent {
	struct umfuse_dirent64 de;
	unsigned short d_reclen32;
	struct umdirent *next;
};

struct fileinfo {
	int context;				/* fusetab entry index */
	char *path;						
	int count;				/* number of processes that opened the file */
	long long pos;				/* file offset */
	struct fuse_file_info ffi;		/* includes open flags, file handle and page_write mode  */
	struct umdirent *dirinfo;		/* conversion fuse-getdir into kernel compliant
						   dirent. Dir head pointer */
	struct umdirent *dirpos;		/* same conversion above: current pos entry */
};

static struct fileinfo **filetab=NULL;
static int filetabmax=0;

#define MNTTABSTEP 4 /* must be a power of two */
#define MNTTABSTEP_1 (MNTTABSTEP-1)
#define FILETABSTEP 4 /* must be a power of two */
#define FILETABSTEP_1 (FILETABSTEP-1)

#define EXACT 1
#define SUBSTR 0

/* static umfuse own debug function */
/* it accept a level of debug: higher level = more important messages only */

#ifdef __UMFUSE_DEBUG__
static void printdebug(int level, const char *file, const int line, const char *func, const char *fmt, ...) {
	va_list ap;
    
	if (level >= __UMFUSE_DEBUG_LEVEL__) {
		va_start(ap, fmt);
#ifdef _PTHREAD_H
		fprintf(stderr, "[%d:%lu] %s:%d %s(): ", getpid(), pthread_self(), file, line, func);
#else
		fprintf(stderr, "[%d] %s:%d %s(): ", getpid(), file, line, func);
#endif
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
		fflush(stderr);
		va_end(ap);
	}
}
#endif

/* search a path, returns the context i.e. the index of info for mounted file
 * -1 otherwise */
static int searchcontext(const char *path,int exact)
{
	register int i;
	int result=-1;
 PRINTDEBUG(0,"SearchContext:%s-%s\n",path, exact?"EXACT":"SUBSTR");
	for (i=0;i<fusetabmax && result<0;i++)
	{
		if ((fusetab[i] != NULL) && (fusetab[i]->fuse != NULL)) {
			if (exact) {
				if (strcmp(path,fusetab[i]->fuse->path) == 0)
					result=i;
			} else {
				int len=fusetab[i]->fuse->pathlen;
				if (strncmp(path,fusetab[i]->fuse->path,len) == 0 && (path[len] == '/' || path[len]=='\0'))
					result=i;
			}
		}
	}
	return result;
}

/*insert a new context in the fuse table*/
static int addfusetab(struct fuse_context *new)
{
	register int i;
	for (i=0;i<fusetabmax && fusetab[i] != NULL;i++)
		;
	if (i>=fusetabmax) {
		register int j;
		fusetabmax=(i + MNTTABSTEP) & ~MNTTABSTEP_1;
		fusetab=(struct fuse_context **)realloc(fusetab,fusetabmax*sizeof(struct fuse_context *));
		assert(fusetab);
		for (j=i;j<fusetabmax;j++)
			fusetab[j]=NULL;
	}
	fusetab[i]=new;
	return i;
}

/* execute a specific function (arg) for each fusetab element */
static void forallfusetabdo(void (*fun)(struct fuse_context *fc))
{
	register int i;
	for (i=0;i<fusetabmax && fusetab[i] != NULL;i++)
		if (fusetab[i] != NULL)
		     fun(fusetab[i]);
}

/*
 * delete the i-th element of the tab.
 * the table cannot be compacted as the index is used as id
 */
static void delmnttab(int i)
{
	fusetab[i]=NULL;
}

/* add an element to the filetab (open file table)
 * each file has a fileinfo record
 */
static int addfiletab()
{
	register int i;
	for (i=0;i<filetabmax && filetab[i] != NULL;i++)
		;
	if (i>=filetabmax) {
		register int j;
		filetabmax=(i + MNTTABSTEP) & ~MNTTABSTEP_1;
		filetab=(struct fileinfo **)realloc(filetab,filetabmax*sizeof(struct fileinfo *));
		assert(filetab);
		for (j=i;j<filetabmax;j++)
			filetab[i]=NULL;
	}
	filetab[i]=(struct fileinfo *)malloc(sizeof(struct fileinfo));
	assert(filetab[i]);
	return i;
}

/* delete an entry from the open file table.
 * RD: there is a counter managed by open and close calls */
static void delfiletab(int i)
{
	struct fileinfo *norace=filetab[i];
	filetab[i]=NULL;
	free(norace->path);
	free(norace);
}

struct startmainopt {
	struct fuse_context *new;
	char *source;
	unsigned long *pmountflags;
	void *data;
};

static char *mountflag2options(unsigned long mountflags, void *data)
{
	char opts[PATH_MAX];
	char *mountopts=data;
	opts[0]=0;
	
	PRINTDEBUG(10,"mountflags: %x\n",mountflags);
	PRINTDEBUG(10,"data: %s\n",data);

	if (mountflags & MS_REMOUNT)
		strcat(opts,"remount,");
	if (mountflags & MS_RDONLY)
		strcat(opts,"ro,");
	if (mountflags & MS_NOATIME)
		strcat(opts,"noatime,");
	if (mountflags & MS_NODEV)
		strcat(opts,"nodev,");
	if (mountflags & MS_NOEXEC)
		strcat(opts,"noexec,");
	if (mountflags & MS_NOSUID)
		strcat(opts,"nosuid,");
	if (mountflags & MS_SYNCHRONOUS)
		strcat(opts,"sync,");
	
	/* if there are options trailing comma is removed,
	 * otherwise "rw" becomes a comment */
	if (data && *mountopts)
		strcat(opts,mountopts);
	else if (*opts)
		opts[strlen(opts)-1]=0;
	     else 
		strcpy(opts,"rw");
	PRINTDEBUG(10,"opts: %s\n",opts);
	return(strdup(opts));
}

static void *startmain(void *vsmo)
{
	struct startmainopt *psmo = vsmo;
	int (*pmain)() = dlsym(psmo->new->fuse->dlhandle,"main");
	if (pmain == NULL) {
		fprintf(stderr, "%s\n", dlerror());
		fflush(stderr);
	}
	
	/* handle -o options and specific filesystem options */
	
	char *opts = mountflag2options(*(psmo->pmountflags), psmo->data);
	
	/* syntax: fuse_filesystem: filesystem_name, mountpoint(or target), source or NULL?!, [-o opt1,opts..] */
	
	/*
	 * char *argv[] = {psmo->new->fuse->filesystemtype,"-o", opts, psmo->source, psmo->new->fuse->path, (char *)0};
	 */
	
	// //char *argv[] = {psmo->new->fuse->filesystemtype, psmo->new->fuse->path,psmo->source, "-o", opts, (char *)0};

	// // pmain(5,argv); /* better with a va_list argument */
	// // free(opts);
	int newargc;
	char **newargv;
	newargc=fuseargs(psmo->new->fuse->filesystemtype,psmo->source, psmo->new->fuse->path,opts, &newargv, &(psmo->new->fuse->flags));
	free(opts);
	if (psmo->new->fuse->flags & FUSE_DEBUG) {
		fprintf(stderr, "UmFUSE Debug enabled\n");
		fprintf(stderr, "MOUNT=>filesystem:%s image:%s path:%s args:%s\n",
				psmo->new->fuse->filesystemtype, psmo->source, psmo->new->fuse->path,opts);
		fflush(stderr);		
	}
	if (pmain(newargc,newargv) != 0)
		umfuse_abort(psmo->new->fuse);
	int i;
	for (i=0;i<newargc;i++)
		free(newargv[i]);
	free(newargv);
	pthread_exit(NULL);
	return NULL;
}

#if 0
//parse and delete flags for fuse but not for filesystem_fuse, es: debug
void *mountflag2fuse(int *flags, void *data)
{
	char *tmpdata = data;
	char *flag = strdup(tmpdata);
	char newdata[PATH_MAX];
	char *tmp;

	//TODO -r passare al filesystem come -o ro

	PRINTDEBUG(10,"DATA:%s\n",data);
	PRINTDEBUG(10,"FLAG:%s\n",flag);
	
	newdata[0] = 0;
	while((tmp = strsep(&flag, ",")) != NULL) {
		if (strcmp("debug", tmp) != 0) {
			if (strlen(newdata))
				strcat(newdata, ",");
			strcat(newdata, tmp);
		}
		else//=debug
			*flags |= FUSE_DEBUG;
	}
	
	PRINTDEBUG(10,"cleaned data: %s\n", newdata);
	return((void *)strdup(newdata));
}
#endif


/*TODO parse cmd, es dummy is rw o ro!*/
//see fuse_setup_common lib/helper.c
int fuse_main_real(int argc, char *argv[], const struct fuse_operations *op,
		size_t op_size)
{
	struct fuse *f;
	//int fd = fuse_mount(mountpoint, "dummy");//sarebbero kernel_opts, ma le ho separate prima!!//non solo lettura!!es helo che us amain!!
	int fd = fuse_mount(NULL, NULL);//sarebbero kernel_opts, ma le ho separate prima!!//non solo lettura!!es helo che us amain!!
	f = fuse_new(fd, NULL, op, op_size);//ora opts sono lib_opts;debug,hard_remove,use_ino
	fuse_loop(f);
	return 0;	
}

#if 0
struct mount_flags {
	const char *opt;
	unsigned long flag;
	int on;
	int safe;
};

static struct mount_flags mount_flags[] = {
	{"rw",      MS_RDONLY,      0, 1},
	{"ro",      MS_RDONLY,      1, 1},
	{"suid",    MS_NOSUID,      0, 0},
	{"nosuid",  MS_NOSUID,      1, 1},
	{"dev",     MS_NODEV,       0, 0},
	{"nodev",   MS_NODEV,       1, 1},
	{"exec",    MS_NOEXEC,      0, 1},
	{"noexec",  MS_NOEXEC,      1, 1},
	{"async",   MS_SYNCHRONOUS, 0, 1},
	{"sync",    MS_SYNCHRONOUS, 1, 1},
	{"atime",   MS_NOATIME,     0, 1},
	{"noatime", MS_NOATIME,     1, 1},
	{NULL,      0,              0, 0}
};

/* convert the option string into the correspondent flag bit_field */
static int find_mount_flag(const char *s, unsigned len, int *flag)
{
	int i;

	for (i = 0; mount_flags[i].opt != NULL; i++) {
		const char *opt = mount_flags[i].opt;
		if (strlen(opt) == len && strncmp(opt, s, len) == 0) {
			if (mount_flags[i].on)
				*flag |= mount_flags[i].flag;
			else
				*flag &= ~mount_flags[i].flag;
			return 1;
		}
	}
	return 0;
}
#endif

int fuse_mount(const char *mountpoint, const char *opts)
{
	/* int fd=searchcontext(mountpoint, EXACT); */
	/* fd == umfuse_current_context && mountpoint == fusetab[fd]->fuse->path */

	PRINTDEBUG(10,"fuse_mount %d %d\n",fd,umfuse_current_context);
	return umfuse_current_context;
}


void fuse_unmount(const char *mountpoint)
{
	//int fd=searchcontext(mountpoint, EXACT);
	/* TODO to be completed ? */
}

/* set standard fuse_operations (umfusestd.c) for undefined fields in the
 * fuse_operations structure */
static void fopsfill (struct fuse_operations *fops,size_t size)
{
	intfun *f=(intfun *)fops;
	intfun *std=(intfun *) &defaultservice;
	int i;
	int nfun=size/sizeof(intfun);
	for (i=0; i<nfun; i++)
		if (f[i] == NULL) {
			//printf("%d->std\n",i);
			f[i]=std[i];
		}
}

struct fuse *fuse_new(int fd, const char *opts,
		const struct fuse_operations *op, size_t op_size)
{
	PRINTDEBUG(10,"%d %d %d %d\n",fd,umfuse_current_context,op_size,sizeof(struct fuse_operations));
	
	if (fd != umfuse_current_context || op_size != sizeof(struct fuse_operations))
		return NULL;
	else {
		fusetab[fd]->fuse->fops = *op;
		fopsfill(&fusetab[fd]->fuse->fops, op_size);
		return fusetab[fd]->fuse;
	}
}

void fuse_destroy(struct fuse *f)
{
/*	**
 * Destroy the FUSE handle.
 *
 * The filesystem is not unmounted.
 *
 * @param f the FUSE handle
 */
//void fuse_destroy(struct fuse *f);

}

int fuse_loop(struct fuse *f)
{
	//printf("loop signal\n");
	pthread_mutex_lock( &condition_mutex );
	pthread_cond_signal( &startloop );
	pthread_mutex_unlock( &condition_mutex );
	if (f != NULL) {
		f->inuse = 0;
		pthread_mutex_lock( &f->endmutex );
		//pthread_mutex_lock( &condition_mutex );
		if (f->inuse != EXITING)
			pthread_cond_wait( &f->endloop, &f->endmutex );
		//pthread_cond_wait( &f->endloop, &condition_mutex );
		pthread_mutex_unlock( &f->endmutex );
		//pthread_mutex_unlock( &condition_mutex );
		//printf("done loopPID %d TID %d \n",getpid(),pthread_self());
	}
	return 0;
}

int umfuse_abort(struct fuse *f)
{
	//printf("ABORT!\n");
	f->inuse = FUSE_ABORT;
	pthread_mutex_lock( &condition_mutex );
	pthread_cond_signal( &startloop );
	pthread_mutex_unlock( &condition_mutex );
}

void fuse_exit(struct fuse *f)
{
	/**
 * Exit from event loop
 *
 * @param f the FUSE handle
 */

}

int fuse_loop_mt(struct fuse *f)
{
//in fuselib is FUSE event loop with multiple threads,
//but here is all with multiple threads ;-)
	if(f != NULL)
		return fuse_loop(f);
	else
		return -1;
}

struct fuse_context *fuse_get_context(void)
{
	return fusetab[umfuse_current_context];
}

int fuse_invalidate(struct fuse *f, const char *path)
{
/**
 * Invalidate cached data of a file.
 *
 * Useful if the 'kernel_cache' mount option is given, since in that
 * case the cache is not invalidated on file open.
 *
 * @return 0 on success or -errno on failure
 */
	//return -errno
	return 0;
}

int fuse_is_lib_option(const char *opt)
{/**
 * Check whether a mount option should be passed to the kernel or the
 * library
 *
 * @param opt the option to check
 * @return 1 if it is a library option, 0 otherwise
 */
	return 0;
}

static int umfuse_mount(char *source, char *target, char *filesystemtype,
		       unsigned long mountflags, void *data)
{
	/* TODO: ENOTDIR if it is not a directory */
	//void *newdata;
	void *dlhandle = dlopen(filesystemtype, RTLD_NOW);
	
	PRINTDEBUG(10, "MOUNT %s %s %s %x %s\n",source,target,filesystemtype,
			mountflags, (data!=NULL)?data:"<NULL>");

	if(dlhandle == NULL || dlsym(dlhandle,"main") == NULL) {
		fprintf(stderr, "%s\n",dlerror());
		fflush(stderr);
		errno=ENODEV;
		return -1;
	} else {
		struct fuse_context *new = (struct fuse_context *)
			malloc(sizeof(struct fuse_context));
		assert(new);
		new->fuse = (struct fuse *)malloc(sizeof(struct fuse));
		assert(new->fuse);
		new->fuse->path = strdup(target);
		new->fuse->pathlen = strlen(target);
		new->fuse->filesystemtype = strdup(filesystemtype);
		new->fuse->dlhandle = dlhandle;
		memset(&new->fuse->fops,0,sizeof(struct fuse_operations));
		new->fuse->inuse = WAITING_FOR_LOOP;
		new->uid = new->gid = new->pid = 0;
		new->private_data = NULL;
		new->fuse->flags = mountflags; /* all the mount flags + FUSE_DEBUG */
		
		/* parse mount options: split fuse options from 
		   filesystem options
		   and traslate options from mount syntax into fuse syntax */
		   
#if 0
		if (data != NULL)
			newdata = mountflag2fuse(&(new->fuse->flags), data);
		else
			newdata = NULL;
		if (new->fuse->flags & FUSE_DEBUG) {
        		fprintf(stderr, "UmFUSE Debug enabled\n");
			fprintf(stderr, "MOUNT=>filesystem:%s image:%s path:%s\n",
					filesystemtype, source, target);
			fflush(stderr);		
		}
#endif
		umfuse_current_context = addfusetab(new);		
		struct startmainopt smo;
		smo.new = new;
		smo.pmountflags = &(new->fuse->flags);
		smo.source = source;
		//smo.data = newdata;
		smo.data = data;
		pthread_cond_init(&(new->fuse->endloop),NULL);
		pthread_mutex_init(&(new->fuse->endmutex),NULL);
		pthread_create(&(new->fuse->thread), NULL, startmain, (void *)&smo);
		
		PRINTDEBUG(10, "PID %d TID %d \n",getpid(),pthread_self());
		
		pthread_mutex_lock( &condition_mutex );
		if (new->fuse->inuse== WAITING_FOR_LOOP)
			pthread_cond_wait( &startloop , &condition_mutex);
		pthread_mutex_unlock( &condition_mutex );
		if (new->fuse->inuse == FUSE_ABORT)
		{
			struct fuse_context *fc_norace=new;
			//printf("UMOUNT ABORT\n");
			delmnttab(umfuse_current_context);
			pthread_join(fc_norace->fuse->thread, NULL);
			dlclose(fc_norace->fuse->dlhandle);
			free(fc_norace->fuse->filesystemtype);
			free(fc_norace->fuse->path);
			free(fc_norace->fuse);
			errno = EIO;
			return -1;
		}
		return 0;
	}
}

static int umfuse_umount2(char *target, int flags)
{
	umfuse_current_context = searchcontext(target, EXACT);
	if (fusetab[umfuse_current_context]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "UMOUNT => path:%s flag:%d\n",target, flags);
		fflush(stderr);
	}
	if (umfuse_current_context < 0) {
		errno=EINVAL;
		return(-1);
	} else {
		/* TODO check inuse and FORCE flag */
		struct fuse_context *fc_norace=fusetab[umfuse_current_context];
		delmnttab(umfuse_current_context);
		//printf("PID %d TID %d \n",getpid(),pthread_self());
		pthread_mutex_lock( &fc_norace->fuse->endmutex );
		//pthread_mutex_lock( &condition_mutex );
		fc_norace->fuse->inuse= EXITING;
		pthread_cond_signal(&fc_norace->fuse->endloop);
		pthread_mutex_unlock(&fc_norace->fuse->endmutex );
		//pthread_mutex_unlock( &condition_mutex );
		pthread_join(fc_norace->fuse->thread, NULL);
		//printf("JOIN done\n");
		dlclose(fc_norace->fuse->dlhandle);
		free(fc_norace->fuse->filesystemtype);
		free(fc_norace->fuse->path);
		free(fc_norace->fuse);
		free(fc_norace);
		return 0;
	}
}

/* Handle for a getdir() operation */
struct fuse_dirhandle {
	struct umdirent *tail;
	long long offset;
};

static int umfusefilldir(fuse_dirh_t h, const char *name, int type, ino_t ino)
{
	if (name != NULL) {
		struct umdirent *new=(struct umdirent *)malloc(sizeof(struct umdirent));
		new->de.d_ino=ino;
		new->de.d_type=type;
		new->de.d_name=strdup(name);
		new->de.d_reclen=WORDALIGN(SIZEDIRENT64NONAME+strlen(name)+1);
		new->d_reclen32=WORDALIGN(SIZEDIRENT32NONAME+strlen(name)+1);

		/* virtualize the offset on a real file, 64bit ino+16len+8namlen+8type */
		new->de.d_off=h->offset=h->offset+WORDALIGN(12+strlen(name));
		if (h->tail==NULL) {
			new->next=new;
		} else {
			new->next=h->tail->next;
			h->tail->next=new;
		}
		h->tail=new;
	}
	return 0;
}

static struct umdirent *umfilldirinfo(struct fileinfo *fi)
{
	int rv;
	struct fuse_dirhandle dh;
	int cc=fi->context;
	dh.tail=NULL;
	dh.offset=0;
	rv=fusetab[cc]->fuse->fops.getdir(fi->path, &dh, umfusefilldir);
	if (rv < 0)
		return NULL;
	else 
		return dh.tail;
}

static void umcleandirinfo(struct umdirent *tail)
{
	if (tail != NULL) {
		while (tail->next != tail) {
			struct umdirent *tmp;
			tmp=tail->next;
			tail->next=tmp->next;
			free(tmp);
		}
		free(tail);
	}
}

static int um_getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
	if (filetab[fd]==NULL) {
		errno=ENOENT;
		return -1;
	} else {
		//int cc=filetab[fd]->context; /* TODO check it is really a dir */
		int curoffs=0;
		if (filetab[fd]->dirinfo == NULL) {
			filetab[fd]->dirinfo = umfilldirinfo(filetab[fd]);
		} 
		/* TODO management of lseek on directories */

		
		if (filetab[fd]->dirinfo==NULL) 
			return 0;
		else {
			struct dirent *current;
			char *base=(char *)dirp;
			int last=0;
			if (filetab[fd]->dirpos==NULL)
				filetab[fd]->dirpos=filetab[fd]->dirinfo;
			else
				last=(filetab[fd]->dirpos==filetab[fd]->dirinfo);
			while (!last && curoffs + filetab[fd]->dirpos->next->d_reclen32 < count)
			{
				filetab[fd]->dirpos=filetab[fd]->dirpos->next;
				current=(struct dirent *)base;
				current->d_ino=filetab[fd]->dirpos->de.d_ino;
				current->d_off=filetab[fd]->dirpos->de.d_off;
				current->d_reclen=filetab[fd]->dirpos->d_reclen32;
				strcpy(current->d_name,filetab[fd]->dirpos->de.d_name);
				base+=filetab[fd]->dirpos->d_reclen32;
				curoffs+=filetab[fd]->dirpos->d_reclen32;
				last=(filetab[fd]->dirpos == filetab[fd]->dirinfo);
			}
		}
		return curoffs;
	}
}

static int um_getdents64(unsigned int fd, struct dirent64 *dirp, unsigned int count)
{
	if (filetab[fd]==NULL) {
		errno=ENOENT;
		return -1;
	} else {
		unsigned int curoffs=0;
		//int cc=filetab[fd]->context; /* TODO check it is really a dir */
		if (filetab[fd]->dirinfo == NULL) {
			filetab[fd]->dirinfo = umfilldirinfo(filetab[fd]);
		} 
		/* TODO management of lseek on directories */

		if (filetab[fd]->dirinfo==NULL) 
			return 0;
		else {
			struct dirent64 *current;
			char *base=(char *)dirp;
			int last=0;
			if (filetab[fd]->dirpos==NULL)
				filetab[fd]->dirpos=filetab[fd]->dirinfo;
			else
				last=(filetab[fd]->dirpos==filetab[fd]->dirinfo);
			while (!last && curoffs + filetab[fd]->dirpos->next->de.d_reclen < count)
			{
				filetab[fd]->dirpos=filetab[fd]->dirpos->next;
				current=(struct dirent64 *)base;
				current->d_ino=filetab[fd]->dirpos->de.d_ino;
				current->d_off=filetab[fd]->dirpos->de.d_off;
				current->d_reclen=filetab[fd]->dirpos->de.d_reclen;
				current->d_type=filetab[fd]->dirpos->de.d_type;
				strcpy(current->d_name,filetab[fd]->dirpos->de.d_name);
				/* workaround: some FS do not set d_ino, but
				 * inode 0 is special and is skipped by libc */
				if (current->d_ino == 0)
					current->d_ino = 2;
				base+=filetab[fd]->dirpos->de.d_reclen;
				curoffs+=filetab[fd]->dirpos->de.d_reclen;
				last=(filetab[fd]->dirpos == filetab[fd]->dirinfo);
			}
		}
		return curoffs;
	}
}

#define TRUE 1
#define FALSE 0

static int alwaysfalse()
{
	return FALSE;
}

static int umfuse_access(char *path, int mode);

/*search the currect context depending on path
 * return 1 succesfull o 0 if an error occur*/

static int fuse_path(char *path)
{
	if(strncmp(path,"umfuse",6) == 0) /* a path with no leading / is a filesystemtype */
		return TRUE;
	else {
		umfuse_current_context=searchcontext(path,SUBSTR);
		if (umfuse_current_context >= 0) {
			return TRUE; 
		}
	}
	return FALSE;
}

static char *unwrap(struct fuse_context *fc,char *path)
{
	char *reduced=path+fc->fuse->pathlen;
	if (*reduced == 0)
		return("/");
	else
		return(reduced);
}

static int umfuse_open(char *path, int flags, mode_t mode)
{
	int cc = searchcontext(path, SUBSTR);
	int fi = addfiletab();
	int rv;
	int exists_err;

#ifdef __UMFUSE_DEBUG__
	PRINTDEBUG(10,"FLAGOPEN path:%s unwrap:%s\nFLAGS:0x%x MODE:%d\n",path,unwrap(fusetab[cc],path),flags,mode);

	if(flags &  O_CREAT)
		PRINTDEBUG(10, "O_CREAT\n");
	if(flags & O_TRUNC)
		PRINTDEBUG(10, "O_TRUNC\n");
	if(flags &  O_RDONLY)
		PRINTDEBUG(10, "O_RDONLY:\n");
	if(flags &  O_APPEND)
		PRINTDEBUG(10, "O_APPEND\n");
	if(flags &  O_WRONLY)
		PRINTDEBUG(10, "O_WRONLY\n");
	if(flags &  O_RDWR)
		PRINTDEBUG(10, "O_RDWR\n");
	if(flags &  O_ASYNC)
		PRINTDEBUG(10, "O_ASYNC\n");
	if(flags &  O_DIRECT)
		PRINTDEBUG(10, "O_DIRECT\n");
	if(flags &  O_DIRECTORY)
		PRINTDEBUG(10, "O_DIRECTORY\n");
	if(flags &  O_EXCL)
		PRINTDEBUG(10, "O_EXCL\n");
	if(flags &  O_LARGEFILE)
		PRINTDEBUG(10, "O_LARGEFILE\n");
	if(flags &  O_DIRECT)
		PRINTDEBUG(10, "O_NOATIME\n");
	if(flags &  O_DIRECTORY)
		PRINTDEBUG(10, "O_NOCTTY\n");
	if(flags &  O_EXCL)
		PRINTDEBUG(10, "O_NOCTTY\n");
	if(flags &  O_NOFOLLOW)
		PRINTDEBUG(10, "O_NOFOLLOW\n");
	if(flags &  (O_NONBLOCK | O_NDELAY))
		PRINTDEBUG(10, "O_NONBLOCK o O_NDELAY\n");
	if(flags &  O_SYNC)
		PRINTDEBUG(10, "SYNC\n");
#endif


	filetab[fi]->context = cc;
	filetab[fi]->count = 0;
	filetab[fi]->pos = 0;
	filetab[fi]->ffi.flags = flags;
	filetab[fi]->ffi.writepage = 0; //XXX do we need writepage != 0?
	filetab[fi]->dirinfo = NULL;
	filetab[fi]->dirpos = NULL;
	filetab[fi]->path = strdup(unwrap(fusetab[cc], path));
	assert(cc>=0);

#ifdef __UMFUSE_EXPERIMENTAL__
	exists_err=umfuse_access(path, F_OK);
	if(exists_err == 0 && (flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
		rv=fusetab[cc]->fuse->fops.truncate(filetab[fi]->path, 0);
		if (rv < 0) {
			errno = -rv;
			return -1;
		}
	}
        if (flags & O_CREAT) { 
		if (exists_err == 0) {
			if (flags & O_EXCL) {
				errno= EEXIST;
				return -1;
			} 
		} else {
			PRINTDEBUG(10, "umfuse open MKNOD call\n");
			rv = fusetab[cc]->fuse->fops.mknod(filetab[fi]->path, S_IFREG | mode, (dev_t) 0);
			if (rv < 0) {
				errno = -rv;
				return -1;
			}
		}
        }
#else
        if (flags & (O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC)) {
                errno = EROFS;
                return -1;
	}
#endif

        filetab[fi]->ffi.flags = flags & ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
	PRINTDEBUG(10,"open_fuse_fulesystem CALL!\n");
	rv = fusetab[cc]->fuse->fops.open(filetab[fi]->path, &filetab[fi]->ffi);

	if (rv < 0)
	{
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        		fprintf(stderr, "OPEN[%d] ERROR => path:%s flags:0x%x\n",
				fi, path, flags);	
			fflush(stderr);
		}		
		delfiletab(fi);
		errno = -rv;
		return -1;
	} else {
		filetab[fi]->count += 1;
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        		fprintf(stderr, "OPEN[%d] => path:%s flags:0x%x\n",
				fi, path, flags);
			fflush(stderr);
		}

		/* TODO update fuse->inuse++ */
		fusetab[cc]->fuse->inuse++;
		return fi;
	}
}

static int umfuse_close(int fd)
{
	int rv;
	
	if (filetab[fd]==NULL) {
		errno=ENOENT;
		return -1;
	} else {
		int cc=filetab[fd]->context;
		umfuse_current_context = cc;

		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	        fprintf(stderr, "CLOSE[%d] %s %d\n",fd,filetab[fd]->path,cc);
                	fflush(stderr);
	        }
	
		rv=fusetab[cc]->fuse->fops.flush(filetab[fd]->path,
				&filetab[fd]->ffi);
		
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
			fprintf(stderr, "FLUSH[%d] => path:%s\n",
				fd, filetab[fd]->path);
			fflush(stderr);
		}
	
		filetab[fd]->count--;
		PRINTDEBUG(10,"->CLOSE %s %d\n",filetab[fd]->path, filetab[fd]->count);
		if (filetab[fd]->count == 0) {			 
			fusetab[cc]->fuse->inuse--;
			rv=fusetab[cc]->fuse->fops.release(
					filetab[fd]->path,
					&filetab[fd]->ffi);
			if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        			fprintf(stderr, "RELEASE[%d] => path:%s flags:0x%x\n",
					fd, filetab[fd]->path, fusetab[cc]->fuse->flags);
				fflush(stderr);					
			}
			//free(filetab[fd]->path);
			umcleandirinfo(filetab[fd]->dirinfo);
			delfiletab(fd);
		}
		if (rv<0) {
			errno= -rv;
			return -1;
		} else {
			return rv;
		}
	} return 0;
}

static int umfuse_read(int fd, void *buf, size_t count)
{
	int rv;
	if (filetab[fd]==NULL) {
		errno=ENOENT;
		return -1;
	} else {
		int cc = filetab[fd]->context;
		umfuse_current_context = cc;
		rv = fusetab[cc]->fuse->fops.read(
				filetab[fd]->path,
				buf,
				count,
				filetab[fd]->pos,
				&filetab[fd]->ffi);
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        		fprintf(stderr, "READ[%d] => path:%s count:%u\n",
				fd, filetab[fd]->path, count);
			fflush(stderr);
		}
		if (rv<0) {
			errno= -rv;
			return -1;
		} else {
			filetab[fd]->pos += rv;
			return rv;
		}
	}
}

static int umfuse_write(int fd, void *buf, size_t count)
{
#ifdef __UMFUSE_EXPERIMENTAL__
//TODO write page?!
	int rv;
	int cc;
	//printf("WRITE!\n");

	if (filetab[fd]==NULL) {
		errno = EBADF;
		/*
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
			fprintf(stderr, "WRITE[%d] => Error File Not Found\n");	
			fflush(stderr);
		}*/
		return -1;
	} else {
		cc=filetab[fd]->context;
		umfuse_current_context = cc;
		rv = fusetab[cc]->fuse->fops.write(filetab[fd]->path,
				buf, count, filetab[fd]->pos, &filetab[fd]->ffi);
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
			fprintf(stderr, "WRITE[%d] => path:%s count:0x%x\n",
				filetab[fd]->ffi.fh, filetab[fd]->path, count);
			fflush(stderr);
		}
	
	PRINTDEBUG(10,"WRITE rv:%d\n",rv); 

//		if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
  //      		fprintf(stderr, "WRITE[%lu] => path:%s count:0x%x\n",
//				filetab[fd]->ffi.fh, filetab[fd]->path, count);
		//printf("WRITE%s[%lu] %u bytes to %llu\n",
                  // (arg->write_flags & 1) ? "PAGE" : "",
                  // (unsigned long) arg->fh, arg->size, arg->offset);
		if (rv<0) {
			errno= -rv;
			return -1;
		} else {
			filetab[fd]->pos += rv;
			return rv;
		}
	}
#else
        errno = EROFS;
        return -1;
#endif
}

static int umfuse_fstat(int fd, struct stat *buf)
{
	if (filetab[fd]==NULL) {
		errno=ENOENT;
		return -1;
	} else {
		int rv;
		int cc = filetab[fd]->context;
		assert(cc>=0);
		umfuse_current_context = cc;
		memset(buf, 0, sizeof(struct stat));
		rv = fusetab[cc]->fuse->fops.getattr(
				filetab[fd]->path,buf);
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        		fprintf(stderr, "fstat->GETATTR[%d] => path:%s status: %s\n", fd,
				filetab[fd]->path, rv ? "Error" : "Succes");
			fflush(stderr);
		}
		if (rv<0) {
			errno= -rv;
			return -1;
		} else
			return rv;
	}
}

static int stat2stat64(struct stat64 *s64, struct stat *s)
{
	s64->st_dev= s->st_dev;
	s64->st_ino= s->st_ino;
	s64->st_mode= s->st_mode;
	s64->st_nlink= s->st_nlink;
	s64->st_uid= s->st_uid;
	s64->st_gid= s->st_gid;
	s64->st_rdev= s->st_rdev;
	s64->st_size= s->st_size;
	s64->st_blksize= s->st_blksize;
	s64->st_blocks= s->st_blocks;
	s64->st_atim= s->st_atim;
	s64->st_mtim= s->st_mtim;
	s64->st_ctim= s->st_ctim;
	return 0;
}

static int umfuse_fstat64(int fd, struct stat64 *buf64)
{
	if (filetab[fd]==NULL) {
		errno=ENOENT;
		return -1;
	} else {
		int rv;
		int cc=filetab[fd]->context;
		struct stat buf;
		assert(cc>=0);
		umfuse_current_context=cc;
		memset(&buf, 0, sizeof(struct stat));
		rv= fusetab[cc]->fuse->fops.getattr(
				filetab[fd]->path,&buf);
		if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        		fprintf(stderr, "fstat64->GETATTR[%d] => path:%s status: %s\n",fd,
				filetab[fd]->path, rv ? "Error" : "Succes");
			fflush(stderr);
		}
		if (rv<0) {
			errno= -rv;
			return -1;
		} else {
			stat2stat64(buf64,&buf);
			return rv;
		}
	}
}

static int umfuse_stat(char *path, struct stat *buf)
{
	int cc=searchcontext(path,SUBSTR);
	int rv;
	assert(cc>=0);
	umfuse_current_context = cc;
	memset(buf, 0, sizeof(struct stat));
	rv= fusetab[cc]->fuse->fops.getattr(
			unwrap(fusetab[cc],path),buf);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "stat->GETATTR => path:%s status: %s\n",
				path, rv ? "Error" : "Succes");
	if (rv<0) {
		errno= -rv;
		return -1;
	} else 
		return rv;
}

static int umfuse_lstat(char *path, struct stat *buf)
{
	int cc=searchcontext(path,SUBSTR);
	int rv;
	assert(cc>=0);
	umfuse_current_context = cc;
	memset(buf, 0, sizeof(struct stat));
	rv= fusetab[cc]->fuse->fops.getattr(
			unwrap(fusetab[cc],path),buf);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "lstat->GETATTR => path:%s status: %s\n",
				path, rv ? "Error" : "Succes");
	if (rv<0) {
		errno= -rv;
		return rv;
	} else 
		return rv;
}

static int umfuse_stat64(char *path, struct stat64 *buf64)
{
	int cc=searchcontext(path,SUBSTR);
	int rv;
	struct stat buf;
	assert(cc>=0);
	umfuse_current_context = cc;
	memset(&buf, 0, sizeof(struct stat));
	rv= fusetab[cc]->fuse->fops.getattr(
			unwrap(fusetab[cc],path),&buf);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "stat64->GETATTR => path:%s status: %s\n",
				path, rv ? "Error" : "Succes");
	if (rv<0) {
		errno= -rv;
		return -1;
	} else {
		stat2stat64(buf64,&buf);
		return rv;
	}
}

static int umfuse_lstat64(char *path, struct stat64 *buf64)
{
	int cc=searchcontext(path,SUBSTR);
	int rv;
	struct stat buf;
	assert(cc>=0);
	umfuse_current_context = cc;
	memset(&buf, 0, sizeof(struct stat));
	rv = fusetab[cc]->fuse->fops.getattr(
			unwrap(fusetab[cc],path),&buf);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "ltat64->GETATTR => path:%s status: %s\n",
				path, rv ? "Error" : "Succes");
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else {
		stat2stat64(buf64,&buf);
		return rv;
	}
}

static int umfuse_readlink(char *path, char *buf, size_t bufsiz)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	assert(cc >= 0);
	umfuse_current_context = cc;
	rv = fusetab[cc]->fuse->fops.readlink(
			unwrap(fusetab[cc], path), buf, bufsiz);
	PRINTDEBUG(10,"umfuse_readlink %s %s %d\n",unwrap(fusetab[cc],path),buf,rv);
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;
}

static int umfuse_access(char *path, int mode)
{
	/* TODO dummy stub for access */

	int cc = searchcontext(path, SUBSTR);
	int rv;
	struct stat buf;
	assert(cc >= 0);
	umfuse_current_context = cc;
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "ACCESS => path:%s mode:%s%s%s%s\n", path,
				(mode & R_OK) ? "R_OK": "",
				(mode & W_OK) ? "W_OK": "",
				(mode & X_OK) ? "X_OK": "",
				(mode & F_OK) ? "F_OK": "");
		fflush(stderr);
	}
	rv = fusetab[cc]->fuse->fops.getattr(unwrap(fusetab[cc], path), &buf);
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else {
		/* XXX user permission mnagement */
		errno = 0;
		return 0;
	}
}
/*
static int umfuse_mknod(const char *path, mode_t mode, dev_t dev)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	assert(cc >= 0);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "MKNOD => path:%s\n",path);
	rv = fusetab[cc]->fuse->fops.mknod(
			unwrap(fusetab[cc], path), mode, dev);
	if (rv < 0) {
		errno = -rv;
		return -1;
	}
	return rv;
}
*/
static int umfuse_mkdir(char *path, int mode)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	assert(cc >= 0);
	umfuse_current_context = cc;
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "MKDIR => path:%s\n",path);
		fflush(stderr);
	}
	rv = fusetab[cc]->fuse->fops.mkdir(
			unwrap(fusetab[cc], path), mode);
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;
}

static int umfuse_rmdir(char *path)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	assert(cc >= 0);
	umfuse_current_context = cc;
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "RMDIR => path:%s\n",path);
		fflush(stderr);
	}
	rv= fusetab[cc]->fuse->fops.rmdir(
			unwrap(fusetab[cc], path));
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;
}

static int umfuse_chmod(char *path, int mode)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	umfuse_current_context = cc;
	assert(cc >= 0);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "CHMOD => path:%s\n",path);
		fflush(stderr);
	}
	rv= fusetab[cc]->fuse->fops.chmod(
			unwrap(fusetab[cc] ,path), mode);
	if (rv < 0) {
		errno = -rv;
		return -1;
	}
	return rv;
}

static int umfuse_chown(char *path, uid_t owner, gid_t group)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	assert(cc >= 0);
	umfuse_current_context = cc;
	rv = fusetab[cc]->fuse->fops.chown(
			unwrap(fusetab[cc], path), owner, group);
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;
}

static int umfuse_lchown(char *path, uid_t owner, gid_t group)
{
//	Do not follow symlinks
//		and call chown
}

static int umfuse_unlink(char *path)
{
	int cc = searchcontext(path, SUBSTR);
	int rv;
	assert(cc >= 0);
	umfuse_current_context = cc;
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "UNLINK => path:%s\n",path);
	rv = fusetab[cc]->fuse->fops.unlink(
			unwrap(fusetab[cc], path));
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;
}

static int umfuse_link(char *oldpath, char *newpath)
{
	int cc = searchcontext(newpath, SUBSTR);
	int rv;
	assert(cc >= 0);
 	umfuse_current_context = cc;

	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "LINK => oldpath:%s newpath:%s\n",oldpath, newpath);
	rv = fusetab[cc]->fuse->fops.link(
			unwrap(fusetab[cc], oldpath),
			unwrap(fusetab[cc], newpath));
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;	
}

//see fuse.h: it is has not the same meaning of syscall
static int umfuse_fsync(int fd)
{
	int cc = filetab[fd]->context;
	umfuse_current_context = cc;
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "kernel FSYNC. It has a different semantics in fuse\n");
		fflush(stderr);
	}
			
	/*	//	rv = fusetab[cc]->fuse->fops.read
	//int cc = searchcontext(oldpath, SUBSTR);
	int rv;
	assert(cc >= 0);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG)
        	fprintf(stderr, "FSYNC => path:%s:\n",filetab[fd]->path);
	rv = fusetab[cc]->fuse->fops.fsync(fd);
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
	        return rv;
*/
	return 0;
}

static int fuse_rename(char *oldpath, char *newpath)
{
	int cc = searchcontext(newpath, SUBSTR);
	int rv;
	assert(cc >= 0);
	umfuse_current_context = cc;
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "RENAME => %s ->%s\n",oldpath, newpath);
		fflush(stderr);
	}
	rv = fusetab[cc]->fuse->fops.rename(
			unwrap(fusetab[cc], oldpath),
			unwrap(fusetab[cc], newpath));
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;	
}

static int umfuse_symlink(char *oldpath, char *newpath)
{
	int cc = searchcontext(newpath, SUBSTR);
	int rv;
	umfuse_current_context = cc;

	assert(cc >= 0);
	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "SYMLINK => %s -> %s\n",
					newpath, oldpath);
		fflush(stderr);
	}
	rv = fusetab[cc]->fuse->fops.symlink(
			oldpath,
			unwrap(fusetab[cc], newpath));
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;	
}

static int umfuse_truncate(char *path, off_t length)
{

	int cc = searchcontext(path, SUBSTR);
	int rv;
	umfuse_current_context = cc;

	if (fusetab[cc]->fuse->flags & FUSE_DEBUG) {
        	fprintf(stderr, "TRUNCATE solodebug => path%s\n",path);		
		fflush(stderr);
	}
	assert(cc >= 0);
	rv = fusetab[cc]->fuse->fops.truncate(
			unwrap(fusetab[cc], path),length);
	if (rv < 0) {
		errno = -rv;
		return -1;
	} else
		return rv;	
}

static int umfuse_ftruncate(int fd, off_t length)
{
	return umfuse_truncate(filetab[fd]->path,length);
}

/** Change the access and/or modification times of a file */
static int umfuse_utime(char *path, struct utimbuf *buf)
{
	//int cc = searchcontext(path, SUBSTR);
	int cc = umfuse_current_context; /*???*/
	assert(cc >= 0);
	int rv;
	if (buf == NULL) {
		struct utimbuf localbuf;
		localbuf.actime=localbuf.modtime=time(NULL);
		rv = fusetab[cc]->fuse->fops.utime(unwrap(fusetab[cc], path), &localbuf);
	} else
		rv = fusetab[cc]->fuse->fops.utime(unwrap(fusetab[cc], path), buf);
	if (rv < 0) {
		errno = -rv;
		return -1;
	}
	return rv;	
}

static int umfuse_utimes(char *path, struct timeval tv[2])
{
	//approximate solution. drop microseconds
	struct utimbuf buf;
	buf.actime=tv[0].tv_sec;
	buf.modtime=tv[1].tv_sec;
	return umfuse_utime(path, &buf);
}

static ssize_t umfuse_pread(int fd, void *buf, size_t count, long long offset)
{
	off_t off=offset;
}

static ssize_t umfuse_pwrite(int fd, const void *buf, size_t count, long long offset)
{
	off_t off=offset;
}

/* TODO management of fcntl */
static int umfuse_fcntl32(int fd, int cmd, void *arg)
{
	//printf("umfuse_fcntl32\n");
	errno=0;
	return 0;
}

static int umfuse_fcntl64(int fd, int cmd, void *arg)
{
	//printf("umfuse_fcntl64\n");
	errno=0;
	return 0;
}

static int umfuse_lseek(int fd, int offset, int whence)
{
	if (filetab[fd]==NULL) {
		errno = EBADF; 
		return -1;
	} else {
		switch (whence) {
			case SEEK_SET:
				filetab[fd]->pos=offset;
				break;
			case SEEK_CUR:
				 filetab[fd]->pos += offset;
				 break;
			case SEEK_END:
				 {
				 struct stat buf;
				 int rv;
				 rv = fusetab[fd]->fuse->fops.getattr(filetab[fd]->path,&buf);
				 if (rv>=0) {
				 	filetab[fd]->pos = buf.st_size + offset;
				 } else {
					 errno=EBADF;
					 return -1;
				 }
				 }
				 break;
		}

		return filetab[fd]->pos;
	}
}

static int umfuse__llseek(unsigned int fd, unsigned long offset_high,  unsigned  long offset_low, loff_t *result, unsigned int whence)
{
	PRINTDEBUG(10,"umfuse__llseek %d %d %d %d\n",fd,offset_high,offset_low,whence);
	if (result == NULL) {
		errno = EFAULT;
		return -1;
	} else if (offset_high != 0) {
		errno = EINVAL;
		return -1;
	} else {
		long rv;
		rv=umfuse_lseek(fd,offset_low,whence);
		if (rv >= 0) {
			*result=rv;
			return 0;
		} else {
			errno = -rv;
			return -1;
		}
	}
}

void contextclose(struct fuse_context *fc)
{
	umfuse_umount2(fc->fuse->path,MNT_FORCE);
}

static struct service s;

static void
__attribute__ ((constructor))
init (void)
{
	printf("umfuse init\n");
	s.name="umfuse fuse ";
	s.code=0x01;
	s.checkpath=fuse_path;
	s.checksocket=alwaysfalse;
	s.syscall=(intfun *)malloc(scmap_scmapsize * sizeof(intfun));
	s.socket=(intfun *)malloc(scmap_sockmapsize * sizeof(intfun));
	s.syscall[uscno(__NR_mount)]=umfuse_mount;
	s.syscall[uscno(__NR_umount)]=umfuse_umount2; /* umount must be mapped onto umount2 */
	s.syscall[uscno(__NR_umount2)]=umfuse_umount2;
	s.syscall[uscno(__NR_open)]=umfuse_open;
	s.syscall[uscno(__NR_creat)]=umfuse_open; /*creat is an open with (O_CREAT|O_WRONLY|O_TRUNC)*/
	s.syscall[uscno(__NR_read)]=umfuse_read;
#ifdef __UMFUSE_EXPERIMENTAL__
	s.syscall[uscno(__NR_write)]=umfuse_write;
#endif
	//s.syscall[uscno(__NR_readv)]=readv;
	//s.syscall[uscno(__NR_writev)]=writev;
	s.syscall[uscno(__NR_close)]=umfuse_close;
	s.syscall[uscno(__NR_stat)]=umfuse_stat;
	s.syscall[uscno(__NR_lstat)]=umfuse_lstat;
	s.syscall[uscno(__NR_fstat)]=umfuse_fstat;
	s.syscall[uscno(__NR_stat64)]=umfuse_stat64;
	s.syscall[uscno(__NR_lstat64)]=umfuse_lstat64;
	s.syscall[uscno(__NR_fstat64)]=umfuse_fstat64;
	s.syscall[uscno(__NR_readlink)]=umfuse_readlink;
	s.syscall[uscno(__NR_getdents)]=um_getdents;
	s.syscall[uscno(__NR_getdents64)]=um_getdents64;
	s.syscall[uscno(__NR_access)]=umfuse_access;
	s.syscall[uscno(__NR_fcntl)]=umfuse_fcntl32;
	s.syscall[uscno(__NR_fcntl64)]=umfuse_fcntl64;
	//s.syscall[uscno(__NR_mknod)]=umfuse_mknod;
#ifdef __UMFUSE_EXPERIMENTAL__
	s.syscall[uscno(__NR_lseek)]=umfuse_lseek;
	s.syscall[uscno(__NR__llseek)]=umfuse__llseek;
	s.syscall[uscno(__NR_mkdir)]=umfuse_mkdir;
	s.syscall[uscno(__NR_rmdir)]=umfuse_rmdir;
	s.syscall[uscno(__NR_chown)]=umfuse_chown;
	//s.syscall[uscno(__NR_lchown)]=umfuse_lchown;
	//s.syscall[uscno(__NR_fchown)]=fchown;
	s.syscall[uscno(__NR_chmod)]=umfuse_chmod;
	//s.syscall[uscno(__NR_fchmod)]=fchmod;
	s.syscall[uscno(__NR_unlink)]=umfuse_unlink;
	s.syscall[uscno(__NR_fsync)]=umfuse_fsync; //not the syscall meaning
	//s.syscall[uscno(__NR_fdatasync)]=fdatasync;
	//s.syscall[uscno(__NR__newselect)]=select;
	s.syscall[uscno(__NR_link)]=umfuse_link;
	s.syscall[uscno(__NR_symlink)]=umfuse_symlink;
	s.syscall[uscno(__NR_truncate)]=umfuse_truncate;
#endif
	//s.syscall[uscno(__NR_pread64)]=umfuse_pread;
	//s.syscall[uscno(__NR_pwrite64)]=umfuse_pwrite;
	s.syscall[uscno(__NR_utime)]=umfuse_utime;
	s.syscall[uscno(__NR_utimes)]=umfuse_utimes;
	//s.syscall[uscno(__NR_ftruncate)]=umfuse_ftruncate;
	add_service(&s);
}

static void
__attribute__ ((destructor))
fini (void)
{
	free(s.syscall);
	free(s.socket);
	forallfusetabdo(contextclose);
	printf("umfuse fini\n");
}
