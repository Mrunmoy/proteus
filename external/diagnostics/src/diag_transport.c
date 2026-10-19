#include "diag/transport.h"

#include "diag_context_internal.h"

enum diag_result diag_transport_attach(struct diag_context         *ctx,
                                       const struct diag_transport *transport)
{
    if (ctx == NULL || transport == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (transport->ops == NULL || transport->ops->send == NULL || transport->ops->receive == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    ctx->transport = *transport;
    diag_context_set_state(ctx, DIAG_CONTEXT_STATE_TRANSPORT_ATTACHED);

    return DIAG_OK;
}
