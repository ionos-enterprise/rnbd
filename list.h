// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Configuration tool for IBNBD driver and IBTRS library.
 *
 * Copyright (c) 2019 1&1 IONOS SE. All rights reserved.
 * Authors: Danil Kipnis <danil.kipnis@cloud.ionos.com>
 *          Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
 */


struct ibnbd_sess_dev;
struct ibnbd_path;
struct ibnbd_sess;
struct table_column;
struct ibnbd_ctx;

int list_devices_term(struct ibnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct ibnbd_ctx *ctx);

void list_devices_csv(struct ibnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct ibnbd_ctx *ctx);

void list_devices_json(struct ibnbd_sess_dev **sds,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx);

void list_devices_xml(struct ibnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct ibnbd_ctx *ctx);

int list_sessions_term(struct ibnbd_sess **sessions,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx);

void list_sessions_csv(struct ibnbd_sess **sessions,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx);

void list_sessions_json(struct ibnbd_sess **sessions,
			struct table_column **cs,
			const struct ibnbd_ctx *ctx);

void list_sessions_xml(struct ibnbd_sess **sessions,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx);

int list_paths_term(struct ibnbd_path **paths, int path_cnt,
		    struct table_column **cs, int tree,
		    const struct ibnbd_ctx *ctx);

void list_paths_csv(struct ibnbd_path **paths,
		    struct table_column **cs,
		    const struct ibnbd_ctx *ctx);

void list_paths_json(struct ibnbd_path **paths,
		     struct table_column **cs,
		     const struct ibnbd_ctx *ctx);

void list_paths_xml(struct ibnbd_path **paths,
		    struct table_column **cs,
		    const struct ibnbd_ctx *ctx);

