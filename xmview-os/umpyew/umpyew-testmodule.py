import os

# If defined, this list contains the system calls that are defined in this
# module. This way, the C binding will call our modSyscall function *at most*
# for these syscalls, avoiding useless invocations. Obviously, in order to be
# called, the modCheckFun must have returned a non-zero value.
# If not defined, every time modCheckFun returns non-zero, modSyscall will be
# called.

# modManagedSyscalls = ['open', 'read', 'write', 'close']

# This list can contain zero or more of the following:
# 'proc', 'module', 'mount'.
modCtlHistorySet = ['proc'];

def modCtl(cls, cmd, cmdArgs):
	return 0
	print "class:", cls, "command:", cmd, "args:", cmdArgs
	# Just an example
	if cls == 'proc':
		if cmd == 'add':
			print "New process with id %d" % cmdArgs[0]
		elif cmd == 'rem':
			print "Process with id %d removed" % cmdArgs[0]
	return 0

def modCheckFun(*arg, **kw):
	if kw.has_key('path'):
#		print "path:", kw['path']
		if kw['path'] == '/tmp/passwd':
			return 1
#	elif kw.has_key('socket'):
#		print "socket:", kw['socket']
#	elif kw.has_key('fstype'):
#		print "fstype:", kw['fstype']
#	elif kw.has_key('sc'):
#		print "sc:", kw['sc']
#	elif kw.has_key('binfmt'):
#		print "binfmt:", kw['binfmt']
	return 0

def sysOpen(pathname, flags, mode, **kw):
	print "opening %s with flags %d and mode %d" % (pathname, flags, mode)
	try:
		return (os.open(pathname, flags, mode), 0)
	except OSError, (errno, strerror):
		return (-1, errno)

def sysClose(fd, **kw):
	print "closing fd %d" % fd
	try:
		return (os.close(fd), 0)
	except OSError, (errno, strerror):
		return (-1, errno)

def sysString(path, **kw):
	print "calling %s(%s)" % (kw['cname'], path)
	try:
		return (getattr(os, kw[cname])(path), 0)
	except OSError, (errno, strerror):
		return (-1, errno)

def sysStringInt(path, mode, **kw):
	print "calling %s(%s, %d)" % (kw['cname'], path, mode)
	try:
		return (getattr(os, kw[cname])(path, mode), 0)
	except OSError, (errno, strerror):
		return (-1, errno)

def sysStringString(oldpath, newpath, **kw):
	print "calling %s(%s, %s)" % (kw['cname'], oldpath, newpath)
	try:
		return (getattr(os, kw[cname])(oldpath, newpath), 0)
	except OSError, (errno, strerror):
		return (-1, errno)

def sysStats(param, buf, **kw):
	print "calling %s(%s)" % (kw['cname'], param)
	try:
		os.stat_float_times(False)
		statinfo = getattr(os, kw['cname'].rstrip('64'))(param)
		for field in filter(lambda s:s.startswith('st_'), dir(statinfo)):
			buf[field] = getattr(statinfo, field)
		return (0, 0)
	except OSError, (errno, strerror):
		return (-1, errno)


sysRmdir = sysUnlink = sysString
sysAccess = sysMkdir = sysChmod = sysStringInt
sysLink = sysSymlink = sysStringString
sysStat64 = sysLstat64 = sysFstat64 = sysStats

sysRead = sysWrite = None