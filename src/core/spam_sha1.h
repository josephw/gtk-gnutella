/*
 * Copyright (c) 2007, Christian Biere
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
 * SHA-1 based spam filtering.
 *
 * @author Christian Biere
 * @date 2007
 */

#ifndef _core_spam_sha1_h_
#define _core_spam_sha1_h_

#include "common.h"

struct sha1;

bool spam_sha1_check(const struct sha1 *sha1);
void spam_sha1_add(const struct sha1 *sha1);
void spam_sha1_sync(void);
void spam_sha1_init(void);
void spam_sha1_close(void);

#endif /* _core_spam_sha1_h_ */

/* vi: set ts=4 sw=4 cindent: */
