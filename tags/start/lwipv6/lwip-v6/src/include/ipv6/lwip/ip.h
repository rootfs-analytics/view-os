/*   This is part of LWIPv6
 *   Developed for the Ale4NET project
 *   Application Level Environment for Networking
 *   
 *   Copyright 2004 Renzo Davoli University of Bologna - Italy
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
/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
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
#ifndef __LWIP_IP_H__
#define __LWIP_IP_H__

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
/* This is the common part of all PCB types. It needs to be at the
 *    beginning of a PCB type definition. It is located here so that
 *       changes to this common part are made in one location instead of
 *          having to change all PCB structs. */
#define IP_PCB struct ip_addr local_ip; \
struct ip_addr remote_ip; \
	/* Socket options */  \
	u16_t so_options;      \
	/* Type Of Service */ \
	u8_t tos;              \
	/* Time To Live */     \
	u8_t ttl

#include "lwip/err.h"

#define IP_HLEN 40

#define IP_PROTO_ICMP 58
#define IP_PROTO_ICMP4 1
#define IP_PROTO_UDP 17
#define IP_PROTO_UDPLITE 170
#define IP_PROTO_TCP 6
#define IP_FRAG_TAG 44

/* This is passed as the destination address to ip_output_if (not
   to ip_output), meaning that an IP header already is constructed
   in the pbuf. This is used when TCP retransmits. */
#ifdef IP_HDRINCL
#undef IP_HDRINCL
#endif /* IP_HDRINCL */
#define IP_HDRINCL  NULL

/* The IPv6 header. */
#if 0
struct ip_hdr {
#if BYTE_ORDER == LITTLE_ENDIAN
  u8_t tclass1:4, v:4;
  u8_t flow1:4, tclass2:4;  
#else
  u8_t v:4, tclass1:4;
  u8_t tclass2:4, flow1:4;
#endif
  u16_t flow2;
  u16_t len;                /* payload length */
  u8_t nexthdr;             /* next header */
  u8_t hoplim;              /* hop limit (TTL) */
  struct ip_addr src, dest;          /* source and destination IP addresses */
};
#endif
#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/bpstruct.h"
#endif
PACK_STRUCT_BEGIN
struct ip_hdr {
	  /* version / class1 */
	  PACK_STRUCT_FIELD(u16_t _v_cl_fl);
	  /* class2 / flow */
	  PACK_STRUCT_FIELD(u16_t flow2);
	  /* length */
	  PACK_STRUCT_FIELD(u16_t len);
	  /* next_hdr / hoplim*/
	  PACK_STRUCT_FIELD(u16_t _next_hop);
	  /* source and destination IP addresses */
	  PACK_STRUCT_FIELD(struct ip_addr src);
	  PACK_STRUCT_FIELD(struct ip_addr dest);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END
#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/epstruct.h"
#endif

#define IPH_V(hdr) (ntohs((hdr)->_v_cl_fl) >> 12)
#define IPH_CL(hdr) ((ntohs((hdr)->_v_cl_fl) >> 4) & 0xff)
#define IPH_FLOW(hdr) ((ntohs((hdr)->_v_cl_fl) & 0xf) + ((hdr)->flow2))
#define IPH_NEXTHDR(hdr) (ntohs((hdr)->_next_hop) >> 8)
#define IPH_HOPLIMIT(hdr) (ntohs((hdr)->_next_hop) & 0xff)
#define IPH_V_SET(hdr,vv) ((hdr)->_v_cl_fl)= \
		htons((ntohs((hdr)->_v_cl_fl) & 0xffffff) | ((vv) << 12))
#define IPH_NEXTHDR_SET(hdr, nexthdr) ((hdr)->_next_hop) = \
		htons((nexthdr) << 8 | IPH_HOPLIMIT(hdr))
#define IPH_HOPLIMIT_SET(hdr, hop) ((hdr)->_next_hop) = \
		htons(IPH_NEXTHDR(hdr) << 8 | (hop))
#define IPH_NEXT_HOP_SET(hdr, nexthdr, hop) ((hdr)->_next_hop) = \
		htons((nexthdr) << 8 | (hop))


void ip_init(void);

/* no variable part */
#define IPH_HL(x) (IP_HLEN >> 2) /*IPv4 compatibility*/
#define IPH_PROTO(x) ((x)->nexthdr)

#include "lwip/netif.h"

struct netif *ip_route(struct ip_addr *dest);

void ip_input(struct pbuf *p, struct netif *inp);

/* source and destination addresses in network byte order, please */
err_t ip_output(struct pbuf *p, struct ip_addr *src, struct ip_addr *dest,
         unsigned char ttl, unsigned char tos, unsigned char proto);

err_t ip_output_if(struct pbuf *p, struct ip_addr *src, struct ip_addr *dest,
      unsigned char ttl, unsigned char tos, unsigned char proto,
      struct netif *netif, struct ip_addr *nexthop, int flags);

#if IP_DEBUG
void ip_debug_print(int how, struct pbuf *p);
#endif /* IP_DEBUG */


/*
 * 
 * Option flags per-socket. These are the same like SO_XXX.
 */
#define	SOF_DEBUG	    (u16_t)0x0001U		/* turn on debugging info recording */
#define	SOF_ACCEPTCONN	(u16_t)0x0002U		/* socket has had listen() */
#define	SOF_REUSEADDR	(u16_t)0x0004U		/* allow local address reuse */
#define	SOF_KEEPALIVE	(u16_t)0x0008U		/* keep connections alive */
#define	SOF_DONTROUTE	(u16_t)0x0010U		/* just use interface addresses */
#define	SOF_BROADCAST	(u16_t)0x0020U		/* permit sending of broadcast msgs */
#define	SOF_USELOOPBACK	(u16_t)0x0040U		/* bypass hardware when possible */
#define	SOF_LINGER	    (u16_t)0x0080U		/* linger on close if data present */
#define	SOF_OOBINLINE	(u16_t)0x0100U		/* leave received OOB data in line */
#define	SOF_REUSEPORT	(u16_t)0x0200U		/* allow local address & port reuse */
#define	SOF_IPV6_CHECKSUM (u16_t)0x8000U	/* RAW socket IPv6 checksum */


#define IP4_HLEN 20

#if 0
/* This is the common part of all PCB types. It needs to be at the
   beginning of a PCB type definition. It is located here so that
   changes to this common part are made in one location instead of
   having to change all PCB structs. */
#define IP_PCB struct ip_addr local_ip; \
  struct ip_addr remote_ip; \
   /* Socket options */  \
  u16_t so_options;      \
   /* Type Of Service */ \
  u8_t tos;              \
  /* Time To Live */     \
  u8_t ttl

/*
 * Option flags per-socket. These are the same like SO_XXX.
 */
#define	SOF_DEBUG	    (u16_t)0x0001U		/* turn on debugging info recording */
#define	SOF_ACCEPTCONN	(u16_t)0x0002U		/* socket has had listen() */
#define	SOF_REUSEADDR	(u16_t)0x0004U		/* allow local address reuse */
#define	SOF_KEEPALIVE	(u16_t)0x0008U		/* keep connections alive */
#define	SOF_DONTROUTE	(u16_t)0x0010U		/* just use interface addresses */
#define	SOF_BROADCAST	(u16_t)0x0020U		/* permit sending of broadcast msgs */
#define	SOF_USELOOPBACK	(u16_t)0x0040U		/* bypass hardware when possible */
#define	SOF_LINGER	    (u16_t)0x0080U		/* linger on close if data present */
#define	SOF_OOBINLINE	(u16_t)0x0100U		/* leave received OOB data in line */
#define	SOF_REUSEPORT	(u16_t)0x0200U		/* allow local address & port reuse */
#endif


#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/bpstruct.h"
#endif
PACK_STRUCT_BEGIN
struct ip4_hdr {
  /* version / header length / type of service */
  PACK_STRUCT_FIELD(u16_t _v_hl_tos);
  /* total length */
  PACK_STRUCT_FIELD(u16_t _len);
  /* identification */
  PACK_STRUCT_FIELD(u16_t _id);
  /* fragment offset field */
  PACK_STRUCT_FIELD(u16_t _offset);
#define IP_RF 0x8000        /* reserved fragment flag */
#define IP_DF 0x4000        /* dont fragment flag */
#define IP_MF 0x2000        /* more fragments flag */
#define IP_OFFMASK 0x1fff   /* mask for fragmenting bits */
  /* time to live / protocol*/
  PACK_STRUCT_FIELD(u16_t _ttl_proto);
  /* checksum */
  PACK_STRUCT_FIELD(u16_t _chksum);
  /* source and destination IP addresses */
  PACK_STRUCT_FIELD(struct ip4_addr src);
  PACK_STRUCT_FIELD(struct ip4_addr dest); 
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END
#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/epstruct.h"
#endif

#define IPH4_V(hdr)  (ntohs((hdr)->_v_hl_tos) >> 12)
#define IPH4_HL(hdr) ((ntohs((hdr)->_v_hl_tos) >> 8) & 0x0f)
#define IPH4_TOS(hdr) (htons((ntohs((hdr)->_v_hl_tos) & 0xff)))
#define IPH4_LEN(hdr) ((hdr)->_len)
#define IPH4_ID(hdr) ((hdr)->_id)
#define IPH4_OFFSET(hdr) ((hdr)->_offset)
#define IPH4_TTL(hdr) (ntohs((hdr)->_ttl_proto) >> 8)
#define IPH4_PROTO(hdr) (ntohs((hdr)->_ttl_proto) & 0xff)
#define IPH4_CHKSUM(hdr) ((hdr)->_chksum)

#define IPH4_VHLTOS_SET(hdr, v, hl, tos) (hdr)->_v_hl_tos = (htons(((v) << 12) | ((hl) << 8) | (tos)))
#define IPH4_LEN_SET(hdr, len) (hdr)->_len = (len)
#define IPH4_ID_SET(hdr, id) (hdr)->_id = (id)
#define IPH4_OFFSET_SET(hdr, off) (hdr)->_offset = (off)
#define IPH4_TTL_SET(hdr, ttl) (hdr)->_ttl_proto = (htons(IPH4_PROTO(hdr) | ((ttl) << 8)))
#define IPH4_PROTO_SET(hdr, proto) (hdr)->_ttl_proto = (htons((proto) | (IPH4_TTL(hdr) << 8)))
#define IPH4_CHKSUM_SET(hdr, chksum) (hdr)->_chksum = (chksum)

#if 0
#if IP_DEBUG
void ip_debug_print(struct pbuf *p);
#else
#define ip_debug_print(p)
#endif /* IP_DEBUG */
#endif


struct pseudo_iphdr {
	u8_t version;
	u16_t iphdrlen;
	u16_t proto;
	struct ip_addr *src;
	struct ip_addr *dest;
};

#endif /* __LWIP_IP_H__ */

