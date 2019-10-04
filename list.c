#include <stdio.h>
#include <errno.h>

#include "list.h"

#include "table.h"
#include "misc.h"
#include "ibnbd-sysfs.h"

extern struct table_column *clms_paths_shortdesc[];

int list_devices_term(struct ibnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct ibnbd_ctx *ctx)
{
	struct ibnbd_dev d_total = {
		.rx_sect = 0,
		.tx_sect = 0
	};
	struct ibnbd_sess_dev total = {
		.dev = &d_total,
		.mapping_path = ""
	};
	struct table_fld *flds;
	int i, cs_cnt, dev_num;
	bool nototals_set = ctx->nototals_set;

	cs_cnt = table_clm_cnt(cs);

	for (dev_num = 0; sds[dev_num]; dev_num++)
		;

	if (!table_has_num(cs))
		nototals_set = true;

	flds = calloc((dev_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR(ctx->trm, "not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; sds[i]; i++) {
		table_row_stringify(sds[i], flds + i * cs_cnt, cs, ctx, true, 0);
		total.dev->rx_sect += sds[i]->dev->rx_sect;
		total.dev->tx_sect += sds[i]->dev->tx_sect;
	}

	if (!nototals_set)
		table_row_stringify(&total, flds + i * cs_cnt,
				    cs, ctx, true, 0);

	if (!ctx->noheaders_set)
		table_header_print_term("", cs, ctx->trm);

	for (i = 0; i < dev_num; i++)
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, ctx->trm, 0);

	if (!nototals_set) {
		table_row_print_line("", cs, ctx->trm, 0);
		table_flds_del_not_num(flds + i * cs_cnt, cs);
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, ctx->trm, 0);
	}

	free(flds);

	return 0;
}

void list_devices_csv(struct ibnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct ibnbd_ctx *ctx)
{
	int i;

	if (!sds[0])
		return;

	if (!ctx->noheaders_set)
		table_header_print_csv(cs);

	for (i = 0; sds[i]; i++)
		table_row_print(sds[i], FMT_CSV, "", cs, false, ctx, false, 0);
}

void list_devices_json(struct ibnbd_sess_dev **sds,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx)
{
	int i;

	if (!sds[0])
		return;

	printf("[\n");

	for (i = 0; sds[i]; i++) {
		if (i)
			printf(",\n");
		table_row_print(sds[i], FMT_JSON, "\t\t", cs, false, ctx, false, 0);
	}

	printf("\n\t]");
}

void list_devices_xml(struct ibnbd_sess_dev **sds,
		      struct table_column **cs,
		      const struct ibnbd_ctx *ctx)
{
	int i;

	for (i = 0; sds[i]; i++) {
		printf("\t<device>\n");
		table_row_print(sds[i], FMT_XML, "\t\t", cs, false, ctx, false, 0);
		printf("\t</device>\n");
	}
}

int list_sessions_term(struct ibnbd_sess **sessions,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx)
{
	struct ibnbd_sess total = {
		.act_path_cnt = 0,
		.path_cnt = 0,
		.rx_bytes = 0,
		.tx_bytes = 0,
		.inflights = 0,
		.reconnects = 0
	};
	int i, cs_cnt, sess_num;
	struct table_fld *flds;

	cs_cnt = table_clm_cnt(cs);
	for (sess_num = 0; sessions[sess_num]; sess_num++)
		;

	flds = calloc((sess_num + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR(ctx->trm, "not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; sessions[i]; i++) {
		table_row_stringify(sessions[i], flds + i * cs_cnt, cs,
				    ctx, true, 0);

		total.act_path_cnt += sessions[i]->act_path_cnt;
		total.path_cnt += sessions[i]->path_cnt;
		total.rx_bytes += sessions[i]->rx_bytes;
		total.tx_bytes += sessions[i]->tx_bytes;
		total.inflights += sessions[i]->inflights;
		total.reconnects += sessions[i]->reconnects;
	}

	if (!ctx->nototals_set) {
		table_row_stringify(&total, flds + sess_num * cs_cnt,
				    cs, ctx, true, 0);
	}

	if (!ctx->noheaders_set)
		table_header_print_term("", cs, ctx->trm);

	for (i = 0; sessions[i]; i++) {
		table_flds_print_term("", flds + i * cs_cnt,
				      cs, ctx->trm, 0);
		if (!ctx->notree_set)
			list_paths_term(sessions[i]->paths,
					sessions[i]->path_cnt,
					clms_paths_shortdesc, 1, ctx);
	}

	if (!ctx->nototals_set && table_has_num(cs)) {
		table_row_print_line("", cs, ctx->trm, 0);
		table_flds_del_not_num(flds + sess_num * cs_cnt, cs);
		table_flds_print_term("", flds + sess_num * cs_cnt,
				      cs, ctx->trm, 0);
	}

	free(flds);

	return 0;
}

void list_sessions_csv(struct ibnbd_sess **sessions,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx)
{
	int i;

	if (!ctx->noheaders_set)
		table_header_print_csv(cs);

	for (i = 0; sessions[i]; i++)
		table_row_print(sessions[i], FMT_CSV, "", cs, false, ctx, false, 0);
}

void list_sessions_json(struct ibnbd_sess **sessions,
			struct table_column **cs,
			const struct ibnbd_ctx *ctx)
{
	int i;

	printf("[\n");

	for (i = 0; sessions[i]; i++) {
		if (i)
			printf(",\n");
		table_row_print(sessions[i], FMT_JSON, "\t\t", cs,
				false, ctx, false, 0);
	}

	printf("\n\t]");
}

void list_sessions_xml(struct ibnbd_sess **sessions,
		       struct table_column **cs,
		       const struct ibnbd_ctx *ctx)
{
	int i;

	for (i = 0; sessions[i]; i++) {
		printf("\t<session>\n");
		table_row_print(sessions[i], FMT_XML, "\t\t", cs,
				false, ctx, false, 0);
		printf("\t</session>\n");
	}
}

int list_paths_term(struct ibnbd_path **paths, int path_cnt,
		    struct table_column **cs, int tree,
		    const struct ibnbd_ctx *ctx)
{
	struct ibnbd_path total = {
		.rx_bytes = 0,
		.tx_bytes = 0,
		.inflights = 0,
		.reconnects = 0
	};
	int i, cs_cnt, fld_cnt = 0;
	struct table_fld *flds;

	cs_cnt = table_clm_cnt(cs);

	flds = calloc((path_cnt + 1) * cs_cnt, sizeof(*flds));
	if (!flds) {
		ERR(ctx->trm, "not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < path_cnt; i++) {
		table_row_stringify(paths[i], flds + fld_cnt, cs, ctx, true, 0);

		fld_cnt += cs_cnt;

		total.rx_bytes += paths[i]->rx_bytes;
		total.tx_bytes += paths[i]->tx_bytes;
		total.inflights += paths[i]->inflights;
		total.reconnects += paths[i]->reconnects;
	}

	if (!ctx->nototals_set)
		table_row_stringify(&total, flds + fld_cnt, cs, ctx, true, 0);

	if (!ctx->noheaders_set && !tree)
		table_header_print_term("", cs, ctx->trm);

	fld_cnt = 0;
	for (i = 0; i < path_cnt; i++) {
		table_flds_print_term(
			!tree ? "" : i < path_cnt - 1 ?
			"├─ " : "└─ ", flds + fld_cnt, cs, ctx->trm, 0);
		fld_cnt += cs_cnt;
	}

	if (!ctx->nototals_set && table_has_num(cs) && !tree) {
		table_row_print_line("", cs, ctx->trm, 0);
		table_flds_del_not_num(flds + fld_cnt, cs);
		table_flds_print_term("", flds + fld_cnt, cs, ctx->trm, 0);
	}

	free(flds);

	return 0;
}

void list_paths_csv(struct ibnbd_path **paths,
		    struct table_column **cs,
		    const struct ibnbd_ctx *ctx)
{
	int i;

	if (!ctx->noheaders_set)
		table_header_print_csv(cs);

	for (i = 0; paths[i]; i++)
		table_row_print(paths[i], FMT_CSV, "", cs,
				false, ctx, false, 0);
}

void list_paths_json(struct ibnbd_path **paths,
		     struct table_column **cs,
		     const struct ibnbd_ctx *ctx)
{
	int i;

	printf("\n\t[\n");

	for (i = 0; paths[i]; i++) {
		if (i)
			printf(",\n");
		table_row_print(paths[i], FMT_JSON, "\t\t", cs,
				false, ctx, false, 0);
	}

	printf("\n\t]");
}

void list_paths_xml(struct ibnbd_path **paths,
		    struct table_column **cs,
		    const struct ibnbd_ctx *ctx)
{
	int i;

	for (i = 0; paths[i]; i++) {
		printf("\t<path>\n");
		table_row_print(paths[i], FMT_XML, "\t\t", cs,
				false, ctx, false, 0);
		printf("\t</path>\n");
	}
}

