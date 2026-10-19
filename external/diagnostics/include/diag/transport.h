/// @file
/// Transport adapter declarations for protocol/framing layers.

#ifndef DIAG_TRANSPORT_H
#define DIAG_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "diag/compiler.h"
#include "diag/result.h"

DIAG_EXTERN_C_BEGIN

/// Transport callback table implemented by a platform or protocol adapter.
///
/// The diagnostics core remains transport-agnostic. CAN, UART, TCP, framing,
/// buffering, blocking behavior, and retries belong in the adapter above or
/// beside this interface.
struct diag_transport_ops
{
    /// Send `size` bytes from `buffer` through the adapter.
    enum diag_result (*send)(void *user, const uint8_t *buffer, size_t size);
    /// Receive up to `buffer_size` bytes and report the number read.
    enum diag_result (*receive)(void *user, uint8_t *buffer, size_t buffer_size,
                                size_t *bytes_read);
};

/// Transport adapter instance.
struct diag_transport
{
    /// Callback table for adapter operations.
    const struct diag_transport_ops *ops;
    /// Opaque adapter state passed to callbacks.
    void *user;
};

/// Opaque diagnostics context initialized with `diag_init()`.
struct diag_context;

/// Attach a transport adapter to an initialized diagnostics context.
///
/// The core stores the adapter for protocol layers that choose to use this
/// callback shape. The library does not define wire framing, timeouts, retries,
/// sessions, security, or transport-specific packet formats.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_transport_attach(struct diag_context         *ctx,
                                       const struct diag_transport *transport);

DIAG_EXTERN_C_END

#endif
