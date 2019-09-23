#ifndef __H_MISC
#define __H_MISC

#include <ctype.h>	/* for isspace() */

#define ARRSIZE(x) (sizeof(x) / sizeof(*x))

struct bit_str {
	const char	*str;
	const char	bits;
	const char	*descr;
};

const struct bit_str bits[] = {
	{"B", 0, "Byte"}, {"K", 10, "KiB"}, {"M", 20, "MiB"}, {"G", 30, "GiB"},
	{"T", 40, "TiB"}, {"P", 50, "PiB"}, {"E", 60, "EiB"}
};

static int get_unit_index(const char *unit, int *index)
{
	int i;

	for (i = 0; i < ARRSIZE(bits); i++)
		if (!strcasecmp(bits[i].str, unit)) {
			*index = i;
			return 0;
		}

	return -ENOENT;
}

static int i_to_str_unit(uint64_t d, char *str, size_t len, int unit, int prec)
{
	if (unit >= ARRSIZE(bits))
		return -ENOENT;

	if (!unit)
		return snprintf(str, len, "%" PRIu64, d);

	return snprintf(str, len, "%.*f", prec,
			(double)d / (1L << bits[unit].bits));
}

static int i_to_str(uint64_t d, char *str, size_t len, int prec)
{
	int i;

	for (i = ARRSIZE(bits) - 1; i > 0; i--)
		if (d >> bits[i].bits)
			break;
	if (!i)
		return snprintf(str, len, "%" PRIu64 "%s", d, bits[i].str);

	return snprintf(str, len, "%.*f%s", prec,
			(double)d / (1L << bits[i].bits), bits[i].str);
}

static void trim(char *s)
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
 * Convert string [0-9][BKMGTPE] to uint64_t
 * return 0 on success, negative if conversion failed
 */
static int str_to_size(char *str, uint64_t *size)
{
	char *unit = NULL;
	int index = 0;
	uint64_t num;
	int ret;

	trim(str);
	ret = sscanf(str, "%" SCNu64 "%ms", &num, &unit);
	if (ret < 1)
		return -1;

	if (ret == 2) {
		ret = get_unit_index(unit, &index);
		free(unit);
		if (ret)
			return ret;
	}

	*size = num << bits[index].bits;
	return 0;
}

#ifndef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#endif

#define container_of(ptr, type, member) ({                      \
		const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
		(type *)( (char *)__mptr - offsetof(type,member) );})

#endif /* __H_MISC */
