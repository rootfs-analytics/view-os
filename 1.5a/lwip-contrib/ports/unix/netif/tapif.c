/*   This is part of LWIPv6
 *   
 *   tap interface for ale4net
 *   Copyright 2005,2010,2011 Renzo Davoli University of Bologna - Italy
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
 *   51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>


#include "lwip/debug.h"

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"

#include "netif/etharp.h"

#if LWIP_NL
#include "lwip/arphdr.h"
#endif

#ifdef IPv6_AUTO_CONFIGURATION
#include "lwip/ip_autoconf.h"
#endif


#if defined(LWIP_DEBUG) && defined(LWIP_TCPDUMP)
#include "netif/tcpdump.h"
#endif /* LWIP_DEBUG && LWIP_TCPDUMP */

#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#define DEVTAP "/dev/net/tun"

/*-----------------------------------------------------------------------------------*/

#ifndef TAPIF_DEBUG
#define TAPIF_DEBUG                     DBG_OFF
#endif

#define IFNAME0 't'
#define IFNAME1 'p'

/*-----------------------------------------------------------------------------------*/

static const struct eth_addr ethbroadcast = {{0xff,0xff,0xff,0xff,0xff,0xff}};

struct tapif {
  struct eth_addr *ethaddr;
  /* Add whatever per-interface state that is needed here. */
  int fd;
	struct netif_fddata *fddata;
};

/* Forward declarations. */
static void  tapif_input(struct netif_fddata *fddata, short revents);
static err_t tapif_output(struct netif *netif, struct pbuf *p, struct ip_addr *ipaddr);

/*-----------------------------------------------------------------------------------*/

static void
arp_timer(void *arg)
{
	etharp_tmr((struct netif *) arg );
	sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler)arp_timer, arg);
}

/*-----------------------------------------------------------------------------------*/
static int
low_level_init(struct netif *netif, char *ifname)
{
	struct tapif *tapif;
	
	tapif = netif->state;

	/* Obtain MAC address from network interface. */
	
	/* (We just fake an address...) */
	tapif->ethaddr->addr[0] = 0x2;
	tapif->ethaddr->addr[1] = 0x2;
	tapif->ethaddr->addr[2] = 0x3;
	tapif->ethaddr->addr[3] = 0x4;
	tapif->ethaddr->addr[4] = 0x5 + netif->num;
	tapif->ethaddr->addr[5] = 0x6;

	/* Do whatever else is needed to initialize interface. */
	
	tapif->fd = open(DEVTAP, O_RDWR);
	LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_init: fd %d\n", tapif->fd));
	if(tapif->fd == -1) {
		perror("tapif_init: try running \"modprobe tun\" or rebuilding your kernel with CONFIG_TUN; cannot open "DEVTAP);
		return ERR_IF;
	}
	{
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
		if (ifname != NULL)
			strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
		if (ioctl(tapif->fd, TUNSETIFF, (void *) &ifr) < 0) {
			perror("tapif_init: DEVTAP ioctl TUNSETIFF");
			return ERR_IF;
		}
	}
	if ((tapif->fddata=netif_addfd(netif,
				tapif->fd, tapif_input, NULL, 0, POLLIN)) == NULL)
		return ERR_IF;
	else
		return ERR_OK;
}

/* cleanup: garbage collection */
static err_t tapif_ctl(struct netif *netif, int request, void *arg)
{
	struct tapif *tapif = netif->state;

	if (tapif) {

		switch (request) {

			case NETIFCTL_CLEANUP:
				close(tapif->fd);

				/* Unset ARP timeout on this interface */
				sys_untimeout((sys_timeout_handler)arp_timer, netif);

				mem_free(tapif);
		}
	}
	return ERR_OK;
}


/*-----------------------------------------------------------------------------------*/
/*
 * low_level_output():
 *
 * Should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 */
/*-----------------------------------------------------------------------------------*/

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
	struct pbuf *q;
	char buf[1514];
	char *bufptr;
	struct tapif *tapif;
	
	tapif = netif->state;
	/* initiate transfer(); */
	
	bufptr = &buf[0];
	
	for(q = p; q != NULL; q = q->next) {
		//printf("%s: q->len=%d   tot_len=%d\n", __func__, q->len, p->tot_len);
		/* Send the data from the pbuf to the interface, one pbuf at a
		time. The size of the data in each pbuf is kept in the ->len
		variable. */    
		/* send data from(q->payload, q->len); */
		memcpy(bufptr, q->payload, q->len);
		bufptr += q->len;
	}
	
	/* signal that packet should be sent(); */
	if(write(tapif->fd, buf, p->tot_len) == -1) {
		perror("tapif: write");
	}

	return ERR_OK;
}
/*-----------------------------------------------------------------------------------*/
/*
 * low_level_input():
 *
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 */
/*-----------------------------------------------------------------------------------*/
static struct pbuf *
low_level_input(struct tapif *tapif,u16_t ifflags)
{
	struct pbuf *p, *q;
	u16_t len;
	char buf[1514];
	char *bufptr;
	
	/* Obtain the size of the packet and put it into the "len"
	variable. */
	len = read(tapif->fd, buf, sizeof(buf));
	if (!(ETH_RECEIVING_RULE(buf,tapif->ethaddr->addr,ifflags))) {
		return NULL;
	}
  
	/* We allocate a pbuf chain of pbufs from the pool. */
	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	
	if(p != NULL) {
		/* We iterate over the pbuf chain until we have read the entire
		packet into the pbuf. */
		bufptr = &buf[0];
		for(q = p; q != NULL; q = q->next) {
			/* Read enough bytes to fill this pbuf in the chain. The
			available data in the pbuf is given by the q->len
			variable. */
			/* read data into(q->payload, q->len); */
			memcpy(q->payload, bufptr, q->len);
			bufptr += q->len;
		}
		/* acknowledge that packet has been read(); */
	} else {
		/* drop packet(); */
	}

	return p;  
}

/*-----------------------------------------------------------------------------------*/
/*
 * tapif_output():
 *
 * This function is called by the TCP/IP stack when an IP packet
 * should be sent. It calls the function called low_level_output() to
 * do the actuall transmission of the packet.
 *
 */
/*-----------------------------------------------------------------------------------*/
static err_t
tapif_output(struct netif *netif, struct pbuf *p, struct ip_addr *ipaddr)
{
	if (! (netif->flags & NETIF_FLAG_UP)) {
		LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_output: interface DOWN, discarded\n"));
		return ERR_OK;
	} else
		return etharp_output(netif, ipaddr, p);
}

/*-----------------------------------------------------------------------------------*/
/*
 * tapif_input():
 *
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.
 *
 */
/*-----------------------------------------------------------------------------------*/
static void
tapif_input(struct netif_fddata *fddata, short revents)
{
	struct netif *netif = fddata->netif;
	struct tapif *tapif;
	struct eth_hdr *ethhdr;
	struct pbuf *p;
	
	tapif = netif->state;
	
	p = low_level_input(tapif,netif->flags);
	
	if(p == NULL) {
		LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_input: low_level_input returned NULL\n"));
		return;
	}
	ethhdr = p->payload;
	
#ifdef LWIP_PACKET
	ETH_CHECK_PACKET_IN(netif,p);
#endif
	switch(htons(ethhdr->type)) {
#ifdef IPv6
		case ETHTYPE_IP6:
#endif
		case ETHTYPE_IP:
			LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_input: IP packet\n"));
			etharp_ip_input(netif, p);
			pbuf_header(p, -14);
#if defined(LWIP_DEBUG) && defined(LWIP_TCPDUMP)
			tcpdump(p);
#endif /* LWIP_DEBUG && LWIP_TCPDUMP */
			netif->input(p, netif);
			break;
		case ETHTYPE_ARP:
			LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_input: ARP packet\n"));
			etharp_arp_input(netif, tapif->ethaddr, p);
			break;
		default:
			pbuf_free(p);
			break;
	}
}

/*-----------------------------------------------------------------------------------*/
/*
 * tapif_init():
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 */
/*-----------------------------------------------------------------------------------*/
err_t
tapif_init(struct netif *netif)
{
	struct tapif *tapif;
	char *ifname;
	
	tapif = mem_malloc(sizeof(struct tapif));
	if (!tapif)
		return ERR_MEM;
	ifname = netif->state; /*state is temporarily used to store the if name */
	netif->state = tapif;
	netif->name[0] = IFNAME0;
	netif->name[1] = IFNAME1;
	netif->link_type = NETIF_TAPIF;
	netif->num=netif_next_num(netif,NETIF_TAPIF);
	netif->output = tapif_output;
	netif->linkoutput = low_level_output;
	netif->netifctl = tapif_ctl;
	netif->mtu = 1500; 	 
	/* hardware address length */
	netif->hwaddr_len = 6;
	netif->flags|=NETIF_FLAG_BROADCAST;
#if LWIP_NL
	netif->type = ARPHRD_ETHER;
#endif
	
	tapif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);
	if (low_level_init(netif, ifname) < 0) {
		mem_free(tapif);
		return ERR_IF;
	}
	
	etharp_init();
	
	sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler)arp_timer, netif);

	return ERR_OK;
}

