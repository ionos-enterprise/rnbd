#ifndef __H_TABLE
#define __H_TABLE

#include <ctype.h>	/* for isspace() */

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
	CWHT
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
	unsigned long	s_off;	/* TODO: ugly move to an embedding struct */
};

#define _CLM(str, s_name, name, header, type, tostr, align, color, descr, width, off) \
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
		.hdr_color	= color, \
		.s_off		= off \
	}

#define CLM(str, name, header, type, tostr, align, color, descr, width, off) \
static struct table_column clm_ ## str ## _ ## name = \
	_CLM(str, #name, name, header, type, tostr, align, color, descr, width, off)

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
};

#define CLR(trm, clr, str) \
	trm ? colors[clr] : "", str, trm ? colors[CNRM] : ""

static int clr_print(enum color trm, enum color clr, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	if (trm && clr) {
		ret = printf("%s", colors[clr]);
		ret += vprintf(format, args);
		ret += printf("%s", colors[CNRM]);
	} else {
		ret = vprintf(format, args);
	}
	va_end(args);
	return ret;
}

static int table_row_stringify(void *s, struct table_fld *flds,
			       struct table_column **cs, int humanize,
			       int pre_len)
{
	int clm;
	size_t len;
	void *v;
	struct table_column *c;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		v = (void *)s + c->s_off + c->m_offset;

		if (c->m_tostr) {
			len = c->m_tostr(flds[clm].str, CLM_MAX_WIDTH,
					 &flds[clm].clr, v, humanize);
		} else {
			len = snprintf(flds[clm].str, CLM_MAX_WIDTH, "%s",
				       (char *)v);
			flds[clm].clr = 0;
		}

		if (!clm)
			len += pre_len;

		if (c->m_width < len)
			c->m_width = len;
	}

	return 0;
}

static int table_get_max_h_width(struct table_column **cs)
{
	int hdr_len;
	int max_hdr_len = 0;
	struct table_column *c;

	for (c = *cs; c; c = *++cs) {
		hdr_len = strlen(c->m_header);
		if (max_hdr_len < hdr_len)
			max_hdr_len = hdr_len;
	}

	return max_hdr_len;
}

static void table_entry_print_term(const char *prefix, struct table_fld *flds,
				   struct table_column **cs, int hdr_width,
				   int clr)
{
	int clm;
	struct table_column *c;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		printf("%s%-*s" CLM_DLM, prefix, hdr_width, c->m_header);
		clr_print(clr, flds[clm].clr, "%s\n", flds[clm].str);
	}
}

static int table_fld_print_as_str(struct table_fld *fld,
				   struct table_column *cs,
				   int clr)
{
	if (cs->m_type == FLD_STR)
		return clr_print(clr, fld->clr, "\"%s\"", fld->str);
	else
		return clr_print(clr, fld->clr, "%s", fld->str);
}

static int table_flds_print_term(const char *pre, struct table_fld *flds,
				 struct table_column **cs, int clr, int pwidth)
{
	int clm = 0;
	struct table_column *c = *cs;

	clr_print(clr, flds[clm].clr, (c->clm_align == 'l') ?
			  "%s%-*s" CLM_DLM : "%s%*s" CLM_DLM, pre,
			  c->m_width - pwidth, flds[clm].str);

	for (c = *++cs, clm = 1; c; c = *++cs, clm++)
		clr_print(clr, flds[clm].clr, (c->clm_align == 'l') ?
			  "%-*s" CLM_DLM : "%*s" CLM_DLM,
			  c->m_width, flds[clm].str);
	printf("\n");

	return 0;
}

/* FIXME: escape ',' in strings */
static int table_flds_print_csv(struct table_fld *flds,
				struct table_column **cs, int clr)
{
	int clm;
	struct table_column *c = *cs;

	if (c)
		table_fld_print_as_str(&flds[0], c, clr);

	for (c = *++cs, clm = 1; c; c = *++cs, clm++) {
		printf(",");
		table_fld_print_as_str(&flds[clm], c, clr);
	}

	printf("\n");

	return 0;
}

/*FIXME: escape '"' in strings */
static int table_flds_print_json(const char *prefix, struct table_fld *flds,
				 struct table_column **cs, int clr)
{
	int clm;
	struct table_column *c = *cs;

	printf("%s{", prefix);

	if (c) {
		printf("\n%s\t\"%s\": ", prefix, c->m_name);
		if (!table_fld_print_as_str(&flds[0], c, clr))
			clr_print(clr, flds[0].clr, "null");
	}

	for (c = *++cs, clm = 1; c; c = *++cs, clm++) {
		printf(",\n%s\t\"%s\": ", prefix, c->m_name);
		if (!table_fld_print_as_str(&flds[clm], c, clr))
			clr_print(clr, flds[clm].clr, "null");
	}

	printf("\n%s}", prefix);

	return 0;
}

static int table_flds_print_xml(const char *prefix, struct table_fld *flds,
				struct table_column **cs, int clr)
{
	int clm;
	struct table_column *c = *cs;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		printf("%s<%s>", prefix, c->m_name);
		table_fld_print_as_str(&flds[clm], c, clr);
		printf("</%s>\n", c->m_name);
	}

	return 0;
}

static int table_flds_print(enum fmt_type fmt, const char *prefix,
			    struct table_fld *flds, struct table_column **cs,
			    int clr, int pwidth)
{
	switch (fmt) {
	case FMT_TERM:
		return table_flds_print_term(prefix, flds, cs, clr, pwidth);
	case FMT_XML:
		return table_flds_print_xml(prefix, flds, cs, clr);
	case FMT_CSV:
		return table_flds_print_csv(flds, cs, clr);
	case FMT_JSON:
		return table_flds_print_json(prefix, flds, cs, clr);
	default:
		return -EINVAL;
	}
}

static int table_row_print(void *v, enum fmt_type fmt, const char *pre,
			   struct table_column **cs, int clr, int humanize,
			   size_t pre_len)
{
	struct table_fld flds[CLM_MAX_CNT];

	table_row_stringify(v, flds, cs, humanize, pre_len);
	table_flds_print(fmt, pre, flds, cs, clr, pre_len);

	return 0;
}

static size_t print_line(char *str, size_t len, int width)
{
	size_t cnt = 0;

	while (width-- && cnt + 3 < len)
		cnt += snprintf(str + cnt, len - cnt, "%s", "─");

	return cnt;
}

static int table_row_print_line(const char *pre, struct table_column **clms,
				int clr, int humanize, size_t pre_len)
{
	struct table_fld flds[CLM_MAX_CNT];
	struct table_column *c;
	struct table_column **cs = clms;
	int clm;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		flds[clm].clr = CNRM;
		if (c->m_type == FLD_NUM)
			print_line(flds[clm].str, CLM_MAX_WIDTH, c->m_width);
		else
			flds[clm].str[0] = '\0';
	}

	table_flds_print(FMT_TERM, pre, flds, clms, clr, pre_len);

	return 0;
}

static void table_flds_del_not_num(struct table_fld *flds,
				   struct table_column **cs)
{
	struct table_column *c;
	int clm;

	for (c = *cs, clm = 0; c; c = *++cs, clm++)
		if (c->m_type != FLD_NUM) {
			flds[clm].str[0] = '\0';
			flds[clm].clr = CNRM;
		}
}

static int table_header_print_term(const char *prefix, struct table_column **cs,
				   int clr, char align)
{
	struct table_column *c;
	char al = align;

	printf("%s", prefix);
	for (c = *cs; c; c = *++cs) {
		if (align == 'a')
			al = c->clm_align;

		if (al == 'c')
			clr_print(clr, c->hdr_color, "%*s%*s" CLM_DLM,
				  (c->m_width + c->hdr_width) / 2, c->m_header,
				  (c->m_width - c->hdr_width + 1) / 2, "");
		else if (al == 'r')
			clr_print(clr, c->hdr_color, "%*s" CLM_DLM,
				  c->m_width, c->m_header);
		else
			clr_print(clr, c->hdr_color, "%-*s" CLM_DLM,
				  c->m_width, c->m_header);
	}
	printf("\n");

	return 0;
}

static void table_header_print_csv(struct table_column **cs)
{
	struct table_column *c = *cs;

	if (c)
		printf("%s", c->m_name);

	for (c = *++cs; c; c = *++cs)
		printf(",%s", c->m_name);

	printf("\n");
}

/*
 * Find column with the name @name in the NULL terminated array
 * of columns @clms
 */
static struct table_column *table_find_column(const char *name,
					      struct table_column **clms)
{
	while (*clms) {
		if (!strcmp((*clms)->m_name, name))
			break;
		clms++;
	}

	return *clms;
}

/*
 * remove all spaces from the string @s in place
 */
static void table_trim(char *s)
{
	char *new = s;

	while (*s) {
		if (!isspace(*s))
			*new++ = *s;
		s++;
	}
	*new = *s;
}

/*
 * Parse @delim separated list of fields @names to be selected and
 * add corresponding columns from the list of all columns @all
 * (NULL terminated array) to the array which should contain
 * the selection @sub (last element will be NULL)
 * @sub_len is the max number of elemnts in the target array @sub
 *
 * If parsing succeeds, returns 0.
 * -EINVAL is returned if corresponding column can't be found.
 */
int table_select_columns(const char *names, const char *delim,
			 struct table_column **all,
			 struct table_column **sub,
			 int sub_len)
{
	char *name, *str;
	struct table_column *clm;
	int i = 0;

	str = strdup(names);
	if (!str)
		return -ENOMEM;

	table_trim(str);

	if (!strlen(str))
		return -EINVAL;

	name = strtok(str, delim);
	while (name && i < sub_len) {
		clm = table_find_column(name, all);
		if (!clm) {
			free(str);
			return -EINVAL;
		}
		sub[i++] = clm;
		name = strtok(NULL, delim);
	}

	sub[i] = NULL;

	free(str);

	return 0;
}

#endif /* __H_TABLE */
