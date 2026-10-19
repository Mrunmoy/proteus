/// @file
/// Compact numeric device identity for diagnostics records.

#ifndef DIAG_IDENTITY_H
#define DIAG_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#include "diag/compiler.h"
#include "diag/result.h"

DIAG_EXTERN_C_BEGIN

/// Schema version for the compact numeric identity layout.
#define DIAG_IDENTITY_SCHEMA_VERSION (1u)

/// Encoded identity size in bytes when serialized by a host or future capsule layer.
#define DIAG_IDENTITY_ENCODED_SIZE (10u)

/// Opaque diagnostics context initialized with `diag_init()`.
struct diag_context;

/// Compact product and firmware identity.
///
/// Fields are numeric by design. Host tooling owns strings, catalogs, and rich
/// product meaning derived from these identifiers.
struct diag_identity
{
    /// Vendor, ecosystem, or fleet namespace.
    uint16_t ecosystem_id;
    /// Product identifier within the ecosystem.
    uint16_t product_id;
    /// Device type or hardware class.
    uint16_t device_type;
    /// Instance index when multiple devices of the same type exist.
    uint8_t device_instance;
    /// Firmware stage such as bootloader, application, or updater.
    uint8_t firmware_stage;
    /// Firmware component identifier within the stage.
    uint8_t firmware_component;
    /// Reserved byte; initialize to zero for forward compatibility.
    uint8_t reserved;
};

// clang-format off
/// Copy an identity value after validating pointers.
///
/// @return `DIAG_OK` or `DIAG_ERROR_INVALID_ARGUMENT`.
enum diag_result diag_identity_copy(const struct diag_identity *identity,
                                    struct diag_identity *out_identity);

/// Attach compact numeric identity to an initialized diagnostics context.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_identity_attach(struct diag_context *ctx,
                                      const struct diag_identity *identity);

/// Copy the identity configured for an initialized diagnostics context.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_identity_get(const struct diag_context *ctx,
                                   struct diag_identity *out_identity);
// clang-format on

/// Compare two identity values for exact numeric equality.
///
/// @return `true` when both pointers are non-null and all fields are equal.
bool diag_identity_equal(const struct diag_identity *left, const struct diag_identity *right);

DIAG_EXTERN_C_END

#endif
