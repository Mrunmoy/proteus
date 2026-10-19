/// @file
/// Lifecycle and reset counter tracking for diagnostics contexts.

#ifndef DIAG_LIFECYCLE_H
#define DIAG_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "diag/compiler.h"
#include "diag/result.h"

DIAG_EXTERN_C_BEGIN

/// Reset reason observed by platform startup code.
enum diag_reset_reason
{
    /// Reset reason was not supplied or cannot be classified.
    DIAG_RESET_REASON_UNKNOWN = 0,
    /// Power-on reset.
    DIAG_RESET_REASON_POWER_ON,
    /// Software-requested reset.
    DIAG_RESET_REASON_SOFTWARE,
    /// Watchdog reset, treated as abnormal for counter policy.
    DIAG_RESET_REASON_WATCHDOG,
    /// Brownout reset, treated as abnormal for counter policy.
    DIAG_RESET_REASON_BROWNOUT,
    /// External reset pin or external supervisor reset.
    DIAG_RESET_REASON_EXTERNAL,
    /// Reset associated with firmware update flow.
    DIAG_RESET_REASON_UPDATE,
    /// Fault-triggered reset, treated as abnormal for counter policy.
    DIAG_RESET_REASON_FAULT
};

/// Policy for reset counter persistence requests.
enum diag_reset_counter_policy
{
    /// Do not maintain reset counters.
    DIAG_RESET_COUNTER_POLICY_DISABLED = 0,
    /// Maintain counters only in RAM for the current context lifetime.
    DIAG_RESET_COUNTER_POLICY_RAM_ONLY,
    /// Request persistence only after abnormal resets.
    DIAG_RESET_COUNTER_POLICY_ABNORMAL_ONLY,
    /// Request persistence every configured N resets.
    DIAG_RESET_COUNTER_POLICY_EVERY_N,
    /// Use a platform-provided counter value instead of incrementing in core code.
    DIAG_RESET_COUNTER_POLICY_PLATFORM
};

/// Dirty flags for lifecycle state that may need explicit persistence.
enum diag_lifecycle_dirty_flag
{
    /// No lifecycle state is dirty.
    DIAG_LIFECYCLE_DIRTY_NONE = 0u,
    /// Reset counter state has crossed the configured persistence policy.
    DIAG_LIFECYCLE_DIRTY_RESET_COUNTER = 1u
};

/// Lifecycle configuration copied into a diagnostics context at init.
struct diag_lifecycle_config
{
    /// Reset counter behavior and persistence-request policy.
    enum diag_reset_counter_policy reset_counter_policy;
    /// Interval for `DIAG_RESET_COUNTER_POLICY_EVERY_N`; zero disables requests.
    uint32_t reset_count_interval;
    /// Initial reset count when using `DIAG_RESET_COUNTER_POLICY_PLATFORM`.
    uint32_t platform_reset_count;
};

/// Readback snapshot of lifecycle state.
struct diag_lifecycle_snapshot
{
    /// Most recent reset reason observed by the context.
    enum diag_reset_reason last_reset_reason;
    /// Active reset counter policy copied from the context config.
    enum diag_reset_counter_policy reset_counter_policy;
    /// Current reset count according to the configured policy.
    uint32_t reset_count;
    /// Count of watchdog, brownout, and fault resets observed in RAM.
    uint32_t abnormal_reset_count;
    /// Bitmask of `enum diag_lifecycle_dirty_flag` values.
    uint32_t dirty_flags;
    /// True when dirty lifecycle state should be persisted by explicit policy.
    bool persist_requested;
};

/// Opaque diagnostics context initialized with `diag_init()`.
struct diag_context;

// clang-format off
/// Attach lifecycle/reset counter policy to an initialized diagnostics context.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_lifecycle_attach(struct diag_context *ctx,
                                       const struct diag_lifecycle_config *config);

/// Record one platform-observed reset reason.
///
/// The function updates RAM counters and dirty flags according to policy. It
/// never performs a hidden storage write.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_lifecycle_observe_reset(struct diag_context *ctx,
                                              enum diag_reset_reason reason);

/// Copy lifecycle state from an initialized diagnostics context.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_lifecycle_get(const struct diag_context *ctx,
                                    struct diag_lifecycle_snapshot *out);
// clang-format on

/// Clear selected lifecycle dirty flags after a successful explicit persistence step.
///
/// Unknown bits are ignored. Passing `DIAG_LIFECYCLE_DIRTY_RESET_COUNTER` clears
/// the lifecycle persistence request and context dirty flag for lifecycle state.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_lifecycle_clear_dirty(struct diag_context *ctx, uint32_t dirty_flags);

DIAG_EXTERN_C_END

#endif
