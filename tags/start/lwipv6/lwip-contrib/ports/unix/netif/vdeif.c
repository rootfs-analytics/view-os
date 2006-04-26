/*   This is part of LWIPv6
 *   
 *   VDE (virtual distributed ethernet) interface for ale4net
 *   (based on tapif interface Adam Dunkels <adam@sics.se>)
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
 */

/* tapif interface Adam Dunkels <adam@sics.se>
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
 */


#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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

#if defined(LWIP_DEBUG) && defined(LWIP_TCPDUMP)
#include "netif/tcpdump.h"
#endif /* LWIP_DEBUG && LWIP_TCPDUMP */

#ifdef linux
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <stdint.h>
#include <libgen.h>
#include "vde.h"
#include <sys/poll.h>

#endif /* linux */

#ifndef VDEIF_DEBUG
#define VDEIF_DEBUG                     DBG_OFF
#endif

#define IFNAME0 'v'
#define IFNAME1 'd'

static const struct eth_addr ethbroadcast = {{0xff,0xff,0xff,0xff,0xff,0xff}};

struct vdeif {
  struct eth_addr *ethaddr;
  /* Add whatever per-interface state that is needed here. */
  char *sockname;
  int connected_fd;
  int fddata;
  struct sockaddr_un dataout;
  int intno;
  int group;
};

/* Forward declarations. */
static void  vdeif_input(struct netif *netif);
static err_t vdeif_output(struct netif *netif, struct pbuf *p,
			       struct ip_addr *ipaddr);

static void vdeif_thread(void *data);

#define SWITCH_MAGIC 0xfeedface
#define BUFSIZE 2048
#define ETH_ALEN 6

enum request_type { REQ_NEW_CONTROL };

struct request_v3 {
  uint32_t magic;
  uint32_t version;
  enum request_type type;
  struct sockaddr_un sock;
};


static int send_fd(char *name, int fddata, struct sockaddr_un *datasock, int intno, int group)
{
  int pid = getpid();
  struct request_v3 req;
  int fdctl;

  struct sockaddr_un sock;

  if((fdctl = socket(AF_UNIX, SOCK_STREAM, 0)) < 0){
		return ERR_IF;
	}

  sock.sun_family = AF_UNIX;
  snprintf(sock.sun_path, sizeof(sock.sun_path), "%s", name);
  if(connect(fdctl, (struct sockaddr *) &sock, sizeof(sock))){
		return ERR_IF;
  }

  req.magic=SWITCH_MAGIC;
  req.version=3;
  req.type=REQ_NEW_CONTROL+((group > 0)?((geteuid()<<8) + group) << 8:0);
  
  req.sock.sun_family=AF_UNIX;
  memset(req.sock.sun_path, 0, sizeof(req.sock.sun_path));
  sprintf(&req.sock.sun_path[1], "%5d-%2d", pid, intno);

  if(bind(fddata, (struct sockaddr *) &req.sock, sizeof(req.sock)) < 0){
		return ERR_IF;
  }

  if (send(fdctl,&req,sizeof(req),0) < 0) {
		return ERR_IF;
  }

  if (recv(fdctl,datasock,sizeof(struct sockaddr_un),0)<0) {
		return ERR_IF;
  }

  return fdctl;
}

/*-----------------------------------------------------------------------------------*/
static int
low_level_init(struct netif *netif)
{
  struct vdeif *vdeif;
  int randaddr;
	char envkey[]="LWIPV6vdx";

  vdeif = netif->state;
  
  randaddr=rand();

  /* Obtain MAC address from network interface. */

  /* (We just fake an address...) */
  vdeif->ethaddr->addr[0] = 0x2;
  vdeif->ethaddr->addr[1] = 0x2;
  vdeif->ethaddr->addr[2] = randaddr >> 24;
  vdeif->ethaddr->addr[3] = randaddr >> 16;
  vdeif->ethaddr->addr[4] = randaddr >> 8;
  vdeif->ethaddr->addr[5] = 0x6;

  /* Do whatever else is needed to initialize interface. */
   
	envkey[8]=netif->num + '0';	
	if ((vdeif->sockname=getenv(envkey))==NULL)
		vdeif->sockname=VDESTDSOCK;
  vdeif->intno=netif->num;
  vdeif->group=0;
  if ((vdeif->fddata=socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
	  perror ("vde: can't open socket");
		return ERR_IF;
  }
  vdeif->connected_fd=send_fd(vdeif->sockname, vdeif->fddata, &(vdeif->dataout), vdeif->intno, vdeif->group);

	if (vdeif->connected_fd >= 0) {
		sys_thread_new(vdeif_thread, netif, DEFAULT_THREAD_PRIO);
		return ERR_OK;
	} else {
		return ERR_IF;
	}
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
  struct vdeif *vdeif;

	if (p->tot_len > 1514)
		return ERR_MEM;

  vdeif = netif->state;
  /* initiate transfer(); */
  
  bufptr = &buf[0];
  
  for(q = p; q != NULL; q = q->next) {
    /* Send the data from the pbuf to the interface, one pbuf at a
       time. The size of the data in each pbuf is kept in the ->len
       variable. */    
    /* send data from(q->payload, q->len); */
    memcpy(bufptr, q->payload, q->len);
    bufptr += q->len;
  }

  /* signal that packet should be sent(); */
  if(sendto(vdeif->fddata,buf,p->tot_len,0,(struct sockaddr *) &(vdeif->dataout), sizeof(struct sockaddr_un)) == -1) {
    perror("vdeif: write");
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
low_level_input(struct vdeif *vdeif,u16_t ifflags)
{
  struct pbuf *p, *q;
  u16_t len;
  char buf[1514];
  char *bufptr;
  struct sockaddr_un datain;
  socklen_t datainsize=sizeof(struct sockaddr_un);

  /* Obtain the size of the packet and put it into the "len"
     variable. */
  len=recvfrom(vdeif->fddata,buf,sizeof(buf),0,(struct sockaddr *)&datain, &datainsize);
  
	if (!(ETH_RECEIVING_RULE(buf,vdeif->ethaddr->addr,ifflags))) {
		/*printf("PACKET DROPPED\n");
		printf("%x:%x:%x:%x:%x:%x %x:%x:%x:%x:%x:%x %x\n",
				buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
				vdeif->ethaddr->addr[0], vdeif->ethaddr->addr[1], vdeif->ethaddr->addr[2],
				vdeif->ethaddr->addr[3], vdeif->ethaddr->addr[4], vdeif->ethaddr->addr[5],
				ifflags);*/
			return NULL;
	}
  /* We allocate a pbuf chain of pbufs from the pool. */
  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  /*printf("vdeif low level recv len %d\n",len);*/
  
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
	  fprintf(stderr,"vdeif: dropped packet (pbuf)\n");
  }

  return p;  
}
/*-----------------------------------------------------------------------------------*/
static void 
vdeif_thread(void *arg)
{
  struct netif *netif;
  struct vdeif *vdeif;
  fd_set fdset;
  int ret;
  
  netif = arg;
  vdeif = netif->state;
  
  while(1) {
    FD_ZERO(&fdset);
    FD_SET(vdeif->fddata, &fdset);

	  LWIP_DEBUGF(VDEIF_DEBUG, ("vde_thread: waiting4packet\n"));
    /* Wait for a packet to arrive. */
    ret = select(vdeif->fddata + 1, &fdset, NULL, NULL, NULL);

    if(ret == 1) {
      /* Handle incoming packet. */
      vdeif_input(netif);
    } else if(ret == -1 && errno != EINTR) {
      perror("vdeif_thread: select");
    }
  }
}
/*-----------------------------------------------------------------------------------*/
/*
 * vdeif_output():
 *
 * This function is called by the TCP/IP stack when an IP packet
 * should be sent. It calls the function called low_level_output() to
 * do the actuall transmission of the packet.
 *
 */
/*-----------------------------------------------------------------------------------*/
static err_t
vdeif_output(struct netif *netif, struct pbuf *p,
		  struct ip_addr *ipaddr)
{
  /*printf("vdeif_output %x:%x:%x:%x\n",
		  ipaddr->addr[0],
		  ipaddr->addr[1],
		  ipaddr->addr[2],
		  ipaddr->addr[3]);*/
  if (! (netif->flags & NETIF_FLAG_UP)) {
	  LWIP_DEBUGF(VDEIF_DEBUG, ("vdeif_output: interface DOWN, discarded\n"));
	  return ERR_OK;
  } else 
	  return etharp_output(netif, ipaddr, p);
}
/*-----------------------------------------------------------------------------------*/
/*
 * vdeif_input():
 *
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.
 *
 */
/*-----------------------------------------------------------------------------------*/
static void
vdeif_input(struct netif *netif)
{
  struct vdeif *vdeif;
  struct eth_hdr *ethhdr;
  struct pbuf *p;


  vdeif = netif->state;
  
  p = low_level_input(vdeif,netif->flags);

  if(p == NULL) {
    LWIP_DEBUGF(VDEIF_DEBUG, ("vdeif_input: low_level_input returned NULL\n"));
    return;
  }

  ethhdr = p->payload;
  /* printf("vdeif_input %x %d\n",htons(ethhdr->type),p->tot_len);*/

#ifdef LWIP_PACKET
	ETH_CHECK_PACKET_IN(netif,p);
#endif
  switch(htons(ethhdr->type)) {
#ifdef IPv6
  case ETHTYPE_IP6:
  case ETHTYPE_IP:
#else
  case ETHTYPE_IP:
#endif
    LWIP_DEBUGF(VDEIF_DEBUG, ("vdeif_input: IP packet\n"));
    etharp_ip_input(netif, p);
    pbuf_header(p, -14);
#if defined(LWIP_DEBUG) && defined(LWIP_TCPDUMP)
    tcpdump(p);
#endif /* LWIP_DEBUG && LWIP_TCPDUMP */
    /*printf("netif->input %x %x %x\n",netif->input,p, netif);*/
    netif->input(p, netif);
    break;
  case ETHTYPE_ARP:
    LWIP_DEBUGF(VDEIF_DEBUG, ("vdeif_input: ARP packet\n"));
    /*printf("vdeif_ARP \n");*/
    etharp_arp_input(netif, vdeif->ethaddr, p);
    break;
  default:
    pbuf_free(p);
    break;
  }
}
/*-----------------------------------------------------------------------------------*/
static void
arp_timer(void *arg)
{
  etharp_tmr();
  sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler)arp_timer, NULL);
}
/*-----------------------------------------------------------------------------------*/
/*
 * vdeif_init():
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 */
/*-----------------------------------------------------------------------------------*/
err_t
vdeif_init(struct netif *netif)
{
  struct vdeif *vdeif;
	static u8_t num=0;
    
  vdeif = mem_malloc(sizeof(struct vdeif));
  if (!vdeif)
      return ERR_MEM;
  netif->state = vdeif;
  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
	netif->num=num++;
  netif->output = vdeif_output;
  netif->linkoutput = low_level_output;
  netif->mtu = 1500; 	 
  /* hardware address length */
  netif->hwaddr_len = 6;
  netif->flags|=NETIF_FLAG_BROADCAST;
#ifdef LWIP_NL
  netif->type=ARPHRD_ETHER;
#endif
  
  vdeif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);
  if (low_level_init(netif) < 0) {
		mem_free(vdeif);
		return ERR_IF;
	}
  etharp_init();
  
  sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler)arp_timer, NULL);
  return ERR_OK;
}
/*-----------------------------------------------------------------------------------*/