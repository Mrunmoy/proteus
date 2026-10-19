/// @file
/// Fixed-capacity Diagnostic Trouble Code registration and runtime state.

#ifndef DIAG_DTC_H
#define DIAG_DTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diag/compiler.h"
#include "diag/result.h"

DIAG_EXTERN_C_BEGIN

/// Public DTC identifier stored as a compact numeric value.
///
/// Host tooling owns the catalog that maps this value to names, descriptions,
/// service procedures, and product-specific meaning.
typedef uint32_t diag_dtc_id_t;

/// Project-local monitor or fault identifier.
///
/// Several local checks may map to externally visible DTCs in a downstream
/// product, but this API currently enforces one registered local fault ID per
/// registered DTC record.
typedef uint32_t diag_local_fault_id_t;

/// Host/catalog compatibility version for interpreting compact DTC IDs.
#define DIAG_DTC_NAMESPACE_VERSION (1u)

/// Default consecutive failed operation cycles required before confirmation.
#define DIAG_DTC_DEFAULT_CONFIRMATION_THRESHOLD (1u)
/// Default consecutive clean operation cycles required before a confirmed DTC heals.
#define DIAG_DTC_DEFAULT_AGING_THRESHOLD (40u)

/// Operation-cycle lifecycle thresholds for all DTCs in a context.
///
/// A zero field selects the matching default macro, so a zero-initialized
/// `struct diag_dtc_config` keeps standard behavior.
struct diag_dtc_config
{
    /// Fixed array used by the context for registered DTC records.
    struct diag_dtc_snapshot *records;
    /// Number of entries available in `records`; must be nonzero.
    size_t capacity;
    /// Consecutive failed operation cycles needed to set `DIAG_DTC_STATUS_CONFIRMED`.
    uint8_t confirmation_threshold;
    /// Consecutive clean operation cycles needed to age a confirmed DTC back to clear.
    uint16_t aging_threshold;
};

/// Bit values in the UDS-compatible DTC status byte.
enum diag_dtc_status_bit
{
    /// The monitor currently reports the fault as failed.
    DIAG_DTC_STATUS_TEST_FAILED = 1u << 0u,
    /// The monitor failed at least once during the current operation cycle.
    DIAG_DTC_STATUS_TEST_FAILED_THIS_OPERATION_CYCLE = 1u << 1u,
    /// The DTC has failed and has not yet seen a clean operation cycle.
    DIAG_DTC_STATUS_PENDING = 1u << 2u,
    /// The DTC has met the configured confirmation threshold.
    DIAG_DTC_STATUS_CONFIRMED = 1u << 3u,
    /// The monitor has not completed since the DTC was cleared.
    DIAG_DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR = 1u << 4u,
    /// The monitor has failed at least once since the DTC was cleared.
    DIAG_DTC_STATUS_TEST_FAILED_SINCE_CLEAR = 1u << 5u,
    /// The monitor has not completed in the current operation cycle.
    DIAG_DTC_STATUS_TEST_NOT_COMPLETED_THIS_OPERATION_CYCLE = 1u << 6u,
    /// The DTC requests a product-specific warning indicator.
    DIAG_DTC_STATUS_WARNING_INDICATOR_REQUESTED = 1u << 7u
};

/// Coarse severity for ordering, replacement policy, or host display.
enum diag_dtc_severity
{
    /// Informational diagnostic with no immediate service implication.
    DIAG_DTC_SEVERITY_INFO = 0,
    /// Warning diagnostic that may need operator or service attention.
    DIAG_DTC_SEVERITY_WARNING,
    /// Error diagnostic for a faulted function.
    DIAG_DTC_SEVERITY_ERROR,
    /// Critical diagnostic for severe or safety-relevant fault handling.
    DIAG_DTC_SEVERITY_CRITICAL
};

/// Caller-owned DTC record and readback snapshot.
///
/// The context stores records in the caller-provided array from
/// `struct diag_config`. Get and list APIs copy the current record into this
/// same structure. Counters saturate instead of wrapping.
struct diag_dtc_snapshot
{
    /// Public DTC identifier.
    diag_dtc_id_t id;
    /// Project-local monitor or fault ID used for direct monitor updates.
    diag_local_fault_id_t local_fault_id;
    /// Registered severity.
    enum diag_dtc_severity severity;
    /// True when the fault is currently active.
    bool active;
    /// Per-operation-cycle latch set when a failure is observed in the current cycle.
    bool failed_this_cycle;
    /// UDS-compatible status byte composed from `enum diag_dtc_status_bit`.
    uint8_t status;
    /// Consecutive failed operation-cycle count used for confirmation.
    uint8_t failed_cycle_count;
    /// Consecutive clean operation-cycle count used for aging confirmed DTCs.
    uint16_t aging_counter;
    /// Saturating count of inactive-to-active fault occurrences.
    uint32_t occurrence_count;
    /// Saturating count of activations.
    uint32_t active_count;
    /// Saturating count of clears that changed active or status state.
    uint32_t clear_count;
};

/// Opaque diagnostics context initialized with `diag_init()`.
struct diag_context;

/// Attach caller-owned DTC storage and thresholds to an initialized context.
///
/// Registration consumes entries from `config->records`. The array must remain
/// valid and writable until `diag_deinit()` completes or DTC storage is attached
/// again with a different buffer.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_attach(struct diag_context *ctx, const struct diag_dtc_config *config);

/// Register a DTC using the DTC ID as its local fault ID.
///
/// Registration consumes one entry from the caller-provided fixed DTC buffer.
/// Duplicate DTC IDs or local fault IDs are rejected.
///
/// @return `DIAG_OK`, `DIAG_ERROR_ALREADY_EXISTS`, `DIAG_ERROR_CAPACITY`,
///         `DIAG_ERROR_INVALID_ARGUMENT`, or `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_register(struct diag_context *ctx, diag_dtc_id_t id,
                                   enum diag_dtc_severity severity);

// clang-format off
/// Register a DTC with a separate project-local fault ID.
///
/// The mapping lets monitor code update a DTC without depending on externally
/// visible DTC numbering. Registration is RAM-only and does not call storage.
///
/// @return `DIAG_OK`, `DIAG_ERROR_ALREADY_EXISTS`, `DIAG_ERROR_CAPACITY`,
///         `DIAG_ERROR_INVALID_ARGUMENT`, or `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_register_fault(struct diag_context *ctx,
                                         diag_local_fault_id_t local_fault_id,
                                         diag_dtc_id_t id,
                                         enum diag_dtc_severity severity);
// clang-format on

/// Mark a registered DTC as failed for the current operation cycle.
///
/// The update is idempotent while already active except for status latches; it
/// does not perform persistent storage writes.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_set_active(struct diag_context *ctx, diag_dtc_id_t id);

/// Mark a registered DTC monitor as passed/inactive.
///
/// Passing clears the current failed state and completion bits, but confirmation
/// and aging progress are driven by `diag_dtc_operation_cycle()`.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_set_inactive(struct diag_context *ctx, diag_dtc_id_t id);

// clang-format off
/// Mark the DTC mapped from a local fault ID as failed.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_set_fault_test_failed(struct diag_context *ctx,
                                                diag_local_fault_id_t local_fault_id);
/// Mark the DTC mapped from a local fault ID as passed/inactive.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_set_fault_test_passed(struct diag_context *ctx,
                                                diag_local_fault_id_t local_fault_id);
// clang-format on

/// Clear one registered DTC's status and lifecycle counters.
///
/// The occurrence, active, and clear counters are retained except for the clear
/// count increment when the clear changed active or status state.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_clear(struct diag_context *ctx, diag_dtc_id_t id);

/// Clear status and lifecycle counters for every registered DTC.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_clear_all(struct diag_context *ctx);

/// Reset runtime counters for one DTC without changing its active/status bits.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_reset_counter(struct diag_context *ctx, diag_dtc_id_t id);

/// Advance every registered DTC by one completed operation cycle.
///
/// This drives the `test_failed -> pending -> confirmed -> aged` lifecycle over
/// the bounded registered DTC array. The call resets the per-cycle failure latch
/// and marks monitors as not yet completed for the next cycle.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_operation_cycle(struct diag_context *ctx);

/// Copy one registered DTC record by public DTC ID.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_get(const struct diag_context *ctx, diag_dtc_id_t id,
                              struct diag_dtc_snapshot *out);
// clang-format off
/// Copy one registered DTC record by project-local fault ID.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_get_by_fault(const struct diag_context *ctx,
                                       diag_local_fault_id_t local_fault_id,
                                       struct diag_dtc_snapshot *out);
// clang-format on

/// Read only the UDS-compatible status byte for one registered DTC.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_get_status(const struct diag_context *ctx, diag_dtc_id_t id,
                                     uint8_t *out_status);

/// Copy all registered DTC records into a caller-provided array.
///
/// `*count` is set to the number of registered DTCs before capacity is checked,
/// allowing callers to size a second request after `DIAG_ERROR_CAPACITY`.
///
/// @return `DIAG_OK`, `DIAG_ERROR_CAPACITY`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_dtc_list(const struct diag_context *ctx, struct diag_dtc_snapshot *out,
                               size_t capacity, size_t *count);

DIAG_EXTERN_C_END

#endif
