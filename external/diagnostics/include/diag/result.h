/// @file
/// Shared result codes returned by the diagnostics C API.

#ifndef DIAG_RESULT_H
#define DIAG_RESULT_H

#include "diag/compiler.h"

DIAG_EXTERN_C_BEGIN

/// Status code for diagnostics operations.
///
/// `DIAG_OK` is always zero so callers can use simple success checks. Every
/// nonzero value describes a specific failure class; APIs do not throw or hide
/// errors behind global state.
enum diag_result
{
    /// Operation completed successfully.
    DIAG_OK = 0,
    /// A required pointer, capacity, enum value, or alignment constraint was invalid.
    DIAG_ERROR_INVALID_ARGUMENT,
    /// The context or subsystem was not initialized.
    DIAG_ERROR_NOT_INITIALIZED,
    /// The requested DTC, fault mapping, section, or record was not present.
    DIAG_ERROR_NOT_FOUND,
    /// The requested registration would duplicate an existing identifier.
    DIAG_ERROR_ALREADY_EXISTS,
    /// Caller-provided fixed storage or output capacity was too small.
    DIAG_ERROR_CAPACITY,
    /// A storage adapter operation failed or could not be accepted.
    DIAG_ERROR_STORAGE,
    /// A transport adapter operation failed or could not be accepted.
    DIAG_ERROR_TRANSPORT,
    /// Untrusted serialized data failed validation.
    DIAG_ERROR_CORRUPT_DATA,
    /// Requested behavior is valid but not implemented by this build or MVP stage.
    DIAG_ERROR_NOT_SUPPORTED
};

DIAG_EXTERN_C_END

#endif
