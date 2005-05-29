/*
 * $Id$
 *
 * Copyright (c) 2004, Raphael Manfredi
 *
 *----------------------------------------------------------------------
 * This file is part of gtk-gnutella.
 *
 *  gtk-gnutella is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  gtk-gnutella is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with gtk-gnutella; if not, write to the Free Software
 *  Foundation, Inc.:
 *      59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *----------------------------------------------------------------------
 */

/**
 * @ingroup core
 * @file
 *
 * Handling UDP datagrams.
 */

#include "common.h"

RCSID("$Id$");

#include "udp.h"
#include "gmsg.h"
#include "inet.h"
#include "nodes.h"
#include "sockets.h"
#include "bsched.h"
#include "gnet_stats.h"
#include "gnutella.h"
#include "mq_udp.h"
#include "routing.h"
#include "pcache.h"
#include "ntp.h"
#include "bogons.h"

#include "if/gnet_property_priv.h"

#include "lib/endian.h"
#include "lib/misc.h"
#include "lib/override.h"		/* Must be the last header included */

/**
 * Look whether the datagram we received is a valid Gnutella packet.
 */
static gboolean
udp_is_valid_gnet(struct gnutella_socket *s)
{
	struct gnutella_node *n = node_udp_get_ip_port(s->ip, s->port);
	struct gnutella_header *head;
	gchar *msg;
	guint32 size;				/* Payload size, from the Gnutella message */

	if (s->pos < GTA_HEADER_SIZE) {
		msg = "Too short";
		goto not;
	}

	head = (struct gnutella_header *) s->buffer;
	READ_GUINT32_LE(head->size, size);

	n->header = *head;						/* Struct copy */
	n->size = s->pos - GTA_HEADER_SIZE;		/* Payload size if Gnutella msg */

	gnet_stats_count_received_header(n);
	gnet_stats_count_received_payload(n);

	if (size + GTA_HEADER_SIZE != s->pos) {
		msg = "Size mismatch";
		goto not;
	}

	/*
	 * We only support a subset of Gnutella message from UDP.  In particular,
	 * messages like HSEP data, BYE or QRP are not expected!
	 */

	switch (head->function) {
	case GTA_MSG_INIT:
	case GTA_MSG_INIT_RESPONSE:
	case GTA_MSG_VENDOR:
	case GTA_MSG_STANDARD:
	case GTA_MSG_PUSH_REQUEST:
	case GTA_MSG_SEARCH_RESULTS:
		break;
	case GTA_MSG_SEARCH:
		msg = "Queries not yet processed from UDP";
		goto drop;			/* XXX don't handle GUESS queries for now */
	default:
		msg = "Gnutella message not processed from UDP";
		goto drop;
	}

	return TRUE;

drop:
	gnet_stats_count_dropped(n, MSG_DROP_UNEXPECTED);
	gnet_stats_count_general(GNR_UDP_UNPROCESSED_MESSAGE, 1);
	goto log;

not:
	gnet_stats_count_general(GNR_UDP_ALIEN_MESSAGE, 1);
	/* FALL THROUGH */

log:
	if (udp_debug) {
		g_warning("got invalid Gnutella packet from UDP (%s): %s",
			ip_port_to_gchar(s->ip, s->port), msg);
		if (s->pos)
			dump_hex(stderr, "UDP datagram", s->buffer, s->pos);
	}

	return FALSE;
}

/**
 * Notification from the socket layer that we got a new datagram.
 */
void
udp_received(struct gnutella_socket *s)
{
	gboolean bogus = FALSE;

	/*
	 * If reply comes from the NTP port, notify that they're running NTP.
	 */

	if (s->ip == 0x7f000001 && s->port == NTP_PORT) {	/* from 127.0.0.1:123 */
		ntp_got_reply(s);
		return;
	}

	/*
	 * This must be regular Gnutella traffic then.
	 */

	inet_udp_got_incoming(s->ip);
	bws_udp_count_read(s->pos);

	/*
	 * If we get traffic from a bogus IP (unroutable), warn, for now.
	 */

	if (bogons_check(s->ip)) {
		bogus = TRUE;

		if (udp_debug) {
			g_warning("UDP datagram (%d byte%s) received from bogus IP %s",
				(gint) s->pos, s->pos == 1 ? "" : "s", ip_to_gchar(s->ip));
		}
		gnet_stats_count_general(GNR_UDP_BOGUS_SOURCE_IP, 1);
	}

	if (!udp_is_valid_gnet(s))
		return;

	/*
	 * Process message as if it had been received from regular Gnet by
	 * another node, only we'll use a special "pseudo UDP node" as origin.
	 */

	if (udp_debug > 19)
		printf("UDP got %s from %s%s\n", gmsg_infostr_full(s->buffer),
			bogus ? "BOGUS " : "", ip_port_to_gchar(s->ip, s->port));

	node_udp_process(s);
}

/**
 * Send a datagram to the specified node, made of `len' bytes from `buf',
 * forming a valid Gnutella message.
 */
void udp_send_msg(gnutella_node_t *n, gpointer buf, gint len)
{
	g_assert(NODE_IS_UDP(n));

	mq_udp_node_putq(n->outq, gmsg_to_pmsg(buf, len), n);
}

/**
 * Send a Gnutella ping to the specified host via UDP, using the
 * specified MUID.
 */
void
udp_connect_back(guint32 ip, guint16 port, const gchar *muid)
{
	struct gnutella_msg_init *m;
	struct gnutella_node *n = node_udp_get_ip_port(ip, port);
	guint32 size;

	if (!udp_active())
		return;

	m = build_ping_msg(muid, 1, FALSE, &size);

	mq_udp_node_putq(n->outq, gmsg_to_pmsg(m, size), n);

	if (udp_debug > 19)
		printf("UDP queued connect-back PING %s (%u bytes) to %s\n",
			guid_hex_str(muid), size, ip_port_to_gchar(ip, port));
}

/**
 * Send a Gnutella ping to the specified host.
 */
void
udp_send_ping(guint32 ip, guint16 port)
{
	struct gnutella_msg_init *m;
	struct gnutella_node *n = node_udp_get_ip_port(ip, port);
	guint32 size;

	if (!udp_active())
		return;

	m = build_ping_msg(NULL, 1, FALSE, &size);
	udp_send_msg(n, m, size);
}

/* vi: set ts=4 sw=4 cindent: */
