/*
 * $Id$
 *
 * Copyright (c) 2001-2002, Raphael Manfredi, Richard Eckart
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

#ifndef _filter_h_
#define _filter_h_

#include "gui.h"


/*
 * Needed stuff from search.h
 */
struct record;
struct search;

typedef struct filter {
    gchar *name;
    GList *ruleset;
    struct search *search;
    gboolean visited;
    gint32 refcount;
    flag_t flags;
    guint32 match_count;
    guint32 fail_count;
} filter_t;



#define RULE_FLAG_NEGATE (1 << 0)
#define RULE_FLAG_VALID  (1 << 1) /* rule has valid target */
#define RULE_FLAG_ACTIVE (1 << 2)
#define RULE_FLAG_SOFT   (1 << 3)
#define RULE_FLAG_SHADOW (1 << 4) /* rule is not yet committed */

#define FILTER_FLAG_ACTIVE (1 << 0)
#define FILTER_FLAG_SHADOW (1 << 4) /* filter is not yet committed */

#define RULE_IS_VALID(r) ((r != NULL) && (r->flags & RULE_FLAG_VALID))
#define RULE_IS_NEGATED(r) ((r != NULL) && (r->flags & RULE_FLAG_NEGATE))
#define RULE_IS_ACTIVE(r) ((r != NULL) && (r->flags & RULE_FLAG_ACTIVE))
#define RULE_IS_SOFT(r) ((r != NULL) && (r->flags & RULE_FLAG_SOFT))
#define RULE_IS_SHADOWED(r) ((r != NULL) && (r->flags & RULE_FLAG_SHADOW))

#define filter_is_active(f) ((f != NULL) && (f->flags & FILTER_FLAG_ACTIVE))
#define filter_is_bound(f) (f->search != NULL)
#define filter_is_shadowed(f) ((f != NULL) && (f->flags & FILTER_FLAG_SHADOW))


/*
 * Some error codes (0 means 'no error')
 */
#define FILTER_EXISTS 1

extern const gchar * const filter_prop_names[];
extern const gchar * const filter_prop_state_names[];

/*
 * The following struct is used to hold the state information of filter
 * properties. A rule can set one or more of those properties to define
 * how the record should be processed (displayed, downloaded, etc).
 */
typedef struct filter_property {
    filter_prop_state_t state;
    gpointer user_data;
} filter_property_t;

/*
 * This is used to hold the result of a filter. The props_set attribute
 * holds the number of properties which have a state != UNKNOWN and the
 * prop array holds the actual property informations.
 */
typedef struct filter_result {
    gint props_set;
    filter_property_t props[MAX_FILTER_PROP];
} filter_result_t;



/* 
 * Definition of a filter rule
 */
typedef struct rule {
    enum rule_type type;	            /* type of rule, see above */
    flag_t flags;
    guint32 match_count;
    guint32 fail_count;
    filter_t *target;
    union {
        struct _f_text {
            int case_sensitive:1;	    /* case sensitive (true) or not (false) */
            enum rule_text_type type; /* type of match, see above */
            char *match; 	            /* match string */
            union {
                cpattern_t *pattern;	/* substring pattern */
                GList *words;		    /* a list of substring patterns */
                regex_t *re;		    /* regular expression match */
            } u;
        } text;
        struct _f_ip {
            guint32 addr;		        /* IP address */
            guint32 mask;		        /* netmask */
        } ip;
        struct _f_size {
            size_t lower;		        /* lower limit or 0 */
            size_t upper;		        /* upper limit or ~0 */
        } size;
        struct _f_sha1 {
            gchar *hash;                /* sha1 hash */
            gchar *filename;            /* filename sha1 comes from */
        } sha1;
        struct _f_flag {
            enum rule_flag_action busy;
            enum rule_flag_action stable;
            enum rule_flag_action push;
        } flag;
        struct _f_state {
            enum filter_prop_state display;
            enum filter_prop_state download;
        } state;
    } u;
} rule_t;



/*
 * Public variables.
 */
extern filter_t *work_filter;



/*
 * Public interface.
 */
filter_t *filter_new(gchar *);
filter_result_t *filter_record(struct search *, struct record *);
gchar *filter_rule_condition_to_gchar(const rule_t *r);
gchar *filter_rule_to_gchar(rule_t *f);
inline gboolean filter_is_builtin(filter_t *f);
inline gboolean filter_is_global(filter_t *f);
inline void filter_reset_stats(filter_t *filter);
inline void filter_rule_reset_stats(rule_t *rule);
rule_t *filter_duplicate_rule(rule_t *rule);
rule_t *filter_new_ip_rule(guint32, guint32, filter_t *, guint16);
rule_t *filter_new_jump_rule(filter_t *,guint16);
rule_t *filter_new_size_rule(size_t, size_t, filter_t *, guint16);
rule_t *filter_new_text_rule(gchar *, gint, gboolean, filter_t *, guint16);
rule_t *filter_new_sha1_rule(guchar *, gchar *, filter_t *, guint16);
rule_t *filter_new_flag_rule
    (enum rule_flag_action stable, enum rule_flag_action busy, 
    enum rule_flag_action push, filter_t *target, guint16 flags);
rule_t *filter_new_state_rule
    (enum filter_prop_state display, enum filter_prop_state download,
    filter_t *target, guint16 flags);
void filter_adapt_order(void);
void filter_append_rule(filter_t *f, rule_t * const r);
void filter_append_rule_to_session(filter_t * f, rule_t * const r);
void filter_revert_changes();
void filter_apply_changes();
void filter_close_dialog(gboolean);
void filter_close_search(struct search *);
void filter_apply_changes();
void filter_add_to_session(filter_t *f);
void filter_remove_from_session(filter_t *f);
void filter_init(void);
void filter_new_for_search(struct search *s);
void filter_open_dialog();
void filter_remove_rule_from_session(filter_t *, rule_t * const);
void filter_replace_rule_in_session(filter_t *, rule_t * const, rule_t * const);
void filter_set(filter_t *);
void filter_set_enabled(filter_t *filter, gboolean active);
void filter_shutdown(void);
void filter_timer(void);
void filter_update_targets(void);
void filter_free_result(filter_result_t *);
void filter_free_rule(rule_t *rule);
filter_t *filter_find_by_name_in_session(gchar *name);
gboolean filter_is_valid_in_session(filter_t *f);
inline filter_t *filter_get_drop_target(void);
inline filter_t *filter_get_show_target(void);
inline filter_t *filter_get_download_target(void);
inline filter_t *filter_get_nodownload_target(void);
inline filter_t *filter_get_return_target(void);
inline filter_t *filter_get_global_pre(void);
inline filter_t *filter_get_global_post(void);
#endif /* _filter_h_ */
