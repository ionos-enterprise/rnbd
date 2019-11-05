/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Configuration tool for IBNBD driver and IBTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */

#ifndef __H_MISC
#define __H_MISC

#include <ctype.h>	/* for isspace() */
#include <stddef.h>

#define ARRSIZE(x) (sizeof(x) / sizeof(*x))

#define ERR(trm, fmt, ...)			\
	do { \
		if (trm) \
			printf("%s%s", colors[CRED], colors[CBLD]); \
		printf("error: "); \
		if (trm) \
			printf("%s", colors[CNRM]); \
		printf(fmt, ##__VA_ARGS__); \
	} while (0)

struct bit_str {
	const char	*str;
	const char	bits;
	const char	*descr;
};

extern const struct bit_str bits[];

struct path {
	const char *src;
	const char *dst;
};

struct ibnbd_ctx {
	const char *pname;
	const char *name;

	uint64_t size_sect;
	bool size_set;
	short sign;

	enum fmt_type fmt;
	bool fmt_set;

	char io_mode[64];
	bool io_mode_set;

	unsigned int lstmode;
	bool lstmode_set;

	unsigned int ibnbdmode;
	bool ibnbdmode_set;
	bool pname_with_mode;

	const char *access_mode;
	bool access_mode_set;

	const char *help_arg;
	bool help_arg_set;

	struct table_column *clms_devices_clt[CLM_MAX_CNT];
	struct table_column *clms_devices_srv[CLM_MAX_CNT];

	struct table_column *clms_sessions_clt[CLM_MAX_CNT];
	struct table_column *clms_sessions_srv[CLM_MAX_CNT];

	struct table_column *clms_paths_clt[CLM_MAX_CNT];
	struct table_column *clms_paths_srv[CLM_MAX_CNT];

	bool notree_set;
	bool noterm_set;
	bool help_set;
	bool verbose_set;
	bool debug_set;
	bool simulate_set;

	int unit_id;
	bool unit_set;
	char unit[5];

	int prec;
	bool prec_set;

	bool noheaders_set;
	bool nototals_set;
	bool force_set;

	struct path paths[32]; /* lazy */
	int path_cnt;

	const char *from;
	bool from_set;

	bool trm; /* True if STDOUT is a terminal */
};

int get_unit_index(const char *unit, int *index);

int i_to_str_unit(uint64_t d, char *str, size_t len, int unit, int prec);

int i_to_str(uint64_t d, char *str, size_t len, int prec);

void trim(char *s);

/*
 * Convert string [0-9][BKMGTPE] to uint64_t
 * return 0 on success, negative if conversion failed
 */
int str_to_size(const char *str, uint64_t *size);

int i_to_byte_unit(char *str, size_t len, const struct ibnbd_ctx *ctx, uint64_t v, bool humanize);

int byte_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		enum color *clr, void *v, bool humanize);

int sd_state_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		    enum color *clr, void *v, bool humanize);

int sdd_io_mode_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		       enum color *clr, void *v, bool humanize);

int sd_devname_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		      enum color *clr, void *v, bool humanize);

int sd_devpath_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		      enum color *clr, void *v, bool humanize);

int sd_rx_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		 enum color *clr, void *v, bool humanize);

int sd_tx_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
		 enum color *clr, void *v, bool humanize);

int dev_sessname_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
			enum color *clr, void *v, bool humanize);

int ibnbd_path_state_to_str(char *str, size_t len, const struct ibnbd_ctx *ctx,
			    enum color *clr, void *v, bool humanize);

int path_to_sessname(char *str, size_t len, const struct ibnbd_ctx *ctx,
		     enum color *clr, void *v, bool humanize);

int sd_sess_to_direction(char *str, size_t len, const struct ibnbd_ctx *ctx,
			 enum color *clr, void *v, bool humanize);

int act_path_cnt_to_state(char *str, size_t len, const struct ibnbd_ctx *ctx,
			  enum color *clr, void *v, bool humanize);

int sess_side_to_direction(char *str, size_t len, const struct ibnbd_ctx *ctx,
			   enum color *clr, void *v, bool humanize);

int path_sess_to_direction(char *str, size_t len, const struct ibnbd_ctx *ctx,
			   enum color *clr, void *v, bool humanize);

int path_to_shortdesc(char *str, size_t len, const struct ibnbd_ctx *ctx,
		      enum color *clr, void *v, bool humanize);

bool is_path_addr(const char *arg);

bool match_path_addr(const char *left, const char *right);

#define container_of(ptr, type, member) ({                      \
		const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
		(type *)( (char *)__mptr - offsetof(type,member) );})

enum ibnbd_token {

	/* mode */
	TOK_NONE      = 0,
	TOK_CLIENT    = 1,
	TOK_SERVER,
	TOK_BOTH,

	/* object */
	TOK_DEVICES,
	TOK_SESSIONS,
	TOK_PATHS,

	/* commands */
	TOK_LIST,
	TOK_SHOW,
	TOK_MAP,
	TOK_RESIZE,
	TOK_UNMAP,
	TOK_REMAP,
	TOK_DISCONNECT,
	TOK_RECONNECT,
	TOK_ADD,
	TOK_DELETE,
	TOK_READD,

	/* access permissions */
	TOK_RO,
	TOK_RW,
	TOK_MIGRATION,

	TOK_FROM,

	/* output format */
	TOK_XML,
	TOK_CSV,
	TOK_JSON,
	TOK_TERM,

	/* i/o mode */
	TOK_BLOCKIO,
	TOK_FILEIO,

	/* output */
	TOK_HELP,
	TOK_VERBOSE,

	/* units */
	TOK_BYTE,
	TOK_KIB,
	TOK_MIB,
	TOK_GIB,
	TOK_TIB,
	TOK_PIB,
	TOK_EIB,

	/*flags*/
	TOK_NOTREE,
	TOK_NOHEADERS,
	TOK_NOTOTALS,
	TOK_FORCE,
	TOK_NOTERM,

	TOK_ALL,
};

#endif /* __H_MISC */
