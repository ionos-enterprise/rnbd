// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Configuration tool for RNBD driver and RTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */


struct rnbd_sess_dev;
struct rnbd_path;
struct rnbd_sess;
struct table_column;
struct rnbd_ctx;

int list_devices_term(struct rnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct rnbd_ctx *ctx);

void list_devices_csv(struct rnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct rnbd_ctx *ctx);

void list_devices_json(struct rnbd_sess_dev **sds,
		       struct table_column **cs,
		       const struct rnbd_ctx *ctx);

void list_devices_xml(struct rnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct rnbd_ctx *ctx);

int list_sessions_term(struct rnbd_sess **sessions,
		       struct table_column **cs,
		       const struct rnbd_ctx *ctx);

void list_sessions_csv(struct rnbd_sess **sessions,
		       struct table_column **cs,
		       const struct rnbd_ctx *ctx);

void list_sessions_json(struct rnbd_sess **sessions,
			struct table_column **cs,
			const struct rnbd_ctx *ctx);

void list_sessions_xml(struct rnbd_sess **sessions,
		       struct table_column **cs,
		       const struct rnbd_ctx *ctx);

int list_paths_term(struct rnbd_path **paths, int path_cnt,
		    struct table_column **cs, int tree,
		    const struct rnbd_ctx *ctx);

void list_paths_csv(struct rnbd_path **paths,
		    struct table_column **cs,
		    const struct rnbd_ctx *ctx);

void list_paths_json(struct rnbd_path **paths,
		     struct table_column **cs,
		     const struct rnbd_ctx *ctx);

void list_paths_xml(struct rnbd_path **paths,
		    struct table_column **cs,
		    const struct rnbd_ctx *ctx);

