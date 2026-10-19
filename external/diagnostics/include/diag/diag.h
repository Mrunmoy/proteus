/// @file
/// Convenience umbrella include for the diagnostics C API.
///
/// @mainpage Diagnostics C API
///
/// The diagnostics library is an embedded-first C99 API for fixed-capacity
/// diagnostic state. The core owns DTC and lifecycle behavior while callers own
/// all memory, storage adapters, transport adapters, and product-specific
/// catalogs or strings.
///
/// Include `<diag/diag.h>` to pull in the complete public C API, or include the
/// narrower headers under `include/diag/` when a module only needs one surface.
/// Runtime state stays in caller-provided RAM; persistent storage is accessed
/// only through explicit APIs and caller-supplied adapters.
///
/// @section diag_usage_model Usage Model
///
/// 1. Allocate `struct diag_context_storage` in caller-owned memory.
/// 2. Call `diag_init()` with a zero-initialized `struct diag_config`.
/// 3. Attach only the feature modules the product uses, such as DTC, lifecycle,
///    identity, storage, or transport.
/// 4. Update diagnostic state from firmware monitors.
/// 5. Call explicit save/load APIs only where the product wants persistence.
///
/// The library never allocates heap memory and never writes persistent storage
/// from DTC or lifecycle hot paths.
///
/// @section diag_result_contract Result Contract
///
/// Public APIs return `enum diag_result`. The common contract is:
///
/// - `DIAG_OK`: operation completed successfully.
/// - `DIAG_ERROR_INVALID_ARGUMENT`: a required pointer, capacity, enum value, or
///   alignment constraint is invalid.
/// - `DIAG_ERROR_NOT_INITIALIZED`: the context has not been initialized or the
///   requested feature module has not been attached.
/// - `DIAG_ERROR_NOT_FOUND`: a requested DTC, local fault mapping, capsule
///   section, or record does not exist.
/// - `DIAG_ERROR_ALREADY_EXISTS`: registration would duplicate an existing
///   identifier.
/// - `DIAG_ERROR_CAPACITY`: caller-owned fixed storage is too small.
/// - `DIAG_ERROR_STORAGE`: a storage adapter callback rejected or failed an
///   operation.
/// - `DIAG_ERROR_TRANSPORT`: a transport adapter callback rejected or failed an
///   operation.
/// - `DIAG_ERROR_CORRUPT_DATA`: untrusted serialized bytes failed validation.
/// - `DIAG_ERROR_NOT_SUPPORTED`: the request is valid but unavailable in this
///   build or MVP stage.

#ifndef DIAG_DIAG_H
#define DIAG_DIAG_H

#include "diag/compiler.h"
#include "diag/features.h"
#include "diag/context.h"
#include "diag/result.h"

#if DIAG_FEATURE_CAPSULE
#include "diag/capsule.h"
#endif

#if DIAG_FEATURE_DTC
#include "diag/dtc.h"
#endif

#if DIAG_FEATURE_IDENTITY
#include "diag/identity.h"
#endif

#if DIAG_FEATURE_LIFECYCLE
#include "diag/lifecycle.h"
#endif

#if DIAG_FEATURE_STORAGE
#include "diag/storage.h"
#endif

#if DIAG_FEATURE_TRANSPORT
#include "diag/transport.h"
#endif

#endif
