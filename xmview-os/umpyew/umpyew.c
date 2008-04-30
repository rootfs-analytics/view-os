/*   This is part of um-ViewOS
 *   The user-mode implementation of OSVIEW -- A Process with a View
 *
 *   example of um-ViewOS module:
 *   remap of /unreal onto the real FS
 *   /unreal/XXXX is mapped to XXXX in th real FS
 *   
 *   Copyright 2005 Renzo Davoli University of Bologna - Italy
 *   
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License, version 2, as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *   $Id$
 *
 */   
#include <Python.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <string.h>
#include <utime.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <config.h>
#include "module.h"
#include "libummod.h"

#include "gdebug.h"

struct pService
{
	PyObject *ctl;
	PyObject *checkfun;
	PyObject **syscall;
	PyObject **socket;
};

/*
struct cpymap_s
{
	char *cname;
	char *pyname;
};
*/

static struct service s;
static struct pService ps;

#define PYTHON_SYSCALL(cname, pyname) \
	{ \
		if (PyObject_HasAttrString(pModule, #pyname)) \
		{ \
			pTmpFunc = PyObject_GetAttrString(pModule, #pyname); \
			if (pTmpFunc && PyCallable_Check(pTmpFunc)) \
			{ \
				pTmpObj = PyTuple_New(2); \
				PyTuple_SET_ITEM(pTmpObj, 0, pTmpFunc); \
				pTmpDict = PyDict_New(); \
				PyDict_SetItemString(pTmpDict, "cname", PyString_FromString(#cname)); \
				PyDict_SetItemString(pTmpDict, "pyname", PyString_FromString(#pyname)); \
				PyTuple_SET_ITEM(pTmpObj, 1, pTmpDict); \
				GMESSAGE("found function for system call %s, adding", #cname); \
				GENSERVICESYSCALL(ps, cname, pTmpObj, PyObject*); \
				SERVICESYSCALL(s, cname, umpyew_##cname); \
			} \
			else if (pTmpFunc == Py_None) \
			{ \
				GMESSAGE("system call %s is mapped to itself", #cname); \
				SERVICESYSCALL(s, cname, cname); \
			} \
		} \
	}

#if 0
static struct cpymap_s cpymap_syscall[] = {
	{ "execve", "sysExecve" },
	{ "chdir", "sysChdir" },
	{ "fchdir", "sysFchdir" },
	{ "getcwd", "sysGetcwd" },
	{ "select", "sysSelect" },
	{ "poll", "sysPoll" },
	{ "_newselect", "sys_newselect" },
	{ "pselect6", "sysPselect6" },
	{ "ppoll", "sysPpoll" },
	{ "umask", "sysUmask" },
	{ "chroot", "sysChroot" },
	{ "dup", "sysDup" },
	{ "dup2", "sysDup2" },
	{ "mount", "sysMount" },
	{ "umount2", "sysUmount2" },
	{ "ioctl", "sysIoctl" },
	{ "read", "sysRead" },
	{ "write", "sysWrite" },
	{ "stat64", "sysStat64" },
	{ "lstat64", "sysLstat64" },
	{ "fstat64", "sysFstat64" },
	{ "fchown", "sysFchown" },
	{ "chown32", "sysChown32" },
	{ "lchown32", "sysLchown32" },
	{ "fchown32", "sysFchown32" },
	{ "fchmod", "sysFchmod" },
	{ "getxattr", "sysGetxattr" },
	{ "lgetxattr", "sysLgetxattr" },
	{ "fgetxattr", "sysFgetxattr" },
	{ "readlink", "sysReadlink" },
	{ "getdents64", "sysGetdents64" },
	{ "fcntl", "sysFcntl" },
	{ "fcntl64", "sysFcntl64" },
	{ "lseek", "sysLseek" },
	{ "_llseek", "sys_llseek" },
	{ "rename", "sysRename" },
	{ "statfs64", "sysStatfs64" },
	{ "fstatfs64", "sysFstatfs64" },
	{ "utime", "sysUtime" },
	{ "utimes", "sysUtimes" },
	{ "fsync", "sysFsync" },
	{ "fdatasync", "sysFdatasync" },
	{ "truncate64", "sysTruncate64" },
	{ "ftruncate64", "sysFtruncate64" },
#ifdef __NR_pread64
	{ "pread64", "sysPread64" },
#else
	{ "pread", "sysPread" },
#endif
#ifdef __NR_pwrite64
	{ "pwrite64", "sysPwrite64" },
#else
	{ "pwrite", "sysPwrite" },
#endif
#ifdef _UM_MMAP
	{ "mmap", "sysMmap" },
	{ "mmap2", "sysMmap2" },
	{ "munmap", "sysMunmap" },
	{ "mremap", "sysMremap" },
#endif
	{ "gettimeofday", "sysGettimeofday" },
	{ "settimeofday", "sysSettimeofday" },
	{ "adjtimex", "sysAdjtimex" },
	{ "clock_gettime", "sysClock_gettime" },
	{ "clock_settime", "sysClock_settime" },
	{ "clock_getres", "sysClock_getres" },
	{ "uname", "sysUname" },
	{ "gethostname", "sysGethostname" },
	{ "sethostname", "sysSethostname" },
	{ "getdomainname", "sysGetdomainname" },
	{ "setdomainname", "sysSetdomainname" },
	{ "getuid", "sysGetuid" },
	{ "setuid", "sysSetuid" },
	{ "geteuid", "sysGeteuid" },
	{ "setfsuid", "sysSetfsuid" },
	{ "setreuid", "sysSetreuid" },
	{ "getresuid", "sysGetresuid" },
	{ "setresuid", "sysSetresuid" },
	{ "getgid", "sysGetgid" },
	{ "setgid", "sysSetgid" },
	{ "getegid", "sysGetegid" },
	{ "setfsgid", "sysSetfsgid" },
	{ "setregid", "sysSetregid" },
	{ "getresgid", "sysGetresgid" },
	{ "setresgid", "sysSetresgid" },
	{ "nice", "sysNice" },
	{ "getpriority", "sysGetpriority" },
	{ "setpriority", "sysSetpriority" },
	{ "getpid", "sysGetpid" },
	{ "getppid", "sysGetppid" },
	{ "getpgid", "sysGetpgid" },
	{ "setpgid", "sysSetpgid" },
	{ "getsid", "sysGetsid" },
	{ "setsid", "sysSetsid" },
#if 0
	{ "sysctl", "sysSysctl" },
	{ "ptrace", "sysPtrace" },
#endif
	{ "kill", "sysKill" },
#if (__NR_socketcall != __NR_doesnotexist)
};

static struct cpymap_s cpymap_socket[] =
{
	{ "doesnotexist", "sysDoesnotexist" },
	{ "socket", "sysSocket" },
#else 
	{ "socket", "sysSocket" },
#endif
	{ "bind", "sysBind" },
	{ "connect", "sysConnect" },
	{ "listen", "sysListen" },
	{ "accept", "sysAccept" },
	{ "getsockname", "sysGetsockname" },
	{ "getpeername", "sysGetpeername" },
	{ "socketpair", "sysSocketpair" },
	{ "send", "sysSend" },
	{ "recv", "sysRecv" },
	{ "sendto", "sysSendto" },
	{ "recvfrom", "sysRecvfrom" },
	{ "shutdown", "sysShutdown" },
	{ "setsockopt", "sysSetsockopt" },
	{ "getsockopt", "sysGetsockopt" },
	{ "sendmsg", "sysSendmsg" },
	{ "recvmsg", "sysRecvmsg" },
#if (__NR_socketcall != __NR_doesnotexist)
	{ "msocket", "sysMsocket" },
#endif
};

#endif

/* Used for calling PyCall with empty arg */
static PyObject *pEmptyTuple;

#if 0
static struct timestamp t1;
static struct timestamp t2;

static epoch_t unrealpath(int type,void *arg)
{
	/* This is an example that shows how to pick up extra info in the
	 * calling process. */
/*	printf("test umph info pid=%d, scno=%d, arg[0]=%d, argv[1]=%d\n",
			um_mod_getpid(),um_mod_getsyscallno(),
			um_mod_getargs()[0],um_mod_getargs()[1]); */
/* NB: DEVELOPMENT PHASE !! */
	if (type== CHECKPATH) {
		char *path=arg;
		epoch_t e=0;
		if(strncmp(path,"/unreal",7) == 0) {
			if ((e=tst_matchingepoch(&t2)) == 0)
				e=tst_matchingepoch(&t1);
			//fprint2("MATCH e=%lld\n",e);
		}
		return e;
	}
	else
		return 0;
}

static char *unwrap(char *path)
{
	char *s;
	s=&(path[7]);
	if (*s == 0) s = "/";
	return (s);
}

static long umpyew_statfs64(char *pathname, struct statfs64 *buf)
{
	return statfs64(unwrap(pathname),buf);
}

static long umpyew_stat64(char *pathname, struct stat64 *buf)
{
	return stat64(unwrap(pathname),buf);
}

static long umpyew_lstat64(char *pathname, struct stat64 *buf)
{
	return lstat64(unwrap(pathname),buf);
}

static long umpyew_readlink(char *path, char *buf, size_t bufsiz)
{
	return readlink(unwrap(path),buf,bufsiz);
}


static long umpyew_utime(char *filename, struct utimbuf *buf)
{
	return utime(unwrap(filename),buf);
}

static long umpyew_utimes(char *filename, struct timeval tv[2])
{
	return utimes(unwrap(filename),tv);
}

static ssize_t umpyew_pread(int fd, void *buf, size_t count, long long offset)
{
	off_t off=offset;
	return pread(fd,buf,count,off);
}

static ssize_t umpyew_pwrite(int fd, const void *buf, size_t count, long long offset)
{
	off_t off=offset;
	return pwrite(fd,buf,count,off);
}

static long umpyew_lseek(int fildes, int offset, int whence)
{
	return (int) lseek64(fildes, (off_t) offset, whence);
}

#endif

#define PYIN(type, cname, argc) \
	PyObject *pRetVal; \
	long retval; \
	PyObject *pFunc = PyTuple_GetItem(GETSERVICE##type(ps, cname), 0); \
	PyObject *pKw = PyTuple_GetItem(GETSERVICE##type(ps, cname), 1); \
	PyObject *pArg = PyTuple_New(argc)

#define PYOUT \
	GMESSAGE("calling python..."); \
	pRetVal = PyObject_Call(pFunc, pArg, pKw); \
	GMESSAGE("returned from python"); \
	Py_DECREF(pArg); \
	if (pRetVal) \
	{ \
		retval = PyInt_AsLong(PyTuple_GetItem(pRetVal, 0)); \
		errno = PyInt_AsLong(PyTuple_GetItem(pRetVal, 1)); \
		Py_DECREF(pRetVal); \
	} \
	else \
	{ \
		GMESSAGE("retval is null?"); \
		retval = -1; \
		errno = ENOSYS; \
	}

#define PYINSYS(cname, argc) PYIN(SYSCALL, cname, argc)
#define PYINSOCK(cname, argc) PYIN(SOCKET, cname, argc)

#define PYARG(argn, argval) PyTuple_SET_ITEM(pArg, argn, argval)

static long umpyew_open(char *pathname, int flags, mode_t mode)
{
	PYINSYS(open, 3);
	PYARG(0, PyString_FromString(pathname));
	PYARG(1, PyInt_FromLong(flags));
	PYARG(2, PyInt_FromLong(mode));
	PYOUT;
	return retval;
}

static long umpyew_close(int fd)
{
	PYINSYS(close, 1);
	PYARG(0, PyInt_FromLong(fd));
	PYOUT;
	return retval;
}

static long umpyew_access(char *path, int mode)
{
	PYINSYS(access, 2);
	PYARG(0, PyString_FromString(path));
	PYARG(1, PyInt_FromLong(mode));
	PYOUT;

	return retval;
}

static long umpyew_mkdir(char *path, int mode)
{
	PYINSYS(mkdir, 2);
	PYARG(0, PyString_FromString(path));
	PYARG(1, PyInt_FromLong(mode));
	PYOUT;
	return retval;
}

static long umpyew_rmdir(char *path)
{
	PYINSYS(rmdir, 1);
	PYARG(0, PyString_FromString(path));
	PYOUT;
	return retval;
}

static long umpyew_chmod(char *path, int mode)
{
	PYINSYS(chmod, 2);
	PYARG(0, PyString_FromString(path));
	PYARG(1, PyInt_FromLong(mode));
	PYOUT;
	return retval;
}

static long umpyew_chown(char *path, uid_t owner, gid_t group)
{
	PYINSYS(chown, 3);
	PYARG(0, PyString_FromString(path));
	PYARG(1, PyInt_FromLong(owner));
	PYARG(2, PyInt_FromLong(group));
	PYOUT;
	return retval;
}

static long umpyew_lchown(char *path, uid_t owner, gid_t group)
{
	PYINSYS(lchown, 3);
	PYARG(0, PyString_FromString(path));
	PYARG(1, PyInt_FromLong(owner));
	PYARG(2, PyInt_FromLong(group));
	PYOUT;
	return retval;
}

static long umpyew_unlink(char *path)
{
	PYINSYS(unlink, 1);
	PYARG(0, PyString_FromString(path));
	PYOUT;
	return retval;
}

static long umpyew_link(char *oldpath, char *newpath)
{
	PYINSYS(link, 2);
	PYARG(0, PyString_FromString(oldpath));
	PYARG(1, PyString_FromString(newpath));
	PYOUT;
	return retval;
}

static long umpyew_symlink(char *oldpath, char *newpath)
{
	PYINSYS(symlink, 2);
	PYARG(0, PyString_FromString(oldpath));
	PYARG(1, PyString_FromString(newpath));
	PYOUT;
	return retval;
}

#define PY_COPYSTATFIELD(field) \
	if ((pStatField = PyDict_GetItemString(pStatDict, #field))) \
		buf->field = PyInt_AsLong(pStatField)

#define UMPYEW_STATFUNC(name, fptype, fpname, fppycmd) \
	static long umpyew_##name(fptype fpname, struct stat64 *buf) \
	{ \
		PyObject *pStatDict = PyDict_New(); \
		PyObject *pStatField; \
		\
		PYINSYS(name, 2); \
		PYARG(0, fppycmd(fpname)); \
		\
		Py_INCREF(pStatDict); \
		PYARG(1, pStatDict); \
		PYOUT; \
		\
		if (retval == 0) \
		{ \
			PY_COPYSTATFIELD(st_dev); \
			PY_COPYSTATFIELD(st_ino); \
			PY_COPYSTATFIELD(st_mode); \
			PY_COPYSTATFIELD(st_nlink); \
			PY_COPYSTATFIELD(st_uid); \
			PY_COPYSTATFIELD(st_gid); \
			PY_COPYSTATFIELD(st_rdev); \
			PY_COPYSTATFIELD(st_size); \
			PY_COPYSTATFIELD(st_blksize); \
			PY_COPYSTATFIELD(st_blocks); \
			PY_COPYSTATFIELD(st_atime); \
			PY_COPYSTATFIELD(st_mtime); \
			PY_COPYSTATFIELD(st_ctime); \
			GMESSAGE("ino: %d", buf->st_ino); \
		} \
		 \
		GMESSAGE("returning %d", retval); \
		return retval; \
	}

UMPYEW_STATFUNC(stat64, char*, pathname, PyString_FromString);
UMPYEW_STATFUNC(lstat64, char*, pathname, PyString_FromString);
UMPYEW_STATFUNC(fstat64, int, fd, PyInt_FromLong);


static epoch_t checkfun(int type, void *arg)
{
	PyObject *pKw = PyDict_New();
	PyObject *pArg;
	PyObject *pRetVal;
	epoch_t retval = -1;
	struct binfmt_req *bf;

	switch(type)
	{
		case CHECKPATH:
			pArg = PyString_FromString((char*)arg);
			PyDict_SetItemString(pKw, "path", pArg);
			break;

		case CHECKSOCKET:
			pArg = PyInt_FromLong((long)arg);
			PyDict_SetItemString(pKw, "socket", pArg);
			break;

		case CHECKFSTYPE:
			pArg = PyString_FromString((char*)arg);
			PyDict_SetItemString(pKw, "fstype", pArg);
			break;

		case CHECKSC:
			pArg = PyInt_FromLong(*((long*)arg));
			PyDict_SetItemString(pKw, "sc", pArg);
			break;

		case CHECKBINFMT:
			bf = (struct binfmt_req*) arg;
			pArg = PyTuple_New(3);
			if (bf->path)
				PyTuple_SET_ITEM(pArg, 0, PyString_FromString(bf->path));

			if (bf->interp)
				PyTuple_SET_ITEM(pArg, 1, PyString_FromString(bf->interp));
				

			PyTuple_SET_ITEM(pArg, 2, PyInt_FromLong(bf->flags));
			PyDict_SetItemString(pKw, "binfmt", pArg);
			break;

		default:
			GERROR("Unknown check type %d", type);
			retval = 0;
			break;
	}

	if (!retval)
	{
		Py_DECREF(pKw);
		return retval;
	}

	pRetVal = PyObject_Call(ps.checkfun, pEmptyTuple, pKw);
	if (!pRetVal)
	{
		PyErr_Print();
		return 0;
	}

	retval = PyInt_AsLong(pRetVal);
	Py_DECREF(pArg);
	Py_DECREF(pKw);
	Py_DECREF(pRetVal);
	return retval;
}

static long ctl(int type, va_list ap)
{
	long retval;
	PyObject *pArg, *pCmdArgs, *pRetVal;

	pArg = PyTuple_New(3);

	switch(type)
	{
		case MC_PROC | MC_ADD:
			PyTuple_SET_ITEM(pArg, 0, PyString_FromString("proc"));
			PyTuple_SET_ITEM(pArg, 1, PyString_FromString("add"));
			pCmdArgs = PyTuple_New(3);
			/* The tuple is (id, ppid, max) */
			PyTuple_SET_ITEM(pCmdArgs, 0, PyInt_FromLong(va_arg(ap, long)));
			PyTuple_SET_ITEM(pCmdArgs, 1, PyInt_FromLong(va_arg(ap, long)));
			PyTuple_SET_ITEM(pCmdArgs, 2, PyInt_FromLong(va_arg(ap, long)));
			break;
			
		case MC_PROC | MC_REM:
			PyTuple_SET_ITEM(pArg, 0, PyString_FromString("proc"));
			PyTuple_SET_ITEM(pArg, 1, PyString_FromString("rem"));
			pCmdArgs = PyTuple_New(1);
			/* The tuple is (id) */
			PyTuple_SET_ITEM(pCmdArgs, 0, PyInt_FromLong(va_arg(ap, long)));
			break;

		case MC_MODULE | MC_ADD:
			PyTuple_SET_ITEM(pArg, 0, PyString_FromString("module"));
			PyTuple_SET_ITEM(pArg, 1, PyString_FromString("add"));
			pCmdArgs = PyTuple_New(1);
			/* The tuple is (code) */
			PyTuple_SET_ITEM(pCmdArgs, 0, PyInt_FromLong(va_arg(ap, long)));
			break;

		case MC_MODULE | MC_REM:
			PyTuple_SET_ITEM(pArg, 0, PyString_FromString("module"));
			PyTuple_SET_ITEM(pArg, 1, PyString_FromString("rem"));
			pCmdArgs = PyTuple_New(1);
			/* The tuple is (code) */
			PyTuple_SET_ITEM(pCmdArgs, 0, PyInt_FromLong(va_arg(ap, long)));
			break;
		
		default:
			Py_DECREF(pArg);
			return -1;
	}
	
	PyTuple_SET_ITEM(pArg, 2, pCmdArgs);

	pRetVal = PyObject_CallObject(ps.ctl, pArg);
	Py_DECREF(pCmdArgs);
	Py_DECREF(pArg);

	retval = PyInt_AsLong(pRetVal);
	Py_DECREF(pRetVal);

	return retval;
}

static long pippo()
{
	GMESSAGE("pippo!");
	errno = EFAULT;
	return -1;
}

static void
__attribute__ ((constructor))
init (void)
{}

void _um_mod_init(char *initargs)
{
	const char *name = "umpyew-testmodule";
	char* tmphs;
	int i;

	PyObject *pName, *pModule, *pTmpObj, *pTmpFunc, *pTmpDict;

	GMESSAGE("umpyew init: %s", initargs);
	s.name="Prototypal Python bindings for *MView";
	s.code=0x07;
	s.syscall=(sysfun *)calloc(scmap_scmapsize,sizeof(sysfun));
	s.socket=(sysfun *)calloc(scmap_sockmapsize,sizeof(sysfun));

	Py_Initialize();
	pEmptyTuple = PyTuple_New(0);
	pName = PyString_FromString(name);

	pModule = PyImport_Import(pName);
	Py_DECREF(pName);

	if (!pModule)
	{
		GERROR("Error loading Python module %s.", name);
		PyErr_Print();
		return;
	}

	/*
	 * Adding ctl
	 */
	if ((ps.ctl = PyObject_GetAttrString(pModule, "modCtl")) && PyCallable_Check(ps.ctl))
		s.ctl = ctl;
	else
	{
		GERROR("function modCtl not defined in module %s", name);
		Py_XDECREF(ps.ctl);
		return;
	}

	/*
	 * Adding checkfun
	 */
	if ((ps.checkfun = PyObject_GetAttrString(pModule, "modCheckFun")) && PyCallable_Check(ps.checkfun))
		s.checkfun = checkfun;
	else
	{
		GERROR("function modCheckFun not defined in module %s", name);
		Py_DECREF(ps.ctl);
		Py_XDECREF(ps.checkfun);
		return;
	}
	
	/*
	 * Adding ctlhs
	 */
	MCH_ZERO(&(s.ctlhs));
	pTmpObj = PyObject_GetAttrString(pModule, "modCtlHistorySet");

	if (pTmpObj && PyList_Check(pTmpObj))
		for (i = 0; i < PyList_Size(pTmpObj); i++)
			if ((tmphs = PyString_AsString(PyList_GET_ITEM(pTmpObj, i))))
			{
				if (!strcmp(tmphs, "proc"))
					MCH_SET(MC_PROC, &(s.ctlhs));
				else if (!strcmp(tmphs, "module"))
					MCH_SET(MC_MODULE, &(s.ctlhs));
				else if (!strcmp(tmphs, "mount"))
					MCH_SET(MC_MOUNT, &(s.ctlhs));
			}

	Py_XDECREF(pTmpObj);

	/*
	 * Adding system calls
	 */
	ps.syscall = calloc(scmap_scmapsize, sizeof(PyObject*));

	PYTHON_SYSCALL(open, sysOpen);
	PYTHON_SYSCALL(close, sysClose);
	PYTHON_SYSCALL(access, sysAccess);
	PYTHON_SYSCALL(mkdir, sysMkdir);
	PYTHON_SYSCALL(rmdir, sysRmdir);
	PYTHON_SYSCALL(chmod, sysChmod);
	PYTHON_SYSCALL(chown, sysChown);
	PYTHON_SYSCALL(lchown, sysLchown);
	PYTHON_SYSCALL(unlink, sysUnlink);
	PYTHON_SYSCALL(link, sysLink);
	PYTHON_SYSCALL(symlink, sysSymlink);
	PYTHON_SYSCALL(stat64, sysStat64);
	PYTHON_SYSCALL(lstat64, sysLstat64);
	PYTHON_SYSCALL(fstat64, sysFstat64);
/*    PYTHON_SYSCALL(read, sysRead);*/
/*    PYTHON_SYSCALL(write, sysWrite);*/
	SERVICESYSCALL(s, read, read);

	add_service(&s);



#if 0	

	SERVICESYSCALL(s, read, read);
	SERVICESYSCALL(s, write, write);
#if 0
	SERVICESYSCALL(s, stat, unreal_stat64);
	SERVICESYSCALL(s, lstat, unreal_lstat64);
	SERVICESYSCALL(s, fstat, fstat64);
#endif
#if !defined(__x86_64__)
	SERVICESYSCALL(s, stat64, unreal_stat64);
	SERVICESYSCALL(s, lstat64, unreal_lstat64);
	SERVICESYSCALL(s, fstat64, fstat64);
#else
	SERVICESYSCALL(s, stat, unreal_stat64);
	SERVICESYSCALL(s, lstat, unreal_lstat64);
	SERVICESYSCALL(s, fstat, fstat64);
#endif
	SERVICESYSCALL(s, readlink, unreal_readlink);
#if 0 
	SERVICESYSCALL(s, getdents, getdents64);
#endif
	SERVICESYSCALL(s, getdents64, getdents64);
	SERVICESYSCALL(s, access, unreal_access);
#if !defined(__x86_64__)
	SERVICESYSCALL(s, fcntl, fcntl32);
	SERVICESYSCALL(s, fcntl64, fcntl64);
	SERVICESYSCALL(s, _llseek, _llseek);
#else
	SERVICESYSCALL(s, fcntl, fcntl);
#endif
	SERVICESYSCALL(s, lseek,  unreal_lseek);
	SERVICESYSCALL(s, fchown, fchown);
	SERVICESYSCALL(s, fchmod, fchmod);
	SERVICESYSCALL(s, fsync, fsync);
	SERVICESYSCALL(s, fdatasync, fdatasync);
	SERVICESYSCALL(s, _newselect, select);
	SERVICESYSCALL(s, pread64, unreal_pread);
	SERVICESYSCALL(s, pwrite64, unreal_pwrite);
	SERVICESYSCALL(s, utime, unreal_utime);
	SERVICESYSCALL(s, utimes, unreal_utimes);
#if !defined(__x86_64__)
	SERVICESYSCALL(s, statfs64, unreal_statfs64);
	SERVICESYSCALL(s, fstatfs64, fstatfs64);
#else
	SERVICESYSCALL(s, statfs, unreal_statfs64);
	SERVICESYSCALL(s, fstatfs, fstatfs64);
#endif
#endif

}

static void
__attribute__ ((destructor))
fini (void)
{
	GBACKTRACE(5,20);
	free(s.syscall);
	free(s.socket);

	/* Finalizing will destroy everything, no need for DECREFs (I think) */
	Py_Finalize();
	GMESSAGE("umpyew fini");
}