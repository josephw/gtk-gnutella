/*
 * $Id$
 *
 * Copyright (c) 2010, Raphael Manfredi
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
 * Versatile XML processing.
 *
 * @author Raphael Manfredi
 * @date 2010
 */

#include "common.h"

RCSID("$Id$")

#include "vxml.h"
#include "atoms.h"
#include "ascii.h"
#include "endian.h"
#include "glib-missing.h"
#include "misc.h"
#include "halloc.h"
#include "nv.h"
#include "parse.h"
#include "symtab.h"
#include "unsigned.h"
#include "utf8.h"
#include "walloc.h"
#include "override.h"	/* Must be the last header included */

/*
 * Define to have XML parsing testing at startup.
 */
#if 1
#define VXML_TESTING
#endif

#define VXML_OUT_SIZE	1024	/**< Initial output buffer size */
#define VXML_LOOKAHEAD	4		/**< Look-ahead buffer size */

struct vxml_buffer;

/**
 * Signature of routine used to grab the next input character from an
 * input buffer.
 *
 * @param str		the string buffer from which we're reading
 * @param len		the length of the string
 * @param retlen	where the byte length of the returned character is written
 */
typedef guint32 (*vxml_reader_t)(const char *str, size_t len, guint *retlen);

enum vxml_buffer_magic { VXML_BUFFER_MAGIC = 0x55203c2aU };

/**
 * An input buffer being parsed.
 *
 * Each input buffer is assumed to contain characters in a given consistent
 * encoding, with no truncation.  For instance, when the encoding is UTF-16,
 * then the length of the buffer MUST be a multiple of two bytes.
 */
struct vxml_buffer {
	enum vxml_buffer_magic magic;
	char *data;					/**< Start of buffer data */
	const char *vb_rptr;		/**< Reading pointer in buffer */
	const char *vb_end;			/**< First character beyond buffer */
	size_t length;				/**< Length of buffer data */
	vxml_reader_t reader;		/**< How to get next Unicode character */
	unsigned generation;		/**< Generation number */
	unsigned allocated:1;		/**< Set when data was internally allocated */
	unsigned user:1;			/**< Set when data come from user-land */
	unsigned malloced:1;		/**< Set when allocated with g_malloc() */
	unsigned utf8:1;			/**< Forcefully converted to UTF-8 */
};

static inline void
vxml_buffer_check(const struct vxml_buffer * const vb)
{
	g_assert(vb != NULL);
	g_assert(VXML_BUFFER_MAGIC == vb->magic);
}

/**
 * Supported character encodings.
 */
enum vxml_encoding {
	VXML_ENC_UTF8,
	VXML_ENC_UTF16_BE,
	VXML_ENC_UTF16_LE,
	VXML_ENC_UTF32_BE,
	VXML_ENC_UTF32_LE,
	VXML_ENC_CHARSET
};

/**
 * How character encoding was determined.
 */
enum vxml_encsrc {
	VXML_ENCSRC_DEFAULT,			/**< Default encoding rules (UTF-8 then) */
	VXML_ENCSRC_INTUITED,			/**< Intuited from document start */
	VXML_ENCSRC_EXPLICIT,			/**< Explicit character encoding */
	VXML_ENCSRC_SUPPLIED			/**< User-supplied character encoding */
};

/**
 * How byte-order was determined.
 */
enum vxml_endsrc {
	VXML_ENDIANSRC_DEFAULT,			/**< Default endianness (big-endian then) */
	VXML_ENDIANSRC_INTUITED,		/**< Intuited from document start */
	VXML_ENDIANSRC_EXPLICIT,		/**< Uses 8-bit characters or UTF-8 */
	VXML_ENDIANSRC_SUPPLIED			/**< User-supplied endianness */
};

/**
 * How version was determined.
 */
enum vxml_versionsrc {
	VXML_VERSRC_DEFAULT,			/**< Default version (1.0 then) */
	VXML_VERSRC_IMPLIED,			/**< Implied: no explicit declaration */
	VXML_VERSRC_EXPLICIT			/**< Explicit version declared */
};

/**
 * Parser user-supplied context.
 */
struct vxml_uctx {
	const struct vxml_ops *ops;	/**< Callbacks to invoke on elements and text */
	nv_table_t *tokens;			/**< Additional tokens for elements */
	void *data;					/**< Additional argument for callbacks */
};

enum vxml_output_magic { VXML_OUTPUT_MAGIC = 0x7e43f7a7U };

/**
 * Output buffer: data stored there is in UTF-8.
 */
struct vxml_output {
	enum vxml_output_magic magic;
	char *data;					/**< Output buffer (resizeable) */
	char *vo_wptr;				/**< Writing pointer into buffer */
	char *vo_end;				/**< Current end of buffer */
	size_t size;				/**< Buffer size */
};

static inline void
vxml_output_check(const struct vxml_output * const vo)
{
	g_assert(vo != NULL);
	g_assert(VXML_OUTPUT_MAGIC == vo->magic);
}

enum vxml_location_magic { VXML_LOCATION_MAGIC = 0x24b3365bU };

/**
 * Location context.
 */
struct vxml_location {
	enum vxml_location_magic magic;
	size_t offset;					/**< Parsing offset, relative to input */
	size_t line;					/**< Current input line, for errors */
	unsigned depth;					/**< Parsing depth */
	unsigned tag_start;				/**< # of content-holding tags started */
};

static inline void
vxml_location_check(const struct vxml_location * const vl)
{
	g_assert(vl != NULL);
	g_assert(VXML_LOCATION_MAGIC == vl->magic);
}

enum vxml_path_entry_magic { VXML_PATH_ENTRY_MAGIC = 0x3f4d2959U };

/**
 * A path entry.
 */
struct vxml_path_entry {
	enum vxml_path_entry_magic magic;
	const char *element;			/**< Element name (atom) */
	const char *namespace;			/**< Element namespace (atom or NULL) */
	unsigned token;					/**< Tokenized value for element */
	unsigned children;				/**< Amount of children elements seen */
	unsigned token_valid:1;			/**< Whether token is valid */
};

static inline void
vxml_path_entry_check(const struct vxml_path_entry * const pe)
{
	g_assert(pe != NULL);
	g_assert(VXML_PATH_ENTRY_MAGIC == pe->magic);
}

enum vxml_parser_magic { VXML_PARSER_MAGIC = 0x718b5b1bU };

/**
 * A versatile XML parser.
 */
struct vxml_parser {
	enum vxml_parser_magic magic;
	const char *name;				/**< Parser name (static string) */
	const char *charset;			/**< Document's charset (atom) */
	GSList *input;					/**< List of input buffers to parse */
	GList *path;					/**< Path (list of vxml_path_entry) */
	nv_table_t *tokens;				/**< For element tokenization */
	nv_table_t *entities;			/**< Entities defined in document */
	nv_table_t *pe_entities;		/**< Entities defined in <!DOCTYPE...> */
	nv_table_t *attrs;				/**< Current element attributes */
	nv_table_t *ns;					/**< Current element namespace decl. */
	symtab_t *namespaces;			/**< Symbol table for namespaces */
	const char *element;			/**< Current element (atom, NULL if none) */
	const char *namespace;			/**< Element's namesapace (atom, or NULL) */
	char *user_error;				/**< User-defined error string */
	struct vxml_output out;			/**< Output parsing buffer (UTF-8) */
	struct vxml_output entity;		/**< Entity parsing buffer (UTF-8) */
	guint32 unread[VXML_LOOKAHEAD];	/**< Unread character stack */
	size_t unread_offset;			/**< Current offset in unread[] */
	enum vxml_encoding encoding;	/**< Character encoding */
	enum vxml_encsrc encsource;		/**< How we determined character encoding */
	enum vxml_endsrc endianness;	/**< How we determined endianness */
	enum vxml_versionsrc versource;	/**< How we determined the version */
	vxml_error_t error;				/**< Parsing error code (0 means OK) */
	unsigned generation;			/**< Generation number for buffers */
	unsigned elem_token;			/**< Current element token */
	unsigned tags;					/**< Counts all < elements (and <?, <!) */
	unsigned ignores;				/**< Counts nested ignored DTD sections */
	unsigned last_uc_generation;	/**< Generation # of last character */
	guint32 last_uc;				/**< Last read character */
	guint32 flags;					/**< Parsing flags */
	guint32 options;				/**< Parsing options */
	guint8 major;					/**< XML version (major) */
	guint8 minor;					/**< XML version (minor) */
	unsigned elem_token_valid:1;	/**< Field "elem_token" is valid */
	unsigned elem_no_content:1;		/**< Element has no content (no end tag) */
	unsigned standalone:1;			/**< Is document standalone? */
	struct vxml_location glob;		/**< Global location in parser */
	struct vxml_location loc;		/**< Current location in parser */
};

static inline void
vxml_parser_check(const struct vxml_parser * const vp)
{
	g_assert(vp != NULL);
	g_assert(VXML_PARSER_MAGIC == vp->magic);
}

/**
 * Parsing flags.
 */
#define VXML_F_INTUITED		(1 << 0)	/**< Initial encoding intuition done */
#define VXML_F_FATAL_ERROR	(1 << 1)	/**< Fatal error condition */
#define VXML_F_EMPTY_TAG	(1 << 2)	/**< Last tag had no content */
#define VXML_F_XML_DECL		(1 << 3)	/**< Set when "<?xml... ?>" was seen */
#define VXML_F_SUBPARSE		(1 << 4)	/**< Can issue sub-parsing */

static guint32 vxml_debug;

/**
 * Important character constants
 */
#define VXC_NUL		0x00U	/* NUL */
#define VXC_HT		0x09U	/* HT */
#define VXC_LF		0x0AU	/* LF */
#define VXC_CR		0x0DU	/* CR */
#define VXC_SP		0x20U	/* ' ' */
#define VXC_BANG	0x21U	/* '!' */
#define VXC_QUOT	0x22U	/* '"' */
#define VXC_PCT		0x25U	/* '%' */
#define VXC_AMP		0x26U	/* '&' */
#define VXC_APOS	0x27U	/* "'" */
#define VXC_MINUS	0x2DU	/* '-' */
#define VXC_SLASH	0x2FU	/* '/' */
#define VXC_COLON	0x3AU	/* ':' */
#define VXC_SC		0x3BU	/* ';' */
#define VXC_LT		0x3CU	/* '<' */
#define VXC_EQ		0x3DU	/* '=' */
#define VXC_GT		0x3EU	/* '>' */
#define VXC_QM		0x3FU	/* '?' */
#define VXC_LBRAK	0x5BU	/* '[' */
#define VXC_RBRAK	0x5DU	/* ']' */

/**
 * Important string constants.
 */

static const char VXS_EMPTY[]		= "";
static const char VXS_XMLNS[]		= "xmlns";
static const char VXS_DEFAULT_NS[]	= ":DEFAULT";
static const char VXS_XML[]			= "xml";
static const char VXS_XML_URI[]		= "http://www.w3.org/XML/1998/namespace";

/**
 * Default entities.
 */
static struct vxml_parser_token vxml_default_entities[] = {
	/* Sorted array */
	{ "amp",	VXC_AMP },	/* '&' */
	{ "apos",	VXC_APOS },	/* "'" */
	{ "gt",		VXC_GT },	/* '>' */
	{ "lt",		VXC_LT },	/* '<' */
	{ "quot",	VXC_QUOT },	/* '"' */
};

/**
 * All known parser tokens.
 */
enum vxml_parser_token_value {
	VXT_UNKNOWN = 0,		/* Not a token, signals "not found" */
	/* Sorted only for ease of maintenance */
	VXT_ANY,
	VXT_ATTLIST,
	VXT_CDATA,
	VXT_DOCTYPE,
	VXT_ELEMENT,
	VXT_EMPTY,
	VXT_ENTITY,
	VXT_FIXED,
	VXT_IGNORE,
	VXT_IMPLIED,
	VXT_INCLUDE,
	VXT_NDATA,
	VXT_PCDATA,
	VXT_PUBLIC,
	VXT_REQUIRED,
	VXT_SYSTEM,
	VXT_MAX_TOKEN			/* Not a token, marks end of enumeration */
};

/**
 * Declaration tokens.
 */
static struct vxml_parser_token vxml_declaration_tokens[] = {
	/* Sorted array */
	{ "ATTLIST",	VXT_ATTLIST },
	{ "DOCTYPE",	VXT_DOCTYPE },
	{ "ELEMENT",	VXT_ELEMENT },
	{ "ENTITY",		VXT_ENTITY },
};

/**
 * Miscellaneous tokens.
 */
static struct vxml_parser_token vxml_misc_tokens[] = {
	/* Sorted array */
	{ "ANY",		VXT_ANY },
	{ "CDATA",		VXT_CDATA },
	{ "EMPTY",		VXT_EMPTY },
	{ "IGNORE",		VXT_IGNORE },
	{ "INCLUDE",	VXT_INCLUDE },
	{ "NDATA",		VXT_NDATA },
	{ "PUBLIC",		VXT_PUBLIC },
	{ "SYSTEM",		VXT_SYSTEM },
};

/**
 * Immediate tokens (introduced by a leading '#' character).
 */
static struct vxml_parser_token vxml_immediate_tokens[] = {
	/* Sorted array */
	{ "FIXED",		VXT_FIXED },
	{ "IMPLIED",	VXT_IMPLIED },
	{ "PCDATA",		VXT_PCDATA },
	{ "REQUIRED",	VXT_REQUIRED },
};

static const char *vxml_token_strings[VXT_MAX_TOKEN];

static gboolean vxml_handle_decl(vxml_parser_t *vp, gboolean doctype);
static gboolean vxml_handle_special(vxml_parser_t *vp, gboolean dtd);
static void vxml_parser_namespace_global(vxml_parser_t *vp,
	const char *ns, const char *uri);

/**
 * Set the VXML debug level.
 */
void
set_vxml_debug(guint32 level)
{
	vxml_debug = level;
}

/**
 * Are we debugging the VXML layer at the specified level or above?
 */
gboolean
vxml_debugging(guint32 level)
{
	return vxml_debug > level;
}

/**
 * Generate a string showing the current parsing path within the document.
 *
 * It will be something like "/a[2]/b/c/d[3]" saying we're within the root
 * element <a>, and within <b> which is the second sibling of <a>.  In that
 * <b>, <c> is the first element (there's no indication of depth in <b>).
 * And <d> which is also the first child of <c> has seen 3 sub-elements.
 *
 * @return a pointer to a static buffer.
 */
static const char *
vxml_parser_where(const vxml_parser_t *vp)
{
	static char buf[2048];
	size_t rw = 0;
	GList *rpath, *rp;

	rpath = rp = g_list_reverse(g_list_copy(vp->path));

	/*
	 * If sub-parsing an XML fragment, strip the leading part of the
	 * path corresponding to the external (out of current scope) part.
	 */

	if (vp->glob.depth != vp->loc.depth) {
		g_assert(vp->glob.depth >= vp->loc.depth);
		rp = g_list_nth(rp, vp->glob.depth - vp->loc.depth);
	}

	if (NULL == rp) {
		gm_snprintf(buf, sizeof buf, "/");
	} else {
		while (rp != NULL) {
			struct vxml_path_entry *pe = rp->data;
			const char *element;
			unsigned children;

			vxml_path_entry_check(pe);
			element = pe->element;
			children = pe->children;

			rp = g_list_next(rp);

			rw += gm_snprintf(&buf[rw], sizeof buf - rw, "/%s", element);
			if (children > ((NULL == rp) ? 0 : 1))
				rw += gm_snprintf(&buf[rw], sizeof buf - rw, "[%u]", children);
		}
	}

	g_list_free(rpath);

	return buf;
}

/**
 * Emit unconditional warning.
 */
static void
vxml_parser_warn(const vxml_parser_t *vp, const char *format, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, format);
	gm_vsnprintf(buf, sizeof buf, format, args);
	va_end(args);

	g_warning("VXML \"%s\" %soffset %lu, line %lu, at %s: %s",
		vp->name,
		VXML_ENC_CHARSET == vp->encoding ? "converted " : "",
		(unsigned long) vxml_parser_offset(vp),
		(unsigned long) vxml_parser_line(vp),
		vxml_parser_where(vp), buf);
}

/**
 * Emit debugging message.
 */
static void
vxml_parser_debug(const vxml_parser_t *vp, const char *format, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, format);
	gm_vsnprintf(buf, sizeof buf, format, args);
	va_end(args);

	g_debug("VXML \"%s\" %soffset %lu, line %lu, at %s: %s",
		vp->name,
		VXML_ENC_CHARSET == vp->encoding ? "converted " : "",
		(unsigned long) vxml_parser_offset(vp),
		(unsigned long) vxml_parser_line(vp),
		vxml_parser_where(vp), buf);
}

/**
 * Returns current parser element depth.
 */
unsigned
vxml_parser_depth(const vxml_parser_t *vp)
{
	vxml_parser_check(vp);

	return vp->loc.depth;
}

/**
 * Return current parser offset, at the last read character.
 */
size_t
vxml_parser_offset(const vxml_parser_t *vp)
{
	vxml_parser_check(vp);

	return vp->loc.offset - 1;	/* Offset points to next character already */
}

/**
 * Return current parser line number.
 */
size_t
vxml_parser_line(const vxml_parser_t *vp)
{
	vxml_parser_check(vp);

	return vp->loc.line;
}

/**
 * Get current element's name.
 *
 * This can be used in tokenized callabacks for logging purposes.
 *
 * @return the current element's name.
 */
const char *
vxml_parser_current_element(const vxml_parser_t *vp)
{
	struct vxml_path_entry *pe;

	vxml_parser_check(vp);
	g_assert(uint_is_positive(vxml_parser_depth(vp)));
	g_assert(vp->path != NULL);

	pe = vp->path->data;
	vxml_path_entry_check(pe);

	return pe->element;
}

/**
 * Get the name of the element's parent.
 *
 * This can be used in tokenized callabacks for logging purposes.
 *
 * @return the parent element's name, or NULL if the current element is
 * already the root element.
 *
 */
const char *
vxml_parser_parent_element(const vxml_parser_t *vp)
{
	vxml_parser_check(vp);
	g_assert(uint_is_positive(vxml_parser_depth(vp)));
	g_assert(vp->path != NULL);

	if (vxml_parser_depth(vp) > 1) {
		struct vxml_path_entry *pe = g_list_next(vp->path)->data;
		vxml_path_entry_check(pe);
		return pe->element;

	} else {
		return NULL;
	}
}

/**
 * Get the name of the nth enclosing parent of the current element, n=1
 * being the immediate parent, n=0 being the current element.
 *
 * @return te n-th parent element's name (in a path from the current element
 * towards the root of the tree) or NULL if there are not enough parents
 * to move up.
 */
const char *
vxml_parser_nth_parent_element(const vxml_parser_t *vp, size_t n)
{
	vxml_parser_check(vp);
	g_assert(size_is_non_negative(n));
	g_assert(uint_is_positive(vxml_parser_depth(vp)));
	g_assert(vp->path != NULL);

	if (n >= vxml_parser_depth(vp)) {
		return NULL;
	} else {
		GList *l;
		struct vxml_path_entry *pe;

		l = g_list_nth(vp->path, n);
		pe = NULL == l ? NULL : l->data;

		if (pe != NULL) {
			vxml_path_entry_check(pe);
			return pe->element;
		} else {
			return NULL;
		}
	}
}

/**
 * Intialize parser location.
 */
static void
vxml_location_init(struct vxml_location *vl)
{
	g_assert(vl != NULL);

	vl->magic = VXML_LOCATION_MAGIC;
	vl->depth = 0;
	vl->offset = 0;
	vl->line = 1;
	vl->tag_start = 0;
}

/**
 * Free input buffer data, if necessary.
 */
static void
vxml_buffer_data_free(struct vxml_buffer *vb)
{
	vxml_buffer_check(vb);

	if (vb->allocated) {
		if (vb->malloced) {
			g_free(vb->data);
		} else {
			hfree(vb->data);
		}
	}
}

/**
 * Free input buffer.
 */
static void
vxml_buffer_free(struct vxml_buffer *vb)
{
	vxml_buffer_check(vb);

	vxml_buffer_data_free(vb);
	vb->magic = 0;
	wfree(vb, sizeof *vb);
}

/**
 * Allocate input buffer to encapsulate an existing memory buffer.
 *
 * @param gen		generation number
 * @param data		start of the memory buffer
 * @param length	amount of data held in buffer
 * @param allocated	whether we allocated (and own) the data buffer
 * @param user		whether data come from user-land
 * @param reader	reading routine to get characters out of the data
 *
 * @return a new input buffer.
 */
static struct vxml_buffer *
vxml_buffer_alloc(unsigned gen, const char *data, size_t length,
	gboolean allocated, gboolean user, vxml_reader_t reader)
{
	struct vxml_buffer *vb;

	g_assert(data != NULL);
	g_assert(size_is_non_negative(length));

	vb = walloc0(sizeof *vb);
	vb->magic = VXML_BUFFER_MAGIC;
	vb->vb_rptr = vb->data = deconstify_gpointer(data);
	vb->length = length;
	vb->vb_end = data + length;
	vb->reader = reader;
	vb->allocated = allocated;
	vb->malloced = FALSE;
	vb->user = user;
	vb->generation = gen;

	return vb;
}

/**
 * Amount of remaining bytes to be read in buffer.
 */
static size_t
vxml_buffer_remains(const struct vxml_buffer *vb)
{
	vxml_buffer_check(vb);

	return vb->vb_end - vb->vb_rptr;
}

/**
 * Convert unread buffer data from given charset to UTF-8.
 *
 * @param vb		the buffer to convert
 * @param charset	source charset
 *
 * @return TRUE if OK, FALSE on error.
 */
static gboolean
vxml_buffer_convert_to_utf8(struct vxml_buffer *vb, const char *charset)
{
	char *converted;

	vxml_buffer_check(vb);
	g_assert(charset != NULL);
	g_assert(!vb->utf8);

	converted = charset_to_utf8(charset, vb->vb_rptr, vxml_buffer_remains(vb));
	if (NULL == converted)
		return FALSE;

	vxml_buffer_data_free(vb);
	vb->vb_rptr = vb->data = converted;
	vb->length = strlen(converted);
	vb->vb_end = vb->data + vb->length;
	vb->reader = utf8_decode_char_buffer;
	vb->allocated = TRUE;
	vb->malloced = TRUE;
	vb->utf8 = TRUE;

	return TRUE;
}

/**
 * Allocate initial parser output buffer.
 */
static void
vxml_output_alloc(struct vxml_output *vo)
{
	vxml_output_check(vo);

	vo->data = halloc(VXML_OUT_SIZE);
	vo->size = VXML_OUT_SIZE;
	vo->vo_wptr = vo->data;
	vo->vo_end = vo->data + VXML_OUT_SIZE;
}

/**
 * Free output buffer.
 */
static void
vxml_output_free(struct vxml_output *vo)
{
	vxml_output_check(vo);

	hfree(vo->data);
	vo->magic = 0;
}

/**
 * Grow parser output buffer to be able to hold at least ``n'' more bytes.
 */
static void
vxml_output_grow(struct vxml_output *vo, size_t n)
{
	vxml_output_check(vo);

	if (G_UNLIKELY(NULL == vo->data))
		vxml_output_alloc(vo);

	if (G_UNLIKELY(ptr_diff(vo->vo_end, vo->vo_wptr) < n)) {
		size_t offset;

		offset = vo->vo_wptr - vo->data;
		g_assert(size_is_non_negative(offset));

		vo->size += VXML_OUT_SIZE;
		vo->data = hrealloc(vo->data, vo->size);
		vo->vo_wptr = vo->data + offset;
		vo->vo_end = vo->data + vo->size;
	}
}

/**
 * Discard all the data held in the output buffer.
 */
static inline void
vxml_output_discard(struct vxml_output *vo)
{
	vxml_output_check(vo);
	vo->vo_wptr = vo->data;
}

/**
 * @return start of buffer.
 */
static inline const char *
vxml_output_start(const struct vxml_output *vo)
{
	vxml_output_check(vo);
	return vo->data;
}

/**
 * @return length of data held in buffer.
 */
static inline size_t
vxml_output_size(const struct vxml_output *vo)
{
	vxml_output_check(vo);
	return vo->vo_wptr - vo->data;
}

/**
 * Mark output buffer as valid.
 */
static void
vxml_output_init(struct vxml_output *vo)
{
	vo->magic = VXML_OUTPUT_MAGIC;
}

/**
 * Append character read as UTF-8 into the specified output buffer.
 */
static void
vxml_output_append(struct vxml_output *vo, guint32 uc)
{
	unsigned len;

	vxml_output_check(vo);

	len = utf8_encoded_len(uc);
	vxml_output_grow(vo, len);
	vo->vo_wptr += utf8_encode_char(uc, vo->vo_wptr, len);
}

/**
 * Free a path entry.
 */
static void
vxml_path_entry_free(struct vxml_path_entry *pe)
{
	vxml_path_entry_check(pe);

	atom_str_free_null(&pe->element);
	atom_str_free_null(&pe->namespace);
	pe->magic = 0;
	wfree(pe, sizeof *pe);
}

/**
 * Allocate a new versatile XML parser.
 *
 * @param name			parser name, for logging and errors
 * @param options		parser options
 *
 * @return a new XML parser.
 */
vxml_parser_t *
vxml_parser_make(const char *name, guint32 options)
{
	vxml_parser_t *vp;

	vp = walloc0(sizeof *vp);
	vp->magic = VXML_PARSER_MAGIC;
	vp->name = name;
	vp->encoding = VXML_ENC_UTF8;
	vp->encsource = VXML_ENCSRC_DEFAULT;
	vp->endianness = VXML_ENDIANSRC_DEFAULT;
	vp->versource = VXML_VERSRC_DEFAULT;
	vp->options = options;
	vp->namespaces = symtab_make();
	vp->major = 1;
	vp->minor = 0;
	vxml_output_init(&vp->out);
	vxml_output_init(&vp->entity);
	vxml_location_init(&vp->glob);
	vxml_parser_namespace_global(vp, VXS_XML, VXS_XML_URI);

	return vp;
}

/**
 * Free XML parser.
 */
void
vxml_parser_free(vxml_parser_t *vp)
{
	GSList *sl;
	GList *l;

	vxml_parser_check(vp);

	GM_SLIST_FOREACH(vp->input, sl) {
		vxml_buffer_free(sl->data);
	}
	gm_slist_free_null(&vp->input);

	GM_LIST_FOREACH(vp->path, l) {
		vxml_path_entry_free(l->data);
	}
	gm_list_free_null(&vp->path);
	nv_table_free_null(&vp->tokens);
	nv_table_free_null(&vp->entities);
	nv_table_free_null(&vp->pe_entities);
	nv_table_free_null(&vp->attrs);
	nv_table_free_null(&vp->ns);
	vxml_output_free(&vp->out);
	vxml_output_free(&vp->entity);
	atom_str_free_null(&vp->charset);
	atom_str_free_null(&vp->element);
	atom_str_free_null(&vp->namespace);
	symtab_free_null(&vp->namespaces);
	HFREE_NULL(vp->user_error);

	vp->magic = 0;
	wfree(vp, sizeof *vp);
}

/**
 * Add (append) input to the XML parser.
 *
 * @param vp		the XML parser
 * @param data		buffer with additional data to parse
 * @param length	length of the data held in buffer
 */
void
vxml_parser_add_input(vxml_parser_t *vp, const char *data, size_t length)
{
	struct vxml_buffer *vb;

	vxml_parser_check(vp);
	g_assert(data != NULL);
	g_assert(size_is_non_negative(length));

	/*
	 * We force a NULL reader method here.  The proper reader will be
	 * selected when the buffer will start to be used depending on the
	 * document's character encoding.
	 */

	vb = vxml_buffer_alloc(vp->generation++, data, length, FALSE, TRUE, NULL);
	vp->input = g_slist_append(vp->input, vb);
}

/**
 * Fill token hash table with items from the supplied token vector.
 */
static void
vxml_fill_tokens(const nv_table_t *tokens, struct vxml_token *tvec, size_t tlen)
{
	size_t i;
	struct vxml_token *t;

	for (i = 0, t = tvec; i < tlen; i++, t++) {
		nv_table_insert_nocopy(tokens, t->name, t, sizeof *t);
	}
}

/**
 * Set global tokens to be used for element parsing.
 *
 * This supersedes any previously recorded tokens and is a global table
 * stored in the parser.
 *
 * It is possible to also setup another tokenization table which can enrich
 * the global table locally during parsing, for instance to parse a given
 * XML section.  The global table defined here acts as a fallback.
 *
 * If the supplied vector is NULL, the existing table is cleared.
 *
 * @attention
 * The supplied vector is referenced by the parser, so it must point
 * to a structure that will still be allocated whilst the parser runs.
 *
 * @param vp		the XML parser
 * @param tvec		token vector (may be NULL)
 * @param tlen		length of token vector (amount of items)
 */
void
vxml_parser_set_tokens(vxml_parser_t *vp,
	struct vxml_token *tvec, size_t tlen)
{
	vxml_parser_check(vp);

	nv_table_free_null(&vp->tokens);
	g_assert(tvec != NULL || 0 == tlen);
	g_assert(tvec == NULL || size_is_positive(tlen));

	if (tvec != NULL) {
		vp->tokens = nv_table_make(FALSE);
		vxml_fill_tokens(vp->tokens, tvec, tlen);
	}
}

/**
 * Encoding to string.
 */
static const char *
vxml_encoding_to_string(enum vxml_encoding e)
{
	switch (e) {
	case VXML_ENC_UTF8:		return "UTF-8";
	case VXML_ENC_UTF16_BE:	return "UTF-16-BE";
	case VXML_ENC_UTF16_LE:	return "UTF-16-LE";
	case VXML_ENC_UTF32_BE:	return "UTF-32-BE";
	case VXML_ENC_UTF32_LE:	return "UTF-32-LE";
	case VXML_ENC_CHARSET:	return "explicit charset";
	}

	return "unknown character encoding";
}

/**
 * Encoding source to string.
 */
static const char *
vxml_encsrc_to_string(enum vxml_encsrc e)
{
	switch (e) {
	case VXML_ENCSRC_DEFAULT:	return "default encoding";
	case VXML_ENCSRC_INTUITED:	return "intuited encoding";
	case VXML_ENCSRC_EXPLICIT:	return "explicit encoding";
	case VXML_ENCSRC_SUPPLIED:	return "user-supplied (meta) encoding";
	}

	return "unknown encoding source";
}

/**
 * Byte-order source to string.
 */
static const char *
vxml_endsrc_to_string(enum vxml_endsrc e)
{
	switch (e) {
	case VXML_ENDIANSRC_DEFAULT:	return "default endianness";
	case VXML_ENDIANSRC_INTUITED:	return "intuited endianness";
	case VXML_ENDIANSRC_EXPLICIT:	return "explicit (8-bit chars or UTF-8)";
	case VXML_ENDIANSRC_SUPPLIED:	return "user-supplied (meta) endianness";
	}

	return "unknown endianness source";
}

/**
 * Version source to string.
 */
static const char *
vxml_versionsrc_to_string(enum vxml_versionsrc v)
{
	switch (v) {
	case VXML_VERSRC_DEFAULT:		return "default version";
	case VXML_VERSRC_IMPLIED:		return "implied version";
	case VXML_VERSRC_EXPLICIT:		return "explicit version";
	}

	return "unknown version source";
}

/**
 * Translates errors into English.
 */
const char *
vxml_strerror(vxml_error_t error)
{
	switch (error) {
	case VXML_E_OK:						return "OK";
	case VXML_E_UNSUPPORTED_BYTE_ORDER:	return "Unsupported byte order";
	case VXML_E_UNSUPPORTED_CHARSET:	return "Unsupported character set";
	case VXML_E_TRUNCATED_INPUT:		return "Truncated input stream";
	case VXML_E_EXPECTED_NAME_START:	return "Expected a valid name start";
	case VXML_E_INVALID_CHAR_REF:		return "Invalid character reference";
	case VXML_E_INVALID_CHARACTER:		return "Invalid Unicode character";
	case VXML_E_INVALID_NAME_CHARACTER:	return "Invalid character in name";
	case VXML_E_UNKNOWN_ENTITY_REF:		return "Uknown entity reference";
	case VXML_E_UNEXPECTED_CHARACTER:	return "Unexpected character";
	case VXML_E_UNEXPECTED_WHITESPACE:	return "Unexpected white space";
	case VXML_E_BAD_CHAR_IN_NAME:		return "Bad character in name";
	case VXML_E_INVALID_TAG_NESTING:	return "Invalid tag nesting";
	case VXML_E_EXPECTED_QUOTE:			return "Expected quote (\"'\" or '\"')";
	case VXML_E_EXPECTED_GT:			return "Expected '>'";
	case VXML_E_EXPECTED_SPACE:			return "Expected white space";
	case VXML_E_EXPECTED_LBRAK:			return "Expected '['";
	case VXML_E_EXPECTED_RBRAK:			return "Expected ']'";
	case VXML_E_EXPECTED_DOCTYPE_DECL:	return "Expected a DOCTYPE declaration";
	case VXML_E_EXPECTED_TWO_MINUS:		return "Expected '--'";
	case VXML_E_EXPECTED_DECL_TOKEN:	return "Expected a declaration token";
	case VXML_E_EXPECTED_NDATA_TOKEN:	return "Expected NDATA token";
	case VXML_E_EXPECTED_CDATA_TOKEN:	return "Expected CDATA token";
	case VXML_E_EXPECTED_COND_TOKEN:	return "Expected INCLUDE or IGNORE";
	case VXML_E_UNEXPECTED_LT:			return "Was not expecting '<'";
	case VXML_E_UNEXPECTED_XML_PI:		return "Unexpected <?xml...?>";
	case VXML_E_UNEXPECTED_TAG_END:		return "Unexpected tag end";
	case VXML_E_NESTED_DOCTYPE_DECL:	return "Nested DOCTYPE declaration";
	case VXML_E_INVALID_VERSION:		return "Invalid version number";
	case VXML_E_VERSION_OUT_OF_RANGE:	return "Version number out of range";
	case VXML_E_USER:					return "User-defined error";
	case VXML_E_DUP_ATTRIBUTE:			return "Duplicate attribute";
	case VXML_E_DUP_DEFAULT_NAMESPACE:	return "Duplicate default namespace";
	case VXML_E_BAD_CHAR_IN_NAMESPACE:	return "Bad character in namespace";
	case VXML_E_NAMESPACE_REDEFINITION:	return "Invalid namespace redefinition";
	case VXML_E_UNKNOWN_NAMESPACE:		return "Unknown namespace prefix";
	case VXML_E_EMPTY_NAME:				return "Empty name";
	case VXML_E_UNKNOWN_CHAR_ENCODING_NAME:
		return "Unknown character encoding name";
	case VXML_E_INVALID_CHAR_ENCODING_NAME:
		return "Invalid character encoding name";
	case VXML_E_UNREADABLE_CHAR_ENCODING:
		return "Input is unreadable in the specified encoding";
	case VXML_E_MAX:
		break;
	}

	return "Invalid VXML error code";
}

/**
 * Same as vxml_strerror() but also support user-defined error, whose string
 * is stored in the parser.
 */
const char *
vxml_parser_strerror(const vxml_parser_t *vp, vxml_error_t error)
{
	if (VXML_E_USER == error && vp->user_error != NULL)
		return vp->user_error;
	else
		return vxml_strerror(error);
}

/**
 * Is encoding UTF-16?
 */
static gboolean
vxml_encoding_is_utf16(enum vxml_encoding e)
{
	return VXML_ENC_UTF16_BE == e || VXML_ENC_UTF16_LE == e;
}

/**
 * Is encoding UTF-32?
 */
static gboolean
vxml_encoding_is_utf32(enum vxml_encoding e)
{
	return VXML_ENC_UTF32_BE == e || VXML_ENC_UTF32_LE == e;
}

/**
 * Formats document parsing name and parsing position into a static buffer
 * for error logging.
 */
static const char *
vxml_document_where(vxml_parser_t *vp)
{
	static char buf[1024];

	vxml_parser_check(vp);

	gm_snprintf(buf, sizeof buf,
		"%sparsing \"%s\" (%s %u.%u), %soffset %lu, line %lu, at %s",
		vp->glob.depth != vp->loc.depth ? "sub-" : "", vp->name,
		vxml_versionsrc_to_string(vp->versource), vp->major, vp->minor,
		VXML_ENC_CHARSET == vp->encoding ? "converted " : "",
		(unsigned long) vxml_parser_offset(vp),
		(unsigned long) vxml_parser_line(vp),
		vxml_parser_where(vp));

	return buf;
}

/**
 * Record fatal error.
 */
static void
vxml_record_fatal_error(vxml_parser_t *vp, vxml_error_t error)
{
	vp->error = error;
	vp->flags |= VXML_F_FATAL_ERROR;

	if (vp->options & VXML_O_FATAL)
		g_error("VXML fatal error: %s", vxml_strerror(error));
}

/**
 * Sets a fatal parsing error.
 */
static void
vxml_fatal_error(vxml_parser_t *vp, vxml_error_t error)
{
	if (vp->flags & VXML_F_FATAL_ERROR)
		return;

	if (vxml_debugging(0)) {
		g_warning("VXML %s: FATAL error: %s",
			vxml_document_where(vp), vxml_strerror(error));
	}

	vxml_record_fatal_error(vp, error);
}

/**
 * Sets fatal user error (from callbacks).
 *
 * To report errors, users should call vxml_parser_strerror(), otherwise
 * the user's error string will not show up.
 *
 * @param vp		the XML parser
 * @param errstr	the human-readable error (may be NULL)
 */
void
vxml_parser_error(vxml_parser_t *vp, const char *errstr, ...)
{
	vxml_parser_check(vp);

	if (errstr != NULL) {
		char *msg;
		va_list args;

		va_start(args, errstr);
		msg = h_strdup_vprintf(errstr, args);
		va_end(args);
		HFREE_NULL(vp->user_error);
		vp->user_error = msg;
	}

	vxml_record_fatal_error(vp, VXML_E_USER);
}

/**
 * Sets a fatal parsing error, near a character.
 *
 * @param vp	the XML parser
 * @param error	the error code
 * @param uc	the problematic character
 */
static void
vxml_fatal_error_uc(vxml_parser_t *vp, vxml_error_t error, guint32 uc)
{
	if (vp->flags & VXML_F_FATAL_ERROR)
		return;

	if (vxml_debugging(0)) {
		guint32 s32[2];
		char s8[5];

		s32[0] = uc;
		s32[1] = 0;

		if (utf32_to_utf8(s32, s8, sizeof s8) >= sizeof s8)
			g_strlcpy(s8, "????", sizeof s8);

		g_warning("VXML %s near '%s' (U+%X): FATAL error: %s",
			vxml_document_where(vp), s8, uc, vxml_strerror(error));
	}

	vxml_record_fatal_error(vp, error);
}

/**
 * Sets a fatal parsing error, near last read character.
 */
static void
vxml_fatal_error_last(vxml_parser_t *vp, vxml_error_t error)
{
	if (VXC_NUL == vp->last_uc)
		vxml_fatal_error(vp, error);
	else
		vxml_fatal_error_uc(vp, error, vp->last_uc);
}

/**
 * Sets a fatal parsing error, near a string.
 *
 * @param vp	the XML parser
 * @param error	the error code
 * @param uc	the problematic string
 */
static void
vxml_fatal_error_str(vxml_parser_t *vp, vxml_error_t error, const char *str)
{
	if (vp->flags & VXML_F_FATAL_ERROR)
		return;

	if (vxml_debugging(0)) {
		g_warning("VXML %s near \"%s\": FATAL error: %s",
			vxml_document_where(vp), str, vxml_strerror(error));
	}

	vxml_record_fatal_error(vp, error);
}

/**
 * Sets a fatal parsing error, near the string held in the output buffer.
 *
 * @param vp	the XML parser
 * @param error	the error code
 */
static void
vxml_fatal_error_out(vxml_parser_t *vp, vxml_error_t error)
{
	vxml_output_append(&vp->out, VXC_NUL);
	vxml_fatal_error_str(vp, error, vxml_output_start(&vp->out));
}

/**
 * Remove buffer from the parser input stream.
 */
static void
vxml_parser_remove_buffer(vxml_parser_t *vp, struct vxml_buffer *vb)
{
	vp->input = g_slist_remove(vp->input, vb);

	if (vxml_debugging(19)) {
		vxml_parser_debug(vp, "removed %sinput buffer (%lu byte%s)",
			NULL == vp->input ? "last " : "", (unsigned long) vb->length,
			1 == vb->length ? "" : "s");
	}

	vxml_buffer_free(vb);

}

/**
 * Reset character reader of input buffer so that a new one be selected
 * depending on the detected encoding.
 */
static void
vxml_parser_reset_buffer_reader(vxml_parser_t *vp)
{
	if (vp->input != NULL) {
		struct vxml_buffer *vb = vp->input->data;
		vb->reader = NULL;
	}
}

/**
 * Skip next ``amount'' bytes of input.
 *
 * @attention
 * This is skipping bytes, not characters.
 */
static void
vxml_parser_skip(vxml_parser_t *vp, size_t amount)
{
	GSList *sl;
	size_t skipped = 0;

	g_assert(0 == vp->unread_offset);	/* No pending unread characters */

restart:
	GM_SLIST_FOREACH(vp->input, sl) {
		struct vxml_buffer *vb = sl->data;
		size_t avail = vxml_buffer_remains(vb);
		size_t needed = amount - skipped;

		if (avail >= needed) {
			if (vb->user) {
				vp->loc.offset += needed;	/* Only count on external input */
				vp->glob.offset += needed;
			}
			if (avail == needed) {
				vxml_parser_remove_buffer(vp, vb);
			} else {
				vb->vb_rptr += needed;
			}
			break;
		} else {
			skipped += avail;
			if (vb->user) {
				vp->loc.offset += avail;	/* Only count on external input */
				vp->glob.offset += avail;	/* Only count on external input */
			}
			vxml_parser_remove_buffer(vp, vb);
			goto restart;		/* List updated, restart from first buffer */
		}
	}
}

/**
 * Attempt to intuit the document encoding based on the start of the document.
 *
 * @return TRUE if we can continue parsing, FALSE on fatal error.
 */
static gboolean
vxml_intuit_encoding(vxml_parser_t *vp)
{
	GSList *sl;
	guchar head[4];
	size_t filled = 0;
	guint32 fourcc;
	guint16 twocc;

	g_assert(vp->input != NULL);

	/*
	 * We're going to peek at the first 4 bytes as a big-endian quantity,
	 * without actually "reading" them as characters.
	 *
	 * The logic is explained in http://www.w3.org/TR/xml11, section E,
	 * "Autodetection of Character Encodings".
	 */

	GM_SLIST_FOREACH(vp->input, sl) {
		struct vxml_buffer *vb = sl->data;
		size_t avail = vxml_buffer_remains(vb);
		size_t needed = sizeof head - filled;
		size_t len = MIN(needed, avail);

		g_assert(filled < G_N_ELEMENTS(head));
		g_assert(len + filled <= sizeof head);

		memcpy(&head[filled], vb->vb_rptr, len);
		filled += len;

		if (sizeof head == filled)
			break;
	}

	vp->flags |= VXML_F_INTUITED;		/* No longer come here */

	if (filled < G_N_ELEMENTS(head))
		return TRUE;

	fourcc = peek_be32(head);

	switch (fourcc) {
	case 0x0000FEFFU:
		vxml_parser_skip(vp, 4);		/* Skip BOM marker */
		/* FALL THROUGH */
	case 0x0000003CU:
		vp->encoding = VXML_ENC_UTF32_BE;
		goto intuited;
	case 0xFFFE0000U:
		vxml_parser_skip(vp, 4);		/* Skip BOM marker */
		/* FALL THROUGH */
	case 0x3C000000U:
		vp->encoding = VXML_ENC_UTF32_BE;
		goto intuited;
	case 0x0000FFFEU:
	case 0xFEFF0000U:
	case 0x003C0000U:
	case 0x00003C00U:
		vxml_fatal_error(vp, VXML_E_UNSUPPORTED_BYTE_ORDER);
		return FALSE;
	case 0x003C003FU:
		vp->encoding = VXML_ENC_UTF16_BE;
		goto intuited;
	case 0x3C003F00U:
		vp->encoding = VXML_ENC_UTF16_LE;
		goto intuited;
	case 0x3C3F786DU:			/* ASCII for "<?xm" */
		vp->encoding = VXML_ENC_UTF8;
		goto intuited;
	case 0x4C6FA794:			/* EBCDIC for "<?xm" */
		vxml_fatal_error(vp, VXML_E_UNSUPPORTED_CHARSET);
		return FALSE;
	default:
		break;
	}

	twocc = peek_be16(head);

	switch (twocc) {
	case 0xFEFFU:
		vp->encoding = VXML_ENC_UTF16_BE;
		vxml_parser_skip(vp, 2);			/* Skip BOM marker */
		goto intuited;
	case 0xFFFEU:
		vp->encoding = VXML_ENC_UTF16_LE;
		vxml_parser_skip(vp, 2);			/* Skip BOM marker */
		goto intuited;
	case 0xEFBBU:
		if (0xBFU == head[2]) {
			vp->encoding = VXML_ENC_UTF8;
			vxml_parser_skip(vp, 3);		/* Skip BOM marker */
			goto intuited;
		}
		/* FALL THROUGH */
	default:
		break;
	}

	/*
	 * Probably UTF-8, but no leading "<?xml" so it's a fragment.
	 * Keep the default.
	 */

	goto done;

intuited:
	vp->encsource = VXML_ENCSRC_INTUITED;
	vp->endianness = VXML_ENDIANSRC_INTUITED;

done:
	return TRUE;
}

/**
 * Unread character.
 *
 * Characters will be read back in a LIFO manner, so they must be unread
 * in the reverse order that they were read: most recently read first, then
 * previously read, etc....
 */
static void
vxml_unread_char(vxml_parser_t *vp, guint32 uc)
{
	g_assert(vp->unread_offset < G_N_ELEMENTS(vp->unread));
	g_assert(size_is_non_negative(vp->unread_offset));

	vp->unread[vp->unread_offset++] = uc;

	if (vxml_debugging(19)) {
		vxml_parser_debug(vp, "unread U+%X '%c'", uc,
			is_ascii_print(uc) ? uc & 0xff : ' ');
	}
}

/**
 * Read next character.
 *
 * The XML parser must not use this routine to get characters  but use
 * vxml_next_char() instead so that End-of-Line normalization can be
 * performed correctly.  Only vxml_next_char() should call us directly.
 *
 * @param vp		the XML parser
 * @param uc		where the next Unicode character is returned
 *
 * @return TRUE when character was read from user input, FALSE otherwise (read
 * from entity expansion output or from the unread buffer).
 * The character returned in "uc" is 0 when the end of input was reached.
 */
static gboolean
vxml_read_char(vxml_parser_t *vp, guint32 *uc)
{
	struct vxml_buffer *vb;
	guint retlen;

	if (vp->flags & VXML_F_FATAL_ERROR)
		return 0;

	/*
	 * If we have unread characters pending, serve them in a LIFO manner.
	 * Note that in that case the parsing offset remains unchanged (because
	 * we do not remember the size of the characters in the input).
	 */

	if (vp->unread_offset != 0) {
		g_assert(vp->unread_offset <= G_N_ELEMENTS(vp->unread));
		g_assert(size_is_positive(vp->unread_offset));

		*uc = vp->last_uc = vp->unread[--(vp->unread_offset)];

		/*
		 * The value of vp->last_uc_generation is unchanged, so in effect
		 * the generation number of this character is the same as the last
		 * one we really read.
		 *
		 * Given that generation numbers are used to properly handle quoting
		 * of strings containing entities whose replacement text contains
		 * quotes, and given that entities are expanded as a whole in one
		 * single buffer, there will be no problem of buffer boundaries.
		 */

		if (vxml_debugging(19)) {
			vxml_parser_debug(vp, "read back U+%X '%c'", vp->last_uc,
				is_ascii_print(vp->last_uc) ? vp->last_uc & 0xff : ' ');
		}

		return FALSE;
	}

	/*
	 * Consume character from next input buffer.
	 */

next_buffer:
	if (NULL == vp->input) {
		*uc = vp->last_uc = VXC_NUL;
		return FALSE;
	}

	vb = vp->input->data;
	vxml_buffer_check(vb);

	if (vb->vb_rptr == vb->vb_end) {
		vxml_parser_remove_buffer(vp, vb);
		goto next_buffer;
	}

	/*
	 * If we don't know the reader yet, configure it based on the
	 * document encoding and endianness.
	 */

	if (G_UNLIKELY(NULL == vb->reader)) {
		switch (vp->encoding) {
		case VXML_ENC_UTF8:
			vb->reader = utf8_decode_char_buffer;
			break;
		case VXML_ENC_UTF16_BE:
			vb->reader = utf16_be_decode_char_buffer;
			break;
		case VXML_ENC_UTF16_LE:
			vb->reader = utf16_le_decode_char_buffer;
			break;
		case VXML_ENC_UTF32_BE:
			vb->reader = utf32_be_decode_char_buffer;
			break;
		case VXML_ENC_UTF32_LE:
			vb->reader = utf32_le_decode_char_buffer;
			break;
		case VXML_ENC_CHARSET:
			if (vxml_debugging(5)) {
				size_t len = vxml_buffer_remains(vb);
				vxml_parser_debug(vp,
					"converting %lu-byte input from %s to UTF-8",
					(unsigned long) len, vp->charset);
			}

			if (!vxml_buffer_convert_to_utf8(vb, vp->charset)) {
				vxml_fatal_error(vp, VXML_E_UNREADABLE_CHAR_ENCODING);
				*uc = vp->last_uc = VXC_NUL;
				return FALSE;
			}
			g_assert(vb->reader != NULL);
			g_assert(vb->utf8);

			if (vxml_debugging(5)) {
				size_t len = vxml_buffer_remains(vb);
				vxml_parser_debug(vp, "converted buffer is %lu-byte long",
					(unsigned long) len);
			}
			break;
		}
	}

	*uc = vp->last_uc =
		(*vb->reader)(vb->vb_rptr, vxml_buffer_remains(vb), &retlen);

	vb->vb_rptr += retlen;
	if (vb->user) {
		vp->loc.offset += retlen;		/* Only counts user-supplied data */
		vp->glob.offset += retlen;
	}
	vp->last_uc_generation = vb->generation;

	if (vxml_debugging(19)) {
		vxml_parser_debug(vp, "read U+%X '%c' %u byte%s%s", vp->last_uc,
			is_ascii_print(vp->last_uc) ? vp->last_uc & 0xff : ' ',
			retlen, 1 == retlen ? "" : "s", vb->user ? "" : " (entity)");
	}

	return vb->user;
}

/**
 * Get next character, after End-of-Line normalization.
 *
 * @return next Unicode character, 0 if we reached the end of input or cannot
 * continue due to a fatal error.
 */
static guint32
vxml_next_char(vxml_parser_t *vp)
{
	guint32 uc;
	gboolean user_input;

	/*
	 * New-line normalization.
	 * See http://www.w3.org/TR/xml11, section 2.11 "End-of-Line Handling".
	 */

	user_input = vxml_read_char(vp, &uc);

	if (VXC_CR == uc) {
		(void) vxml_read_char(vp, &uc);
		if (uc == VXC_LF || uc == 0x85U) {
			/* Previous 0xD we read + current char collapsed into 0xA */
			goto new_line;
		} else {
			vxml_unread_char(vp, uc);
			goto new_line;	/* Transform the previous 0xD we read into 0xA */
		}
	} else if (VXC_LF == uc) {
		goto new_line;		/* Verbatim! */
	} else if (0x85U == uc || 0x2028U == uc) {
		goto new_line;		/* Normalization into 0xA */
	} else {
		return uc;
	}

new_line:
	if (user_input) {
		vp->glob.line++;
		vp->loc.line++;
	}

	return VXC_LF;	/* '\n' */
}

/**
 * Is Unicode character a valid character for a name start.
 */
static gboolean
vxml_is_valid_name_start_char(guint32 uc)
{
	/*
	 * NameStartChar ::= ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] |
	 *                   [#xD8-#xF6] | [#xF8-#x2FF] | [#x370-#x37D] |
	 *                   [#x37F-#x1FFF] | [#x200C-#x200D] | [#x2070-#x218F] |
	 *                   [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF] |
	 *                   [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
	 */

	if (VXC_COLON == uc || 0x5FU == uc)		/* ':' or '_' */
		return TRUE;
	else if (uc >= 0x41U && uc <= 0x5AU)	/* [A-Z] */
		return TRUE;
	else if (uc >= 0x61U && uc <= 0x7AU)	/* [a-z] */
		return TRUE;
	else if (uc >= 0xC0U && uc <= 0xD6U)
		return TRUE;
	else if (uc >= 0xD8U && uc <= 0xF6U)
		return TRUE;
	else if (uc >= 0xF8U && uc <= 0x2FFU)
		return TRUE;
	else if (uc >= 0x370U && uc <= 0x37DU)
		return TRUE;
	else if (uc >= 0x37FU && uc <= 0x1FFFU)
		return TRUE;
	else if (uc >= 0x200CU && uc <= 0x200DU)
		return TRUE;
	else if (uc >= 0x2070U && uc <= 0x218FU)
		return TRUE;
	else if (uc >= 0x2C00U && uc <= 0x2FEFU)
		return TRUE;
	else if (uc >= 0x3001U && uc <= 0xD7FFU)
		return TRUE;
	else if (uc >= 0xF900U && uc <= 0xFDCFU)
		return TRUE;
	else if (uc >= 0xFDF0U && uc <= 0xFFFDU)
		return TRUE;
	else if (uc >= 0x10000U && uc <= 0xEFFFFU)
		return TRUE;
	else
		return FALSE;
}

/*
 * Is Unicode character a valid character within a name.
 */
static gboolean
vxml_is_valid_name_char(guint32 uc)
{
	/*
	 * NameChar ::= NameStartChar | "-" | "." | [0-9] | #xB7 |
	 *              [#x0300-#x036F] | [#x203F-#x2040]
	 */

	if (0x2DU == uc || 0x2EU == uc)		/* '-' or '.' */
		return TRUE;
	else if (uc >= 0x30U && uc <= 0x39U)	/* [0-9] */
		return TRUE;
	else if (0xB7U == uc)
		return TRUE;
	else if (vxml_is_valid_name_start_char(uc))
		return TRUE;
	else if (uc >= 0x300U && uc <= 0x36FU)
		return TRUE;
	else if (uc >= 0x203FU && uc <= 0x2040U)
		return TRUE;
	else
		return FALSE;
}

/**
 * Is Unicode character a white space?
 */
static gboolean
vxml_is_white_space_char(guint32 uc)
{
	/*
	 * S ::= (#x20 | #x9 | #xD | #xA)+
	 */

	return VXC_SP == uc || VXC_HT == uc || VXC_CR == uc || VXC_LF == uc;
}

/*
 * Is Unicode character in the upper-ASCII letter range?
 */
static gboolean
vxml_is_ascii_upper_letter_char(guint32 uc)
{
	return uc >= 0x41U && uc <= 0x5AU;		/* [A-Z] */
}

/**
 * Read a Name into the supplied output buffer which must be empty.
 * Whatever the input encoding was, the returned name is in UTF-8.
 * A NUL is conveniently appended to the output buffer to make it a C string.
 *
 * @param vp	the XML parser
 * @param vo	the output buffer into which name is written.
 * @param c		if non-zero, the character that has to be a NameStartChar
 *
 * @return TRUE if we successfuly parsed the name, FALSE on error with
 * vp->error set.
 */
static gboolean
vxml_parser_handle_name(vxml_parser_t *vp, struct vxml_output *vo, guint32 c)
{
	guint32 uc;

	vxml_output_check(vo);
	g_assert(0 == vxml_output_size(vo));

	/*
	 * Name     ::= NameStartChar (NameChar)*
	 * NameChar ::= NameStartChar | "-" | "." | [0-9] | #xB7 |
	 *              [#x0300-#x036F] | [#x203F-#x2040]
	 */

	uc = (0 == c) ? vxml_next_char(vp) : c;

	if (0 == uc) {
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		return FALSE;
	}

	if (!vxml_is_valid_name_start_char(uc)) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_NAME_START, uc);
		return FALSE;
	}

	vxml_output_append(vo, uc);		/* The NameStartChar */

	while (0 != (uc = vxml_next_char(vp))) {
		if (!vxml_is_valid_name_char(uc)) {
			vxml_unread_char(vp, uc);
			break;
		}
		vxml_output_append(vo, uc);	/* The NameChar */
	}

	if (0 == uc) {
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		return FALSE;
	}

	/*
	 * Append a NUL so that we can use the buffer as a C string.
	 */

	vxml_output_append(vo, VXC_NUL);
	return TRUE;
}

/**
 * Read an upper-cased name into the supplied output buffer which must be empty.
 * Whatever the input encoding was, the returned name is in ASCII (UTF-8).
 * A NUL is conveniently appended to the output buffer to make it a C string.
 *
 * @param vp	the XML parser
 * @param vo	the output buffer into which name is written.
 *
 * @return TRUE if we successfuly parsed the name, FALSE on error with
 * vp->error set.
 */
static gboolean
vxml_parser_handle_uppername(vxml_parser_t *vp, struct vxml_output *vo)
{
	guint32 uc;

	vxml_output_check(vo);
	g_assert(0 == vxml_output_size(vo));

	while (0 != (uc = vxml_next_char(vp))) {
		if (!vxml_is_ascii_upper_letter_char(uc)) {
			vxml_unread_char(vp, uc);
			break;
		}
		vxml_output_append(vo, uc);	/* The ASCII letter */
	}

	if (0 == uc) {
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		return FALSE;
	}

	/*
	 * Append a NUL so that we can use the buffer as a C string.
	 */

	vxml_output_append(vo, VXC_NUL);
	return TRUE;
}

/**
 * Expand character reference (the leading '&#' have been swallowed).
 *
 * @return TRUE if the expansion was successful, FALSE otherwise with
 * vp->error set with the proper error code.
 */
static gboolean
vxml_expand_char_ref(vxml_parser_t *vp)
{
	guint32 uc;
	unsigned base = 10;
	guint32 v = 0;
	gboolean has_digit = FALSE;

	vxml_parser_check(vp);

	/*
	 * CharRef   ::= '&#' [0-9]+ ';' | '&#x' [0-9a-fA-F]+ ';'
	 */

	uc = vxml_next_char(vp);
	if (0x78U == uc) {
		base = 16;
	} else {
		vxml_unread_char(vp, uc);	/* Put digit back */
	}

	/*
	 * Parse the (hexa) digits, constructing the value of the Unicode character
	 * which will be the final value of the expansion.
	 */

	while (0 != (uc = vxml_next_char(vp))) {
		unsigned d;

		if (uc > 0x7FU)
			goto failed;

		if (VXC_SC == uc)		/* Reached trailing ';' */
			break;

		d = alnum2int_inline(uc & 0xff);
		if (d >= base)
			goto failed;

		has_digit = TRUE;
		v = v * base + d;	/* Cannot overflow */

		if (v > 0x10FFFF)
			goto invalid;
	}

	if (0 == uc)
		goto truncated;

	if (!has_digit)			/* "&#;" or "&#x;" are invalid */
		goto failed;

	if (utf32_bad_codepoint(v))
		goto invalid;

	/*
	 * Expansion of the character reference was OK, simply emit this
	 * character with no further interpretation to the parser's output.
	 */

	if (vxml_debugging(19)) {
		vxml_parser_debug(vp, "single char ref: U+%X '%c'", v,
			is_ascii_print(v) ? v & 0xff : ' ');
	}

	vxml_output_append(&vp->out, v);

	return TRUE;

failed:
	vxml_fatal_error_uc(vp, VXML_E_INVALID_CHAR_REF, uc);
	return FALSE;

invalid:
	vxml_fatal_error(vp, VXML_E_INVALID_CHARACTER);
	return FALSE;

truncated:
	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Lookup token in a sorted array of tokens.
 *
 * @param name		the token name
 * @param tokens	the token array
 * @param len		the length of the token array
 *
 * @return the token value if found, 0 if not found.
 */
unsigned
vxml_token_lookup(const char *name,
	const struct vxml_parser_token *tokens, size_t len)
{
#define GET_KEY(i) (tokens[(i)].name)
#define FOUND(i) G_STMT_START { \
	return tokens[(i)].value; \
	/* NOTREACHED */ \
} G_STMT_END

	/* Perform a binary search to find ``name'' */
	BINARY_SEARCH(const char *, name, len, strcmp, GET_KEY, FOUND);

#undef FOUND
#undef GET_KEY

	return 0;		/* Not found */
}

/**
 * Look whether name is that of a known default entity.
 *
 * @return VXC_NUL if entity was not found, its single Unicode character
 * otherwise.
 */
static guint32
vxml_get_default_entity(const char *name)
{
	return vxml_token_lookup(name,
		vxml_default_entities, G_N_ELEMENTS(vxml_default_entities));
}

/**
 * Tokenization of declaration keywords.
 *
 * @return VXT_UNKNOWN if unknown token.
 */
static enum vxml_parser_token_value
vxml_get_declaration_token(const char *name)
{
	return vxml_token_lookup(name,
		vxml_declaration_tokens, G_N_ELEMENTS(vxml_declaration_tokens));
}

/**
 * Tokenization of miscellaneous keywords.
 *
 * @return VXT_UNKNOWN if unknown token.
 */
static enum vxml_parser_token_value
vxml_get_misc_token(const char *name)
{
	return vxml_token_lookup(name,
		vxml_misc_tokens, G_N_ELEMENTS(vxml_misc_tokens));
}

/**
 * Tokenization of immediate keywords (introduced by a leading '#' character).
 *
 * @return VXT_UNKNOWN if unknown token.
 */
static enum vxml_parser_token_value
vxml_get_immediate_token(const char *name)
{
	return vxml_token_lookup(name,
		vxml_immediate_tokens, G_N_ELEMENTS(vxml_immediate_tokens));
}

/**
 * Loads vxml_token_string[] with an inverted token index.
 */
static void
vxml_token_to_string_load(struct vxml_parser_token *tokens, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		size_t value = tokens[i].value;

		g_assert(size_is_non_negative(value));
		g_assert(value < G_N_ELEMENTS(vxml_token_strings));

		vxml_token_strings[value] = tokens[i].name;
	}
}

/**
 * Converts a token back to a string.
 */
static const char *
vxml_token_to_string(enum vxml_parser_token_value token)
{
	static gboolean inited;

	if (!inited) {
		vxml_token_to_string_load(vxml_declaration_tokens,
			G_N_ELEMENTS(vxml_declaration_tokens));
		vxml_token_to_string_load(vxml_misc_tokens,
			G_N_ELEMENTS(vxml_misc_tokens));
		vxml_token_to_string_load(vxml_immediate_tokens,
			G_N_ELEMENTS(vxml_immediate_tokens));
		inited = TRUE;
	}

	if (VXT_UNKNOWN == token)
		return "unknown token";

	if (token < 1 || token >= G_N_ELEMENTS(vxml_token_strings))
		return "invalid token";

	return vxml_token_strings[token];
}

/**
 * Skip white space.
 *
 * @return TRUE if we reached a non-space character, FALSE if we reached
 * an error (EOF), with vp->error set.
 */
static gboolean
vxml_parser_skip_spaces(vxml_parser_t *vp)
{
	guint32 uc;

	while (0 != (uc = vxml_next_char(vp))) {
		if (!vxml_is_white_space_char(uc)) {
			vxml_unread_char(vp, uc);
			return TRUE;
		}
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Swallow mandatory white space, followed by optional additional spaces.
 *
 * @param vp		the XML parser
 * @param ac		if non-VXC_NUL, the next character that was read-ahead
 *
 * @return TRUE if we swallowed space characters, FALSE if we reached
 * an error, with vp->error set.
 */
static gboolean
vxml_parser_swallow_spaces(vxml_parser_t *vp, guint32 ac)
{
	guint32 uc;

	/*
	 * The first character must be a white space.
	 */

	uc = (VXC_NUL == ac) ? vxml_next_char(vp) : ac;

	if (VXC_NUL == uc)
		goto truncated;

	if (!vxml_is_white_space_char(uc)) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_SPACE, uc);
		return FALSE;
	}

	/*
	 * Swallow additional white space characters.
	 */

	while (0 != (uc = vxml_next_char(vp))) {
		if (!vxml_is_white_space_char(uc)) {
			vxml_unread_char(vp, uc);
			return TRUE;
		}
	}

truncated:
	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Grab next miscellaneous token.
 *
 * @param vp		the XML parser
 * @param token		where token ID is written
 * @param error		error to raise if token is unknown
 *
 * @return TRUE if OK, FALSE on error with vp->errno set
 */
static gboolean
vxml_parser_next_misc_token(vxml_parser_t *vp,
	enum vxml_parser_token_value *token, vxml_error_t error)
{
	enum vxml_parser_token_value tok;

	g_assert(0 == vxml_output_size(&vp->out));

	if (!vxml_parser_handle_uppername(vp, &vp->out))
		return FALSE;

	/*
	 * Make sure it's a proper miscellaneous token.
	 */

	tok = vxml_get_misc_token(vxml_output_start(&vp->out));

	if (VXT_UNKNOWN == tok) {
		vxml_fatal_error_out(vp, error);
		return FALSE;
	}

	vxml_output_discard(&vp->out);
	*token = tok;
	return TRUE;
}

/**
 * Expand an entity, if found.
 *
 * @return TRUE if the expansion was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_expand(vxml_parser_t *vp, const char *name, nv_table_t *entities)
{
	nv_pair_t *ev;

	g_assert(name != NULL);

	/*
	 * If no entity has been defined yet in the document, there is no
	 * hash table allocated.
	 */

	if (NULL == entities)
		goto not_found;

	ev = nv_table_lookup(entities, name);
	if (NULL == ev)
		goto not_found;

	/*
	 * Once expanded, the entity needs to be processed by the XML parser
	 * as if its value had been inlined with no entity reference.
	 *
	 * We know that the entity value is already encoded as UTF-8 because
	 * we parsed its definition and we normalize everything to UTF-8
	 * internally.
	 *
	 * To let the parser process the expansion, create a new input buffer
	 * with an UTF-8 reader, and put it in front of the current input.
	 */

	g_assert(0 == vp->unread_offset);	/* No pending look-ahead */

	{
		struct vxml_buffer *vb;
		char *value;
		size_t length;

		value = nv_pair_value_len(ev, &length);	/* NUL-terminated */

		/*
		 * The trailing NUL in the value is not part of the expansion,
		 * hence the use of only the first "length - 1" bytes from the value.
		 */

		vb = vxml_buffer_alloc(vp->generation++,
				value, length - 1, FALSE, FALSE, utf8_decode_char_buffer);
		vp->input = g_slist_prepend(vp->input, vb);

		if (vxml_debugging(19)) {
			vxml_parser_debug(vp, "expanded %c%s; into \"%s\" (%lu byte%s)",
				entities == vp->entities ? '&' :
				entities == vp->pe_entities ? '%' : '?',
				name, value, (unsigned long) vb->length,
				1 == vb->length ? "" : "s");
		}
	}

	return TRUE;

not_found:
	vxml_fatal_error_str(vp, VXML_E_UNKNOWN_ENTITY_REF, name);
	return FALSE;
}

/**
 * Expand a document-defined entity, if found.
 *
 * @return TRUE if the expansion was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_expand_entity(vxml_parser_t *vp, const char *name)
{
	return vxml_expand(vp, name, vp->entities);
}

/**
 * Expand a parameter entity, if found.
 *
 * @param vp		the XML parser
 * @param name		the entity name
 * @param inquote	whether we are in a quoted string (in an entity value)
 *
 * @return TRUE if the expansion was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_expand_pe_entity(vxml_parser_t *vp, const char *name, gboolean inquote)
{
	gboolean ret;

	/*
	 * Expand the parameter entity differently when it is within a
	 * quoted string or when it appears within the DTD outside of any
	 * string.
	 *
	 * As said in http://www.w3.org/TR/xml11, section 4.4.8:
	 *
	 * "When a parameter-entity reference is recognized in the DTD and 
	 * included, its replacement text  MUST be enlarged by the attachment of
	 * one leading and one following space (#x20) character; the intent is to
	 * constrain the replacement text of parameter entities to contain an
	 * integral number of grammatical tokens in the DTD. This behavior MUST NOT
	 * apply to parameter entity references within entity values; these are
	 * described in 4.4.5 Included in Literal."
	 */

	if (!inquote) {
		struct vxml_buffer *vb;
		static char vxc_sp_buf[] = " ";

		/*
		 * Since it is an error if the entity is not recognized, we can safely
		 * behave as if everything will be properly expanded.  To include the
		 * trailing space, we insert a single-byte buffer now.  To include the
		 * leading space, we'll forcefully "unread" a space.
		 */

		vb = vxml_buffer_alloc(vp->generation++,
				vxc_sp_buf, CONST_STRLEN(vxc_sp_buf),
				FALSE, FALSE, utf8_decode_char_buffer);
		vp->input = g_slist_prepend(vp->input, vb);
	}

	ret = vxml_expand(vp, name, vp->pe_entities);

	if (ret && !inquote)
		vxml_unread_char(vp, VXC_SP);

	return ret;
}

/**
 * Expand a parameter entity reference (the leading '%' was swallowed).
 *
 * @param vp		the XML parser
 * @param inquote	TRUE if reference is in a quoted string
 *
 * @return TRUE if the expansion was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_expand_pe_reference(vxml_parser_t *vp, gboolean inquote)
{
	guint32 uc;

	g_assert(0 == vxml_output_size(&vp->entity));

	/*
	 * PEReference   ::= '%' Name ';'
	 */

	if (!vxml_parser_handle_name(vp, &vp->entity, VXC_NUL))
		return FALSE;

	uc = vxml_next_char(vp);
	if (VXC_SC != uc) {		/* Not reached trailing ';' */
		vxml_fatal_error_uc(vp, VXML_E_INVALID_NAME_CHARACTER, uc);
		return FALSE;
	}

	if (!vxml_expand_pe_entity(vp, vxml_output_start(&vp->entity), inquote))
		return FALSE;

	vxml_output_discard(&vp->entity);
	return TRUE;
}

/**
 * Expand reference (the leading '&' has already been swallowed).
 *
 * @param vp		the XML parser
 * @param charonly	if TRUE, only expand character references
 *
 * @return TRUE if the expansion was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_expand_reference(vxml_parser_t *vp, gboolean charonly)
{
	guint32 uc;

	g_assert(0 == vxml_output_size(&vp->entity));

	/*
	 * Reference ::= EntityRef | CharRef
	 * EntityRef ::= '&' Name ';'
	 * CharRef   ::= '&#' [0-9]+ ';' | '&#x' [0-9a-fA-F]+ ';'
	 */

	uc = vxml_next_char(vp);
	if (0 == uc) {
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		return FALSE;
	} else if (0x23U == uc) {		/* '#' introduces a CharRef */
		return vxml_expand_char_ref(vp);
	}

	/*
	 * We're facing an EntityRef.
	 *
	 * If it must not be expanded, emit the '&' character we already read
	 * in the output buffer, and abort, signalling success: in effect we
	 * did not expand anything and the entity will go through, un-expanded.
	 */

	if (charonly) {
		vxml_output_append(&vp->out, VXC_AMP);	/* The '&' character */
		vxml_unread_char(vp, uc);
		return TRUE;							/* Entity not expanded */
	}

	/*
	 * Has to be an EntityRef, which means this character has to
	 * be a valid NameStartChar, or we have an invalid input.
	 */

	if (!vxml_parser_handle_name(vp, &vp->entity, uc))
		return FALSE;

	uc = vxml_next_char(vp);
	if (VXC_SC != uc) {		/* Not reached trailing ';' */
		vxml_fatal_error_uc(vp, VXML_E_INVALID_NAME_CHARACTER, uc);
		return FALSE;
	}

	/*
	 * Look whether we have a standard entity, which is known to be
	 * only one single character.
	 */

	uc = vxml_get_default_entity(vxml_output_start(&vp->entity));
	if (uc != 0) {
		vxml_output_append(&vp->out, uc);	/* The entity value */

		if (vxml_debugging(19)) {
			vxml_parser_debug(vp, "default entity ref: U+%X '%c'", uc,
				is_ascii_print(uc) ? uc & 0xff : ' ');
		}

		goto done;
	}

	/*
	 * Look for an entity defined in the document then.
	 */

	if (!vxml_expand_entity(vp, vxml_output_start(&vp->entity)))
		return FALSE;

done:
	vxml_output_discard(&vp->entity);
	return TRUE;
}

/**
 * Free routine for namespace name/value pairs.
 */
static void
vxml_namespace_free(void *p, size_t unused_len)
{
	(void) unused_len;

	atom_str_free(p);
}

/**
 * Create a new namespace name/value pair, with an attached free routine
 * so that underlying memory is reclaimed as soon as the name/value pair
 * is freed.
 *
 * @param ns		the name space
 * @param uri		the name space URI
 * @param uri_len	the string length of the URI (computed if 0)
 */
static nv_pair_t *
vxml_namespace_make(const char *ns, const char *uri, size_t uri_len)
{
	nv_pair_t *nv;
	const char *uri_atom;
	size_t len;

	g_assert(ns != NULL);
	g_assert(uri != NULL);

	uri_atom = atom_str_get(uri);
	len = (0 == uri_len) ? strlen(uri_atom) : uri_len;
	nv = nv_pair_make_nocopy(ns, uri_atom, uri_len + 1);	/* Trailing NUL */
	nv_pair_set_value_free(nv, vxml_namespace_free);

	return nv;
}

/**
 * Insert a global namespace.
 */
static void
vxml_parser_namespace_global(vxml_parser_t *vp, const char *ns, const char *uri)
{
	nv_pair_t *sym;

	g_assert(vp->namespaces != NULL);

	sym = vxml_namespace_make(ns, uri, strlen(uri));

	if (!symtab_insert_pair(vp->namespaces, sym, 0)) {
		g_error("VXML \"%s\" cannot insert global namespace \"%s\" as \"%s\"",
			vp->name, ns, uri);
	}
}

/**
 * Insert a new namespace (described by the name/value pair associating the
 * local namespace name with the URI value of the namespace) into the parser.
 *
 * @param vp	the XML parser
 * @param ns	the namespace name/value to insert
 * @param err	error to report if symbol table can't record namespace
 */
static void
vxml_parser_namespace_insert(vxml_parser_t *vp, nv_pair_t *ns, vxml_error_t err)
{
	g_assert(ns != NULL);

	/*
	 * Name spaces start to become alive as soon as they are declared.
	 *
	 * Since at this stage we have not entered the tag yet (namespaces are
	 * declared in tag attributes, parsed before we officially enter the
	 * element and increase the parsing depth), we need to increase the depth
	 * by one to define the lexical scope of that namespace correctly.
	 *
	 * Furthermore, we use the global depth of the parser, not the local
	 * one in case sub-parsing is used.  Indeed, namespaces defined earlier
	 * apply until their lexical scope is left, and this requires a common
	 * global depth scale.
	 */

	if (!symtab_insert_pair(vp->namespaces, ns, vp->glob.depth + 1)) {
		vxml_fatal_error_str(vp, err, nv_pair_value_str(ns));
		nv_pair_free(ns);
	}
}

/**
 * Fetch namespace URI value, given local namespace prefix.
 *
 * @return namespace URI if found, an empty string otherwise.
 */
static const char *
vxml_parser_namespace_lookup(const vxml_parser_t *vp, const char *ns)
{
	const char *uri;

	g_assert(ns != NULL);

	uri = symtab_lookup(vp->namespaces, ns);
	return NULL == uri ? VXS_EMPTY : uri;
}

/**
 * Resolve the current namespace local prefix into an URI, considering that
 * a NULL local prefix means no explicit namespace and that means the default
 * namespace for elements (there is no default namespace for unprefixed
 * attributes).
 *
 * @return namespace URI if found, an empty string otherwise.
 */
static const char *
vxml_parser_namespace_uri(const vxml_parser_t *vp, const char *ns)
{
	return vxml_parser_namespace_lookup(vp, NULL == ns ? VXS_DEFAULT_NS : ns);
}

/**
 * Is the namespace prefix known?
 */
static gboolean
vxml_parser_namespace_exists(const vxml_parser_t *vp, const char *ns)
{
	g_assert(ns != NULL);

	return symtab_lookup(vp->namespaces, ns) != NULL;
}

/**
 * Get current element's namespace URI.
 *
 * @return the current element's namespace URI, the empty string if none
 * was defined or if namespace support was disabled in the parser.
 */
const char *
vxml_parser_current_namespace(const vxml_parser_t *vp)
{
	struct vxml_path_entry *pe;

	vxml_parser_check(vp);
	g_assert(uint_is_positive(vxml_parser_depth(vp)));
	g_assert(vp->path != NULL);

	pe = vp->path->data;
	vxml_path_entry_check(pe);

	return vxml_parser_namespace_uri(vp, pe->namespace);
}

/**
 * Define a new default namespace.
 *
 * The new namespace is included in the global namespace table and will.
 * persist until the end of the current element is reached.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_namespace_default(vxml_parser_t *vp, const char *value, size_t len)
{
	nv_pair_t *default_ns;

	if (vp->options & VXML_O_NO_NAMESPACES)
		return TRUE;

	/*
	 * The value of VXS_DEFAULT_NS is an invalid local prefix name since
	 * it starts with a ':' character.  As such, we know there cannot be
	 * any conflict with a user-defined namespace prefix.
	 */

	default_ns = vxml_namespace_make(VXS_DEFAULT_NS, value, len);
	vxml_parser_namespace_insert(vp, default_ns, VXML_E_DUP_DEFAULT_NAMESPACE);

	return TRUE;
}

/**
 * Declare a new namespace.
 *
 * The new namespace is not included in the global namespace table until
 * the end of the tag is reached.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_namespace_decl(vxml_parser_t *vp,
	const char *name, const char *value, size_t value_len)
{
	if (vp->options & VXML_O_NO_NAMESPACES)
		return TRUE;

	if (0 == value_len) {
		vxml_fatal_error(vp, VXML_E_EMPTY_NAME);
		return FALSE;
	}

	if (strchr(name, ':') != NULL) {
		vxml_fatal_error(vp, VXML_E_BAD_CHAR_IN_NAMESPACE);
		return FALSE;
	}

	if (NULL == vp->ns)
		vp->ns = nv_table_make(FALSE);

	/*
	 * No duplicate declarations, regardless of VXML_O_NO_DUP_ATTR.
	 *
	 * FIXME: to detect properly duplicate attributes, we should replace
	 * the namespace name by its URI, catenate it with the local attribute
	 * name and insert that in the hash table.  A conflict would mean a
	 * duplicate.  The default namespace would apply if an unqualified name
	 * is given.  This would impose to declare the URI of 'xml:'.
	 * The symbol table will take care of duplicates in 'xmlns:'...
	 * Current algorithm, with all its limitations, is good enough for now.
	 *		--RAM, 2010-12-20
	 */

	if (nv_table_lookup(vp->ns, name) != NULL) {
		vxml_fatal_error(vp, VXML_E_DUP_ATTRIBUTE);
		return FALSE;
	}

	/*
	 * Register the namespace to be alive during the entire lexical scope
	 * of the element.
	 */

	{
		nv_pair_t *ns = vxml_namespace_make(name, value, value_len);

		nv_table_insert_pair(vp->ns, nv_pair_refcnt_inc(ns));
		vxml_parser_namespace_insert(vp, ns, VXML_E_NAMESPACE_REDEFINITION);
	}

	return TRUE;
}

/**
 * Set parser's current element to given string, or clear it if NULL.
 */
static void
vxml_parser_set_element(vxml_parser_t *vp, const char *element, const char *ns)
{
	if (element != NULL) {
		const char *atom = vp->element;
		atom_str_change(&vp->element, element);
		if (atom != vp->element)
			vp->elem_token_valid = FALSE;
	} else {
		atom_str_free_null(&vp->element);
		vp->elem_token_valid = FALSE;
	}

	if (ns != NULL) {
		atom_str_change(&vp->namespace, ns);
	} else {
		atom_str_free_null(&vp->namespace);
	}

	if (vxml_debugging(19)) {
		const char *uri;
		const char *colon = vp->namespace ? ":" : "";

		uri = vxml_parser_namespace_uri(vp, vp->namespace);

		vxml_parser_debug(vp, "VXML current element: \"%s%s%s\" (%s)",
			NULL == vp->namespace ? "" : vp->namespace, colon,
			NULL == vp->element ? "(none)" : vp->element, uri);
	}
}

/*
 * Update current element.
 */
static void
vxml_parser_update_current_element(vxml_parser_t *vp)
{
	if (vp->path != NULL) {
		struct vxml_path_entry *pe = vp->path->data;
		vxml_path_entry_check(pe);
		vxml_parser_set_element(vp, pe->element, pe->namespace);
		vp->elem_token = pe->token;
		vp->elem_token_valid = pe->token_valid;
	} else {
		vxml_parser_set_element(vp, NULL, NULL);
	}
}

/**
 * Ensure that current element's namespace prefix is known.
 *
 * @return TRUE if OK, FALSE otherwise with vp->error set.
 */
static gboolean
vxml_parser_element_prefix_known(vxml_parser_t *vp)
{
	if (
		!(vp->options & VXML_O_NO_NAMESPACES) &&
		vp->namespace != NULL &&
		!vxml_parser_namespace_exists(vp, vp->namespace)
	) {
		vxml_fatal_error_str(vp, VXML_E_UNKNOWN_NAMESPACE, vp->namespace);
		return FALSE;
	}

	return  TRUE;
}

/**
 * Attempt element tokenization.
 *
 * @param vp		the XML parser
 * @param tokens	if non-NULL, additional tokens to consider for elements
 */
static void
vxml_tokenize_element(vxml_parser_t *vp, nv_table_t *tokens, const char *name)
{
	nv_pair_t *nvp = NULL;

	g_assert(name != NULL);

	vp->elem_token_valid = FALSE;

	/*
	 * If they supplied a token table, use that before the parser's defaults.
	 */

	if (tokens != NULL) {
		nvp = nv_table_lookup(tokens, name);
	}
	if (NULL == nvp && vp->tokens != NULL) {
		nvp = nv_table_lookup(vp->tokens, name);
	}

	if (nvp != NULL) {
		struct vxml_token *vt;

		vt = nv_pair_value(nvp);
		g_assert(0 == strcmp(name, vt->name));

		vp->elem_token_valid = TRUE;
		vp->elem_token = vt->id;

		if (vxml_debugging(19))
			vxml_parser_debug(vp, "VXML tokenized \"%s\" as %u", name, vt->id);
	}
}

/**
 * Do we have to notify user on available element text?
 */
static gboolean
vxml_parser_notify_text(const vxml_parser_t *vp, const struct vxml_ops *ops)
{
	if (NULL == ops)
		return FALSE;

	if (0 == vxml_output_size(&vp->out))
		return FALSE;		/* No text data */

	/*
	 * If we have an element token, check whether we have a text callback
	 * for such elements.  Otherwise, we can fallback on the plain element
	 * callback, even if the current element was tokenized.
	 */

	if (vp->elem_token_valid && ops->tokenized_text != NULL)
		return TRUE;

	return ops->plain_text != NULL;
}

/**
 * Strip leading and trailing blanks in UTF-8 text.
 *
 * @param text		start of text to strip
 * @param len_ptr	initial length of text, updated with new length
 *
 * @returns start of new text, and adjusted length in case we stripped.
 */
static const char *
vxml_strip_blanks(char *text, size_t *len_ptr)
{
	guint32 uc;
	guint retlen;
	size_t len;
	char *p;
	gboolean seen_non_blank = FALSE;
	char *last_non_blank;
	const char *first_non_blank;
	const char *end;

	g_assert(text != NULL);
	g_assert(len_ptr != NULL);

	first_non_blank = last_non_blank = p = text;
	len = *len_ptr;		/* In bytes */
	end = text + len;

	if (0 == len)
		return text;

	g_assert(size_is_non_negative(len));

	/*
	 * Text is NUL-terminated, so we can use utf8_decode_char_fast().
	 */

	while (0 != (uc = utf8_decode_char_fast(p, &retlen))) {
		p += retlen;
		len -= retlen;

		g_assert(size_is_non_negative(len));
		g_assert(p <= end);

		if (seen_non_blank) {
			if (!vxml_is_white_space_char(uc))
				last_non_blank = p;				/* next char */
		} else {
			if (!vxml_is_white_space_char(uc)) {
				seen_non_blank = TRUE;
				last_non_blank = p;				/* next char */
				first_non_blank = p - retlen;	/* this char */
			}
		}
	}

	g_assert(size_is_non_negative(ptr_diff(last_non_blank, first_non_blank)));

	if (last_non_blank != p) {
		g_assert(size_is_positive(ptr_diff(p, last_non_blank)));
		g_assert(size_is_positive(ptr_diff(end, last_non_blank)));
		*last_non_blank = '\0';
	}

	*len_ptr = ptr_diff(last_non_blank, first_non_blank);

	return first_non_blank;
}

/**
 * Notify user on available element text.
 */
static void
vxml_parser_do_notify_text(vxml_parser_t *vp,
	const struct vxml_ops *ops, void *data)
{
	const char *text;
	size_t len;

	/*
	 * The text we're passing to callbacks is held in the "static" buffer
	 * of the parser, where it will remain valid until the parser is
	 * re-entered.  Callbacks willing to recurse into the parser need to save
	 * the text or promise to never use the pointer they were given.
	 *
	 * For callback convenience, the text is NUL-terminated.
	 */

	text = vxml_output_start(&vp->out);
	len = vxml_output_size(&vp->out);

	g_assert(len >= 1);						/* There must be something */
	g_assert(text[len - 1] != '\0');		/* No NUL already */

	vxml_output_append(&vp->out, VXC_NUL);	/* NUL-terminated */

	if (vxml_debugging(10)) {
		g_debug("VXML \"%s\" notifying text (%lu bytes) in <%s>, "
			"token is %svalid",
			vp->name, (unsigned long) len, vp->element,
			vp->elem_token_valid ? "" : "in");
	}

	/*
	 * Optionally strip leading and trailing blanks from text.
	 */

	if (vp->options & VXML_O_STRIP_BLANKS) {
		text = vxml_strip_blanks(deconstify_char(text), &len);

		g_assert('\0' == text[len]);		/* Still NUL-terminated */
		g_assert(len <= vxml_output_size(&vp->out) - 1);

		if (vxml_debugging(10)) {
			if (len != vxml_output_size(&vp->out) - 1) {
				const char *previous_end = vxml_output_start(&vp->out) +
					vxml_output_size(&vp->out) - 1;
				const char *current_end = text + len;

				g_debug("VXML \"%s\" stripped blanks "
					"(leading: %s, trailing %s), text down to %lu byte%s",
					vp->name,
					text ==  vxml_output_start(&vp->out) ? "no" : "yes",
					previous_end == current_end ? "no" : "yes",
					(unsigned long) len, 1 == len ? "" : "s");
			}
		}

		if (0 == len)
			return;			/* Nothing left to notify */
	}

	if (vp->elem_token_valid && ops->tokenized_text != NULL) {
		(*ops->tokenized_text)(vp, vp->elem_token, text, len, data);
	} else if (ops->plain_text != NULL) {
		(*ops->plain_text)(vp, vp->element, text, len, data);
	} else {
		g_error("vxml_parser_notify_text() must be called before");
	}
}

/**
 * Do we have to notify user on element start?
 */
static gboolean
vxml_parser_notify_start(const vxml_parser_t *vp, const struct vxml_ops *ops)
{
	if (NULL == ops)
		return FALSE;

	/*
	 * If we have an element token, check whether we have a start callback
	 * for such elements.  Otherwise, we can fallback on the plain element
	 * callback, even if the current element was tokenized.
	 */

	if (vp->elem_token_valid && ops->tokenized_start != NULL)
		return TRUE;

	return ops->plain_start != NULL;
}

/**
 * Notify user on element start.
 */
static void
vxml_parser_do_notify_start(vxml_parser_t *vp, const struct vxml_uctx *ctx)
{
	const struct vxml_ops *ops;
	struct vxml_location saved_location;

	ops = ctx->ops;

	if (vxml_debugging(10)) {
		if (ops->tokenized_end != NULL || ops->plain_end != NULL) {
			g_debug("VXML \"%s\" notifying start of <%s>, token is %svalid",
				vp->name, vp->element, vp->elem_token_valid ? "" : "in");
		}
	}

	/*
	 * The callback for element start can re-enter the parser to only parse
	 * the fragment of the XML contained in the tag itself.
	 *
	 * Upon return from the notification, we need to undo any sub-parsing
	 * settings to resume the document parsing where we left of.
	 */

	/* Save context */
	saved_location = vp->loc;			/* Struct copy */
	vp->flags |= VXML_F_SUBPARSE;

	if (vp->elem_token_valid && ops->tokenized_start != NULL) {
		(*ops->tokenized_start)(vp, vp->elem_token, vp->attrs, ctx->data);
	} else if (ops->plain_start != NULL) {
		(*ops->plain_start)(vp, vp->element, vp->attrs, ctx->data);
	} else {
		g_error("vxml_parser_notify_start() must be called before");
	}

	/*
	 * Upon return, if the VXML_F_SUBPARSE flag is still set, we did not
	 * recurse.
	 */

	if (vp->flags & VXML_F_SUBPARSE) {
		vp->flags &= ~VXML_F_SUBPARSE;
	} else {
		/* Restore context */
		vp->loc = saved_location;		/* Struct copy */
		vxml_parser_update_current_element(vp);
	}
}

/**
 * Notify user on element end, if needed.
 */
static void
vxml_parser_do_notify_end(vxml_parser_t *vp,
	const struct vxml_ops *ops, void *data)
{
	if (NULL == ops || 0 == vp->loc.depth)
		return;

	if (vxml_debugging(10)) {
		if (ops->tokenized_end != NULL || ops->plain_end != NULL) {
			g_debug("VXML \"%s\" notifying end of <%s>, token is %svalid",
				vp->name, vp->element, vp->elem_token_valid ? "" : "in");
		}
	}

	if (vp->elem_token_valid && ops->tokenized_end != NULL) {
		(*ops->tokenized_end)(vp, vp->elem_token, data);
	} else if (ops->plain_end != NULL) {
		(*ops->plain_end)(vp, vp->element, data);
	}
}

/**
 * Swallow everything until the next specified character.
 *
 * @param vp		the XML parser
 * @param fc		the final character after which we shall resume parsing.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_swallow_until(vxml_parser_t *vp, guint32 fc)
{
	guint32 uc;

	while (0 != (uc = vxml_next_char(vp))) {
		if (fc == uc)
			return TRUE;
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Swallow all input until we reach the (ASCII) marker string.
 *
 * @param vp		the XML parser
 * @param mark		the final marker string
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_swallow_until_marker(vxml_parser_t *vp, const char *mark)
{
	guint32 fc;

	fc = mark[0];

	while (vxml_parser_swallow_until(vp, fc)) {
		guint32 nc;
		size_t i = 1;

		while (0 != (nc = mark[i++])) {
			guint32 uc = vxml_next_char(vp);
			if (uc != nc)
				break;
		}

		if (0 == nc)
			return TRUE;
	}

	return FALSE;
}

/**
 * Handle the attribute value.
 *
 * Collects the value in the supplied output buffer, with leading and
 * trailing quotes stripped and a trailing NUL appended.
 *
 * @param vp			the XML parser
 * @param vo			where the attribute value is collected.
 *
 * @return TRUE if handling was successful, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_attrval(vxml_parser_t *vp, struct vxml_output *vo)
{
	guint32 quote;
	guint32 uc;
	unsigned generation;

	vxml_output_check(vo);
	g_assert(0 == vxml_output_size(vo));

	/*
	 * AttValue	::= '"' ([^<&"] | Reference)* '"'
	 *			  | "'" ([^<&'] | Reference)* "'"
	 */

	quote = vxml_next_char(vp);
	generation = vp->last_uc_generation;

	if (VXC_QUOT != quote && VXC_APOS != quote) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_QUOTE, quote);
		return FALSE;
	}

	/*
	 * See comment in vxml_parser_handle_quoted_string() for the usage
	 * of vp->last_uc_generation in the loop below.
	 */

	while (0 != (uc = vxml_next_char(vp))) {
		if (quote == uc && vp->last_uc_generation <= generation) {
			vxml_output_append(vo, VXC_NUL);
			return TRUE;
		} else if (VXC_AMP == uc) {
			if (!vxml_expand_reference(vp, FALSE))
				return FALSE;
		}  else if (VXC_LT == uc) {
			vxml_fatal_error_out(vp, VXML_E_UNEXPECTED_LT);
			return FALSE;
		} else {
			vxml_output_append(vo, uc);
		}
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Handle one element attribute.
 *
 * @param vp			the XML parser
 * @param in_document	true if we are within a document tag (and not a PI)
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_attribute(vxml_parser_t *vp, gboolean in_document)
{
	guint32 uc;
	const char *name;
	gboolean ok = TRUE;

	/*
	 * If there is no attribute yet for the current element, create a new
	 * hash table to hold them.
	 */

	if (NULL == vp->attrs)
		vp->attrs = nv_table_make(TRUE);

	/*
	 * Attribute	::= Name Eq AttValue  
	 * Eq			::= S? '=' S?
	 * AttValue		::=   '"' ([^<&"] | Reference)* '"'
	 *				    | "'" ([^<&'] | Reference)* "'"
	 */

	/*
	 * Build attribute name in output buffer.
	 */

	vxml_output_discard(&vp->out);		/* Discard any previous text */

	if (!vxml_parser_handle_name(vp, &vp->out, VXC_NUL))
		return FALSE;

	/*
	 * The '=' sign can be surrounded by spaces.
	 */

	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	if (VXC_EQ != (uc = vxml_next_char(vp))) {
		vxml_fatal_error_uc(vp, VXML_E_UNEXPECTED_CHARACTER, uc);
		return FALSE;
	}

	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	/*
	 * We saw "name=" so far.  Save the name to be able to parse the value.
	 */

	name = atom_str_get(vxml_output_start(&vp->out));
	vxml_output_discard(&vp->out);

	if (vxml_debugging(18))
		vxml_parser_debug(vp, "vxml_handle_attribute: name is \"%s\"", name);

	if (!vxml_parser_handle_attrval(vp, &vp->out)) {
		atom_str_free(name);
		return FALSE;
	}

	/*
	 * Record the attribute value.
	 *
	 * We substract 1 for the trailing NUL to get the actual value length.
	 * But for the purpose of name/value pairs, we need to supply NULL
	 * if the length of the value is 0.
	 */

	{
		char *value = deconstify_gchar(vxml_output_start(&vp->out));
		size_t len = vxml_output_size(&vp->out) - 1;
		const char *start;

		g_assert('\0' == value[len]);		/* Properly NUL-terminated */

		/*
		 * The xmlns: namespace is handled by the parser and not made
		 * visible to the user.  It is used to declare name spaces.
		 *
		 * The xmlns attribute is used to declare a default namespace
		 * for the tag's content.
		 *
		 * This does not apply to attributes in processing instructions.
		 */

		if (
			in_document &&
			!(vp->options & VXML_O_NO_NAMESPACES) &&
			NULL != (start = is_strprefix(name, VXS_XMLNS))
		) {
			if (*start == ':') {
				/* xmlns:ns='blah' */
				ok = vxml_parser_namespace_decl(vp, start + 1, value, len);
			} else {
				/* xmlns='blah' */
				ok = vxml_parser_namespace_default(vp, value, len);
			}
			goto done;
		}

		start = name;

		/*
		 * Stip name space in attribute names, unless we're running with
		 * no namespace support.
		 */

		if (!(vp->options & VXML_O_NO_NAMESPACES)) {
			const char *local_name;

			local_name = strchr(start, ':');
			if (local_name != NULL) {
				unsigned retlen;
				char *ns = h_strndup(start, local_name - start);

				/*
				 * If a namespace prefix is specified, it must be known.
				 *
				 * The special "xmlns" prefix (which is undeclared) has been
				 * already handled above.
				 */

				if (!vxml_parser_namespace_exists(vp, ns)) {
					vxml_fatal_error_str(vp, VXML_E_UNKNOWN_NAMESPACE, ns);
					hfree(ns);
					goto error;
				}

				start = local_name + 1;
				HFREE_NULL(ns);

				if (vxml_debugging(18)) {
					vxml_parser_debug(vp, "vxml_handle_attribute: "
						"stripped name is \"%s\"", start);
				}

				/*
				 * After stripping, the name must not be empty and it must
				 * start with a valid character, as if the namespace had
				 * not been there.
				 */

				if ('\0' == *start) {
					vxml_fatal_error_str(vp, VXML_E_EMPTY_NAME, name);
					goto error;
				}

				uc = utf8_decode_char_fast(start, &retlen);
				if (!vxml_is_valid_name_start_char(uc) || VXC_COLON == uc) {
					vxml_fatal_error_uc(vp, VXML_E_BAD_CHAR_IN_NAME, uc);
					goto error;
				}
			}
		}

		/*
		 * Warn if an attribute is redefined (ignoring redefinition).
		 *
		 * Note that the conflicts can arise from namespace stripping, so
		 * we warn using the possibly stripped name but log an error with the
		 * original (complete) name.
		 *
		 * In practice, this will very rarely occur.  Qualified attribute
		 * names are really required in the xml: namespace and conflicts
		 * there are unlikely.  For "private" tags in a namespace, attributes
		 * will be namespace-specific and will not mix with those from other
		 * namespaces, so stripping should not create conflicts.
		 *
		 * Conflicts are non-fatal unless the option VXML_O_NO_DUP_ATTR is set.
		 */

		if (nv_table_lookup(vp->attrs, start) != NULL) {
			if (vxml_debugging(0))
				vxml_parser_warn(vp, "duplicate attribute \"%s\"", start);

			if (vp->options & VXML_O_NO_DUP_ATTR) {
				vxml_fatal_error_str(vp, VXML_E_DUP_ATTRIBUTE, name);
				goto error;
			}
		} else {
			nv_table_insert(vp->attrs, start, value, len + 1);
		}

		if (vxml_debugging(18)) {
			vxml_parser_debug(vp, "vxml_handle_attribute: "
				"value is \"%s\"", value);
		}
	}

done:
	vxml_output_discard(&vp->out);
	atom_str_free(name);

	return ok;

error:
	ok = FALSE;
	goto done;
}

/**
 * Check for processing instruction ending.
 *
 * @param vp		the XML parser
 * @param uc		the next character we read
 *
 * @return TRUE if reached the end of a PI, FALSE otherwise.
 */
static gboolean
vxml_parser_pi_has_ended(vxml_parser_t *vp, guint32 uc)
{
	if (VXC_QM == uc) {			/* Reached a '?' */
		guint32 nc = vxml_next_char(vp);

		if (VXC_GT == nc) {		/* Followed by a '>', end of PI */
			vxml_output_discard(&vp->out);	/* Clear any pending text */
			return TRUE;
		}

		vxml_unread_char(vp, nc);
	}

	return FALSE;
}

/**
 * Warn that document-sepcified encoding is being ignored.
 */
static void
vxml_encoding_ignored(const vxml_parser_t *vp, const char *encoding)
{
	vxml_parser_warn(vp,
		"ignoring document-specified encoding \"%s\": keeping intuited \"%s\"",
		encoding, vxml_encoding_to_string(vp->encoding));
}

/**
 * Handle the "<?xml ... ?>" processing instruction (leading "<?xml" read).
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_xml_pi(vxml_parser_t *vp)
{
	guint32 uc;
	gboolean need_space;
	nv_pair_t *nvp;

	/*
	 * The official grammar:
	 *
	 * prolog       ::= XMLDecl Misc* (doctypedecl  Misc*)?
	 * XMLDecl      ::= '<?xml' VersionInfo  EncodingDecl? SDDecl? S? '?>'
	 * VersionInfo  ::= S 'version' Eq ("'" VersionNum "'" | '"' VersionNum '"')
	 * Eq           ::= S? '=' S?
	 * VersionNum   ::= '1.0' | '1.1'
	 * EncodingDecl::= S 'encoding' Eq ('"' EncName '"' | "'" EncName "'")
	 * EncName      ::= [A-Za-z] ([A-Za-z0-9._] | '-')*
	 * SDDecl       ::= S 'standalone' Eq
	 *                  (("'" ('yes' | 'no') "'") | ('"' ('yes' | 'no') '"')) 
	 * Misc         ::= Comment | PI | S  
	 *
	 * In practice, we're going to be much more tolerant and allow any
	 * well-formed attribute.
	 */

	vp->flags |= VXML_F_XML_DECL;

	/*
	 * Handle attributes until we reach the end of the tag ("?>").
	 */

	need_space = TRUE;

	for (;;) {
		uc = vxml_next_char(vp);
		if (vxml_parser_pi_has_ended(vp, uc))
			break;

		if (need_space) {
			if (vxml_parser_swallow_spaces(vp, uc)) {
				need_space = FALSE;
				continue;
			} else {
				vxml_fatal_error_uc(vp, VXML_E_UNEXPECTED_CHARACTER, uc);
				return FALSE;
			}
		} else {
			vxml_unread_char(vp, uc);
		}

		/*
		 * We're not at the end of the processing instruction because "?>"
		 * is handled at the top of the loop.
		 */

		if (!vxml_handle_attribute(vp, FALSE))
			return FALSE;

		need_space = TRUE;
	}

	/*
	 * The processing instruction's attributes are in vp->attrs.
	 *
	 * Our relaxed parsing allows attributes in any order, contrary to
	 * the grammar, but we're not a validating parser.
	 */

	nvp = nv_table_lookup(vp->attrs, "version");

	if (NULL == nvp) {
		vp->major = 1;
		vp->minor = 0;
		vp->versource = VXML_VERSRC_IMPLIED;
	} else {
		const char *version = nv_pair_value_str(nvp);
		guint major, minor;
		if (0 != parse_major_minor(version, NULL, &major, &minor)) {
			vxml_fatal_error_str(vp, VXML_E_INVALID_VERSION, version);
			return FALSE;
		}
		if (major > MAX_INT_VAL(guint8) || minor > MAX_INT_VAL(guint8)) {
			vxml_fatal_error_str(vp, VXML_E_VERSION_OUT_OF_RANGE, version);
			return FALSE;
		}
		vp->major = major;
		vp->minor = minor;
		vp->versource = VXML_VERSRC_EXPLICIT;
	}

	nvp = nv_table_lookup(vp->attrs, "encoding");

	if (nvp != NULL) {
		const char *encoding = nv_pair_value_str(nvp);

		/*
		 * Check whether it's one of the known UTF-xx charsets.
		 */

		if (is_strcaseprefix(encoding, "UTF-8")) {
			if (VXML_ENCSRC_INTUITED == vp->encsource) {
				if (vp->encoding != VXML_ENC_UTF8) {
					vxml_encoding_ignored(vp, encoding);
				} else {
					vp->encsource = VXML_ENCSRC_EXPLICIT;
				}
			} else {
				vp->encoding = VXML_ENC_UTF8;
				vp->encsource = VXML_ENCSRC_EXPLICIT;
			}
		} else if (
			is_strcaseprefix(encoding, "UTF-16") ||
			is_strcaseprefix(encoding, "UCS-2")
		) {
			if (VXML_ENCSRC_INTUITED == vp->encsource) {
				if (!vxml_encoding_is_utf16(vp->encoding)) {
					vxml_encoding_ignored(vp, encoding);
				} else {
					vp->encsource = VXML_ENCSRC_EXPLICIT;
				}
			} else {
				/* Can't have gone that far with a wrong endianness */
				vp->encsource = VXML_ENCSRC_EXPLICIT;
			}
		} else if (
			is_strcaseprefix(encoding, "UTF-32") ||
			is_strcaseprefix(encoding, "UCS-4")
		) {
			if (VXML_ENCSRC_INTUITED == vp->encsource) {
				if (!vxml_encoding_is_utf32(vp->encoding)) {
					vxml_encoding_ignored(vp, encoding);
				} else {
					vp->encsource = VXML_ENCSRC_EXPLICIT;
				}
			} else {
				/* Can't have gone that far with a wrong endianness */
				vp->encsource = VXML_ENCSRC_EXPLICIT;
			}
		} else {
			const char *charset;

			if (!is_ascii_string(encoding)) {
				vxml_fatal_error_str(vp,
					VXML_E_INVALID_CHAR_ENCODING_NAME, encoding);
				return FALSE;
			}

			charset = get_iconv_charset_alias(encoding);

			if (NULL == charset) {
				vxml_fatal_error_str(vp,
					VXML_E_UNKNOWN_CHAR_ENCODING_NAME, encoding);
				return FALSE;
			}

			vp->charset = atom_str_get(charset);
			vp->encoding = VXML_ENC_CHARSET;
			vp->encsource = VXML_ENCSRC_EXPLICIT;
			vxml_parser_reset_buffer_reader(vp);
		}
	} else {
		vp->encsource = VXML_ENCSRC_DEFAULT;
	}

	nvp = nv_table_lookup(vp->attrs, "standalone");

	if (nvp != NULL) {
		const char *standalone = nv_pair_value_str(nvp);

		if (0 == strcasecmp("yes", standalone))
			vp->standalone = TRUE;
	}

	return TRUE;
}

/**
 * Handle a processing instruction (leading "<?" already read).
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_pi(vxml_parser_t *vp)
{
	/*
	 * PI       ::= '<?' PITarget (S (Char* - (Char* '?>' Char*)))? '?>'
	 * PITarget ::= Name - (('X' | 'x') ('M' | 'm') ('L' | 'l'))
	 */

	/*
	 * Read the PITarget as it is.
	 */

	if (!vxml_parser_handle_name(vp, &vp->out, VXC_NUL))
		return FALSE;

	if (vxml_debugging(18)) {
		vxml_parser_debug(vp, "vxml_handle_pi: target is \"%s\"",
			vxml_output_start(&vp->out));
	}

	/*
	 * If we're dealing with <?xml ...?>, it has to be the first tag
	 * of the document.
	 */

	if (0 == strcmp("xml", vxml_output_start(&vp->out))) {
		if (vp->tags != 1) {
			vxml_fatal_error(vp, VXML_E_UNEXPECTED_XML_PI);
			return FALSE;
		}
		return vxml_handle_xml_pi(vp);
	}

	/*
	 * Ignore other processing instructions.
	 */

	vxml_output_discard(&vp->out);

	return vxml_parser_swallow_until_marker(vp, "?>");
}

/**
 * Handle a comment (leading "<!--" already read).
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_comment(vxml_parser_t *vp)
{
	guint32 uc;

	/*
	 * Comment ::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
	 */

	if (vxml_debugging(18))
		vxml_parser_debug(vp, "vxml_handle_comment: begin");

	while (0 != (uc = vxml_next_char(vp))) {
		if (uc != VXC_MINUS)
			continue;
		uc = vxml_next_char(vp);
		if (VXC_NUL == uc)
			break;
		if (uc != VXC_MINUS)
			continue;

		/*
		 * At this stage, we saw "--".
		 *
		 * The grammar above mandates that there be now a '>', in effect
		 * making "--" an invalid sequence of characters in comments.
		 *
		 * If the parsing option VXML_O_STRICT_COMMENTS is requested,
		 * then we're strictly following the grammar.  The default is to
		 * silently allow '--' in comments because it's an easy mistake
		 * to make.
		 */

		uc = vxml_next_char(vp);
		if (uc != VXC_GT) {
			if (vp->options & VXML_O_STRICT_COMMENTS) {
				vxml_fatal_error_uc(vp, VXML_E_EXPECTED_GT, uc);
				return FALSE;
			} else {
				vxml_unread_char(vp, uc);

				/*
				 * If the character was also a '-', then we need to unread
				 * it again, in effect putting '--' back in the input.  It
				 * will be swallowed by the next loop iteration and the
				 * character after it will be inspected as well.  This allows
				 * a long string of consecutive '-' characters, although it
				 * is inefficient to parse that way; which is why the grammar
				 * forbids any '--' not followed by '>'.
				 */

				if (VXC_MINUS == uc)
					vxml_unread_char(vp, uc);
			}
		} else {
			if (vxml_debugging(18))
				vxml_parser_debug(vp, "vxml_handle_comment: end");

			return TRUE;
		}
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Handles a quoted string within a DTD, with optional '%' reference handling.
 *
 * The quoted string is left in the output buffer, with leading and trailing
 * quoting characters removed.
 *
 * @param vp		the XML parser
 * @param vo		the output buffer
 * @param verbatim	TRUE if we must ignore '%' references.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_quoted_string(vxml_parser_t *vp, struct vxml_output *vo,
	gboolean verbatim)
{
	guint32 uc, quote;
	unsigned generation;

	g_assert(0 == vxml_output_size(vo));

	quote = vxml_next_char(vp);
	generation = vp->last_uc_generation;

	if (quote != VXC_QUOT && quote != VXC_APOS) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_QUOTE, quote);
	}

	/*
	 * Citing, http://www.w3.org/TR/xml11, section 4.4.5:
	 * 
	 * "When an entity reference appears in an attribute value, or a parameter
	 * entity reference appears in a literal entity value, its replacement text
	 * MUST be processed in place of the reference itself as though it were part
	 * of the document at the location the reference was recognized, except that
	 * a single or double quote character in the replacement text MUST always be
	 * treated as a normal data character and MUST NOT terminate the literal.
	 * For example, this is well-formed:
	 *
	 *   <!ENTITY % YN '"Yes"' >
	 *   <!ENTITY WhatHeSaid "He said %YN;" >
	 *
	 * while this is not:
	 *
	 *   <!ENTITY EndAttr "27'" >
	 *   <element attribute='a-&EndAttr;>
	 * "
	 *
	 * To support that, we ignore any quote coming out of a buffer that
	 * was generated after the one from which we got the initial quote from.
	 * Indeed, entity expansion is done in buffers whose generation numbers
	 * will be necessarily larger than the buffer from which we started
	 * the expansion.
	 */

	while (0 != (uc = vxml_next_char(vp))) {
		if (quote == uc && vp->last_uc_generation <= generation) {
			vxml_output_append(vo, VXC_NUL);	/* NUL-terminate buffer */
			return TRUE;
		} else if (verbatim) {
			vxml_output_append(vo, uc);
		} else if (VXC_PCT == uc) {
			if (!vxml_expand_pe_reference(vp, TRUE))
				return FALSE;
		} else if (VXC_AMP == uc) {
			if (!vxml_expand_reference(vp, TRUE))	/* Only CharRef */
				return FALSE;
		} else {
			vxml_output_append(vo, uc);
		}
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Check that we are at the end of a tag, allowing optional white space
 * before the closing '>'.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_at_tag_end(vxml_parser_t *vp)
{
	guint32 uc;

	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	uc = vxml_next_char(vp);
	if (VXC_NUL == uc) {
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		return FALSE;
	} else if (uc != VXC_GT) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_GT, uc);
		return FALSE;
	}

	return TRUE;
}

/**
 * Handle external ID, swallowing extra trailing spaces.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_handle_external_id(vxml_parser_t *vp)
{
	enum vxml_parser_token_value token;

	g_assert(0 == vxml_output_size(&vp->out));

	/*
	 * Read upper-cased identifier (SYSTEM or PUBLIC).
	 */

	if (!vxml_parser_next_misc_token(vp, &token, VXML_E_EXPECTED_DECL_TOKEN))
		return FALSE;

	switch (token) {
	case VXT_PUBLIC:
		/*
		 * 'PUBLIC' S PubidLiteral S SystemLiteral
		 *
		 * Grab the PubidLiteral before falling through to get the
		 * remaining SystemLiteral.
		 */
		if (!vxml_parser_swallow_spaces(vp, VXC_NUL))
			return FALSE;
		if (!vxml_parser_handle_quoted_string(vp, &vp->out, TRUE))
			return FALSE;
		/* FALL THROUGH */
	case VXT_SYSTEM:
		/*
		 * 'SYSTEM' S SystemLiteral
		 */
		vxml_output_discard(&vp->out);
		if (!vxml_parser_swallow_spaces(vp, VXC_NUL))
			return FALSE;
		if (!vxml_parser_handle_quoted_string(vp, &vp->out, TRUE))
			return FALSE;
		break;
		default:
			vxml_fatal_error_out(vp, VXML_E_EXPECTED_DECL_TOKEN);
			return FALSE;
	}

	vxml_output_discard(&vp->out);

	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	return TRUE;
}

/**
 * Handle ELEMENT declaration ('<!ELEMENT S Name S' already read).
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_element_decl(vxml_parser_t *vp, const char *name)
{
	/*
	 * FIXME: add proper parsing of declaration.
	 */

	(void) name;

	if (!vxml_parser_swallow_until(vp, VXC_GT))
		return FALSE;

	return TRUE;
}

/**
 * Handle ENTITY declaration ('<!ENTITY (S '%')? S Name S' already read).
 *
 * @param vp			the XML parser
 * @param name			the entity name
 * @param with_percent	whether it's a parameter or general entity
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_entity_decl(vxml_parser_t *vp, const char *name,
	gboolean with_percent)
{
	guint32 uc;

	g_assert(0 == vxml_output_size(&vp->out));

	/*
	 * EntityDecl    ::= GEDecl | PEDecl  
	 * GEDecl        ::= '<!ENTITY' S Name S EntityDef S? '>'
	 * PEDecl        ::= '<!ENTITY' S '%' S Name S PEDef S? '>'
	 * EntityDef     ::= EntityValue | (ExternalID NDataDecl?)
	 * NDataDecl     ::= S 'NDATA' S Name
	 * PEDef         ::= EntityValue | ExternalID  
	 * EntityValue   ::= '"' ([^%&"] | PEReference | Reference)* '"' 
	 *                 | "'" ([^%&'] | PEReference | Reference)* "'"
	 * ExternalID    ::= 'SYSTEM' S SystemLiteral
	 *                 | 'PUBLIC' S PubidLiteral S SystemLiteral
	 */

	uc = vxml_next_char(vp);

	/*
	 * A quote introduces an EntityValue.
	 */

	if (VXC_QUOT == uc || VXC_APOS == uc) {
		nv_table_t *nvt;
		nv_table_t **nvt_ptr;
		size_t len;
		const char *value;

		vxml_unread_char(vp, uc);
		if (!vxml_parser_handle_quoted_string(vp, &vp->out, FALSE))
			return FALSE;

		/*
		 * Record the association 'Name' -> 'EntityValue' in the proper
		 * entity table: if there was a '%' sign, it's a parameter entity,
		 * otherwise it's a general entity.
		 */

		nvt_ptr = with_percent ? &vp->pe_entities : &vp->entities;
		nvt = *nvt_ptr;
		if (NULL == nvt)
			nvt = *nvt_ptr = nv_table_make(FALSE);

		value = vxml_output_start(&vp->out);
		len = vxml_output_size(&vp->out) - 1;

		g_assert('\0' == value[len]);	/* Properly NUL-terminated */

		nv_table_insert(nvt, name, value, len + 1);

		if (vxml_debugging(17)) {
			vxml_parser_debug(vp, "defined %s entity \"%s\" as "
				"\"%s\" (%lu byte%s)",
				with_percent ? "parameter" : "general", name,
				vxml_output_start(&vp->out), (unsigned long) len,
				1 == len ? "" : "s");
		}

		vxml_output_discard(&vp->out);
		goto finish;
	}

	/*
	 * No quote, it's an ExternalID then.
	 */

	if (!vxml_handle_external_id(vp))
		return FALSE;

	/*
	 * For a general entity, there may be an optional NDataDecl part.
	 */

	if (!with_percent) {
		enum vxml_parser_token_value tok;

		uc = vxml_next_char(vp);

		if (!vxml_is_white_space_char(uc)) {
			vxml_unread_char(vp, uc);
			goto finish;
		}

		if (!vxml_parser_skip_spaces(vp))
			return FALSE;

		if (!vxml_parser_next_misc_token(vp, &tok, VXML_E_EXPECTED_NDATA_TOKEN))
			return FALSE;

		if (tok != VXT_NDATA) {
			vxml_fatal_error_out(vp, VXML_E_EXPECTED_NDATA_TOKEN);
			return FALSE;
		}

		if (!vxml_parser_handle_name(vp, &vp->out, VXC_NUL))
			return FALSE;

		vxml_output_discard(&vp->out);
	}

	/*
	 * The trailing part must be: S? '>'
	 */

finish:
	if (!vxml_parser_at_tag_end(vp))
		return FALSE;

	return TRUE;
}

/**
 * Handle ATTLIST declaration ('<!ATTLIST S Name S' already read).
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_attlist_decl(vxml_parser_t *vp, const char *name)
{
	/*
	 * FIXME: add proper parsing of declaration.
	 */

	(void) name;

	/*
	 * The following useless code is there just to prevent an unused
	 * warning on vxml_get_immediate_token();
	 */

	(void) vxml_get_immediate_token;	/* FIXME */

	if (!vxml_parser_swallow_until(vp, VXC_GT))
		return FALSE;

	return TRUE;
}

/**
 * Handle the internal declarations within a DOCTYPE.
 * The leading '[' character was read.
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_int_subset(vxml_parser_t *vp)
{
	/*
	 * intSubset     ::= (markupdecl | DeclSep)*
	 * DeclSep       ::= PEReference | S  
	 * PEReference   ::= '%' Name ';'
	 * markupdecl    ::= elementdecl | AttlistDecl | EntityDecl | NotationDecl
	 *                   | PI | Comment
	 */

	for (;;) {
		guint32 uc;

		if (!vxml_parser_skip_spaces(vp))
			return FALSE;

		uc = vxml_next_char(vp);
		if (VXC_RBRAK == uc)		/* Reached ending ']' */
			return TRUE;

		if (VXC_PCT == uc) {		/* A '%' introduces a PEReference */
			if (!vxml_expand_pe_reference(vp, FALSE))
				break;
			continue;
		} else if (VXC_LT == uc) {	/* A '<' introduces a markupdecl */
			uc = vxml_next_char(vp);
			if (VXC_QM == uc) {
				if (!vxml_handle_pi(vp))
					return FALSE;
				continue;
			} else if (VXC_BANG == uc) {
				/* Already in a <!DOCTYPE...>, so not expecting another */
				if (!vxml_handle_decl(vp, FALSE))
					return FALSE;
				continue;
			} else {
				goto unexpected;
			}
		} else if (VXC_NUL == uc) {
			break;
		}

	unexpected:
		vxml_fatal_error_uc(vp, VXML_E_UNEXPECTED_CHARACTER, uc);
		return FALSE;
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Handle DOCTYPE declaration ('<!DOCTYPE S Name' already read, plus following
 * white space if any present).
 *
 * @return TRUE if OK, FALSE on error with vp->error set.
 */
static gboolean
vxml_parser_handle_doctype_decl(vxml_parser_t *vp, const char *name)
{
	guint32 uc;

	/*
	 * doctypedecl   ::= '<!DOCTYPE' S Name (S  ExternalID)? S?
	 *                   ('[' intSubset ']' S?)? '>'
	 * intSubset     ::= (markupdecl | DeclSep)*
	 * DeclSep       ::= PEReference | S  
	 * PEReference   ::= '%' Name ';'
	 * markupdecl    ::= elementdecl | AttlistDecl | EntityDecl | NotationDecl
	 *                   | PI | Comment
	 * ExternalID    ::= 'SYSTEM' S SystemLiteral
	 *                 | 'PUBLIC' S PubidLiteral S SystemLiteral
	 * SystemLiteral ::= ('"' [^"]* '"') | ("'" [^']* "'")
	 * PubidLiteral	 ::= '"' PubidChar* '"' | "'" (PubidChar - "'")* "'"
	 * PubidChar     ::= #x20 | #xD | #xA | [a-zA-Z0-9] | [-'()+,./:=?;!*#@$_%]
	 */

	if (vxml_debugging(18)) {
		vxml_parser_debug(vp, "vxml_parser_handle_doctype_decl: name is \"%s\"",
			name);
	}

	uc = vxml_next_char(vp);

	if (uc != VXC_LBRAK) {
		if (!vxml_handle_external_id(vp))
			return FALSE;

		uc = vxml_next_char(vp);
	}

	if (uc != VXC_LBRAK) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_LBRAK, uc);
		return FALSE;
	}

	/*
	 * Parse the declarations between '[' and ']'.
	 */

	if (!vxml_parser_handle_int_subset(vp))
		return FALSE;

	if (!vxml_parser_at_tag_end(vp))
		return FALSE;

	return TRUE;
}

/**
 * Handle the inside of a "<![CDATA[" verbatim section, until closing "]]>".
 *
 * All the characters without the section is verbatim text that is sent
 * as-is to the parser's output. There is no markup nor entity processing.
 */
static gboolean
vxml_handle_cdata(vxml_parser_t *vp)
{
	guint32 uc;

	if (vxml_debugging(18))
		vxml_parser_debug(vp, "vxml_handle_cdata: begin");

	while (0 != (uc = vxml_next_char(vp))) {
		if (VXC_RBRAK == uc) {				/* ']' */
			guint32 nc = vxml_next_char(vp);
			if (VXC_RBRAK == nc) {			/* ']' */
				guint32 fc = vxml_next_char(vp);
				if (VXC_GT == fc)			/* '>' */
					goto ended;
				vxml_unread_char(vp, fc);
			}
			vxml_unread_char(vp, nc);
		}
		vxml_output_append(&vp->out, uc);
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;

ended:
	if (vxml_debugging(18))
		vxml_parser_debug(vp, "vxml_handle_cdata: end");

	return TRUE;
}

/**
 * Check that we are at the end of a conditional section, with ']>' to
 * be read (the first ']' marking the real ending sequence was already
 * parsed).
 *
 * @return TRUE if at the end of the section, or FALSE with vp->error set.
 */
static gboolean
vxml_parser_at_cond_close(vxml_parser_t *vp)
{
	guint32 uc;

	uc = vxml_next_char(vp);
	if (uc != VXC_RBRAK) {
		vxml_fatal_error(vp, VXML_E_EXPECTED_RBRAK);
		return FALSE;
	}

	uc = vxml_next_char(vp);
	if (uc != VXC_GT) {
		vxml_fatal_error(vp, VXML_E_EXPECTED_GT);
		return FALSE;
	}

	return TRUE;
}

/**
 * Handle ignored sections of the DTD, up to the first matching ']]>'.
 *
 * Every tag is ignored and conditional sections are also parsed and ignored,
 * to make sure we do not mistakenly end the ignoring process at the first
 * lexical ']]>' token.
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_parser_handle_ignore(vxml_parser_t *vp)
{
	guint32 uc;

	g_assert(uint_is_positive(vp->ignores));

	while (0 != (uc = vxml_next_char(vp))) {
		if (VXC_LT == uc) {
			/*
			 * If we find a '<![' section, go handle it recursively, but
			 * it will be ignored anyway.
			 *
			 * If we reach a ']]>' mark, we've reached the end of the section.
			 */

			uc = vxml_next_char(vp);
			if (VXC_LT == uc || VXC_RBRAK == uc) {
				goto unread_and_next;
			} else if (VXC_BANG == uc) {
				uc = vxml_next_char(vp);
				if (VXC_LT == uc || VXC_RBRAK == uc) {
					goto unread_and_next;
				} else if (VXC_LBRAK == uc) {
					if (!vxml_handle_special(vp, TRUE))
						return FALSE;
				}
			}
		} else if (VXC_RBRAK == uc) {
			uc = vxml_next_char(vp);
			if (VXC_LT == uc) {
				goto unread_and_next;
			} else if (VXC_RBRAK == uc) {
				uc = vxml_next_char(vp);
				if (VXC_LT == uc)
					goto unread_and_next;
				if (VXC_GT == uc)
					return TRUE;
				if (uc != VXC_RBRAK)
					continue;
				vxml_unread_char(vp, uc);
			}
			vxml_unread_char(vp, VXC_RBRAK);
			continue;
		}
		continue;
	unread_and_next:
		vxml_unread_char(vp, uc);
		continue;
	}

	vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
	return FALSE;
}

/**
 * Handle special tags (leading "<![" already read).
 *
 * @param vp		the XML parser
 * @param dtd		TRUE if within the '<!DOCTYPE' declaration (DTD)
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_special(vxml_parser_t *vp, gboolean dtd)
{
	enum vxml_parser_token_value tok;
	guint32 uc;

	if (!dtd) {
		/*
	 	 * Outside the Document Type Definition (DTD) section, the only
	 	 * special tag we can face is the "<![CDATA[" verbatim marker.
		 *
		 * CDSect  ::= CDStart  CData  CDEnd
		 * CDStart ::= '<![CDATA['
		 * CData   ::= (Char* - (Char* ']]>' Char*))
		 * CDEnd   ::= ']]>'
		 */

		if (!vxml_parser_next_misc_token(vp, &tok, VXML_E_EXPECTED_CDATA_TOKEN))
			return FALSE;

		if (tok != VXT_CDATA) {
			vxml_fatal_error_out(vp, VXML_E_EXPECTED_CDATA_TOKEN);
			return FALSE;
		}

		uc = vxml_next_char(vp);
		if (uc != VXC_LBRAK) {
			vxml_fatal_error_out(vp, VXML_E_EXPECTED_LBRAK);
			return FALSE;
		}

		return vxml_handle_cdata(vp);
	}

	/*
	 * Within a DTD section, special tags are INCLUDE and IGNORE.
	 *
	 * conditionalSect ::= includeSect | ignoreSect
	 * includeSect ::= '<![' S? 'INCLUDE' S? '[' extSubsetDecl ']]>'
	 * ignoreSect ::= '<![' S? 'IGNORE' S? '[' ignoreSectContents* ']]>'
	 * ignoreSectContents ::= Ignore ('<![' ignoreSectContents ']]>' Ignore)*
	 * Ignore ::= Char* - (Char* ('<![' | ']]>') Char*) 
	 *
	 * extSubset ::= TextDecl? extSubsetDecl
	 * extSubsetDecl ::= ( markupdecl | conditionalSect | DeclSep)*
	 * TextDecl     ::= '<?xml' VersionInfo? EncodingDecl  S? '?>'
	 */

	g_assert(0 == vxml_output_size(&vp->out));	/* In a DTD, no free text */

skip_spaces:
	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	/*
	 * We special case the parameter entity reference which could expand
	 * into 'INCLUDE' or 'IGNORE'.
	 */

	uc = vxml_next_char(vp);
	if (VXC_PCT == uc) {
		if (!vxml_expand_pe_reference(vp, FALSE))
			return FALSE;
		goto skip_spaces;
	}
	vxml_unread_char(vp, uc);

	if (!vxml_parser_next_misc_token(vp, &tok, VXML_E_EXPECTED_COND_TOKEN))
		return FALSE;

	if (tok != VXT_IGNORE && tok != VXT_INCLUDE) {
		vxml_fatal_error_out(vp, VXML_E_EXPECTED_COND_TOKEN);
		return FALSE;
	}

	vxml_output_discard(&vp->out);

	/*
	 * Now look for the opening '[', marking the beginning of the conditional
	 * section, whose grammar is very close to intSubset so we'll handle
	 * any included section recursively from there.
	 */

	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	uc = vxml_next_char(vp);
	if (uc != VXC_LBRAK) {
		vxml_fatal_error(vp, VXML_E_EXPECTED_LBRAK);
		return FALSE;
	}

	/*
	 * If we're already in an ignoring section, all subsequent INCLUDE are
	 * actually ignored.
	 */

	if (VXT_INCLUDE == tok && 0 == vp->ignores) {
		if (!vxml_parser_handle_int_subset(vp))
			return FALSE;

		/*
		 * The intSubest finished on a ']'.  To close the conditional
		 * section we need to read ']>'.
		 */

		return vxml_parser_at_cond_close(vp);
	} else {
		vp->ignores++;

		if (!vxml_parser_handle_ignore(vp))
			return FALSE;

		g_assert(uint_is_positive(vp->ignores));

		vp->ignores--;
		return TRUE;
	}
}

/**
 * Handle a declaration (leading "<!" already read).
 *
 * This also dispatches comments ("<!--"), and special tags ("<![").
 *
 * @param vp		the XML parser
 * @param doctype	TRUE if a '<!DOCTYPE' declaration is expected
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_decl(vxml_parser_t *vp, gboolean doctype)
{
	enum vxml_parser_token_value token;
	guint32 uc;
	const char *name;
	gboolean seen_pct = FALSE;
	gboolean mandatory_space = TRUE;
	gboolean ret;

	/*
	 * If it starts with '--', it's a comment.
	 * Otherwise it must be an ASCII upper-cased letter to begin a declaration.
	 */

	uc = vxml_next_char(vp);

	if (vxml_debugging(18))
		vxml_parser_debug(vp, "vxml_handle_decl: next char is U+%X '%c'",
			uc, is_ascii_print(uc) ? uc & 0xff : ' ');

	if (VXC_MINUS == uc) {
		uc = vxml_next_char(vp);
		if (VXC_MINUS == uc) {
			return vxml_handle_comment(vp);
		} else {
			vxml_fatal_error_uc(vp, VXML_E_EXPECTED_TWO_MINUS, uc);
			return FALSE;
		}
	} else if (VXC_LBRAK == uc) {
		return vxml_handle_special(vp, !doctype);
	} else {
		vxml_unread_char(vp, uc);
	}

	/*
	 * The complete grammar involving declarations is:
	 *
	 * doctypedecl   ::= '<!DOCTYPE' S Name (S  ExternalID)? S?
	 *                   ('[' intSubset ']' S?)? '>'
	 * intSubset     ::= (markupdecl | DeclSep)*
	 * markupdecl    ::= elementdecl | AttlistDecl | EntityDecl | NotationDecl
	 *                   | PI | Comment
	 * PI            ::= '<?' PITarget (S (Char* - (Char* '?>' Char*)))? '?>'
	 * PITarget	     ::= Name - (('X' | 'x') ('M' | 'm') ('L' | 'l'))
	 * Comment       ::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
	 * DeclSep       ::= PEReference | S  
	 * PEReference   ::= '%' Name ';'
	 * elementdecl   ::= '<!ELEMENT' S Name S contentspec S? '>'
	 * contentspec   ::= 'EMPTY' | 'ANY' | Mixed | children
	 * Mixed         ::= '(' S? '#PCDATA' (S? '|' S? Name)* S? ')*'
	 *                 | '(' S? '#PCDATA' S? ')' 
	 * children      ::= (choice | seq) ('?' | '*' | '+')?
	 * cp            ::= (Name | choice | seq) ('?' | '*' | '+')?
	 * choice        ::= '(' S? cp ( S? '|' S? cp )+ S? ')'
	 * seq           ::= '(' S? cp ( S? ',' S? cp )* S? ')'
	 * AttlistDecl   ::= '<!ATTLIST' S Name AttDef* S? '>'
	 * AttDef        ::= S Name S AttType S DefaultDecl
	 * AttType       ::= StringType | TokenizedType | EnumeratedType
	 * StringType    ::= 'CDATA'
	 * TokenizedType ::= 'ID' | 'IDREF' | 'IDREFS' | 'ENTITY' | 'ENTITIES'
	 *                 | 'NMTOKEN' | 'NMTOKENS'
	 * EnumeratedType::= NotationType | Enumeration
	 * NotationType  ::= 'NOTATION' S '(' S? Name (S? '|' S? Name)* S? ')'
	 * Enumeration   ::= '(' S? Nmtoken (S? '|' S? Nmtoken)* S? ')'
	 * Nmtoken       ::= (NameChar)+
	 * EntityDecl    ::= GEDecl | PEDecl  
	 * GEDecl        ::= '<!ENTITY' S Name S EntityDef S? '>'
	 * PEDecl        ::= '<!ENTITY' S '%' S Name S PEDef S? '>'
	 * EntityDef     ::= EntityValue | (ExternalID NDataDecl?)
	 * NDataDecl     ::= S 'NDATA' S Name
	 * PEDef         ::= EntityValue | ExternalID  
	 * EntityValue   ::= '"' ([^%&"] | PEReference | Reference)* '"' 
	 *                 | "'" ([^%&'] | PEReference | Reference)* "'"
	 * ExternalID    ::= 'SYSTEM' S SystemLiteral
	 *                 | 'PUBLIC' S PubidLiteral S SystemLiteral
	 * SystemLiteral ::= ('"' [^"]* '"') | ("'" [^']* "'")
	 * PubidLiteral	 ::= '"' PubidChar* '"' | "'" (PubidChar - "'")* "'"
	 * PubidChar     ::= #x20 | #xD | #xA | [a-zA-Z0-9] | [-'()+,./:=?;!*#@$_%]
	 */

	/*
	 * Read upper-cased identifier.
	 */

	vxml_output_discard(&vp->out);

	if (!vxml_parser_handle_uppername(vp, &vp->out))
		return FALSE;

	/*
	 * Make sure it's a known declaration token.
	 */

	token = vxml_get_declaration_token(vxml_output_start(&vp->out));

	if (VXT_UNKNOWN == token) {
		vxml_fatal_error_out(vp, VXML_E_EXPECTED_DECL_TOKEN);
		return FALSE;
	}

	/*
	 * When we expect a DOCTYPE, we don't expect any other declaration,
	 * and conversely.
	 */

	if (doctype) {
		if (token != VXT_DOCTYPE) {
			vxml_fatal_error_out(vp, VXML_E_EXPECTED_DOCTYPE_DECL);
			return FALSE;
		}
	} else {
		if (VXT_DOCTYPE == token) {
			vxml_fatal_error_out(vp, VXML_E_NESTED_DOCTYPE_DECL);
			return FALSE;
		}
	}

	if (vxml_debugging(19)) {
		vxml_parser_debug(vp, "vxml_handle_decl: parsed '<!%s'",
			vxml_token_to_string(token));
	}

	vxml_output_discard(&vp->out);

	/*
	 * All the 4 valid declarations begin with the same grammatical form:
	 *
	 *	'<!TOKEN' S Name S ....
	 *
	 * However, ENTITY has two forms:
	 *
	 * 	'<!ENTITY' S Name S .....
	 *	'<!ENTITY' S '%' S Name S ....
	 *
	 * In any case, the token which we parsed is followed by mandatory
	 * space.  Swallow it.
	 */

	if (!vxml_parser_swallow_spaces(vp, VXC_NUL))
		return FALSE;

	if (VXT_ENTITY == token) {
		uc = vxml_next_char(vp);
		if (VXC_PCT == uc) {
			seen_pct = TRUE;
			if (!vxml_parser_swallow_spaces(vp, VXC_NUL))
				return FALSE;
		} else {
			vxml_unread_char(vp, uc);
		}
	}

	/*
	 * Follows Name, then mandatory spaces excepted for DOCTYPE.
	 */

	if (!vxml_parser_handle_name(vp, &vp->out, VXC_NUL))
		return FALSE;

	if (VXT_DOCTYPE == token) {
		uc = vxml_next_char(vp);
		if (VXC_LBRAK == uc || VXC_GT == uc)
			mandatory_space = FALSE;	/* No ExternalID */
		vxml_unread_char(vp, uc);
	}

	if (mandatory_space && !vxml_parser_swallow_spaces(vp, VXC_NUL))
		return FALSE;

	name = atom_str_get(vxml_output_start(&vp->out));
	vxml_output_discard(&vp->out);

	if (vxml_debugging(18)) {
		vxml_parser_debug(vp, "vxml_handle_decl: parsed '<!%s %s%s'",
			vxml_token_to_string(token), seen_pct ? "% " : "", name);
	}

	switch (token) {
	case VXT_DOCTYPE:
		ret = vxml_parser_handle_doctype_decl(vp, name);
		break;
	case VXT_ELEMENT:
		ret = vxml_parser_handle_element_decl(vp, name);
		break;
	case VXT_ENTITY:
		ret = vxml_parser_handle_entity_decl(vp, name, seen_pct);
		break;
	case VXT_ATTLIST:
		ret = vxml_parser_handle_attlist_decl(vp, name);
		break;
	default:
		g_assert_not_reached();
	}

	atom_str_free(name);
	return ret;
}

/**
 * Create new element out of the parser's output buffer.
 */
static void
vxml_parser_new_element(vxml_parser_t *vp)
{
	const char *start;
	char *ns = NULL;

	g_assert(vxml_output_size(&vp->out) != 0);

	/*
	 * For easier processing, NUL-terminate the buffer, which is a valid
	 * UTF-8 string.
	 */

	vxml_output_append(&vp->out, VXC_NUL);

	/*
	 * Strip namespace indication from the element name unless they requested
	 * no namespace support.
	 */

	start = vxml_output_start(&vp->out);

	if (!(vp->options & VXML_O_NO_NAMESPACES)) {
		const char *local_name;

		local_name = strchr(start, ':');
		if (local_name != NULL) {
			ns = h_strndup(start, local_name - start);
			start = local_name + 1;
		}
	}

	vxml_parser_set_element(vp, start, ns);

	HFREE_NULL(ns);
	vxml_output_discard(&vp->out);
}

/**
 * Update element path and parser depth when we enter a new element.
 *
 * @param vp			the XML parser
 * @param name			the element name (string)
 * @param ns			the element's namespace symbolic name (string or NULL)
 * @param token			the element token (only valid if token_valid is TRUE)
 * @param token_valid	whether element was tokenized successfully
 *
 */
static void
vxml_parser_path_enter(vxml_parser_t *vp,
	const char *name, const char *ns, unsigned token, gboolean token_valid)
{
	struct vxml_path_entry *pe;

	vp->loc.depth++;
	vp->glob.depth++;

	/*
	 * The vp->path list contains the (reverse) path of the item, that
	 * is the set of elements we have to traverse to reach the root.
	 */

	pe = walloc(sizeof *pe);
	pe->magic = VXML_PATH_ENTRY_MAGIC;
	pe->element = atom_str_get(name);
	pe->namespace = NULL == ns ? NULL : atom_str_get(ns);
	pe->token = token;
	pe->token_valid = token_valid;

	/*
	 * Numbering starts at 0: our first children will move that to 1.
	 *
	 * If error occurs in text, after some children have been seen, the count
	 * will reflect how many sub-elements we already parsed at this level,
	 * 0 meaning that there was no such sub-elements since the beginning
	 * of the element.
	 */

	pe->children = 0;

	if (vp->path != NULL) {
		struct vxml_path_entry *parent = vp->path->data;

		vxml_path_entry_check(parent);
		g_assert(uint_is_non_negative(parent->children));

		parent->children++;
	}

	vp->path = g_list_prepend(vp->path, pe);
}

/**
 * Update element path and parser depth when we leave an element.
 */
static void
vxml_parser_path_leave(vxml_parser_t *vp)
{
	struct vxml_path_entry *pe;

	if (0 == vp->loc.depth) {
		atom_str_free_null(&vp->element);
		vxml_fatal_error(vp, VXML_E_UNEXPECTED_TAG_END);
		return;
	}

	g_assert(vp->element != NULL);
	g_assert(uint_is_positive(vp->loc.depth));
	g_assert(uint_is_positive(vp->glob.depth));
	g_assert(vp->path != NULL);

	pe = vp->path->data;
	vxml_path_entry_check(pe);

	if (vxml_debugging(18)) {
		vxml_parser_debug(vp, "vxml_parser_path_leave: "
			"leaving '%s' at depth %u (parsed \"%s\")",
			pe->element, vp->loc.depth, vp->element);
	}

	/*
	 * Ensure that tags do match.
	 *
	 * If the element was empty ("<a/>"), then vp->element is still the
	 * same as it was when we entered the element, so no problem will be
	 * detected.
	 *
	 * If the element was not empty ("<a>"), then when we leave the depth
	 * level of that element, we must find a matching closing tag ("</a>").
	 */

	if (0 != strcmp(vp->element, pe->element))
		vxml_fatal_error(vp, VXML_E_INVALID_TAG_NESTING);

	/*
	 * Unless namespace support was disabled, make sure the tag namespaces
	 * match.  Use global URI for comparison, meaning that if "x" and "y"
	 * both refer to the same URI, one can open a tag with <x:a> and close
	 * it with </y:a>.  This is logical since "a" refers to the same tag,
	 * conceptually.
	 */

	if (!(vp->options & VXML_O_NO_NAMESPACES)) {
		const char *start_uri = vxml_parser_namespace_uri(vp, pe->namespace);
		const char *end_uri = vxml_parser_namespace_uri(vp, vp->namespace);

		if (vxml_debugging(18)) {
			vxml_parser_debug(vp, "vxml_parser_path_leave: "
				"leaving namespace '%s' at depth %u (parsed \"%s\")",
				start_uri, vp->loc.depth, end_uri);
		}

		if (0 != strcmp(start_uri, end_uri))
			vxml_fatal_error(vp, VXML_E_INVALID_TAG_NESTING);
	}

	/*
	 * We always use the global depth to manage the symbol table.
	 */

	symtab_leave(vp->namespaces, vp->glob.depth);

	/*
	 * Now remove the element from the path, and update the current element
	 * information for the next item in the path (i.e. the parent element).
	 */

	vp->path = g_list_remove(vp->path, pe);
	vp->loc.depth--;
	vp->glob.depth--;
	vxml_path_entry_free(pe);

	vxml_parser_update_current_element(vp);
}

/**
 * Finish element.
 *
 * This is called when we have fully handled a content-less tag, or when
 * we reach the element's ending tag.
 *
 * @param vp		the XML parser
 * @param ctx		user-supplied context
 */
static void
vxml_parser_end_element(vxml_parser_t *vp, const struct vxml_uctx *ctx)
{
	/*
	 * Notify if necessary, then update the path.
	 */

	vxml_parser_do_notify_end(vp, ctx->ops, ctx->data);
	vxml_parser_path_leave(vp);
}

/**
 * Begin element.
 *
 * This is called when we have fully handled a new tag start, with all its
 * attributes parsed.
 *
 * @param vp		the XML parser
 * @param ops		callbacks to invoke on element start/end and data
 * @param tokens	if non-NULL, additional tokens to consider for elements
 * @param data		additional argument to give to callbacks
 */
static void
vxml_parser_begin_element(vxml_parser_t *vp, const struct vxml_uctx *ctx)
{
	gboolean empty;

	vxml_tokenize_element(vp, ctx->tokens, vp->element);
	vxml_parser_path_enter(vp, vp->element, vp->namespace,
		vp->elem_token, vp->elem_token_valid);

	/*
	 * If a namespace prefix is specified, it must be known now.
	 */

	if (!vxml_parser_element_prefix_known(vp))
		return;

	if (!vxml_parser_notify_start(vp, ctx->ops)) {
		if (vp->elem_no_content)
			vxml_parser_end_element(vp, ctx);
		return;
	}

	/*
	 * Flag empty tags in case they want to re-enter the parser to parse
	 * the content of that tag, without knowing a priori that it will be empty.
	 */

	if (vp->elem_no_content) {
		empty = TRUE;
		vp->flags |= VXML_F_EMPTY_TAG;
	}

	/*
	 * Invoke user callback.
	 */

	vxml_parser_do_notify_start(vp, ctx);

	vp->flags &= ~VXML_F_EMPTY_TAG;

	/*
	 * End element if it was empty.
	 */

	if (empty)
		vxml_parser_end_element(vp, ctx);
}

/**
 * Handle element end (leading "</" characters already read).
 *
 * @param vp		the XML parser
 * @param ctx		user-supplied context
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_tag_end(vxml_parser_t *vp, const struct vxml_uctx *ctx)
{
	guint32 uc;

	/*
	 * ETag	::= '</' Name S? '>'
	 */

	vxml_output_discard(&vp->out);		/* Discard any previous text */

	/*
	 * Build element name in output buffer.
	 */

	if (!vxml_parser_handle_name(vp, &vp->out, VXC_NUL))
		return FALSE;

	vxml_parser_new_element(vp);	/* Update vp->element */

	/*
	 * If a namespace prefix is specified, it must be known now.
	 */

	if (!vxml_parser_element_prefix_known(vp))
		return FALSE;

	if (!vxml_parser_skip_spaces(vp))
		return FALSE;

	if (VXC_GT != (uc = vxml_next_char(vp))) {
		vxml_fatal_error_uc(vp, VXML_E_EXPECTED_GT, uc);
		return FALSE;
	}

	vxml_parser_end_element(vp, ctx);

	return TRUE;
}

/**
 * Check for tag ending.
 *
 * @param vp		the XML parser
 * @param uc		the next character we read
 * @param error		set to TRUE on error when we're not at the end of a tag
 * @param empty		set to TRUE if at end of a tag with no content
 *
 * @return TRUE if reached the end of a tag and no error was encountered,
 * FALSE otherwise with error possibly set.
 */
static gboolean
vxml_parser_tag_has_ended(vxml_parser_t *vp, guint32 uc,
	gboolean *error, gboolean *empty)
{
	if (vxml_debugging(18)) {
		vxml_parser_debug(vp, "vxml_parser_tag_has_ended: char is U+%X '%c'",
			uc, is_ascii_print(uc) ? uc & 0xff : ' ');
	}

	if (VXC_SLASH == uc) {		/* Reached a '/', tag has no content */
		guint32 nc = vxml_next_char(vp);
		*empty = TRUE;
		if (VXC_GT == nc)		/* Followed by a '>', end of tag */
			goto tag_ended;

		/*
		 * '/' was not immediately followed by '>', we have an error.
		 */

		if (0 == nc)
			vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		else if (vxml_is_white_space_char(nc))
			vxml_fatal_error(vp, VXML_E_UNEXPECTED_WHITESPACE);
		else
			vxml_fatal_error_uc(vp, VXML_E_UNEXPECTED_CHARACTER, nc);

		*error = TRUE;
		return FALSE;
	} else if (VXC_GT == uc) {	/* Reached a '>', end of tag */
		*empty = FALSE;
		goto tag_ended;
	} else {
		*error = FALSE;
		return FALSE;
	}

tag_ended:
	if (vxml_debugging(18)) {
		vxml_parser_debug(vp, "vxml_parser_tag_has_ended: yes, %s empty",
			*empty ? "" : "not");
	}

	return TRUE;
}

/**
 * Handle element (leading '<' character already read).
 *
 * @param vp		the XML parser
 * @param ctx		user-supplied context
 *
 * @return TRUE if the handling was successful, FALSE otherwise with
 * vp->error set to the proper error code.
 */
static gboolean
vxml_handle_tag(vxml_parser_t *vp, const struct vxml_uctx *ctx)
{
	guint32 uc;
	gboolean seen_space;
	gboolean need_space;
	gboolean empty, error;

	/*
	 * Starting a new tag, cleanup context.
	 */

	vp->tags++;
	vxml_output_discard(&vp->out);
	nv_table_free_null(&vp->attrs);
	nv_table_free_null(&vp->ns);

	/*
	 * If we haven't seen the leading "<?xml ...?>" and we're at the second
	 * tag already, then it's necessarily an XML 1.0 document.
	 */

	if (!(vp->flags & VXML_F_XML_DECL) && vp->tags > 1) {
		vp->major = 1;
		vp->minor = 0;
		vp->versource = VXML_VERSRC_IMPLIED;
		vp->flags |= VXML_F_XML_DECL;
	}

	uc = vxml_next_char(vp);
	if (VXC_NUL == uc) {
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);
		return FALSE;
	}

	if (vxml_debugging(18))
		vxml_parser_debug(vp, "vxml_handle_tag: next char is U+%X '%c'",
			uc, is_ascii_print(uc) ? uc & 0xff : ' ');

	/*
	 * If we're not starting an element tag, re-route.
	 */

	if (VXC_QM == uc)			/* '?' introduces processing instructions */
		return vxml_handle_pi(vp);
	else if (VXC_BANG == uc)	/* '!' introduces declarations */
		return vxml_handle_decl(vp, TRUE);
	else if (VXC_SLASH == uc)	/* '/' marks the end tag */
		return vxml_handle_tag_end(vp, ctx);

	/*
	 * Count real (content-holding) tags, as opposed to processing instructions,
	 * comments or DTD-related tags.
	 *
	 * This matters when sub-parsing, because we don't want to stop parsing
	 * until we've seen at least one content-holding tag: the sub-root of the
	 * fragment we're parsing.
	 */

	vp->loc.tag_start++;
	vp->glob.tag_start++;

	/*
	 * This is a tag start, collect its name in the output buffer.
	 *
	 * STag			::= '<' Name (S  Attribute)* S? '>'
	 * EmptyElemTag	::= '<' Name (S  Attribute)* S? '/>'	
	 *
	 * Note that there is no space allowed in the grammar between '<' and the
	 * start of the element name.
	 */

	if (!vxml_parser_handle_name(vp, &vp->out, uc))
		return FALSE;

	seen_space = FALSE;

	/*
	 * Handle attributes until we reach the end of the tag.
	 */

	need_space = TRUE;

	for (;;) {
		/*
		 * We are right after the tag name, or after an attribute.
		 */

		uc = vxml_next_char(vp);
		if (vxml_parser_tag_has_ended(vp, uc, &error, &empty)) {
			vp->elem_no_content = empty;
			break;
		} else if (error) {
			return FALSE;
		}

		if (need_space) {
			if (vxml_parser_swallow_spaces(vp, uc)) {
				if (!seen_space) {
					vxml_parser_new_element(vp);
					seen_space = TRUE;	/* Flags that element was created */
				}
				need_space = FALSE;
				continue;
			} else {
				vxml_fatal_error_last(vp, VXML_E_UNEXPECTED_CHARACTER);
				return FALSE;
			}
		} else {
			vxml_unread_char(vp, uc);
		}

		/*
		 * Since we have seen some white space already, then any non-space
		 * means we have additional attributes to process.
		 *
		 * We're not at the end of the tag because "/>" or '>' are handled
		 * at the top of the loop.
		 */

		g_assert(seen_space);

		if (!vxml_handle_attribute(vp, TRUE))
			return FALSE;

		need_space = TRUE;
	}

	/*
	 * Output buffer contains the name of the current element, in UTF-8.
	 * If we saw spaces earlier, we already created the element.
	 */

	if (!seen_space)
		vxml_parser_new_element(vp);

	vxml_parser_begin_element(vp, ctx);

	return TRUE;
}

/**
 * XML parsing engine (re-entrant at will from callbacks).
 *
 * @param vp		the XML parser
 * @param ctx		user-supplied context
 */
static void
vxml_parse_engine(vxml_parser_t *vp, const struct vxml_uctx *ctx)
{
	guint32 uc;

	vxml_location_check(&vp->glob);

	if (vxml_debugging(5)) {
		g_debug("VXML %sparsing \"%s\" depth=%u offset=%lu starting",
			vp->glob.depth != 0 ? "sub-" : "",
			vp->name, vp->glob.depth, (unsigned long) vp->glob.offset);
	}

	/*
	 * Make sure we have a suitable character encoding setup to read the
	 * initial "<?xml" processing instruction, if present, by guessing the
	 * encoding and the byte order by inspecting the start of the document.
	 *
	 * This relies on the fact that there will be no leading whitespace at
	 * the head of the document, which is the case in practice.  If there are
	 * whitespaces nonetheless, we'll guess wrong unless there's a BOM marker
	 * at the start.  It really matters only when the document is not encoded
	 * using the default settings (UTF-8 encoding) and no meta data were
	 * otherwise available to hint at the proper encoding before parsing starts.
	 */

	if (!(vp->flags & VXML_F_INTUITED)) {
		if (!vxml_intuit_encoding(vp))
			goto done;
	}

	if (vxml_debugging(5)) {
		g_debug("VXML %sparsing \"%s\" with %s: %s (%s)",
			vp->glob.depth != 0 ? "sub-" : "",
			vp->name, vxml_encsrc_to_string(vp->encsource),
			vxml_encoding_to_string(vp->encoding),
			vxml_endsrc_to_string(vp->endianness));
	}

	/*
	 * If the depth is not 0, we're re-entering.
	 */

	if (vp->glob.depth != 0) {
		/*
		 * Only "element start" callbacks can re-enter, and this is indicated
		 * by having the callback dispatching code set the internal
		 * VXML_F_SUBPARSE flag before invoking user code.
		 *
		 * This guarantees that the parsing context (as modified below) was
		 * properly saved and will be properly restored upon return.
		 */

		if (!(vp->flags & VXML_F_SUBPARSE))
			g_error("VXML \"%s\" unsupported re-entry from callback", vp->name);

		/*
		 * Since we're about to modify the parsing context, clear the
		 * VXML_F_SUBPARSE flag now so that the original context can be
		 * restored when we return from the recursive parsing.
		 *
		 * We're not fully done with sub-parsing initialization though so
		 * we remember the fact that we are in a sub-parsing call.
		 */

		vp->flags &= ~VXML_F_SUBPARSE;
	}

	/*
	 * We're re-entering the parsing engine so we need to clean the parser
	 * location to start afresh and have all positions logged relatively
	 * to our starting point.
	 */

	vxml_location_init(&vp->loc);

	/*
	 * If re-entering the parsing engine from a "start element" type of
	 * callback when the element was empty, we need to end the element
	 * before resuming processing.
	 *
	 * See vxml_parser_begin_element().
	 */

	if (vp->flags & VXML_F_EMPTY_TAG) {
		vp->flags &= ~VXML_F_EMPTY_TAG;

		if (vxml_debugging(18)) {
			vxml_parser_debug(vp, "recursed withing empty element \"%s\"",
				vp->element);
		}

		goto done;	/* Sub-parsing was short -- no content to parse */
	}

	/*
	 * Reset parser global state.
	 */

	atom_str_free_null(&vp->element);
	atom_str_free_null(&vp->namespace);
	vp->elem_token_valid = FALSE;
	vxml_output_discard(&vp->out);		/* Discard any previous text */
	vxml_output_discard(&vp->entity);

	/*
	 * Parse input.
	 */

	while (0 != (uc = vxml_next_char(vp))) {
		/*
		 * If we are outside any markup:
		 *   - Swallow white spaces.
		 *   - Abort on any character not starting an element or a reference.
		 */

		if (0 == vxml_parser_depth(vp)) {
			if (vxml_is_white_space_char(uc))
				continue;
			if (uc == VXC_LT)			/* A '<' */
				goto tag_start;
			else if (uc != VXC_AMP) {	/* A '&' */
				vxml_fatal_error_uc(vp, VXML_E_UNEXPECTED_CHARACTER, uc);
				goto done;
			}
		}

		/*
		 * Reference handling.
		 */

		if (VXC_AMP == uc) {	/* '&' introduces EntityRef or CharRef */
			if (!vxml_expand_reference(vp, FALSE))
				break;
			continue;
		}

		/*
		 * Markup handling.
		 */

		if (VXC_LT == uc)		/* '<' */
			goto tag_start;

		/*
		 * This is text then.
		 */

		vxml_output_append(&vp->out, uc);
		continue;

		/*
		 * We are at the start of an XML tag.
		 */
	tag_start:

		/*
		 * If we have text data to report to the user, then we must cleanup
		 * our internal state to prepare for possible re-entrance: unread
		 * the tag start character so that, should we re-enter, we can resume
		 * properly.
		 */

		if (vxml_parser_notify_text(vp, ctx->ops)) {
			vxml_unread_char(vp, uc);				/* Put '<' back */
			vxml_parser_do_notify_text(vp, ctx->ops, ctx->data);
			vxml_output_discard(&vp->out);
			continue;
		}

		if (!vxml_handle_tag(vp, ctx))
			goto done;

		if (vxml_debugging(19)) {
			vxml_parser_debug(vp, "back to main loop in <%s>",
				vp->element != NULL ? vp->element : "(none)");
		}

		if (0 == vp->loc.depth && vp->loc.tag_start != 0) {
			if (vxml_debugging(17)) {
				vxml_parser_debug(vp, "ending %sparsing",
					vp->glob.depth != 0 ? "sub-" : "");
			}
			goto done;
		}
	}

	/*
	 * When no specific parser option was given to only focus on a fraction
	 * of the document, the input is truncated if we're still in the middle
	 * of a tag.
	 */

	if (vp->loc.depth != 0)
		vxml_fatal_error(vp, VXML_E_TRUNCATED_INPUT);

	/* FALL THROUGH */

done:
	if (vxml_debugging(5)) {
		g_debug("VXML %sparsing \"%s\" depth=%u offset=%lu exiting (%s)",
			vp->glob.depth != 0 ? "sub-" : "",
			vp->name, vp->glob.depth, (unsigned long) vp->glob.offset,
			vxml_strerror(vp->error));
	}
}

/**
 * Parse data with callbacks on element start / end and element data,
 * until the end of the document.
 *
 * The specified callbacks are invoked when non-NULL, the tokenized form
 * being preferred if the element name can be tokenized.  The supplied
 * token vector is used for tokenization, before the global token table
 * at the parser level, set via vxml_parser_set_tokens().
 *
 * The parser is fully re-entrant by design, and another parsing method may
 * be invoked recursively from callbacks to continue parsing with another
 * method.
 *
 * @attention
 * The supplied vector is referenced by the parser, so it must point
 * to a structure that will still be allocated whilst the parser runs.
 *
 * @param vp		the XML parser
 * @param ops		callbacks to invoke (NULL means no callbacks)
 * @param tvec		token vector
 * @param tlen		length of token vector (amount of items)
 * @param data		additional argument to give to callbacks
 *
 * @return 0 if parsing was OK, an error code otherwise.  Use the
 * vxml_strerror() routine to get a translation into an error message.
 */
vxml_error_t
vxml_parse_callbacks_tokens(vxml_parser_t *vp, const struct vxml_ops *ops,
	struct vxml_token *tvec, size_t tlen, void *data)
{
	struct vxml_uctx ctx;
	nv_table_t *tokens;

	vxml_parser_check(vp);
	g_assert(tvec != NULL);
	g_assert(size_is_positive(tlen));

	tokens = nv_table_make(FALSE);
	vxml_fill_tokens(tokens, tvec, tlen);

	ctx.ops = ops;
	ctx.tokens = tokens;
	ctx.data = data;

	vxml_parse_engine(vp, &ctx);
	nv_table_free(tokens);

	return vp->error;
}

/**
 * Parse data with callbacks on element start / end and element data,
 * until the end of the document.
 *
 * The specified callbacks are invoked when non-NULL.  If element tokens
 * where specified and there are tokenized callbacks defined, the parser
 * will attempt tokenization and if it fails, will not invoke the tokenized
 * callback but the regular by-name element callback instead (if non-NULL).
 *
 * The parser is fully re-entrant by design, and another parsing method may
 * be invoked recursively from callbacks to continue parsing with another
 * method.
 *
 * @param vp		the XML parser
 * @param ops		callbacks to invoke (NULL means NO callback)
 * @param data		additional argument to give to callbacks
 *
 * @return 0 if parsing was OK, an error code otherwise.  Use the
 * vxml_strerror() routine to get a translation into an error message.
 */
vxml_error_t
vxml_parse_callbacks(vxml_parser_t *vp, const struct vxml_ops *ops, void *data)
{
	struct vxml_uctx ctx;

	vxml_parser_check(vp);

	ctx.ops = ops;
	ctx.tokens = NULL;
	ctx.data = data;

	vxml_parse_engine(vp, &ctx);
	return vp->error;
}

/**
 * Parse data until the end of the document, to make sure it is well-formed.
 */
vxml_error_t
vxml_parse(vxml_parser_t *vp)
{
	struct vxml_uctx ctx;

	vxml_parser_check(vp);

	ctx.ops = NULL;
	ctx.tokens = NULL;
	ctx.data = NULL;

	vxml_parse_engine(vp, &ctx);
	return vp->error;
}

/***
 *** XML parser testing.
 ***/

#ifdef VXML_TESTING
const char simple[] =
	"<a v='simple'>this is a <b>simple</b> valid document\n"
	"<!-- comment -->\n"
	"with <c x=\"a\" y='b'/> no XML declaration\n"
	"<![CDATA[but with <x>verbatim<y>&unknown;</x>[[/]] data]]></a>";

const char bad_comment[] = "<!-- comment --->";

const char iso[] =
	"<?xml version='1.1' encoding='ISO-8859-1' standalone='yes'?>\n"
	"<voyels><esc>\xe0\xe9\xee\xf4\xfc\xff</esc>\n"
	"<ent>&#xe0;&#xe9;&#xEE;&#xF4;&#xfc;&#xFF;</ent></voyels>";

const char tricky[] =
	"<?xml version='1.1'?>\n"
	"<!DOCTYPE test [\n"
	"<!ELEMENT test (#PCDATA) >\n"
	"<!ENTITY % YN '\"Yes\"' >\n"
	"<!ENTITY WhatHeSaid \"He said %YN;\" >\n"
	"<!ENTITY % test 'INCLUDE' >\n"
	"<![%test;[\n"
	"<!ENTITY % xx '&#37;zz;'>\n"
	"]]>\n"
	"<!ENTITY % zz '&#60;!ENTITY tricky \"error-prone\" >' >\n"
	"%xx;\n"
	"]>\n"
	"<test>This sample shows a &tricky; method.</test>";

const char evaluation[] =
	"<?xml version='1.1' encoding=\"UTF-8\"?>\n"
	"<!DOCTYPE test [\n"
	"<!ENTITY example \"<p>An ampersand (&#38;#38;) may be escaped\n"
	"numerically (&#38;#38;#38;) or with a general entity\n"
	"(&amp;amp;).</p>\" >\n"
	"]>\n"
	"&example;";

const char blanks[] =
	"<test><![CDATA[   <x> <y>  &unknown; </x> [[/]] data      ]]></test>";

const char subparse[] =
	"<a><b>text-b<c/><d>text-d</d>text2-b</b>text-a</a>";

const char tag_end[] = "</a>";
const char dup_attr[] = "<a dup='1' dup='2'/>";

const char namespaces[] =
	"<a xmlns=''><x:b xmlns:x='urn:x-ns' xmlns:y='urn:y-ns' x:fr='y'>\n"
	"<y:c xmlns='urn:y-ns'><d>foo</d></c></x:b></a>";

const char bad_prefix1[] = "<x:a xmlns:x='urn:x-ns'><b/></z:a>";
const char bad_prefix2[] = "<x:a xmlns:x='urn:x-ns'><y:b/></x:a>";
const char bad_prefix3[] = "<x:a xmlns:x='urn:x-ns' z:foo=''></x:a>";
const char bad_prefix4[] = "<x:a xmlns:x='urn:x-ns' x::foo=''></x:a>";

static void
vxml_run_simple_test(int num, const char *name,
	const char *data, size_t len, guint32 flags, vxml_error_t error)
{
	vxml_error_t e;
	vxml_parser_t *vp;

	g_assert('\0' == data[len]);	/* Given length is correct */

	if (VXML_E_OK == error)
		flags |= VXML_O_FATAL;

	vp = vxml_parser_make(name, flags);
	vxml_parser_add_input(vp, data, len);
	e = vxml_parse(vp);
	if (vxml_debugging(0)) {
		g_info("VXML %s test #%d (simple \"%s\"): %s",
			error == e ? "SUCCESSFUL" : "FAILED",
			num, name, vxml_strerror(e));
	}
	g_assert(error == e);
	vxml_parser_free(vp);
}

static void
vxml_run_ns_simple_test(int num, const char *name,
	const char *data, size_t len, guint32 flags,
	vxml_error_t error_no_ns, vxml_error_t error_with_ns)
{
	char buf[128];

	g_assert(!(flags & VXML_O_NO_NAMESPACES));
	g_assert('\0' == data[len]);	/* Given length is correct */

	gm_snprintf(buf, sizeof buf, "%s (no NS)", name);
	vxml_run_simple_test(num, buf, data, len,
		flags | VXML_O_NO_NAMESPACES, error_no_ns);

	gm_snprintf(buf, sizeof buf, "%s (with NS)", name);
	vxml_run_simple_test(num, buf, data, len, flags, error_with_ns);
}

struct vxml_test_info {
	int num;
	const char *name;
	void *data;
};

static void
vxml_run_callback_test(int num, const char *name,
	const char *data, size_t len, guint32 flags,
	const struct vxml_ops *ops, struct vxml_token *tvec, size_t tlen,
	void *udata)
{
	vxml_error_t e;
	vxml_parser_t *vp;
	struct vxml_test_info info;

	g_assert('\0' == data[len]);	/* Given length is correct */

	info.num = num;
	info.name = name;
	info.data = udata;

	vp = vxml_parser_make(name, flags);
	vxml_parser_add_input(vp, data, len);
	if (tvec != NULL) {
		e = vxml_parse_callbacks_tokens(vp, ops, tvec, tlen, &info);
	} else {
		e = vxml_parse_callbacks(vp, ops, &info);
	}
	if (vxml_debugging(0)) {
		g_info("VXML %s test #%d (callback \"%s\"): %s",
			VXML_E_OK == e ? "SUCCESSFUL" : "FAILED",
			num, name, vxml_strerror(e));
	}
	g_assert(VXML_E_OK == e);
	vxml_parser_free(vp);
}

static void
tricky_text(vxml_parser_t *vp,
	const char *name, const char *text, size_t len, void *data)
{
	struct vxml_test_info *info = data;
	gboolean *seen_text = info->data;

	if (vxml_debugging(0)) {
		g_info("VXML test #%d \"%s\": "
			"tricky_text: got \"%s\" (%lu byte%s) in <%s> at depth %u",
			info->num, info->name, text, (unsigned long) len,
			1 == len ? "" : "s",
			name, vxml_parser_depth(vp));
	}

	*seen_text = TRUE;

	g_assert(name != NULL);
	g_assert(0 == strcmp(text, "This sample shows a error-prone method."));
}

#define T_TEST	0
#define T_P		1
#define T_B		2
#define T_C		3
#define T_D		4

static void
evaluation_text(vxml_parser_t *vp,
	unsigned id, const char *text, size_t len, void *data)
{
	struct vxml_test_info *info = data;
	const char *expected =
		"An ampersand (&) may be escaped\n"
		"numerically (&#38;) or with a general entity\n"
		"(&amp;).";

	if (vxml_debugging(0)) {
		g_info("VXML test #%d \"%s\": "
			"evaluation_text: "
			"got \"%s\" (%lu byte%s) in <token #%u> at depth %u",
			info->num, info->name, text, (unsigned long) len,
			1 == len ? "" : "s",
			id, vxml_parser_depth(vp));
	}

	g_assert(T_P == id);
	g_assert(0 == strcmp(text, expected));
}

static void
blank_text(vxml_parser_t *vp,
	unsigned id, const char *text, size_t len, void *data)
{
	struct vxml_test_info *info = data;
	const char *unstripped = "   <x> <y>  &unknown; </x> [[/]] data      ";
	const char *stripped = "<x> <y>  &unknown; </x> [[/]] data";
	gboolean stripping = GPOINTER_TO_UINT(info->data);

	if (vxml_debugging(0)) {
		g_info("VXML test #%d \"%s\": "
			"blank_text: got \"%s\" (%lu byte%s) in <token #%u> at depth %u",
			info->num, info->name, text, (unsigned long) len,
			1 == len ? "" : "s",
			id, vxml_parser_depth(vp));
	}

	g_assert(T_TEST == id);
	g_assert(0 == strcmp(text, stripping ? stripped : unstripped));
}

static void
subparse_text(vxml_parser_t *vp,
	const char *name, const char *text, size_t len, void *data)
{
	(void) text;
	(void) data;

	g_assert(6 == len);		/* "text-a" */
	g_assert(0 == strcmp("text-a", text));
	g_assert(0 == strcmp("a", name));
	g_assert(1 == vxml_parser_depth(vp));
}

static void
subparse_end(vxml_parser_t *vp, const char *name, void *data)
{
	(void) data;

	g_assert(0 == strcmp("a", name));
	g_assert(1 == vxml_parser_depth(vp));
}

static void
subparse_token_start(vxml_parser_t *vp,
	unsigned id, const nv_table_t *attrs, void *data)
{
	g_assert(NULL == attrs);

	(void) data;

	switch (id) {
	case T_B:
		g_assert(vxml_parser_depth(vp) == 1);
		g_assert(vxml_parser_parent_element(vp) == NULL);
		break;
	case T_C:
		g_assert(vxml_parser_depth(vp) == 2);
		g_assert(0 == strcmp("b", vxml_parser_parent_element(vp)));
		g_assert(NULL == vxml_parser_nth_parent_element(vp, 2));
		g_assert(VXML_E_OK == vxml_parse(vp));
		break;
	case T_D:
		g_assert(vxml_parser_depth(vp) == 2);
		g_assert(0 == strcmp("b", vxml_parser_parent_element(vp)));
		g_assert(0 == strcmp("d", vxml_parser_current_element(vp)));
		break;
	default:
		g_assert_not_reached();
	}
}

static void
subparse_token_text(vxml_parser_t *vp,
	unsigned id, const char *text, size_t len, void *data)
{
	(void) data;
	(void) vp;

	switch (id) {
	case T_B:
	case T_D:
		g_assert(len != 0);
		g_assert(is_strprefix(text, "text-") || is_strprefix(text, "text2-"));
		break;
	default:
		g_assert_not_reached();
	}
}

static void
subparse_token_end(vxml_parser_t *vp, unsigned id, void *data)
{
	(void) data;

	switch (id) {
	case T_B:
		g_assert(vxml_parser_depth(vp) == 1);
		g_assert(vxml_parser_parent_element(vp) == NULL);
		break;
	case T_C:
		g_assert(vxml_parser_depth(vp) == 2);
		g_assert(0 == strcmp("b", vxml_parser_parent_element(vp)));
		g_assert(NULL == vxml_parser_nth_parent_element(vp, 2));
		break;
	case T_D:
		g_assert(vxml_parser_depth(vp) == 2);
		g_assert(0 == strcmp("b", vxml_parser_parent_element(vp)));
		g_assert(0 == strcmp("d", vxml_parser_current_element(vp)));
		break;
	default:
		g_assert_not_reached();
	}
}

static void
subparse_start(vxml_parser_t *vp,
	const char *name, const nv_table_t *attrs, void *data)
{
	vxml_error_t e;
	struct vxml_ops ops;
	struct vxml_token tvec[] = {
		{ "b",	T_B },
		{ "c",	T_C },
		{ "d",	T_D },
	};

	g_assert(NULL == attrs);
	g_assert(0 == strcmp("a", name));
	g_assert(1 == vxml_parser_depth(vp));

	memset(&ops, 0, sizeof ops);
	ops.tokenized_start = subparse_token_start;
	ops.tokenized_end = subparse_token_end;
	ops.tokenized_text = subparse_token_text;

	e = vxml_parse_callbacks_tokens(vp, &ops, tvec, G_N_ELEMENTS(tvec), data);

	g_assert(VXML_E_OK == e);
}

static void
namespace_text(vxml_parser_t *vp,
	const char *name, const char *text, size_t len, void *data)
{
	(void) data;
	(void) len;

	if (0 != strcmp("d", name))
		return;

	g_assert(0 == strcmp("foo", text));
	g_assert(0 == strcmp("urn:y-ns", vxml_parser_current_namespace(vp)));
}

void
vxml_test(void)
{
	struct vxml_ops ops;
	struct vxml_token tvec[] = {
		{ "test",	T_TEST },
		{ "p",		T_P },
	};
	gboolean seen_text;

	vxml_run_simple_test(1,
		"simple", simple, CONST_STRLEN(simple), 0, VXML_E_OK);
	vxml_run_simple_test(2,
		"iso", iso, CONST_STRLEN(iso), 0, VXML_E_OK);
	vxml_run_simple_test(3,
		"bad_comment", bad_comment, CONST_STRLEN(bad_comment), 0, VXML_E_OK);
	vxml_run_simple_test(4,
		"bad_comment", bad_comment, CONST_STRLEN(bad_comment),
		VXML_O_STRICT_COMMENTS, VXML_E_EXPECTED_GT);

	memset(&ops, 0, sizeof ops);
	ops.plain_text = tricky_text;
	seen_text = FALSE;

	vxml_run_callback_test(5, "tricky", tricky, CONST_STRLEN(tricky),
		VXML_O_FATAL, &ops, tvec, 2, &seen_text);
	g_assert(TRUE == seen_text);

	memset(&ops, 0, sizeof ops);
	ops.tokenized_text = evaluation_text;

	vxml_run_callback_test(6, "evaluation", evaluation,
		CONST_STRLEN(evaluation), 0, &ops, tvec, 2, NULL);

	memset(&ops, 0, sizeof ops);
	ops.tokenized_text = blank_text;

	vxml_run_callback_test(7, "blanks", blanks, CONST_STRLEN(blanks),
		0, &ops, tvec, 2, GUINT_TO_POINTER(FALSE));
	vxml_run_callback_test(8, "blanks", blanks, CONST_STRLEN(blanks),
		VXML_O_STRIP_BLANKS, &ops, tvec, 2, GUINT_TO_POINTER(TRUE));

	memset(&ops, 0, sizeof ops);
	ops.plain_start = subparse_start;
	ops.plain_end = subparse_end;
	ops.plain_text = subparse_text;

	vxml_run_callback_test(9, "subparse", subparse, CONST_STRLEN(subparse),
		VXML_O_FATAL, &ops, NULL, 0, NULL);

	vxml_run_simple_test(10, "tag_end",
		tag_end, CONST_STRLEN(tag_end), 0, VXML_E_UNEXPECTED_TAG_END);

	vxml_run_simple_test(11, "dup_attr", dup_attr, CONST_STRLEN(dup_attr),
		0, VXML_E_OK);
	vxml_run_simple_test(12, "dup_attr", dup_attr, CONST_STRLEN(dup_attr),
		VXML_O_NO_DUP_ATTR, VXML_E_DUP_ATTRIBUTE);

	/*
	 * With no namespace support, the test cannot parse correctly because
	 * we open a tag with <y:c> and close it with </c>.  It works with
	 * namespaces thanks to the default namespace being set by <y:c>.
	 */

	memset(&ops, 0, sizeof ops);
	ops.plain_text = namespace_text;

	vxml_run_callback_test(13, "namespaces", namespaces,
		CONST_STRLEN(namespaces), VXML_O_FATAL, &ops, NULL, 0, NULL);
	vxml_run_simple_test(14, "namespaces", namespaces, CONST_STRLEN(namespaces),
		VXML_O_NO_NAMESPACES, VXML_E_INVALID_TAG_NESTING);

	vxml_run_ns_simple_test(15, "bad_prefix1", bad_prefix1,
		CONST_STRLEN(bad_prefix1), 0,
		VXML_E_INVALID_TAG_NESTING, VXML_E_UNKNOWN_NAMESPACE);

	vxml_run_ns_simple_test(16, "bad_prefix2", bad_prefix2,
		CONST_STRLEN(bad_prefix2), 0,
		VXML_E_OK, VXML_E_UNKNOWN_NAMESPACE);

	vxml_run_ns_simple_test(17, "bad_prefix3", bad_prefix3,
		CONST_STRLEN(bad_prefix3), 0,
		VXML_E_OK, VXML_E_UNKNOWN_NAMESPACE);

	vxml_run_ns_simple_test(18, "bad_prefix4", bad_prefix4,
		CONST_STRLEN(bad_prefix4), 0,
		VXML_E_OK, VXML_E_BAD_CHAR_IN_NAME);
}
#else	/* !VXML_TESTING */
void
vxml_test(void)
{
	/* Nothing */
}
#endif	/* VXML_TESTING */

/* vi: set ts=4 sw=4 cindent: */