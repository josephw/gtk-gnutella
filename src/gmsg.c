/*
 * Copyright (c) 2002, Raphael Manfredi
 *
 * Gnutella Messages.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>		/* For memcpy() */

#include "pmsg.h"
#include "gmsg.h"
#include "nodes.h"
#include "mq.h"
#include "gnutella.h"
#include "routing.h"

static gchar *msg_name[256];
static gint msg_weight[256];		/* For gmsg_cmp() */

/*
 * gmsg_init
 *
 * Initialization of the Gnutella message structures.
 */
void gmsg_init(void)
{
	gint i;

	for (i = 0; i < 256; i++)
		msg_name[i] = "unknown";

	msg_name[GTA_MSG_INIT]				= "ping";
	msg_name[GTA_MSG_INIT_RESPONSE]		= "pong";
	msg_name[GTA_MSG_BYE]				= "bye";
	msg_name[GTA_MSG_SEARCH]			= "query";
	msg_name[GTA_MSG_SEARCH_RESULTS]	= "query hit";
	msg_name[GTA_MSG_PUSH_REQUEST]		= "push";

	for (i = 0; i < 256; i++)
		msg_weight[i] = 0;

	msg_weight[GTA_MSG_INIT]			= 1;
	msg_weight[GTA_MSG_SEARCH]			= 2;
	msg_weight[GTA_MSG_INIT_RESPONSE]	= 3;
	msg_weight[GTA_MSG_SEARCH_RESULTS]	= 4;
	msg_weight[GTA_MSG_PUSH_REQUEST]	= 5;
}

/*
 * gmsg_name
 *
 * Convert message function number into name.
 */
gchar *gmsg_name(gint function)
{
	if (function < 0 || function > 255)
		return "invalid";

	return msg_name[function];
}

/*
 * gmsg_to_pmsg
 *
 * Construct PDU from message.
 */
static pmsg_t *gmsg_to_pmsg(guchar *msg, guint32 size)
{
	return pmsg_new(0, msg, size);
}

/*
 * gmsg_split_to_pmsg
 *
 * Construct PDU from header and data.
 */
static pmsg_t *gmsg_split_to_pmsg(guchar *head, guchar *data, guint32 size)
{
	pmsg_t *mb;
	gint written;

	mb = pmsg_new(0, NULL, size);
	written = pmsg_write(mb, head, sizeof(struct gnutella_header));
	written += pmsg_write(mb, data, size - sizeof(struct gnutella_header));

	g_assert(written == size);

	return mb;
}

/***
 *** Sending of Gnutella messages.
 ***/

/*
 * gmsg_sendto_one
 *
 * Send message to one node.
 */
void gmsg_sendto_one(struct gnutella_node *n, guchar *msg, guint32 size)
{
	mq_putq(n->outq, gmsg_to_pmsg(msg, size));
}

/*
 * gmsg_split_sendto_one
 *
 * Send message consisting of header and data to one node.
 */
void gmsg_split_sendto_one(struct gnutella_node *n,
	guchar *head, guchar *data, guint32 size)
{
	mq_putq(n->outq, gmsg_split_to_pmsg(head, data, size));
}

/*
 * gmsg_sendto_all
 *
 * Broadcast message to all nodes in the list.
 */
void gmsg_sendto_all(GSList *l, guchar *msg, guint32 size)
{
	pmsg_t *mb = gmsg_to_pmsg(msg, size);

	for (/* empty */; l; l = l->next) {
		struct gnutella_node *dn = (struct gnutella_node *) l->data;
		if (!NODE_IS_WRITABLE(dn))
			continue;
		mq_putq(dn->outq, pmsg_clone(mb));
	}

	pmsg_free(mb);
}

/*
 * gmsg_split_sendto_all_but_one
 *
 * Send message consisting of header and data to all nodes in the list
 * but one node.
 */
void gmsg_split_sendto_all_but_one(GSList *l, struct gnutella_node *n,
	guchar *head, guchar *data, guint32 size)
{
	pmsg_t *mb = gmsg_split_to_pmsg(head, data, size);

	for (/* empty */; l; l = l->next) {
		struct gnutella_node *dn = (struct gnutella_node *) l->data;
		if (dn == n)
			continue;
		if (!NODE_IS_WRITABLE(dn))
			continue;
		mq_putq(dn->outq, pmsg_clone(mb));
	}

	pmsg_free(mb);
}

/*
 * gmsg_sendto_route
 *
 * Send message held in current node according to route specification.
 */
void gmsg_sendto_route(struct gnutella_node *n, struct route_dest *rt)
{
	switch (rt->type) {
	case ROUTE_NONE:
		break;
	case ROUTE_ONE:
		gmsg_split_sendto_one(rt->node,
			(guchar *) &n->header, n->data,
			n->size + sizeof(struct gnutella_header));
		break;
	case ROUTE_ALL_BUT_ONE:
		gmsg_split_sendto_all_but_one(sl_nodes, rt->node,
			(guchar *) &n->header, n->data,
			n->size + sizeof(struct gnutella_header));
	}
}

/***
 *** Miscellaneous utilities.
 ***/

/*
 * gmsg_can_drop
 *
 * Test whether the Gnutella message can be safely dropped on the connection.
 * We're given the whole PDU, not just the payload.
 *
 * Dropping of messages only happens when the connection is flow-controlled,
 * and there's not enough room in the queue.
 */
gboolean gmsg_can_drop(gpointer pdu, gint size)
{
	struct gnutella_header *head = (struct gnutella_header *) pdu;

	if (size < sizeof(struct gnutella_header))
		return TRUE;

	switch (head->function) {
	case GTA_MSG_INIT:
	case GTA_MSG_SEARCH:
	case GTA_MSG_INIT_RESPONSE:
		return TRUE;
	default:
		return FALSE;
	}
}

/*
 * gmsg_cmp
 *
 * Perform a priority comparison between two messages, given as the whole PDU.
 *
 * Return algebraic -1/0/+1 depending on relative order.
 */
gint gmsg_cmp(gpointer pdu1, gpointer pdu2)
{
	struct gnutella_header *h1 = (struct gnutella_header *) pdu1;
	struct gnutella_header *h2 = (struct gnutella_header *) pdu2;
	gint w1 = msg_weight[h1->function];
	gint w2 = msg_weight[h2->function];

	/*
	 * The more weight a message type has, the more prioritary it is.
	 */

	if (w1 != w2)
		return w1 < w2 ? -1 : +1;

	/*
	 * Same weight.
	 *
	 * Compare hops.
	 *
	 * For queries: the more hops a message has travelled, the less prioritary
	 * it is.
	 * For replies: the more hops a message has travelled, the more prioritary
	 * it is (to maximize network's usefulness, or it would have just been a
	 * waste of bandwidth).
	 */

	g_assert(h1->function == h2->function);

	if (h1->hops == h2->hops)
		return 0;

	switch (h1->function) {
	case GTA_MSG_INIT:
	case GTA_MSG_SEARCH:
		return h1->hops > h2->hops ? -1 : +1;
	default:
		return h1->hops < h2->hops ? -1 : +1;
	}
}

/*
 * gmsg_infostr
 *
 * Returns formatted static string:
 *
 *     msg_type (payload length) [hops=x, TTL=x]
 */
gchar *gmsg_infostr(gpointer head)
{
	static gchar a[80];
	struct gnutella_header *h = (struct gnutella_header *) head;
	gint size;

	size = *(gint *) h->size;

	g_snprintf(a, sizeof(a), "%s (%d byte%s) [hops=%d, TTL=%d]",
		gmsg_name(h->function), size, size == 1 ? "" : "s", h->hops, h->ttl);

	return a;
}

/*
 * gmsg_infostr2
 *
 * Same as gmsg_infostr(), but different static buffer.
 */
static gchar *gmsg_infostr2(gpointer head)
{
	static gchar a[80];
	struct gnutella_header *h = (struct gnutella_header *) head;
	gint size;

	size = *(gint *) h->size;

	g_snprintf(a, sizeof(a), "%s (%d byte%s) [hops=%d, TTL=%d]",
		gmsg_name(h->function), size, size == 1 ? "" : "s", h->hops, h->ttl);

	return a;
}

/*
 * gmsg_log_dropped
 *
 * Log dropped message, and reason.
 */
void gmsg_log_dropped(gpointer head, gchar *reason, ...)
{
	fputs("DROP ", stdout);
	fputs(gmsg_infostr2(head), stdout);	/* Allows gmsg_infostr() in arglist */

	if (reason) {
		va_list args;
		va_start(args, reason);
		fputs(": ", stdout);
		vprintf(reason, args);
	}

	fputc('\n', stdout);
}

