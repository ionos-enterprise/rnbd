#ifndef __H_TABLE
#define __H_TABLE

#include <stdlib.h>

enum fld_type {
	FLD_STR,
	FLD_VAL,
	FLD_NUM
};

enum fmt_type {
	FMT_TERM,
	FMT_CSV,
	FMT_JSON,
	FMT_XML
};

enum color {
	CNRM,
	CBLD,
	CUND,
	CRED,
	CGRN,
	CYEL,
	CBLU,
	CMAG,
	CCYN,
	CWHT,
	CDIM,
	CDGR,
	CSTRIKETHROUGH,
};

struct table_column {
	const char	*m_name;
	char		m_header[16];
	int		hdr_width;
	const char	*m_descr;
	enum fld_type	m_type;
	int		m_width;
	unsigned long	m_offset;
	int		(*m_tostr)(char *str, size_t len, enum color *clr,
				   void *v, int humanize);
	char		clm_align;
	enum color	hdr_color;
	enum color	clm_color;
	unsigned long	s_off;	/* TODO: ugly move to an embedding struct */
};

#ifndef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#endif

#define _CLM(str, s_name, name, header, type, tostr, align, h_clr, c_clr, descr, width, off) \
	{ \
		.m_name		= s_name, \
		.m_header	= header, \
		.hdr_width	= sizeof(header) - 1, \
		.m_type		= type, \
		.m_descr	= descr, \
		.m_width	= width, \
		.m_offset	= offsetof(struct str, name), \
		.m_tostr	= tostr, \
		.clm_align	= align, \
		.hdr_color	= h_clr, \
		.clm_color	= c_clr, \
		.s_off		= off \
	}

#define CLM(str, name, header, type, tostr, align, h_clr, c_clr, descr, width, off) \
struct table_column clm_ ## str ## _ ## name = \
	_CLM(str, #name, name, header, type, tostr, align, h_clr, c_clr, descr, width, off)

#define CLM_MAX_WIDTH 128
#define CLM_MAX_CNT 50
#define CLM_DLM "  "

struct table_fld {
	char str[CLM_MAX_WIDTH];
	enum color clr;
};

static const char * const colors[] = {
	[CNRM] = "\x1B[0m",
	[CBLD] = "\x1B[1m",
	[CUND] = "\x1B[4m",
	[CRED] = "\x1B[31m",
	[CGRN] = "\x1B[32m",
	[CYEL] = "\x1B[33m",
	[CBLU] = "\x1B[34m",
	[CMAG] = "\x1B[35m",
	[CCYN] = "\x1B[36m",
	[CWHT] = "\x1B[37m",
	[CDIM] = "\x1B[2m",
	[CDGR] = "\x1B[90m",
	[CSTRIKETHROUGH] = "\x1B[9m\x1B[90m",
};

#define CLR(trm, clr, str) \
	trm ? colors[clr] : "", str, trm ? colors[CNRM] : ""

int clr_print(enum color trm, enum color clr, const char *format, ...);

int table_row_stringify(void *s, struct table_fld *flds,
			struct table_column **cs, int humanize,
			int pre_len);

int table_get_max_h_width(struct table_column **cs);


void table_entry_print_term(const char *prefix, struct table_fld *flds,
			    struct table_column **cs, int hdr_width, int clr);


int table_fld_print_as_str(struct table_fld *fld, struct table_column *cs,
			   int clr);

int table_flds_print_term(const char *pre, struct table_fld *flds,
			  struct table_column **cs, int clr, int pwidth);

int table_flds_print_csv(struct table_fld *flds,
			 struct table_column **cs, int clr);

int table_flds_print_json(const char *prefix, struct table_fld *flds,
			  struct table_column **cs, int clr);

int table_flds_print_xml(const char *prefix, struct table_fld *flds,
			 struct table_column **cs, int clr);

int table_flds_print(enum fmt_type fmt, const char *prefix,
		     struct table_fld *flds, struct table_column **cs,
		     int clr, int pwidth);

int table_row_print(void *v, enum fmt_type fmt, const char *pre,
		    struct table_column **cs, int clr, int humanize,
		    size_t pre_len);

size_t print_line(char *str, size_t len, int width);

int table_row_print_line(const char *pre, struct table_column **clms,
			 int clr, int humanize, size_t pre_len);

void table_flds_del_not_num(struct table_fld *flds,
			    struct table_column **cs);

int table_header_print_term(const char *prefix, struct table_column **cs,
			    int clr, char align);

void table_header_print_csv(struct table_column **cs);
/*
 * Find column with the name @name in the NULL terminated array
 * of columns @clms
 */
struct table_column *table_find_column(const char *name,
				       struct table_column **clms);
/*
 * remove all spaces from the string @s in place
 */
void table_trim(char *s);
/*
 * Parse @delim separated list of fields @names to be selected and
 * add corresponding columns from the list of all columns @all
 * (NULL terminated array); to the array which should contain
 * the selection @sub (last element will be NULL)
 * @sub_len is the max number of elemnts in the target array @sub
 *
 * If parsing succeeds, returns 0.
 * -EINVAL is returned if corresponding column can't be found.
 */
int table_select_columns(const char *names, const char *delim,
			 struct table_column **all,
			 struct table_column **sub,
			 int sub_len);
int table_extend_columns(const char *names, const char *delim,
			 struct table_column **all,
			 struct table_column **cs,
			 int sub_len);

int table_tbl_print_term(const char *prefix, struct table_column **clm,
			 int clr);
#endif /* __H_TABLE */
