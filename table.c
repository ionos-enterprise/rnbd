// SPDX-License-Identifier: GPL-2.0-or-later
#include "table.h"
#include <ctype.h>	/* for isspace(); */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int clr_print(enum color trm, enum color clr, const char *format, ...)
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

static const char * const fld_fmt_str[] = {
	[FLD_STR] = "%s",
	[FLD_VAL] = "%d",
	[FLD_INT] = "%d",
	[FLD_LLU] = "%" PRIu64,
};

int table_row_stringify(void *s, struct table_fld *flds,
			struct table_column **cs, int humanize,
			int pre_len)
{
	struct table_column *c;
	size_t len;
	int clm;
	void *v;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		v = (void *)s + c->s_off + c->m_offset;

		if (c->m_tostr) {
			len = c->m_tostr(flds[clm].str, CLM_MAX_WIDTH,
					 &flds[clm].clr, v, humanize);
		} else {
			if (c->m_type == FLD_INT || c->m_type == FLD_VAL)
				len = snprintf(flds[clm].str, CLM_MAX_WIDTH,
					       fld_fmt_str[c->m_type],
					       *(int *)v);
			else if (c->m_type == FLD_LLU)
				len = snprintf(flds[clm].str, CLM_MAX_WIDTH,
					       fld_fmt_str[c->m_type],
					       *(uint64_t *)v);
			else
				len = snprintf(flds[clm].str, CLM_MAX_WIDTH,
					       fld_fmt_str[c->m_type],
					       (char *)v);

			flds[clm].clr = c->clm_color;
		}

		if (!clm)
			len += pre_len;

		if (c->m_width < len)
			c->m_width = len;
	}

	return 0;
}

int table_get_max_h_width(struct table_column **cs)
{
	struct table_column *c;
	int max_hdr_len = 0;
	int hdr_len;

	for (c = *cs; c; c = *++cs) {
		hdr_len = strlen(c->m_header);
		if (max_hdr_len < hdr_len)
			max_hdr_len = hdr_len;
	}

	return max_hdr_len;
}

void table_entry_print_term(const char *prefix, struct table_fld *flds,
			    struct table_column **cs, int hdr_width,
			    int trm)
{
	struct table_column *c;
	int clm;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		printf("%s%-*s" CLM_DLM, prefix, hdr_width, c->m_header);
		clr_print(trm, flds[clm].clr, "%s\n", flds[clm].str);
	}
}

int table_fld_print_as_str(struct table_fld *fld,
			   struct table_column *cs, int trm)
{
	if (cs->m_type == FLD_STR)
		return clr_print(trm, fld->clr, "\"%s\"", fld->str);
	else
		return clr_print(trm, fld->clr, "%s", fld->str);
}

int table_flds_print_term(const char *pre, struct table_fld *flds,
			  struct table_column **cs, int trm, int pwidth)
{
	struct table_column *c = *cs;
	int clm = 0;

	if (!c)
		return 0;

	clr_print(trm, flds[clm].clr,
		  (c->clm_align == 'l') ? "%s%-*s" CLM_DLM : "%s%*s" CLM_DLM,
		  pre, c->m_width - pwidth, flds[clm].str);

	for (c = *++cs, clm = 1; c; c = *++cs, clm++)
		clr_print(trm, flds[clm].clr, (c->clm_align == 'l') ?
			  "%-*s" CLM_DLM : "%*s" CLM_DLM,
			  c->m_width, flds[clm].str);
	printf("\n");

	return 0;
}

/* FIXME: escape ',' in strings */
int table_flds_print_csv(struct table_fld *flds,
			 struct table_column **cs, int trm)
{
	struct table_column *c = *cs;
	int clm;

	if (c)
		table_fld_print_as_str(&flds[0], c, trm);

	for (c = *++cs, clm = 1; c; c = *++cs, clm++) {
		printf(",");
		table_fld_print_as_str(&flds[clm], c, trm);
	}

	printf("\n");

	return 0;
}

/*FIXME: escape '"' in strings */
int table_flds_print_json(const char *prefix, struct table_fld *flds,
			  struct table_column **cs, int trm)
{
	struct table_column *c = *cs;
	int clm;

	printf("%s{", prefix);

	if (c) {
		printf("\n%s\t\"%s\": ", prefix, c->m_name);
		if (!table_fld_print_as_str(&flds[0], c, trm))
			clr_print(trm, flds[0].clr, "null");
	}

	for (c = *++cs, clm = 1; c; c = *++cs, clm++) {
		printf(",\n%s\t\"%s\": ", prefix, c->m_name);
		if (!table_fld_print_as_str(&flds[clm], c, trm))
			clr_print(trm, flds[clm].clr, "null");
	}

	printf("\n%s}", prefix);

	return 0;
}

int table_flds_print_xml(const char *prefix, struct table_fld *flds,
			 struct table_column **cs, int trm)
{
	struct table_column *c = *cs;
	int clm;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		printf("%s<%s>", prefix, c->m_name);
		table_fld_print_as_str(&flds[clm], c, trm);
		printf("</%s>\n", c->m_name);
	}

	return 0;
}

int table_flds_print(enum fmt_type fmt, const char *prefix,
		     struct table_fld *flds, struct table_column **cs,
		     int trm, int pwidth)
{
	switch (fmt) {
	case FMT_TERM:
		return table_flds_print_term(prefix, flds, cs, trm, pwidth);
	case FMT_XML:
		return table_flds_print_xml(prefix, flds, cs, trm);
	case FMT_CSV:
		return table_flds_print_csv(flds, cs, trm);
	case FMT_JSON:
		return table_flds_print_json(prefix, flds, cs, trm);
	default:
		return -EINVAL;
	}
}

int table_row_print(void *v, enum fmt_type fmt, const char *pre,
		    struct table_column **cs, int trm, int humanize,
		    size_t pre_len)
{
	struct table_fld flds[CLM_MAX_CNT];

	table_row_stringify(v, flds, cs, humanize, pre_len);
	table_flds_print(fmt, pre, flds, cs, trm, pre_len);

	return 0;
}

size_t print_line(char *str, size_t len, int width)
{
	size_t cnt = 0;

	while (width-- && cnt + 3 < len)
		cnt += snprintf(str + cnt, len - cnt, "%s", "─");

	return cnt;
}

int table_row_print_line(const char *pre, struct table_column **clms,
			 int trm, int humanize, size_t pre_len)
{
	struct table_column **cs = clms, *c;
	struct table_fld flds[CLM_MAX_CNT];
	int clm;

	for (c = *cs, clm = 0; c; c = *++cs, clm++) {
		flds[clm].clr = CNRM;
		if (c->m_type == FLD_INT || c->m_type == FLD_LLU)
			print_line(flds[clm].str, CLM_MAX_WIDTH, c->m_width);
		else
			flds[clm].str[0] = '\0';
	}

	table_flds_print(FMT_TERM, pre, flds, clms, trm, pre_len);

	return 0;
}

bool table_has_num(struct table_column **cs)
{
	int i;

	for (i = 0; cs[i]; i++)
		if (cs[i]->m_type != FLD_STR && cs[i]->m_type != FLD_VAL)
			return true;

	return false;
}

void table_flds_del_not_num(struct table_fld *flds,
			    struct table_column **cs)
{
	struct table_column *c;
	int clm;

	for (c = *cs, clm = 0; c; c = *++cs, clm++)
		if (c->m_type != FLD_INT && c->m_type != FLD_LLU) {
			flds[clm].str[0] = '\0';
			flds[clm].clr = CNRM;
		}
}

int table_header_print_term(const char *prefix, struct table_column **cs,
			    int trm, char align)
{
	struct table_column *c;
	char al = align;

	printf("%s", prefix);
	for (c = *cs; c; c = *++cs) {
		if (align == 'a')
			al = c->clm_align;

		if (al == 'c')
			clr_print(trm, c->hdr_color, "%*s%*s" CLM_DLM,
				  (c->m_width + c->hdr_width) / 2, c->m_header,
				  (c->m_width - c->hdr_width + 1) / 2, "");
		else if (al == 'r')
			clr_print(trm, c->hdr_color, "%*s" CLM_DLM,
				  c->m_width, c->m_header);
		else
			clr_print(trm, c->hdr_color, "%-*s" CLM_DLM,
				  c->m_width, c->m_header);
	}
	printf("\n");

	return 0;
}

void table_header_print_csv(struct table_column **cs)
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
struct table_column *table_find_column(const char *name,
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
void table_trim(char *s)
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
	struct table_column *clm;
	char *name, *str;
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

int table_clm_cnt(struct table_column **cs)
{
	int i = 0;

	while (cs[i])
		i++;

	return i;
}

static int contains(struct table_column *s, struct table_column **cs)
{
	int i = 0;

	while (cs[i]) {
		if (s == cs[i])
			return 1;
		i++;
	}

	return 0;
}

int table_extend_columns(const char *arg, const char *delim,
			 struct table_column **all,
			 struct table_column **cs,
			 int sub_len)
{
	struct table_column *sub[CLM_MAX_CNT];
	const char *names = arg;
	int rc, i;

	if (*arg == '+' || *arg == '-')
		names = arg + 1;

	rc = table_select_columns(names, delim, all, sub, sub_len);
	if (rc)
		return rc;

	if (*arg == '-') {
		int k = 0;

		for (i = 0; cs[i]; i++)
			if (!contains(cs[i], sub))
				cs[k++] = cs[i];
		cs[k] = NULL;
	} else {
		if (*arg == '+')
			cs = &cs[table_clm_cnt(cs)];

		for (i = 0; sub[i]; i++)
			cs[i] = sub[i];
		cs[i] = NULL;
	}

	return 0;
}

#define CLM_LST(m_name, m_header, m_width, m_type, tostr, align, h_clr, c_clr,\
		m_descr) \
	CLM(table_column, m_name, m_header, m_type, tostr, \
	    align, h_clr, c_clr, m_descr, m_width, 0)

static int pstr_to_str(char *str, size_t len, enum color *clr, void *v,
		       int humanize)
{
	*clr = 0;
	return snprintf(str, len, "%s", *(char **)v);
}

CLM_LST(m_name, "Field", 14, FLD_STR, pstr_to_str, 'l', CBLD, CNRM, "");
CLM_LST(m_header, "Header", 13, FLD_STR, NULL, 'l', CBLD, CNRM, "");
CLM_LST(m_descr, "Description", 50, FLD_STR, pstr_to_str, 'l', CBLD, CNRM, "");

static struct table_column *l_clmns[] = {
	&clm_table_column_m_name,
	&clm_table_column_m_header,
	&clm_table_column_m_descr,
	NULL
};

int table_tbl_print_term(const char *prefix, struct table_column **clm, int trm)
{
	int row = 0;

	table_header_print_term(prefix, l_clmns, trm, 'a');
	while (clm[row]) {
		table_row_print(clm[row], FMT_TERM, prefix, l_clmns, trm, 1, 0);
		row++;
	}

	return 0;
}
