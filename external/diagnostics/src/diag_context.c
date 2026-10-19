#include "diag_context_internal.h"

#include <stdint.h>

static int diag_context_storage_is_aligned(const struct diag_context_storage *storage)
{
    return ((uintptr_t)(const void *)storage % DIAG_CONTEXT_STORAGE_ALIGN) == 0u;
}

enum diag_result diag_init(struct diag_context_storage *context_storage,
                           const struct diag_config *config, struct diag_context **out_ctx)
{
    struct diag_context *ctx;

    // Clear the out-parameter up front so a failed init never leaves the caller
    // holding a stale context pointer from a previous successful call.
    if (out_ctx != NULL)
    {
        *out_ctx = NULL;
    }

    if (context_storage == NULL || config == NULL || out_ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_storage_is_aligned(context_storage))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    ctx = (struct diag_context *)(void *)context_storage;
    (void)config;
    ctx->state_flags = DIAG_CONTEXT_STATE_INITIALIZED;
    ctx->dirty_flags = DIAG_DIRTY_NONE;
#if DIAG_FEATURE_DTC
    ctx->dtc.records = NULL;
    ctx->dtc.capacity = 0u;
    ctx->dtc.confirmation_threshold = 0u;
    ctx->dtc.aging_threshold = 0u;
    ctx->dtc_count = 0u;
#endif
#if DIAG_FEATURE_IDENTITY
    ctx->identity = (struct diag_identity){0};
#endif
#if DIAG_FEATURE_LIFECYCLE
    ctx->lifecycle = (struct diag_lifecycle_config){0};
    ctx->last_reset_reason = DIAG_RESET_REASON_UNKNOWN;
    ctx->reset_count = 0u;
    ctx->abnormal_reset_count = 0u;
    ctx->lifecycle_dirty_flags = DIAG_LIFECYCLE_DIRTY_NONE;
#endif
#if DIAG_FEATURE_STORAGE
    ctx->storage = (struct diag_storage){0};
#endif
#if DIAG_FEATURE_TRANSPORT
    ctx->transport = (struct diag_transport){0};
#endif
    *out_ctx = ctx;

    return DIAG_OK;
}

enum diag_result diag_deinit(struct diag_context *ctx)
{
    if (ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    ctx->state_flags = 0u;
    ctx->dirty_flags = DIAG_DIRTY_NONE;
#if DIAG_FEATURE_DTC
    ctx->dtc.records = NULL;
    ctx->dtc.capacity = 0u;
    ctx->dtc.confirmation_threshold = 0u;
    ctx->dtc.aging_threshold = 0u;
    ctx->dtc_count = 0u;
#endif
#if DIAG_FEATURE_IDENTITY
    ctx->identity = (struct diag_identity){0};
#endif
#if DIAG_FEATURE_LIFECYCLE
    ctx->lifecycle = (struct diag_lifecycle_config){0};
    ctx->last_reset_reason = DIAG_RESET_REASON_UNKNOWN;
    ctx->reset_count = 0u;
    ctx->abnormal_reset_count = 0u;
    ctx->lifecycle_dirty_flags = DIAG_LIFECYCLE_DIRTY_NONE;
#endif
#if DIAG_FEATURE_STORAGE
    ctx->storage = (struct diag_storage){0};
#endif
#if DIAG_FEATURE_TRANSPORT
    ctx->transport = (struct diag_transport){0};
#endif

    return DIAG_OK;
}

enum diag_result diag_get_dirty_flags(const struct diag_context *ctx, uint32_t *out_dirty_flags)
{
    if (ctx == NULL || out_dirty_flags == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    *out_dirty_flags = ctx->dirty_flags;

    return DIAG_OK;
}
