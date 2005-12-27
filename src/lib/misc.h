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

/**
 * @ingroup lib
 * @file
 *
 * Misc functions.
 *
 * This misc.[ch] provides several miscellaneous small routines & macros for:
 *
 * - Array size determination
 * - Flag handling
 * - Sorting constants
 * - Network related string routines
 * - Date string conversions
 * - Time string conversions
 * - Size string conversions
 * - SHA1<->base32 string conversion
 * - Tests
 * - Random numbers
 * - Stuff...
 *
 * @author Raphael Manfredi
 * @date 2001-2003
 */

#ifndef _misc_h_
#define _misc_h_

#include "config.h"

#include <time.h>
#include <stdio.h>
#include <ctype.h>

#include <glib.h>

#define SIZE_FIELD_MAX 64		/**< Max size of sprintf-ed size quantity */
#define GUID_RAW_SIZE	16		/**< Binary representation of 128 bits */
#define GUID_HEX_SIZE	32		/**< Hexadecimal GUID representation */

/**
 * Needs to be defined if we are not using Glib 2
 */
#ifndef USE_GLIB2

#ifndef HAS_STRLCPY
size_t strlcpy(gchar *dst, const gchar *src, size_t dst_size);
#endif /* HAS_STRLCPY */

#ifndef HAS_STRLCAT
size_t strlcat(gchar *dst, const gchar *src, size_t dst_size);
#endif /* HAS_STRLCAT */

#define g_string_printf g_string_sprintf
#define g_strlcpy strlcpy
#define g_strlcat strlcat
#endif

size_t concat_strings(gchar *dst, size_t size,
	const gchar *s, ...) WARN_NEED_SENTINEL;
size_t w_concat_strings(gchar **dst,
	const gchar *first, ...) WARN_NEED_SENTINEL;

/**
 * Cast a ``const gchar *'' to ``gchar *''. This allows the compiler to
 * print a diagnostic message if you accidently try to deconstify an
 * incompatible type. A direct typecast would hide such a mistake.
 */
static inline G_GNUC_CONST WARN_UNUSED_RESULT gchar *
deconstify_gchar(const gchar *p)
{
	return (gchar *) p;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT guint32 *
deconstify_guint32(const guint32 *p)
{
	return (guint32 *) p;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gpointer
deconstify_gpointer(gconstpointer p)
{
	return (gpointer) p;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gconstpointer
cast_to_gconstpointer(gconstpointer p)
{
	return p; 
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gpointer
cast_to_gpointer(gpointer p)
{
	return p;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gchar *
cast_to_gchar_ptr(gpointer p)
{
	return p;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT guchar *
cast_to_guchar_ptr(gpointer p)
{
	return p;
}

typedef void (*func_ptr_t)(void);

static inline G_GNUC_CONST WARN_UNUSED_RESULT gpointer
cast_func_to_gpointer(func_ptr_t f)
{
	union {
		func_ptr_t f;
		gpointer p;
	} u;

	u.f = f;
	return u.p;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT func_ptr_t
cast_gpointer_to_func(gconstpointer p)
{
	union {
		func_ptr_t f;
		gconstpointer p;
	} u;

	u.p = p;
	return u.f;
}

gint ascii_strcasecmp(const gchar *s1, const gchar *s2);
gint ascii_strncasecmp(const gchar *s1, const gchar *s2, size_t len);

/**
 * Converts a hexadecimal char (0-9, A-F, a-f) to an integer.
 *
 * @param c the character to convert.
 * @return 0..15 for valid hexadecimal ASCII characters, -1 otherwise.
 */
static inline gint
hex2int_inline(guchar c)
{
	extern const gint8 *hex2int_tab;
	return hex2int_tab[c];
}

/**
 * Converts a decimal char (0-9) to an integer.
 *
 * @param c the character to convert.
 * @return 0..9 for valid decimal ASCII characters, -1 otherwise.
 */
static inline gint
dec2int_inline(guchar c)
{
	extern const gint8 *dec2int_tab;
	return dec2int_tab[c];
}

/**
 * Converts an alphanumeric char (0-9, A-Z, a-z) to an integer.
 *
 * @param c the character to convert.
 * @return 0..9 for valid alphanumeric ASCII characters, -1 otherwise.
 */
static inline gint
alnum2int_inline(guchar c)
{
	extern const gint8 *alnum2int_tab;
	return alnum2int_tab[c];
}

/**
 * ctype-like functions that allow only ASCII characters whereas the locale
 * would allow others. The parameter doesn't have to be casted to (unsigned
 * char) because these functions return false for everything out of [0..127].
 *
 * GLib 2.x has similar macros/functions but defines only a subset.
 */

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_blank(gint c)
{
	return c == 32 || c == 9;	/* space, tab */
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_cntrl(gint c)
{
	return (c >= 0 && c <= 31) || c == 127;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_digit(gint c)
{
	return c >= 48 && c <= 57;	/* 0-9 */
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_xdigit(gint c)
{
	return -1 != hex2int_inline(c) && !(c & ~0x7f);
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_upper(gint c)
{
	return c >= 65 && c <= 90;		/* A-Z */
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_lower(gint c)
{
	return c >= 97 && c <= 122;		/* a-z */
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_alpha(gint c)
{
	return is_ascii_upper(c) || is_ascii_lower(c);	/* A-Z, a-z */
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_alnum(gint c)
{
	return -1 != alnum2int_inline(c) && !(c & ~0x7f); /* A-Z, a-z, 0-9 */
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_space(gint c)
{
	return c == 32 || (c >= 9 && c <= 13);
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_graph(gint c)
{
	return c >= 33 && c <= 126;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_print(gint c)
{
	return is_ascii_graph(c) || c == 32;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gboolean
is_ascii_punct(gint c)
{
	return c >= 33 && c <= 126 && !is_ascii_alnum(c);
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gint
ascii_toupper(gint c)
{
	return is_ascii_lower(c) ? c - 32 : c;
}

static inline G_GNUC_CONST WARN_UNUSED_RESULT gint
ascii_tolower(gint c)
{
	return is_ascii_upper(c) ? c + 32 : c;
}

#if !GLIB_CHECK_VERSION(2,4,0)
static inline WARN_UNUSED_RESULT const gchar *
g_strip_context(const gchar *id, const gchar *val)
{
	const gchar *s;

	s = id != val ? NULL : strchr(id, '|');
	return s ? ++s : val;
}
#endif /* GLib < 2.4.0 */

/**
 * Skips over all ASCII space characters starting at ``s''.
 *
 * @return a pointer to the first non-space character starting from s.
 */
static inline WARN_UNUSED_RESULT gchar *
skip_ascii_spaces(const gchar *s)
{
	while (is_ascii_space(*s))
		s++;

	return deconstify_gchar(s);
}

/**
 * Skips over all ASCII blank characters starting at ``s''.
 *
 * @return a pointer to the first non-blank character starting from s.
 */
static inline WARN_UNUSED_RESULT gchar *
skip_ascii_blanks(const gchar *s)
{
	while (is_ascii_blank(*s))
		s++;

	return deconstify_gchar(s);
}

/*
 * Determine the length of string literals
 */
#define CONST_STRLEN(x) (sizeof(x) - 1)

/*
 * Array size determination
 */
#ifndef G_N_ELEMENTS
#define G_N_ELEMENTS(arr) (sizeof (arr) / sizeof ((arr)[0]))
#endif

/*
 * Set/clear binary flags
 */
typedef guint16 flag_t;
#define set_flags(r,f) (r |= (f))
#define clear_flags(r,f) (r &= ~(f))

/*
 * Macros to determine the maximum buffer size required to hold a
 * NUL-terminated string.
 */
#define UINT8_HEX_BUFLEN	(sizeof "FF")
#define UINT8_DEC_BUFLEN	(sizeof "255")
#define UINT16_HEX_BUFLEN	(sizeof "01234")
#define UINT16_DEC_BUFLEN	(sizeof "65535")
#define UINT32_HEX_BUFLEN	(sizeof "012345678")
#define UINT32_DEC_BUFLEN	(sizeof "4294967295")
#define UINT64_HEX_BUFLEN	(sizeof "0123456789ABCDEF")
#define UINT64_DEC_BUFLEN	(sizeof "18446744073709551615")
#define IPV4_ADDR_BUFLEN	(sizeof "255.255.255.255")
#define IPV6_ADDR_BUFLEN \
	  (sizeof "0001:0203:0405:0607:0809:1011:255.255.255.255")
#define TIMESTAMP_BUF_LEN	(sizeof "9999-12-31 23:59:61")

gboolean parse_ipv6_addr(const gchar *s, uint8_t *dst, const gchar **endptr);
const gchar *ipv6_to_string(const guint8 *ipv6);
size_t ipv6_to_string_buf(const guint8 *ipv6, gchar *dst, size_t size);

/*
 * Network related string routines
 */
guint32  string_to_ip(const gchar *);
gboolean string_to_ip_strict(const gchar *s, guint32 *addr, const gchar **ep);
gboolean string_to_ip_and_mask(const gchar *str, guint32 *ip, guint32 *netmask);
gboolean string_to_ip_port(const gchar *str, guint32 *ip, guint16 *port);
const gchar *ip_to_string(guint32);
const gchar *ip_to_string2(guint32);
size_t ip_to_string_buf(guint32 ip, gchar *buf, size_t size);
const gchar *ip_port_to_string(guint32, guint16);
const gchar *hostname_port_to_string(const gchar *hostname, guint16 port);
const gchar *local_hostname(void);
#define port_is_valid(port) (port != 0)

/*
 * Date string conversions
 */
const gchar *timestamp_to_string(time_t date);
const gchar *timestamp_utc_to_string(time_t date);
const gchar *timestamp_rfc822_to_string(time_t date);
const gchar *timestamp_rfc822_to_string2(time_t date);
const gchar *timestamp_rfc1123_to_string(time_t date);

size_t timestamp_to_string_buf(time_t date, gchar *dst, size_t size);
size_t time_locale_to_string_buf(time_t date, gchar *dst, size_t size);
/*
 * Time string conversions
 */
gchar *short_time(gint s);
gchar *short_uptime(gint s);

/* Use the direct difference instead of difftime() for systems that are
 * known to use a flat time_t encoding. */
#if !defined(LINUX_SYSTEM) && \
	!defined(__FreeBSD__) && \
	!defined(__NetBSD__) && \
	!defined(__OpenBSD__)
#define USE_DIFFTIME
#endif

/* Use a macro so that's possible to not use difftime where it's not
 * necessary because time_t is flat encoded */
#if defined(USE_DIFFTIME)
#define delta_time(a, b) ((gint64) difftime((a), (b)))
#else
#define delta_time(a, b) ((gint64) ((a) - (b)))
#endif

/*
 * Size string conversions
 */
const gchar *short_size(guint64 size);
const gchar *short_html_size(guint64 size);
const gchar *short_kb_size(guint64 size);
const gchar *short_rate(guint64 rate);
const gchar *compact_size(guint64 size);
const gchar *compact_rate(guint64 rate);
const gchar *compact_kb_size(guint32 size);
gchar *short_value(gchar *buf, size_t size, guint64 v);
gchar *compact_value(gchar *buf, size_t size, guint64 v);

/*
 * SHA1<->base32 string conversion
 */
gchar *sha1_base32(const gchar *sha1);
gchar *base32_sha1(const gchar *base32);

/*
 * GUID<->hex string conversion
 */
gchar *guid_hex_str(const gchar *guid);
gboolean hex_to_guid(const gchar *hexguid, gchar *guid);

/*
 * GUID<->base32 string conversion
 */
gchar *guid_base32_str(const gchar *guid);
gchar *base32_to_guid(const gchar *base32);

/*
 * Tests
 */
gboolean is_absolute_path(const char *);
gboolean is_directory(const gchar *);
gboolean is_regular(const gchar *);
gboolean is_symlink(const gchar *);
gboolean file_exists(const gchar *);
guint32 next_pow2(guint32 n);

/**
 * Checks whether the given value is a power of 2.
 *
 * @param value a 32-bit integer
 * @return TRUE if ``value'' is a power of 2. Otherwise FALSE.
 */
static inline G_GNUC_CONST gboolean
is_pow2(guint32 value)
#ifdef HAVE_BUILTIN_POPCOUNT
{
	return 0 == __builtin_popcount(value);
}
#else /* !HAVE_BUILTIN_POPCOUNT */
{
	return value && !(value & (value - 1));
}
#endif /* HAVE_BUILTIN_POPCOUNT */

/*
 * Random numbers
 */
void random_init(void);
guint32 random_value(guint32 max) WARN_UNUSED_RESULT;
void guid_random_fill(gchar *xuid);

/*
 * Stuff
 */
void misc_init(void);
gint str_chomp(gchar *str, gint len);
gint hex2int(guchar c);
gboolean is_printable(const gchar *buf, gint len);
void dump_hex(FILE *, const gchar *, gconstpointer, gint);
void locale_strlower(gchar *, const gchar *);
void ascii_strlower(gchar *dst, const gchar *src);
gint strcmp_delimit(const gchar *a, const gchar *b, const gchar *delimit);
gint strcasecmp_delimit(const gchar *a, const gchar *b, const gchar *delimit);
char *unique_filename(const gchar *path, const gchar *file, const gchar *ext);
gchar *hex_escape(const gchar *name, gboolean strict);
gchar *control_escape(const gchar *s);
const gchar *lazy_string_to_printf_escape(const gchar *src);
gint highest_bit_set(guint32 n) G_GNUC_CONST;
gfloat force_range(gfloat value, gfloat min, gfloat max);
gchar *make_pathname(const gchar *dir, const gchar *file);
gchar *short_filename(gchar *fullname);
gchar *data_hex_str(const gchar *data, size_t len);
gint create_directory(const gchar *dir);
gboolean filepath_exists(const gchar *dir, const gchar *file);
guint16 parse_uint16(const gchar *, gchar const **, guint, gint *)
	NON_NULL_PARAM((1, 4));
guint32 parse_uint32(const gchar *, gchar const **, guint, gint *)
	NON_NULL_PARAM((1, 4));
guint64 parse_uint64(const gchar *, gchar const **, guint, gint *)
	NON_NULL_PARAM((1, 4));
size_t uint32_to_string_buf(guint64 v, gchar *dst, size_t size);
size_t uint64_to_string_buf(guint64 v, gchar *dst, size_t size);
const gchar *uint32_to_string(guint32 v);
const gchar *uint64_to_string(guint64 v);
const gchar *uint64_to_string2(guint64 v);
gint parse_major_minor(const gchar *src, gchar const **endptr,
	guint *major, guint *minor);
gchar *is_strprefix(const gchar *s, const gchar *prefix) WARN_UNUSED_RESULT;
gchar *is_strcaseprefix(const gchar *s, const gchar *prefix) WARN_UNUSED_RESULT;
size_t html_escape(const gchar *src, gchar *dst, size_t dst_size);
gint canonize_path(gchar *dst, const gchar *path);
guint compat_max_fd(void);
gint compat_mkdir(const gchar *path, mode_t mode);
size_t compat_pagesize(void);
gpointer compat_page_align(size_t size);
void compat_page_free(gpointer p, size_t size);
gboolean compat_is_superuser(void);

typedef void (*signal_handler_t)(gint signo);
signal_handler_t set_signal(gint signo, signal_handler_t handler);

gchar *ascii_strcasestr(const gchar *haystack, const gchar *needle);

#define NULL_STRING(s) (NULL != (s) ? (s) : "(null)")
#define EMPTY_STRING(s) (NULL != (s) ? (s) : "")

/**
 * Swap endianness of a guint32.
 *
 * @param i the guint32 to swap
 *
 * @returns the value of i after swapping its byte order.
 */
static inline G_GNUC_CONST guint32
swap_guint32(guint32 i)
{
	gint a = i & 0x000000ff;
	gint b = (i & 0x0000ff00) >> 8;
	gint c = (i & 0x00ff0000) >> 16;
	gint d = (i & 0xff000000) >> 24;

	return d + (c << 8) + (b << 16) + (a << 24);
}

/**
 * Converts the given IPv4 netmask in host byte order to a CIDR prefix length.
 * No checks are performed whether the netmask is proper and if it's not
 * the result is unspecified.
 *
 * @param netmask an IPv4 netmask in host byte order.
 * @return The CIDR prefix length (0..32).
 */
static inline G_GNUC_CONST WARN_UNUSED_RESULT guint8
netmask_to_cidr(guint32 netmask)
#ifdef HAVE_BUILTIN_POPCOUNT
{
	__builtin_popcount(netmask);
}
#else	/* HAVE_BUILTIN_POPCOUNT */
{
	guint8 bits = 32;

	while (0 == (netmask & 0x1)) {
		netmask >>= 1;
		bits--;
	}
	return bits;
}
#endif /* HAVE_BUILTIN_POPCOUNT */

/**
 * Rounds ``n'' up so that it matches the given alignment ``align''.
 */
static inline size_t
round_size(size_t align, size_t n)
{
	size_t m = n % align;
	return m ? n + (align - m) : MAX(n, align);
}

/*
 * Syscall wrappers for errno == 0 bug. --RAM, 27/10/2003
 */

struct stat;

extern gint do_errno;

gint do_stat(const gchar *path, struct stat *buf);

/*
 * CIDR split of IP range.
 */

typedef void (*cidr_split_t)(guint32 ip, guint bits, gpointer udata);

void ip_range_split(
	guint32 lower_ip, guint32 upper_ip, cidr_split_t cb, gpointer udata);

/**
 * Perform a binary search over an array.
 *
 * bs_type is the type of bs_item
 * bs_key is the key to lookup
 * bs_size is the array length
 * bs_cmp(bs_item, bs_key) is used to compare the key with the current item
 * bs_get_key(bs_index) must return the key at bs_index
 * bs_found(bs_index) is executed if bs_key is found
 *
 * All local variables are prefixed with bs_ to prevent clashes with
 * other visible variables.
 */
#define BINARY_SEARCH(bs_type, bs_key, bs_size, bs_cmp, bs_get_key, bs_found) \
G_STMT_START { \
	size_t bs_index, bs_j = 0, bs_k; \
	for (bs_k = (bs_size); bs_k != 0; bs_k >>= 1) { \
		bs_type bs_item; \
		gint bs_cmp_result; \
\
		bs_index = bs_j + (bs_k >> 1); \
		bs_item = bs_get_key(bs_index); \
		bs_cmp_result = bs_cmp(bs_item, bs_key); \
		if (0 == bs_cmp_result) {	\
			bs_found(bs_index); \
			break; \
		} else if (bs_cmp_result < 0) { \
			bs_j = bs_index + 1; \
			bs_k--; \
		} \
	} \
} G_STMT_END

/**
 * Ensure a table used for binary search is sorted.
 *
 * bs_array is the (static) array to scan.
 * bs_type is the type of bs_item
 * bs_field is the field in the bs_item structure to compare.
 * bs_cmp() is the comparison function to use between items
 * bs_field2str is how one can stringify the bs_field.
 *
 * Skip the first to have a previous element, tables with a single
 * element are sorted anyway.
 */
#define BINARY_ARRAY_SORTED(bs_array, bs_type, bs_field, bs_cmp, bs_field2str) \
G_STMT_START { \
	size_t bs_index; \
	size_t bs_size = G_N_ELEMENTS(bs_array); \
\
	for (bs_index = 1; bs_index < bs_size; bs_index++) { \
		const bs_type *prev = &bs_array[bs_index - 1]; \
		const bs_type *e = &bs_array[bs_index]; \
\
		if (bs_cmp(prev->bs_field, e->bs_field) >= 0) \
			g_error(STRINGIFY(bs_array) "[] unsorted (near item \"%s\")", \
				bs_field2str(e->bs_field)); \
	} \
} G_STMT_END

#endif /* _misc_h_ */

/* vi: set ts=4 sw=4 cindent: */
