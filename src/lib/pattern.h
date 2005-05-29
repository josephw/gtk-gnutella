/*
 * $Id$
 *
 * Copyright (c) 2001-2003, Raphael Manfredi
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

#ifndef _pattern_h_
#define _pattern_h_

/**
 * @ingroup lib
 * @file
 *
 * Sunday pattern search data structures.
 */

#define ALPHA_SIZE	256			/* Alphabet size */

typedef struct {				/* Compiled pattern */
	gchar *pattern;				/* The pattern */
	guint32 len;				/* Pattern length */
	gboolean duped;				/* Was `pattern' strdup()'ed? */
	guint32 delta[ALPHA_SIZE];	/* Shifting deltas */
} cpattern_t;

typedef enum {
	qs_any = 0,					/* Match anywhere */
	qs_begin,					/* Match beginning of words */
	qs_whole					/* Match whole words only */
} qsearch_mode_t;

void pattern_init(void);
void pattern_close(void);

cpattern_t *pattern_compile(gchar *pattern);
cpattern_t *pattern_compile_fast(gchar *pattern, guint32 plen);
void pattern_free(cpattern_t *cpat);
gchar *pattern_qsearch(cpattern_t *cpat,
	gchar *text, guint32 tlen, guint32 toffset, qsearch_mode_t word);

#endif /* _pattern_h_ */

/* vi: set ts=4: */
