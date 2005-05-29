/*
 * $Id$
 *
 * This file comes from http://sourceforge.net/projects/tigertree/
 * Inclusion in gtk-gnutella is:
 *
 *   Copyright (c) 2003 - 2004, Jeroen Asselman
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

/* (PD) 2003 The Bitzi Corporation
 *
 * Copyright (C) 2001 Bitzi (aka Bitcollider) Inc. & Gordon Mohr
 * Released into the public domain by same; permission is explicitly
 * granted to copy, modify, and use freely.
 *
 * THE WORK IS PROVIDED "AS IS," AND COMES WITH ABSOLUTELY NO WARRANTY,
 * EXPRESS OR IMPLIED, TO THE EXTENT PERMITTED BY APPLICABLE LAW,
 * INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Please see file COPYING or http://bitzi.com/publicdomain
 * for more info.
 *
 * tigertree.c - Implementation of the TigerTree algorithm
 *
 * Patterned after sha.c by A.M. Kuchling and others.
 *
 * To use:
 *    (1) allocate a TT_CONTEXT in your own code;
 *    (2) tt_init(ttctx);
 *    (3) tt_update(ttctx, buffer, length); as many times as necessary
 *    (4) tt_digest(ttctx,resultptr);
 *
 * NOTE: The TigerTree hash value cannot be calculated using a
 * constant amount of memory; rather, the memory required grows
 * with the (binary log of the) size of input. (Roughly, one more
 * interim value must be remembered for each doubling of the
 * input size.) This code reserves a counter and stack for input
 * up to about 2^72 bytes in length. PASSING IN LONGER INPUT WILL
 * LEAD TO A BUFFER OVERRUN AND UNDEFINED RESULTS. Of course,
 * that would be over 4.7 trillion gigabytes of data, so problems
 * are unlikely in practice anytime soon. :)
 *
 * Requires the tiger() function as defined in the reference
 * implementation provided by the creators of the Tiger
 * algorithm. See
 *
 *    http://www.cs.technion.ac.il/~biham/Reports/Tiger/
 *
 * $Id$
 *
 */

#include "common.h"

RCSID("$Id$");

#include "endian.h"
#include "misc.h"
#include "tigertree.h"
#include "override.h"		/* Must be the last header included */

static inline void
tt_endian(gchar *s)
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	(void) s;
#else
	guint64 *p;

	p = (guint64 *) s;
	
	p[0] = guint64_to_LE(p[0]);
	p[1] = guint64_to_LE(p[1]);
	p[2] = guint64_to_LE(p[2]);
#endif /* G_LITTLE_ENDIAN */
}

/**
 * Initialize the tigertree context
 */
void
tt_init(TT_CONTEXT *ctx)
{
	ctx->count = 0;
	ctx->leaf[0] = 0;	/* flag for leaf  calculation -- never changed */
	ctx->node[0] = 1;	/* flag for inner node calculation -- never changed */
	ctx->block = ctx->leaf + 1;	/* working area for blocks */
	ctx->index = 0;		/* partial block pointer/block length */
	ctx->top = ctx->nodes;
}

static inline void
tt_compose(TT_CONTEXT *ctx)
{
	gchar *node = ctx->top - NODESIZE;
	memmove(ctx->node + 1, node, NODESIZE);	/* copy to scratch area */
	tiger(ctx->node, NODESIZE + 1, (guint64 *) ctx->top); /* combine 2 nodes */

	tt_endian(ctx->top);

	memmove(node, ctx->top, TIGERSIZE);	/* move up result */
	ctx->top -= TIGERSIZE;				/* update top ptr */
}

static inline void
tt_block(TT_CONTEXT *ctx)
{
	gint64 b;

	tiger(ctx->leaf, ctx->index + 1, (guint64 *) ctx->top);

	tt_endian(ctx->top);

	ctx->top += TIGERSIZE;
	++ctx->count;
	b = ctx->count;

	while (!(b & 1)) { /* while evenly divisible by 2... */
		tt_compose(ctx);
		b >>= 1;
	}
}

void
tt_update(TT_CONTEXT *ctx, gchar *buffer, gint32 len)
{
	/* Try to fill partial block */
	if (ctx->index) {
		gint32 left = BLOCKSIZE - ctx->index;

		if (len < left) {
			memmove(ctx->block + ctx->index, buffer, len);
			ctx->index += len;
			return; /* Finished */
		} else {
			memmove(ctx->block + ctx->index, buffer, left);
			ctx->index = BLOCKSIZE;
			tt_block(ctx);
			buffer += left;
			len -= left;
		}
	}

	while (len >= BLOCKSIZE) {
		memmove(ctx->block, buffer, BLOCKSIZE);
		ctx->index = BLOCKSIZE;
		tt_block(ctx);
		buffer += BLOCKSIZE;
		len -= BLOCKSIZE;
	}

	if (0 != (ctx->index = len)) {
		/* Buffer leftovers */
		memmove(ctx->block, buffer, len);
	}
}

/* no need to call this directly; tt_digest calls it for you */
static inline void
tt_final(TT_CONTEXT *ctx)
{
	/*
	 * Do last partial block, unless index is 1 (empty leaf)
  	 * AND we're past the first block
	 */
	if (ctx->index > 0 || ctx->top == ctx->nodes)
		tt_block(ctx);
}

void
tt_digest(TT_CONTEXT *ctx, gchar *s)
{
	tt_final(ctx);

	while ((ctx->top - TIGERSIZE) > ctx->nodes) {
		tt_compose(ctx);
	}

	memmove(s, ctx->nodes, TIGERSIZE);
}

/* this code untested; use at own risk	*/
void
tt_copy(TT_CONTEXT *dest, TT_CONTEXT *src)
{
	dest->count = src->count;
	memcpy(dest->block, src->block, BLOCKSIZE);
	dest->index = src->index;
	memcpy(dest->nodes, src->nodes, STACKSIZE);
	dest->top = src->top;
}

/* vi: set ts=4 sw=4 cindent: */
