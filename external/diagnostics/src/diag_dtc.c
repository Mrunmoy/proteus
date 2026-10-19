#include <stdbool.h>
#include <stdint.h>

#include "diag_context_internal.h"

static enum diag_result diag_dtc_validate_context(const struct diag_context *ctx)
{
    if (ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_DTC_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    return DIAG_OK;
}

enum diag_result diag_dtc_attach(struct diag_context *ctx, const struct diag_dtc_config *config)
{
    if (ctx == NULL || config == NULL || config->records == NULL || config->capacity == 0u)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    ctx->dtc = *config;
    ctx->dtc_count = 0u;
    diag_context_clear_dirty(ctx, DIAG_DIRTY_DTC);
    diag_context_set_state(ctx, DIAG_CONTEXT_STATE_DTC_ATTACHED);

    return DIAG_OK;
}

static enum diag_result diag_dtc_find_index(const struct diag_context *ctx, diag_dtc_id_t id,
                                            size_t *out_index)
{
    size_t i;

    for (i = 0u; i < ctx->dtc_count; ++i)
    {
        const struct diag_dtc_snapshot *record = &ctx->dtc.records[i];

        if (record->id == id)
        {
            *out_index = i;
            return DIAG_OK;
        }
    }

    return DIAG_ERROR_NOT_FOUND;
}

static enum diag_result diag_dtc_find_fault_index(const struct diag_context *ctx,
                                                  diag_local_fault_id_t      local_fault_id,
                                                  size_t                    *out_index)
{
    size_t i;

    for (i = 0u; i < ctx->dtc_count; ++i)
    {
        const struct diag_dtc_snapshot *record = &ctx->dtc.records[i];

        if (record->local_fault_id == local_fault_id)
        {
            *out_index = i;
            return DIAG_OK;
        }
    }

    return DIAG_ERROR_NOT_FOUND;
}

static uint32_t diag_dtc_increment_saturating_u32(uint32_t value)
{
    if (value == UINT32_MAX)
    {
        return UINT32_MAX;
    }

    return value + 1u;
}

static uint8_t diag_dtc_increment_saturating_u8(uint8_t value)
{
    if (value == UINT8_MAX)
    {
        return UINT8_MAX;
    }

    return (uint8_t)(value + 1u);
}

static uint16_t diag_dtc_increment_saturating_u16(uint16_t value)
{
    if (value == UINT16_MAX)
    {
        return UINT16_MAX;
    }

    return (uint16_t)(value + 1u);
}

static bool diag_dtc_snapshot_equal(const struct diag_dtc_snapshot *left,
                                    const struct diag_dtc_snapshot *right)
{
    return left->id == right->id && left->local_fault_id == right->local_fault_id &&
           left->severity == right->severity && left->active == right->active &&
           left->failed_this_cycle == right->failed_this_cycle && left->status == right->status &&
           left->failed_cycle_count == right->failed_cycle_count &&
           left->aging_counter == right->aging_counter &&
           left->occurrence_count == right->occurrence_count &&
           left->active_count == right->active_count && left->clear_count == right->clear_count;
}

static void diag_dtc_mark_dirty_if_changed(struct diag_context            *ctx,
                                           const struct diag_dtc_snapshot *before,
                                           const struct diag_dtc_snapshot *after)
{
    if (!diag_dtc_snapshot_equal(before, after))
    {
        diag_context_mark_dirty(ctx, DIAG_DIRTY_DTC);
    }
}

static void diag_dtc_mark_test_failed(struct diag_dtc_snapshot *record)
{
    if (!record->active)
    {
        record->occurrence_count = diag_dtc_increment_saturating_u32(record->occurrence_count);
        record->active_count = diag_dtc_increment_saturating_u32(record->active_count);
    }

    record->active = true;
    record->failed_this_cycle = true;
    record->status = (uint8_t)(record->status | DIAG_DTC_STATUS_TEST_FAILED |
                               DIAG_DTC_STATUS_TEST_FAILED_THIS_OPERATION_CYCLE |
                               DIAG_DTC_STATUS_PENDING | DIAG_DTC_STATUS_TEST_FAILED_SINCE_CLEAR);
    record->status =
        (uint8_t)(record->status &
                  (uint8_t) ~(DIAG_DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR |
                              DIAG_DTC_STATUS_TEST_NOT_COMPLETED_THIS_OPERATION_CYCLE));
}

static void diag_dtc_mark_test_passed(struct diag_dtc_snapshot *record)
{
    record->active = false;
    record->status =
        (uint8_t)(record->status &
                  (uint8_t) ~(DIAG_DTC_STATUS_TEST_FAILED |
                              DIAG_DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR |
                              DIAG_DTC_STATUS_TEST_NOT_COMPLETED_THIS_OPERATION_CYCLE));
}

enum diag_result diag_dtc_register(struct diag_context *ctx, diag_dtc_id_t id,
                                   enum diag_dtc_severity severity)
{
    return diag_dtc_register_fault(ctx, id, id, severity);
}

// clang-format off
enum diag_result diag_dtc_register_fault(struct diag_context *ctx,
                                         diag_local_fault_id_t local_fault_id,
                                         diag_dtc_id_t id,
                                         enum diag_dtc_severity severity)
// clang-format on
{
    struct diag_dtc_snapshot *record;
    enum diag_result          result;
    size_t                    ignored_index;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    if (diag_dtc_find_index(ctx, id, &ignored_index) == DIAG_OK ||
        diag_dtc_find_fault_index(ctx, local_fault_id, &ignored_index) == DIAG_OK)
    {
        return DIAG_ERROR_ALREADY_EXISTS;
    }

    if (ctx->dtc_count >= ctx->dtc.capacity)
    {
        return DIAG_ERROR_CAPACITY;
    }

    record = &ctx->dtc.records[ctx->dtc_count];
    record->id = id;
    record->local_fault_id = local_fault_id;
    record->severity = severity;
    record->active = false;
    record->failed_this_cycle = false;
    record->status = 0u;
    record->failed_cycle_count = 0u;
    record->aging_counter = 0u;
    record->occurrence_count = 0u;
    record->active_count = 0u;
    record->clear_count = 0u;
    ++ctx->dtc_count;
    diag_context_mark_dirty(ctx, DIAG_DIRTY_DTC);

    return DIAG_OK;
}

enum diag_result diag_dtc_set_active(struct diag_context *ctx, diag_dtc_id_t id)
{
    struct diag_dtc_snapshot *record;
    enum diag_result          result;
    size_t                    index;
    struct diag_dtc_snapshot  before;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_index(ctx, id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    record = &ctx->dtc.records[index];
    before = *record;
    diag_dtc_mark_test_failed(record);
    diag_dtc_mark_dirty_if_changed(ctx, &before, record);

    return DIAG_OK;
}

enum diag_result diag_dtc_set_inactive(struct diag_context *ctx, diag_dtc_id_t id)
{
    enum diag_result         result;
    size_t                   index;
    struct diag_dtc_snapshot before;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_index(ctx, id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    before = ctx->dtc.records[index];
    diag_dtc_mark_test_passed(&ctx->dtc.records[index]);
    diag_dtc_mark_dirty_if_changed(ctx, &before, &ctx->dtc.records[index]);

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_dtc_set_fault_test_failed(struct diag_context *ctx,
                                                diag_local_fault_id_t local_fault_id)
// clang-format on
{
    enum diag_result         result;
    size_t                   index;
    struct diag_dtc_snapshot before;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_fault_index(ctx, local_fault_id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    before = ctx->dtc.records[index];
    diag_dtc_mark_test_failed(&ctx->dtc.records[index]);
    diag_dtc_mark_dirty_if_changed(ctx, &before, &ctx->dtc.records[index]);

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_dtc_set_fault_test_passed(struct diag_context *ctx,
                                                diag_local_fault_id_t local_fault_id)
// clang-format on
{
    enum diag_result         result;
    size_t                   index;
    struct diag_dtc_snapshot before;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_fault_index(ctx, local_fault_id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    before = ctx->dtc.records[index];
    diag_dtc_mark_test_passed(&ctx->dtc.records[index]);
    diag_dtc_mark_dirty_if_changed(ctx, &before, &ctx->dtc.records[index]);

    return DIAG_OK;
}

enum diag_result diag_dtc_clear(struct diag_context *ctx, diag_dtc_id_t id)
{
    struct diag_dtc_snapshot *record;
    struct diag_dtc_snapshot  before;
    enum diag_result          result;
    bool                      changed;
    size_t                    index;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_index(ctx, id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    record = &ctx->dtc.records[index];
    before = *record;
    changed = record->active || record->status != 0u;
    if (changed)
    {
        record->active = false;
        record->clear_count = diag_dtc_increment_saturating_u32(record->clear_count);
    }
    record->status = 0u;
    record->failed_this_cycle = false;
    record->failed_cycle_count = 0u;
    record->aging_counter = 0u;
    diag_dtc_mark_dirty_if_changed(ctx, &before, record);

    return DIAG_OK;
}

enum diag_result diag_dtc_clear_all(struct diag_context *ctx)
{
    enum diag_result result;
    size_t           i;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    for (i = 0u; i < ctx->dtc_count; ++i)
    {
        struct diag_dtc_snapshot *record = &ctx->dtc.records[i];
        struct diag_dtc_snapshot  before = *record;

        if (record->active || record->status != 0u)
        {
            record->active = false;
            record->clear_count = diag_dtc_increment_saturating_u32(record->clear_count);
        }
        record->status = 0u;
        record->failed_this_cycle = false;
        record->failed_cycle_count = 0u;
        record->aging_counter = 0u;
        diag_dtc_mark_dirty_if_changed(ctx, &before, record);
    }

    return DIAG_OK;
}

enum diag_result diag_dtc_reset_counter(struct diag_context *ctx, diag_dtc_id_t id)
{
    struct diag_dtc_snapshot *record;
    enum diag_result          result;
    size_t                    index;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_index(ctx, id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    record = &ctx->dtc.records[index];
    {
        const struct diag_dtc_snapshot before = *record;

        record->occurrence_count = 0u;
        record->active_count = 0u;
        record->clear_count = 0u;
        record->failed_cycle_count = 0u;
        record->aging_counter = 0u;
        diag_dtc_mark_dirty_if_changed(ctx, &before, record);
    }

    return DIAG_OK;
}

enum diag_result diag_dtc_get(const struct diag_context *ctx, diag_dtc_id_t id,
                              struct diag_dtc_snapshot *out)
{
    enum diag_result result;
    size_t           index;

    if (out == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_index(ctx, id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    *out = ctx->dtc.records[index];
    return DIAG_OK;
}

// clang-format off
enum diag_result diag_dtc_get_by_fault(const struct diag_context *ctx,
                                       diag_local_fault_id_t local_fault_id,
                                       struct diag_dtc_snapshot *out)
// clang-format on
{
    enum diag_result result;
    size_t           index;

    if (out == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_fault_index(ctx, local_fault_id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    *out = ctx->dtc.records[index];
    return DIAG_OK;
}

enum diag_result diag_dtc_get_status(const struct diag_context *ctx, diag_dtc_id_t id,
                                     uint8_t *out_status)
{
    enum diag_result result;
    size_t           index;

    if (out_status == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_dtc_find_index(ctx, id, &index);
    if (result != DIAG_OK)
    {
        return result;
    }

    *out_status = ctx->dtc.records[index].status;
    return DIAG_OK;
}

enum diag_result diag_dtc_list(const struct diag_context *ctx, struct diag_dtc_snapshot *out,
                               size_t capacity, size_t *count)
{
    enum diag_result result;
    size_t           i;

    if (out == NULL || count == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    *count = ctx->dtc_count;
    if (capacity < ctx->dtc_count)
    {
        return DIAG_ERROR_CAPACITY;
    }

    for (i = 0u; i < ctx->dtc_count; ++i)
    {
        out[i] = ctx->dtc.records[i];
    }

    return DIAG_OK;
}

enum diag_result diag_dtc_operation_cycle(struct diag_context *ctx)
{
    enum diag_result result;
    uint8_t          confirmation_threshold;
    uint16_t         aging_threshold;
    size_t           i;
    bool             changed = false;

    result = diag_dtc_validate_context(ctx);
    if (result != DIAG_OK)
    {
        return result;
    }

    confirmation_threshold = ctx->dtc.confirmation_threshold;
    if (confirmation_threshold == 0u)
    {
        confirmation_threshold = DIAG_DTC_DEFAULT_CONFIRMATION_THRESHOLD;
    }

    aging_threshold = ctx->dtc.aging_threshold;
    if (aging_threshold == 0u)
    {
        aging_threshold = DIAG_DTC_DEFAULT_AGING_THRESHOLD;
    }

    for (i = 0u; i < ctx->dtc_count; ++i)
    {
        struct diag_dtc_snapshot *record = &ctx->dtc.records[i];
        struct diag_dtc_snapshot  before = *record;

        if (record->failed_this_cycle)
        {
            // Failed during the cycle: progress toward confirmation, reset aging.
            record->aging_counter = 0u;
            record->failed_cycle_count =
                diag_dtc_increment_saturating_u8(record->failed_cycle_count);
            if (record->failed_cycle_count >= confirmation_threshold)
            {
                // PENDING is intentionally left set: per ISO 14229, pendingDTC and
                // confirmedDTC are not mutually exclusive. A confirmed DTC that is
                // still failing this cycle is both. PENDING clears on the next clean
                // cycle (see the else branch below), not at the moment of confirm.
                record->status = (uint8_t)(record->status | DIAG_DTC_STATUS_CONFIRMED);
            }
        }
        else if (!record->active)
        {
            // Clean cycle: one clean cycle clears pending; a confirmed DTC ages.
            record->failed_cycle_count = 0u;
            record->status = (uint8_t)(record->status & (uint8_t)~DIAG_DTC_STATUS_PENDING);

            if ((record->status & DIAG_DTC_STATUS_CONFIRMED) != 0u)
            {
                record->aging_counter = diag_dtc_increment_saturating_u16(record->aging_counter);
                if (record->aging_counter >= aging_threshold)
                {
                    // Aged out: the DTC heals back to a cleared state.
                    record->status = 0u;
                    record->active = false;
                    record->aging_counter = 0u;
                    record->failed_cycle_count = 0u;
                    continue;
                }
            }
        }
        else
        {
            // Still active, but the monitor did not report another failure this
            // cycle. This breaks consecutive-failure confirmation, but it is not a
            // clean pass and must not clear pending or age confirmed DTCs.
            record->failed_cycle_count = 0u;
        }

        // Start the next operation cycle: drop the per-cycle latch and bit, and
        // mark the monitor as not yet completed in the new cycle.
        record->failed_this_cycle = false;
        record->status =
            (uint8_t)(record->status & (uint8_t)~DIAG_DTC_STATUS_TEST_FAILED_THIS_OPERATION_CYCLE);
        record->status =
            (uint8_t)(record->status | DIAG_DTC_STATUS_TEST_NOT_COMPLETED_THIS_OPERATION_CYCLE);
        if (!diag_dtc_snapshot_equal(&before, record))
        {
            changed = true;
        }
    }

    if (changed)
    {
        diag_context_mark_dirty(ctx, DIAG_DIRTY_DTC);
    }

    return DIAG_OK;
}
