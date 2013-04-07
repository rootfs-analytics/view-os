/*   This is part of um-ViewOS
 *   The user-mode implementation of OSVIEW -- A Process with a View
 *
 *   UMNETNATIVE: Virtual Native Network
 *    Copyright (C) 2008  Renzo Davoli <renzo@cs.unibo.it>
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/net.h>
#include <linux/net.h>
#include <linux/sockios.h>
#include <linux/if.h>

#include "umnet.h"

static int umnetnative_ioctlparms(int fd, int req, struct umnet *nethandle)
{
	switch (req) {
		case FIONREAD:
			return _IOR(0,0,int);
		case FIONBIO:
			return _IOW(0,0,int);
		case SIOCGIFCONF:
			return _IOWR(0,0,struct ifconf);
		case SIOCGSTAMP:
			return _IOR(0,0,struct timeval);
		case SIOCGIFTXQLEN:
			return _IOWR(0,0,struct ifreq);
		case SIOCGIFFLAGS:
		case SIOCGIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCGIFMEM:
		case SIOCGIFMTU:
		case SIOCGIFHWADDR:
		case SIOCGIFINDEX:
			return _IOWR(0,0,struct ifreq);
		case SIOCSIFFLAGS:
		case SIOCSIFADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFBRDADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFMETRIC:
		case SIOCSIFMEM:
		case SIOCSIFMTU:
		case SIOCSIFHWADDR:
			return _IOW(0,0,struct ifreq);
		default:
			return 0;
	}
}

int umnetnative_msocket (int domain, int type, int protocol,
		struct umnet *nethandle){
	return msocket(NULL,domain, type, protocol);
}

int umnetnative_init (char *source, char *mountpoint, unsigned long flags, char *args, struct umnet *nethandle) {
	return 0;
}

int umnetnative_fini (struct umnet *nethandle){
	return 0;
}

int um_mod_event_subscribe(void (* cb)(), void *arg, int fd, int how);

static int umnetnative_ioctl(int d, int request, void *arg)
{
	if (request == SIOCGIFCONF) {
		int rv;
		void *save;
		struct ifconf *ifc=(struct ifconf *)arg;
		save=ifc->ifc_buf;
		ioctl(d,request,arg);
		ifc->ifc_buf=malloc(ifc->ifc_len);
		um_mod_umoven((long) save,ifc->ifc_len,ifc->ifc_buf);
		rv=ioctl(d,request,arg);
		if (rv>=0)
			um_mod_ustoren((long) save,ifc->ifc_len,ifc->ifc_buf);
		free(ifc->ifc_buf);
		ifc->ifc_buf=save;
		return rv;
	}
	return ioctl(d,request,arg);
}

struct umnet_operations umnet_ops={
	.msocket=umnetnative_msocket,
	.bind=bind,
	.connect=connect,
	.listen=listen,
	.accept=accept,
	.getsockname=getsockname,
	.getpeername=getpeername,
	.send=send,
	.sendto=sendto,
	.recvfrom=recvfrom,
	.sendmsg=sendmsg,
	.recvmsg=recvmsg,
	.getsockopt=getsockopt,
	.setsockopt=setsockopt,
	.shutdown=shutdown,
	.read=read,
	.write=write,
	.ioctl=umnetnative_ioctl,
	.close=close,
	.fcntl=(void *)fcntl,
	.ioctlparms=umnetnative_ioctlparms,
	.init=umnetnative_init,
	.fini=umnetnative_fini,
	.event_subscribe=um_mod_event_subscribe
};
