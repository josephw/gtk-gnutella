/*
 * $Id$
 *
 * Copyright (c) 2002, Raphael Manfredi
 *
 * Query Routing Protocol (LimeWire's scheme).
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

#ifndef _qrp_h_
#define _qrp_h_

#include <glib.h>

#include "gnutella.h"

typedef void (*qrp_callback_t)(gpointer arg, gboolean cancelled);

guint32 qrp_hash(guchar *x, gint bits);
guint32 qrp_hashcode(guchar *x);
guint32 qrp_hash_restrict(guint32 hashcode, gint bits);

void qrp_init(char_map_t map);
void qrp_close(void);

void qrp_prepare_computation(void);
void qrp_add_file(struct shared_file *sf);
void qrp_finalize_computation(void);

gpointer qrt_update_create(struct gnutella_node *n, gpointer query_table);
void qrt_update_free(gpointer handle);
gboolean qrt_update_send_next(gpointer handle);
gboolean qrt_update_was_ok(gpointer handle);

gpointer qrt_get_table(void);
gpointer qrt_ref(gpointer obj);
void qrt_unref(gpointer obj);

#endif	/* _qrp_h_ */

/* vi: set ts=4: */

