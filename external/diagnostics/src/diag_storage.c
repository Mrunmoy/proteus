#include "diag/storage.h"

#include "diag_context_internal.h"

#if DIAG_FEATURE_CAPSULE
#include "diag/capsule.h"
#endif

#include <string.h>

#define DIAG_DTC_CAPSULE_SECTION_VERSION (1u)
#define DIAG_DTC_CAPSULE_PAYLOAD_HEADER_SIZE (4u)
#define DIAG_DTC_CAPSULE_RECORD_SIZE (28u)
#define DIAG_LIFECYCLE_CAPSULE_SECTION_VERSION (1u)
#define DIAG_LIFECYCLE_CAPSULE_PAYLOAD_SIZE (16u)

static int diag_storage_size_is_aligned(size_t size, size_t alignment)
{
    return (size % alignment) == 0u;
}

#if DIAG_FEATURE_CAPSULE && (DIAG_FEATURE_DTC || DIAG_FEATURE_LIFECYCLE)
static size_t diag_storage_align_up_size(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;

    if (remainder == 0u)
    {
        return value;
    }

    return value + (alignment - remainder);
}

static int diag_storage_context_buffer_is_valid(const struct diag_storage *storage)
{
    return storage->capsule_buffer != NULL && storage->capsule_buffer_size != 0u;
}

static void diag_storage_write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFu);
    buffer[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void diag_storage_write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFu);
    buffer[1] = (uint8_t)((value >> 8u) & 0xFFu);
    buffer[2] = (uint8_t)((value >> 16u) & 0xFFu);
    buffer[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t diag_storage_read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8u));
}

static uint32_t diag_storage_read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)((uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8u) |
                      ((uint32_t)buffer[2] << 16u) | ((uint32_t)buffer[3] << 24u));
}
#endif

#if DIAG_FEATURE_DTC && DIAG_FEATURE_CAPSULE
// clang-format off
static enum diag_result diag_storage_encode_dtc_payload(struct diag_context *ctx,
                                                        uint8_t *payload,
                                                        size_t payload_capacity,
                                                        size_t *out_used_length)
// clang-format on
{
    size_t i = 0u;
    size_t required = DIAG_DTC_CAPSULE_PAYLOAD_HEADER_SIZE;

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_DTC_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    required += ctx->dtc_count * (size_t)DIAG_DTC_CAPSULE_RECORD_SIZE;
    if (payload_capacity < required || ctx->dtc_count > UINT16_MAX)
    {
        return DIAG_ERROR_CAPACITY;
    }

    diag_storage_write_u16_le(&payload[0], (uint16_t)ctx->dtc_count);
    diag_storage_write_u16_le(&payload[2], DIAG_DTC_CAPSULE_RECORD_SIZE);

    for (i = 0u; i < ctx->dtc_count; ++i)
    {
        const struct diag_dtc_snapshot *record = &ctx->dtc.records[i];
        const size_t                    offset =
            DIAG_DTC_CAPSULE_PAYLOAD_HEADER_SIZE + (i * DIAG_DTC_CAPSULE_RECORD_SIZE);

        diag_storage_write_u32_le(&payload[offset], record->id);
        diag_storage_write_u32_le(&payload[offset + 4u], record->local_fault_id);
        payload[offset + 8u] = (uint8_t)record->severity;
        payload[offset + 9u] = record->active ? 1u : 0u;
        payload[offset + 10u] = record->failed_this_cycle ? 1u : 0u;
        payload[offset + 11u] = record->status;
        payload[offset + 12u] = record->failed_cycle_count;
        payload[offset + 13u] = 0u;
        diag_storage_write_u16_le(&payload[offset + 14u], record->aging_counter);
        diag_storage_write_u32_le(&payload[offset + 16u], record->occurrence_count);
        diag_storage_write_u32_le(&payload[offset + 20u], record->active_count);
        diag_storage_write_u32_le(&payload[offset + 24u], record->clear_count);
    }

    *out_used_length = required;

    return DIAG_OK;
}

// clang-format off
static enum diag_result diag_storage_decode_dtc_payload(struct diag_context *ctx,
                                                        const uint8_t *payload,
                                                        size_t payload_length)
// clang-format on
{
    uint16_t count = 0u;
    uint16_t record_size = 0u;
    size_t   expected_length = 0u;
    size_t   i = 0u;

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_DTC_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (payload_length < DIAG_DTC_CAPSULE_PAYLOAD_HEADER_SIZE)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    count = diag_storage_read_u16_le(&payload[0]);
    record_size = diag_storage_read_u16_le(&payload[2]);
    if (record_size != DIAG_DTC_CAPSULE_RECORD_SIZE)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    expected_length =
        DIAG_DTC_CAPSULE_PAYLOAD_HEADER_SIZE + ((size_t)count * DIAG_DTC_CAPSULE_RECORD_SIZE);
    if (payload_length != expected_length)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if ((size_t)count > ctx->dtc.capacity)
    {
        return DIAG_ERROR_CAPACITY;
    }

    for (i = 0u; i < count; ++i)
    {
        struct diag_dtc_snapshot *record = &ctx->dtc.records[i];
        const size_t              offset =
            DIAG_DTC_CAPSULE_PAYLOAD_HEADER_SIZE + (i * DIAG_DTC_CAPSULE_RECORD_SIZE);

        if (payload[offset + 8u] > (uint8_t)DIAG_DTC_SEVERITY_CRITICAL ||
            payload[offset + 9u] > 1u || payload[offset + 10u] > 1u || payload[offset + 13u] != 0u)
        {
            return DIAG_ERROR_CORRUPT_DATA;
        }

        record->id = diag_storage_read_u32_le(&payload[offset]);
        record->local_fault_id = diag_storage_read_u32_le(&payload[offset + 4u]);
        record->severity = (enum diag_dtc_severity)payload[offset + 8u];
        record->active = payload[offset + 9u] != 0u;
        record->failed_this_cycle = payload[offset + 10u] != 0u;
        record->status = payload[offset + 11u];
        record->failed_cycle_count = payload[offset + 12u];
        record->aging_counter = diag_storage_read_u16_le(&payload[offset + 14u]);
        record->occurrence_count = diag_storage_read_u32_le(&payload[offset + 16u]);
        record->active_count = diag_storage_read_u32_le(&payload[offset + 20u]);
        record->clear_count = diag_storage_read_u32_le(&payload[offset + 24u]);
    }

    ctx->dtc_count = count;
    diag_context_clear_dirty(ctx, DIAG_DIRTY_DTC);

    return DIAG_OK;
}
#endif

#if DIAG_FEATURE_LIFECYCLE && DIAG_FEATURE_CAPSULE
// clang-format off
static enum diag_result diag_storage_encode_lifecycle_payload(struct diag_context *ctx,
                                                              uint8_t *payload,
                                                              size_t payload_capacity,
                                                              size_t *out_used_length)
// clang-format on
{
    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (payload_capacity < DIAG_LIFECYCLE_CAPSULE_PAYLOAD_SIZE)
    {
        return DIAG_ERROR_CAPACITY;
    }

    if (ctx->last_reset_reason > DIAG_RESET_REASON_FAULT ||
        ctx->lifecycle.reset_counter_policy > DIAG_RESET_COUNTER_POLICY_PLATFORM)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    diag_storage_write_u16_le(&payload[0], DIAG_LIFECYCLE_CAPSULE_PAYLOAD_SIZE);
    payload[2] = (uint8_t)ctx->last_reset_reason;
    payload[3] = (uint8_t)ctx->lifecycle.reset_counter_policy;
    diag_storage_write_u32_le(&payload[4], ctx->reset_count);
    diag_storage_write_u32_le(&payload[8], ctx->abnormal_reset_count);
    diag_storage_write_u32_le(&payload[12], 0u);

    *out_used_length = DIAG_LIFECYCLE_CAPSULE_PAYLOAD_SIZE;

    return DIAG_OK;
}

// clang-format off
static enum diag_result diag_storage_decode_lifecycle_payload(struct diag_context *ctx,
                                                              const uint8_t *payload,
                                                              size_t payload_length)
// clang-format on
{
    uint16_t payload_size = 0u;

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (payload_length != DIAG_LIFECYCLE_CAPSULE_PAYLOAD_SIZE)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    payload_size = diag_storage_read_u16_le(&payload[0]);
    if (payload_size != DIAG_LIFECYCLE_CAPSULE_PAYLOAD_SIZE ||
        payload[2] > (uint8_t)DIAG_RESET_REASON_FAULT ||
        payload[3] > (uint8_t)DIAG_RESET_COUNTER_POLICY_PLATFORM ||
        diag_storage_read_u32_le(&payload[12]) != 0u)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    ctx->last_reset_reason = (enum diag_reset_reason)payload[2];
    ctx->lifecycle.reset_counter_policy = (enum diag_reset_counter_policy)payload[3];
    ctx->reset_count = diag_storage_read_u32_le(&payload[4]);
    ctx->abnormal_reset_count = diag_storage_read_u32_le(&payload[8]);
    ctx->lifecycle_dirty_flags = DIAG_LIFECYCLE_DIRTY_NONE;
    diag_context_clear_dirty(ctx, DIAG_DIRTY_LIFECYCLE);

    return DIAG_OK;
}
#endif

#if DIAG_FEATURE_CAPSULE && (DIAG_FEATURE_DTC || DIAG_FEATURE_LIFECYCLE)
static enum diag_result diag_storage_save_context_capsule(struct diag_context *ctx)
{
    struct diag_capsule_descriptor descriptor = {0};
    uint8_t                       *buffer = ctx->storage.capsule_buffer;
    size_t                         payload_offset = DIAG_CAPSULE_HEADER_SIZE;
    size_t                         section_index = 0u;
    size_t                         used_length = 0u;
    size_t                         length = 0u;
    size_t                         total_length = 0u;
    enum diag_result               result = DIAG_OK;

    if (!diag_storage_context_buffer_is_valid(&ctx->storage))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (ctx->storage.capsule_buffer_size < payload_offset)
    {
        return DIAG_ERROR_CAPACITY;
    }

    memset(buffer, ctx->storage.capabilities.erase_value, ctx->storage.capsule_buffer_size);

    descriptor.schema_version = DIAG_CAPSULE_SCHEMA_VERSION;
    descriptor.generation = 0u;

#if DIAG_FEATURE_DTC
    if ((ctx->dirty_flags & DIAG_DIRTY_DTC) != 0u)
    {
        payload_offset += DIAG_CAPSULE_SECTION_ENTRY_SIZE;
    }
#endif

#if DIAG_FEATURE_LIFECYCLE
    if ((ctx->dirty_flags & DIAG_DIRTY_LIFECYCLE) != 0u)
    {
        payload_offset += DIAG_CAPSULE_SECTION_ENTRY_SIZE;
    }
#endif

    if (ctx->storage.capsule_buffer_size < payload_offset)
    {
        return DIAG_ERROR_CAPACITY;
    }

#if DIAG_FEATURE_DTC
    if ((ctx->dirty_flags & DIAG_DIRTY_DTC) != 0u)
    {
        used_length = 0u;
        result = diag_storage_encode_dtc_payload(ctx, &buffer[payload_offset],
                                                 ctx->storage.capsule_buffer_size - payload_offset,
                                                 &used_length);
        if (result != DIAG_OK)
        {
            return result;
        }

        length = diag_storage_align_up_size(used_length, ctx->storage.capabilities.write_alignment);
        if (length > (ctx->storage.capsule_buffer_size - payload_offset) ||
            payload_offset > UINT32_MAX || length > UINT32_MAX || used_length > UINT32_MAX)
        {
            return DIAG_ERROR_CAPACITY;
        }

        descriptor.sections[section_index].type = DIAG_CAPSULE_SECTION_APPLICATION_DTC;
        descriptor.sections[section_index].version = DIAG_DTC_CAPSULE_SECTION_VERSION;
        descriptor.sections[section_index].offset = (uint32_t)payload_offset;
        descriptor.sections[section_index].length = (uint32_t)length;
        descriptor.sections[section_index].used_length = (uint32_t)used_length;
        ++section_index;
        payload_offset += length;
    }
#endif

#if DIAG_FEATURE_LIFECYCLE
    if ((ctx->dirty_flags & DIAG_DIRTY_LIFECYCLE) != 0u)
    {
        used_length = 0u;
        result = diag_storage_encode_lifecycle_payload(
            ctx, &buffer[payload_offset], ctx->storage.capsule_buffer_size - payload_offset,
            &used_length);
        if (result != DIAG_OK)
        {
            return result;
        }

        length = diag_storage_align_up_size(used_length, ctx->storage.capabilities.write_alignment);
        if (length > (ctx->storage.capsule_buffer_size - payload_offset) ||
            payload_offset > UINT32_MAX || length > UINT32_MAX || used_length > UINT32_MAX)
        {
            return DIAG_ERROR_CAPACITY;
        }

        descriptor.sections[section_index].type = DIAG_CAPSULE_SECTION_LIFECYCLE;
        descriptor.sections[section_index].version = DIAG_LIFECYCLE_CAPSULE_SECTION_VERSION;
        descriptor.sections[section_index].offset = (uint32_t)payload_offset;
        descriptor.sections[section_index].length = (uint32_t)length;
        descriptor.sections[section_index].used_length = (uint32_t)used_length;
        ++section_index;
        payload_offset += length;
    }
#endif

    total_length =
        diag_storage_align_up_size(payload_offset, ctx->storage.capabilities.write_alignment);
    if (total_length > ctx->storage.capsule_buffer_size || total_length > UINT32_MAX ||
        section_index > UINT16_MAX)
    {
        return DIAG_ERROR_CAPACITY;
    }

    descriptor.section_count = (uint16_t)section_index;
    descriptor.total_length = (uint32_t)total_length;

    result = diag_capsule_encode_v1(buffer, ctx->storage.capsule_buffer_size, &descriptor, NULL);
    if (result != DIAG_OK)
    {
        return result;
    }

    result = diag_storage_save(&ctx->storage, buffer, total_length);
    if (result == DIAG_OK)
    {
#if DIAG_FEATURE_DTC
        if ((ctx->dirty_flags & DIAG_DIRTY_DTC) != 0u)
        {
            diag_context_clear_dirty(ctx, DIAG_DIRTY_DTC);
        }
#endif
#if DIAG_FEATURE_LIFECYCLE
        if ((ctx->dirty_flags & DIAG_DIRTY_LIFECYCLE) != 0u)
        {
            ctx->lifecycle_dirty_flags = DIAG_LIFECYCLE_DIRTY_NONE;
            diag_context_clear_dirty(ctx, DIAG_DIRTY_LIFECYCLE);
        }
#endif
    }

    return result;
}

static enum diag_result diag_storage_load_context_capsule(struct diag_context *ctx)
{
    struct diag_capsule_descriptor     descriptor = {0};
    const struct diag_capsule_section *section = NULL;
    size_t                             bytes_read = 0u;
    enum diag_result                   result = DIAG_OK;

    if (!diag_storage_context_buffer_is_valid(&ctx->storage))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    result = diag_storage_load(&ctx->storage, ctx->storage.capsule_buffer,
                               ctx->storage.capsule_buffer_size, &bytes_read);
    if (result != DIAG_OK)
    {
        return result;
    }

    if (bytes_read == 0u)
    {
        return DIAG_OK;
    }

    result = diag_capsule_decode(ctx->storage.capsule_buffer, bytes_read, &descriptor);
    if (result != DIAG_OK)
    {
        return result;
    }

#if DIAG_FEATURE_DTC
    result = diag_capsule_find_section_by_type(&descriptor, DIAG_CAPSULE_SECTION_APPLICATION_DTC,
                                               &section);
    if (result != DIAG_OK)
    {
        if (result != DIAG_ERROR_NOT_FOUND)
        {
            return result;
        }
    }
    else
    {
        if (section->version != DIAG_DTC_CAPSULE_SECTION_VERSION)
        {
            return DIAG_ERROR_CORRUPT_DATA;
        }

        result = diag_storage_decode_dtc_payload(ctx, &ctx->storage.capsule_buffer[section->offset],
                                                 section->used_length);
        if (result != DIAG_OK)
        {
            return result;
        }
    }
#endif

#if DIAG_FEATURE_LIFECYCLE
    section = NULL;
    result =
        diag_capsule_find_section_by_type(&descriptor, DIAG_CAPSULE_SECTION_LIFECYCLE, &section);
    if (result != DIAG_OK)
    {
        if (result != DIAG_ERROR_NOT_FOUND)
        {
            return result;
        }
    }
    else
    {
        if (section->version != DIAG_LIFECYCLE_CAPSULE_SECTION_VERSION)
        {
            return DIAG_ERROR_CORRUPT_DATA;
        }

        result = diag_storage_decode_lifecycle_payload(
            ctx, &ctx->storage.capsule_buffer[section->offset], section->used_length);
        if (result != DIAG_OK)
        {
            return result;
        }
    }
#endif

    return DIAG_OK;
}
#endif

enum diag_result diag_storage_attach(struct diag_context *ctx, const struct diag_storage *storage)
{
    enum diag_result result;

    if (ctx == NULL || storage == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    result = diag_storage_validate(storage);
    if (result != DIAG_OK)
    {
        return result;
    }

    ctx->storage = *storage;
    diag_context_set_state(ctx, DIAG_CONTEXT_STATE_STORAGE_ATTACHED);

    return DIAG_OK;
}

enum diag_result
diag_storage_validate_capabilities(const struct diag_storage_capabilities *capabilities)
{
    if (capabilities == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (capabilities->write_alignment == 0u)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if ((capabilities->atomic_commit != DIAG_STORAGE_ATOMIC_COMMIT_NONE) &&
        (capabilities->atomic_commit != DIAG_STORAGE_ATOMIC_COMMIT_ADAPTER))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if ((capabilities->wear_leveling != DIAG_STORAGE_WEAR_LEVELING_NONE) &&
        (capabilities->wear_leveling != DIAG_STORAGE_WEAR_LEVELING_ADAPTER))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    return DIAG_OK;
}

enum diag_result diag_storage_validate(const struct diag_storage *storage)
{
    if ((storage == NULL) || (storage->ops == NULL) || (storage->ops->load == NULL) ||
        (storage->ops->save == NULL) || (storage->ops->clear == NULL))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    return diag_storage_validate_capabilities(&storage->capabilities);
}

enum diag_result diag_storage_load(const struct diag_storage *storage, uint8_t *buffer,
                                   size_t buffer_size, size_t *bytes_read)
{
    enum diag_result result = diag_storage_validate(storage);

    if (result != DIAG_OK)
    {
        return result;
    }

    if ((buffer == NULL) || (buffer_size == 0u) || (bytes_read == NULL))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    *bytes_read = 0u;
    return storage->ops->load(storage->user, buffer, buffer_size, bytes_read);
}

enum diag_result diag_storage_save(const struct diag_storage *storage, const uint8_t *buffer,
                                   size_t size)
{
    enum diag_result result = diag_storage_validate(storage);

    if (result != DIAG_OK)
    {
        return result;
    }

    if ((buffer == NULL) || (size == 0u) ||
        !diag_storage_size_is_aligned(size, storage->capabilities.write_alignment))
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    return storage->ops->save(storage->user, buffer, size);
}

enum diag_result diag_storage_clear(const struct diag_storage *storage)
{
    enum diag_result result = diag_storage_validate(storage);

    if (result != DIAG_OK)
    {
        return result;
    }

    return storage->ops->clear(storage->user);
}

enum diag_result diag_save(struct diag_context *ctx)
{
    uint32_t supported_dirty_flags = DIAG_DIRTY_NONE;

    if (ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED |
                                         DIAG_CONTEXT_STATE_STORAGE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

    if (ctx->dirty_flags == DIAG_DIRTY_NONE)
    {
        return DIAG_OK;
    }

#if DIAG_FEATURE_DTC && DIAG_FEATURE_CAPSULE
    supported_dirty_flags |= DIAG_DIRTY_DTC;
#endif

#if DIAG_FEATURE_LIFECYCLE && DIAG_FEATURE_CAPSULE
    supported_dirty_flags |= DIAG_DIRTY_LIFECYCLE;
#endif

    if ((ctx->dirty_flags & ~supported_dirty_flags) != 0u)
    {
        return DIAG_ERROR_NOT_SUPPORTED;
    }

#if DIAG_FEATURE_CAPSULE && (DIAG_FEATURE_DTC || DIAG_FEATURE_LIFECYCLE)
    return diag_storage_save_context_capsule(ctx);
#endif

    return DIAG_ERROR_NOT_SUPPORTED;
}

enum diag_result diag_load(struct diag_context *ctx)
{
    if (ctx == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (!diag_context_has_state(ctx, DIAG_CONTEXT_STATE_INITIALIZED |
                                         DIAG_CONTEXT_STATE_STORAGE_ATTACHED))
    {
        return DIAG_ERROR_NOT_INITIALIZED;
    }

#if DIAG_FEATURE_CAPSULE && (DIAG_FEATURE_DTC || DIAG_FEATURE_LIFECYCLE)
    return diag_storage_load_context_capsule(ctx);
#else
    return DIAG_ERROR_NOT_SUPPORTED;
#endif
}
