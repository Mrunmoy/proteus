#include "diag/lifecycle.h"

#include "diag_context_internal.h"

static bool diag_lifecycle_is_abnormal_reset(enum diag_reset_reason reason)
{
    return reason == DIAG_RESET_REASON_WATCHDOG || reason == DIAG_RESET_REASON_BROWNOUT ||
           reason == DIAG_RESET_REASON_FAULT;
}

// clang-format off
enum diag_result diag_lifecycle_attach(struct diag_context *ctx,
                                       const struct diag_lifecycle_config *config)
// clang-format on
{
    if (ctx == NULL || config == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    ctx->lifecycle = *config;
    ctx->last_reset_reason = DIAG_RESET_REASON_UNKNOWN;
    if (config->reset_counter_policy == DIAG_RESET_COUNTER_POLICY_PLATFORM)
    {
        ctx->reset_count = config->platform_reset_count;
    }
    else
    {
        ctx->reset_count = 0u;
    }
    ctx->abnormal_reset_count = 0u;
    ctx->lifecycle_dirty_flags = DIAG_LIFECYCLE_DIRTY_NONE;
    diag_context_clear_dirty(ctx, DIAG_DIRTY_LIFECYCLE);
    diag_context_set_state(ctx, DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED);

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_lifecycle_observe_reset(struct diag_context *ctx,
                                              enum diag_reset_reason reason)
// clang-format on
{
    enum diag_reset_counter_policy policy;
    bool                           abnormal;

    if (ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    policy = ctx->lifecycle.reset_counter_policy;
    abnormal = diag_lifecycle_is_abnormal_reset(reason);
    ctx->last_reset_reason = reason;

    if (policy != DIAG_RESET_COUNTER_POLICY_DISABLED &&
        policy != DIAG_RESET_COUNTER_POLICY_PLATFORM)
    {
        ++ctx->reset_count;
    }

    if (policy != DIAG_RESET_COUNTER_POLICY_DISABLED &&
        policy != DIAG_RESET_COUNTER_POLICY_PLATFORM && abnormal)
    {
        ++ctx->abnormal_reset_count;
    }

    switch (policy)
    {
        case DIAG_RESET_COUNTER_POLICY_ABNORMAL_ONLY:
            if (abnormal)
            {
                ctx->lifecycle_dirty_flags |= DIAG_LIFECYCLE_DIRTY_RESET_COUNTER;
                diag_context_mark_dirty(ctx, DIAG_DIRTY_LIFECYCLE);
            }
            break;

        case DIAG_RESET_COUNTER_POLICY_EVERY_N:
            if (ctx->lifecycle.reset_count_interval != 0u &&
                (ctx->reset_count % ctx->lifecycle.reset_count_interval) == 0u)
            {
                ctx->lifecycle_dirty_flags |= DIAG_LIFECYCLE_DIRTY_RESET_COUNTER;
                diag_context_mark_dirty(ctx, DIAG_DIRTY_LIFECYCLE);
            }
            break;

        case DIAG_RESET_COUNTER_POLICY_DISABLED:
        case DIAG_RESET_COUNTER_POLICY_RAM_ONLY:
        case DIAG_RESET_COUNTER_POLICY_PLATFORM:
        default:
            break;
    }

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_lifecycle_get(const struct diag_context *ctx,
                                    struct diag_lifecycle_snapshot *out)
// clang-format on
{
    if (ctx == NULL || out == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    out->last_reset_reason = ctx->last_reset_reason;
    out->reset_counter_policy = ctx->lifecycle.reset_counter_policy;
    out->reset_count = ctx->reset_count;
    out->abnormal_reset_count = ctx->abnormal_reset_count;
    out->dirty_flags = ctx->lifecycle_dirty_flags;
    out->persist_requested = ctx->lifecycle_dirty_flags != DIAG_LIFECYCLE_DIRTY_NONE;

    return DIAG_OK;
}

enum diag_result diag_lifecycle_clear_dirty(struct diag_context *ctx, uint32_t dirty_flags)
{
    if (ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    ctx->lifecycle_dirty_flags &= ~dirty_flags;
    if (ctx->lifecycle_dirty_flags == DIAG_LIFECYCLE_DIRTY_NONE)
    {
        diag_context_clear_dirty(ctx, DIAG_DIRTY_LIFECYCLE);
    }

    return DIAG_OK;
}
