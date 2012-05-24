/* author: jianing yang <jianingy.yang@gmail.com> */

#include <postgres.h>
#include <fmgr.h>
#include <utils/inet.h>
#include <nodes/execnodes.h>

typedef struct {
    uint32_t start[2], end[2];
    uint32_t current, size, shift;
} netblock_context;

#define ip_family(inetptr) \
	(((inet_struct *) VARDATA_ANY(inetptr))->family)

#define ip_bits(inetptr) \
	(((inet_struct *) VARDATA_ANY(inetptr))->bits)

#define ip_addr(inetptr) \
	(((inet_struct *) VARDATA_ANY(inetptr))->ipaddr)

#define ip_maxbits(inetptr) \
	(ip_family(inetptr) == PGSQL_AF_INET ? 32 : 128)

#define reverse_shift(x) (32 - (x))

static inline
int ip_to_int32(unsigned char ip[]) {
    uint32_t retval = (ip[0] << 24 | ip[1] << 16 | ip[2] << 8 | ip[3]);
    return retval;
}

static inline
void int32_to_ip(uint32_t int_ip, uint32_t cidr, inet* inet_ip) {
    unsigned char *ip = ip_addr(inet_ip);
    memset(ip, 0, sizeof(ip_addr(inet_ip)));
    ip[3] = int_ip & 0xff;
    ip[2] = (int_ip >> 8) & 0xff;
    ip[1] = (int_ip >> 16) & 0xff;
    ip[0] = (int_ip >> 24) & 0xff;

    ip_bits(inet_ip) = cidr;
    ip_family(inet_ip) = PGSQL_AF_INET;
}

static inet*
netblock_split(netblock_context *ctx, ReturnSetInfo *resultInfo)
{
    while ((1 << ctx->shift) < ctx->size) {
        if (ctx->current & (1 << ctx->shift))
            break;
        ++ctx->shift;
    }

    if ((1 << ctx->shift) > ctx->size)
        --ctx->shift;

    inet *inet_ip = palloc(sizeof(inet));
    int current_size = 1 << ctx->shift;
    memset(inet_ip, 0, sizeof(inet));
    int32_to_ip(ctx->current, reverse_shift(ctx->shift), inet_ip);
    ctx->current = ctx->current + current_size;
    ctx->size = ctx->size - current_size;
    resultInfo->isDone = ExprMultipleResult;

    return inet_ip;
}

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(netblock_acc);
Datum netblock_acc(PG_FUNCTION_ARGS);

Datum
netblock_acc(PG_FUNCTION_ARGS)
{
    #define EPREFIX "netblock_acc: "

    FmgrInfo *fmgr_info = fcinfo->flinfo;
    ReturnSetInfo *resultInfo = (ReturnSetInfo *)fcinfo->resultinfo;
    netblock_context *ctx;

    if( fcinfo->resultinfo == NULL )
        elog(ERROR, EPREFIX "context does not accept a set result");

    if( !IsA( fcinfo->resultinfo, ReturnSetInfo ))
        elog(ERROR, EPREFIX "context does not accept a set result");

    if (fmgr_info->fn_extra == NULL) {

        inet *block[2], *tmp;
        int num[2];

        block[0] = DatumGetInetP(PG_GETARG_INET_P(0));
        block[1] = DatumGetInetP(PG_GETARG_INET_P(1));

        if ((ip_family(block[0]) != PGSQL_AF_INET) ||
            (ip_family(block[1]) != PGSQL_AF_INET)) {
            elog(ERROR, EPREFIX "only IPv4 is supported");
        }

        fmgr_info->fn_extra = MemoryContextAlloc(fmgr_info->fn_mcxt,
                                                 sizeof(netblock_context));
        ctx = (netblock_context *)fmgr_info->fn_extra;

        num[0] = ip_to_int32(ip_addr(block[0]));
        num[1] = ip_to_int32(ip_addr(block[1]));

        if (num[0] > num[1]) {
            tmp = block[0];
            block[0] = block[1];
            block[1] = tmp;
        }

        ctx->start[0] = ip_to_int32(ip_addr(block[0]));
        ctx->start[1] = ip_to_int32(ip_addr(block[1]));

        ctx->end[0] = ctx->start[0] +
                (1 << (ip_maxbits(block[0]) - ip_bits(block[0])));
        ctx->end[1] = ctx->start[1] +
                (1 << (ip_maxbits(block[1]) - ip_bits(block[1])));

        ctx->current = ctx->start[0];

        if (ctx->start[1] <= ctx->end[0] && ctx->end[0] <= ctx->end[1]) {
            /*
              |--------------|-------|-------------|
              s0             s1      e0            e1
            */
            ctx->size = ctx->end[1] - ctx->start[0];
            ctx->shift = 0;
        } else if (ctx->start[1] <= ctx->end[0] && ctx->end[1] <= ctx->end[0]) {
            /*
              |------|--------|-----|
              s0     s1       e1    e0
            */
            ctx->size = ctx->end[0] - ctx->start[0];
        } else {
            /*
              |--------|  |--------|
              s0       e0 s1       e1
             */
            ctx->size = ctx->end[0] - ctx->start[0];
            ctx->shift = 0;
        }
    }

    ctx = (netblock_context *)fmgr_info->fn_extra;
    if (ctx->current >= ctx->end[0] && ctx->current <= ctx->start[1]) {
        ctx->current = ctx->start[1];
        ctx->size = ctx->end[1] - ctx->start[1];
        ctx->shift = 0;
    }
    if (ctx->current >= Max(ctx->end[0], ctx->end[1])) {
        resultInfo->isDone = ExprEndResult;
        PG_RETURN_NULL();
    }
    PG_RETURN_INET_P(netblock_split(ctx, resultInfo));
out:
    pfree(fmgr_info->fn_extra);
    fmgr_info->fn_extra = NULL;
    resultInfo->isDone = ExprEndResult;
    PG_RETURN_NULL();

    #undef EPREFIX
}


PG_FUNCTION_INFO_V1(netblock_sub);
Datum netblock_sub(PG_FUNCTION_ARGS);

Datum
netblock_sub(PG_FUNCTION_ARGS)
{
    #define EPREFIX "netblock_sub: "

    FmgrInfo *fmgr_info = fcinfo->flinfo;
    ReturnSetInfo *resultInfo = (ReturnSetInfo *)fcinfo->resultinfo;
    netblock_context *ctx;

    if( fcinfo->resultinfo == NULL )
        elog(ERROR, EPREFIX "context does not accept a set result");

    if( !IsA( fcinfo->resultinfo, ReturnSetInfo ))
        elog(ERROR, EPREFIX "context does not accept a set result");

    if (fmgr_info->fn_extra == NULL) {
        inet *block[2];

        block[0] = DatumGetInetP(PG_GETARG_INET_P(0));
        block[1] = DatumGetInetP(PG_GETARG_INET_P(1));

        if ((ip_family(block[0]) != PGSQL_AF_INET) ||
            (ip_family(block[1]) != PGSQL_AF_INET)) {
            elog(ERROR, EPREFIX "only IPv4 is supported");
        }

        fmgr_info->fn_extra = MemoryContextAlloc(fmgr_info->fn_mcxt,
                                                 sizeof(netblock_context));
        ctx = (netblock_context *)fmgr_info->fn_extra;

        ctx->start[0] = ip_to_int32(ip_addr(block[0]));
        ctx->start[1] = ip_to_int32(ip_addr(block[1]));

        ctx->end[0] = ctx->start[0] +
                (1 << (ip_maxbits(block[0]) - ip_bits(block[0])));
        ctx->end[1] = ctx->start[1] +
                (1 << (ip_maxbits(block[1]) - ip_bits(block[1])));

        ctx->current = ctx->start[0];
    }

    ctx = (netblock_context *)fmgr_info->fn_extra;

    if (!(ctx->start[1] >= ctx->start[0] && ctx->end[1] <= ctx->end[0]))
        goto out;

    if (ctx->current == ctx->start[0]) {
        ctx->size = ctx->start[1] - ctx->start[0];
        ctx->shift = 0;
    }

    if (ctx->current == ctx->start[1]) {
        ctx->current = ctx->end[1];
        ctx->size = ctx->end[0] - ctx->end[1];
        ctx->shift = 0;
    }

    if (ctx->current == ctx->end[0])
        goto out;

    if (ctx->current > ctx->end[0]) {
        elog(ERROR, EPREFIX "BUG");
    }

    PG_RETURN_INET_P(netblock_split(ctx, resultInfo));

out:
    pfree(fmgr_info->fn_extra);
    fmgr_info->fn_extra = NULL;
    resultInfo->isDone = ExprEndResult;
    PG_RETURN_NULL();

    #undef EPREFIX
}

// vim: ts=4 sw=4 et cindent
