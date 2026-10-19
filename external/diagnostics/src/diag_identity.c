#include "diag/identity.h"

#include "diag_context_internal.h"

// clang-format off
enum diag_result diag_identity_copy(const struct diag_identity *identity,
                                    struct diag_identity *out_identity)
// clang-format on
{
    if (identity == NULL || out_identity == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    *out_identity = *identity;

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_identity_attach(struct diag_context *ctx,
                                      const struct diag_identity *identity)
// clang-format on
{
    if (ctx == NULL || identity == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    ctx->identity = *identity;
    diag_context_set_state(ctx, DIAG_CONTEXT_STATE_IDENTITY_ATTACHED);

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_identity_get(const struct diag_context *ctx,
                                   struct diag_identity *out_identity)
// clang-format on
{
    if (ctx == NULL || out_identity == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_IDENTITY_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    return diag_identity_copy(&ctx->identity, out_identity);
}

bool diag_identity_equal(const struct diag_identity *left, const struct diag_identity *right)
{
    if (left == NULL || right == NULL)
    {
        return false;
    }

    return left->ecosystem_id == right->ecosystem_id && left->product_id == right->product_id &&
           left->device_type == right->device_type &&
           left->device_instance == right->device_instance &&
           left->firmware_stage == right->firmware_stage &&
           left->firmware_component == right->firmware_component &&
           left->reserved == right->reserved;
}
