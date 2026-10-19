#include "diag/capsule.h"

#include <string.h>

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFu);
    buffer[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFu);
    buffer[1] = (uint8_t)((value >> 8u) & 0xFFu);
    buffer[2] = (uint8_t)((value >> 16u) & 0xFFu);
    buffer[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)((uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8u) |
                      ((uint32_t)buffer[2] << 16u) | ((uint32_t)buffer[3] << 24u));
}

static size_t section_table_end(uint16_t section_count)
{
    return DIAG_CAPSULE_HEADER_SIZE +
           ((size_t)section_count * (size_t)DIAG_CAPSULE_SECTION_ENTRY_SIZE);
}

static int range_past_end(uint32_t offset, uint32_t length, uint32_t total_length)
{
    return offset > total_length || length > (total_length - offset);
}

// clang-format off
static int sections_overlap(const struct diag_capsule_section *left,
                            const struct diag_capsule_section *right)
// clang-format on
{
    const uint32_t left_end = left->offset + left->length;
    const uint32_t right_end = right->offset + right->length;

    return left->offset < right_end && right->offset < left_end;
}

// clang-format off
static enum diag_result validate_sections(const struct diag_capsule_section *sections,
                                          uint16_t section_count, uint32_t total_length,
                                          size_t payload_start)
// clang-format on
{
    uint16_t i = 0u;

    for (i = 0u; i < section_count; ++i)
    {
        const struct diag_capsule_section *section = &sections[i];

        if (section->offset < payload_start)
        {
            return DIAG_ERROR_CORRUPT_DATA;
        }

        if (section->used_length > section->length)
        {
            return DIAG_ERROR_CORRUPT_DATA;
        }

        if (range_past_end(section->offset, section->length, total_length) != 0)
        {
            return DIAG_ERROR_CORRUPT_DATA;
        }
    }

    for (i = 0u; i < section_count; ++i)
    {
        uint16_t j = 0u;

        for (j = (uint16_t)(i + 1u); j < section_count; ++j)
        {
            if (sections_overlap(&sections[i], &sections[j]) != 0)
            {
                return DIAG_ERROR_CORRUPT_DATA;
            }
        }
    }

    return DIAG_OK;
}

uint32_t diag_capsule_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t   i = 0u;

    if (data == NULL && length != 0u)
    {
        return 0u;
    }

    for (i = 0u; i < length; ++i)
    {
        uint8_t bit = 0u;

        crc ^= (uint32_t)data[i];
        for (bit = 0u; bit < 8u; ++bit)
        {
            const uint32_t mask = (uint32_t)(0u - (crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

// clang-format off
enum diag_result diag_capsule_encode_v1(uint8_t *buffer, size_t capacity,
                                        const struct diag_capsule_descriptor *descriptor,
                                        size_t *encoded_length)
// clang-format on
{
    uint16_t i = 0u;
    size_t   payload_start = 0u;
    uint32_t content_crc32 = 0u;

    if (buffer == NULL || descriptor == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (descriptor->schema_version != DIAG_CAPSULE_SCHEMA_VERSION)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (descriptor->section_count > DIAG_CAPSULE_MAX_SECTIONS)
    {
        return DIAG_ERROR_CAPACITY;
    }

    payload_start = section_table_end(descriptor->section_count);
    if (descriptor->total_length < payload_start || capacity < descriptor->total_length)
    {
        return DIAG_ERROR_CAPACITY;
    }

    // The descriptor is caller-supplied input, so a structural failure is an
    // argument error. DIAG_ERROR_CORRUPT_DATA is reserved for decode, where it
    // signals untrusted on-wire/on-disk bytes that failed the same checks.
    if (validate_sections(descriptor->sections, descriptor->section_count, descriptor->total_length,
                          payload_start) != DIAG_OK)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    write_u32_le(&buffer[0], DIAG_CAPSULE_MAGIC);
    write_u16_le(&buffer[4], DIAG_CAPSULE_SCHEMA_VERSION);
    write_u16_le(&buffer[6], DIAG_CAPSULE_HEADER_SIZE);
    write_u32_le(&buffer[8], descriptor->total_length);
    write_u32_le(&buffer[12], descriptor->generation);
    write_u16_le(&buffer[16], descriptor->section_count);
    write_u16_le(&buffer[18], 0u);
    write_u32_le(&buffer[20], 0u);

    for (i = 0u; i < descriptor->section_count; ++i)
    {
        const struct diag_capsule_section *section = &descriptor->sections[i];
        const size_t                       entry_offset =
            DIAG_CAPSULE_HEADER_SIZE + ((size_t)i * DIAG_CAPSULE_SECTION_ENTRY_SIZE);

        write_u16_le(&buffer[entry_offset], section->type);
        write_u16_le(&buffer[entry_offset + 2u], section->version);
        write_u32_le(&buffer[entry_offset + 4u], section->offset);
        write_u32_le(&buffer[entry_offset + 8u], section->length);
        write_u32_le(&buffer[entry_offset + 12u], section->used_length);
    }

    content_crc32 = diag_capsule_crc32(&buffer[DIAG_CAPSULE_HEADER_SIZE],
                                       (size_t)descriptor->total_length - DIAG_CAPSULE_HEADER_SIZE);
    write_u32_le(&buffer[20], content_crc32);

    if (encoded_length != NULL)
    {
        *encoded_length = descriptor->total_length;
    }

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_capsule_decode(const uint8_t *buffer, size_t length,
                                     struct diag_capsule_descriptor *out_descriptor)
// clang-format on
{
    uint16_t section_count = 0u;
    uint16_t i = 0u;
    size_t   payload_start = 0u;
    uint32_t total_length = 0u;
    uint32_t expected_content_crc32 = 0u;
    uint32_t actual_content_crc32 = 0u;

    if (buffer == NULL || out_descriptor == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (length < DIAG_CAPSULE_HEADER_SIZE)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (read_u32_le(&buffer[0]) != DIAG_CAPSULE_MAGIC)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (read_u16_le(&buffer[4]) != DIAG_CAPSULE_SCHEMA_VERSION)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (read_u16_le(&buffer[6]) != DIAG_CAPSULE_HEADER_SIZE)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    total_length = read_u32_le(&buffer[8]);
    if (total_length > length)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    section_count = read_u16_le(&buffer[16]);
    if (section_count > DIAG_CAPSULE_MAX_SECTIONS)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    payload_start = section_table_end(section_count);
    if (total_length < payload_start)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    memset(out_descriptor, 0, sizeof(*out_descriptor));
    out_descriptor->schema_version = DIAG_CAPSULE_SCHEMA_VERSION;
    out_descriptor->section_count = section_count;
    out_descriptor->total_length = total_length;
    out_descriptor->generation = read_u32_le(&buffer[12]);
    out_descriptor->content_crc32 = read_u32_le(&buffer[20]);

    for (i = 0u; i < section_count; ++i)
    {
        struct diag_capsule_section *section = &out_descriptor->sections[i];
        const size_t                 entry_offset =
            DIAG_CAPSULE_HEADER_SIZE + ((size_t)i * DIAG_CAPSULE_SECTION_ENTRY_SIZE);

        section->type = read_u16_le(&buffer[entry_offset]);
        section->version = read_u16_le(&buffer[entry_offset + 2u]);
        section->offset = read_u32_le(&buffer[entry_offset + 4u]);
        section->length = read_u32_le(&buffer[entry_offset + 8u]);
        section->used_length = read_u32_le(&buffer[entry_offset + 12u]);
    }

    if (validate_sections(out_descriptor->sections, section_count, total_length, payload_start) !=
        DIAG_OK)
    {
        memset(out_descriptor, 0, sizeof(*out_descriptor));
        return DIAG_ERROR_CORRUPT_DATA;
    }

    expected_content_crc32 = out_descriptor->content_crc32;
    actual_content_crc32 = diag_capsule_crc32(&buffer[DIAG_CAPSULE_HEADER_SIZE],
                                              (size_t)total_length - DIAG_CAPSULE_HEADER_SIZE);
    if (actual_content_crc32 != expected_content_crc32)
    {
        memset(out_descriptor, 0, sizeof(*out_descriptor));
        return DIAG_ERROR_CORRUPT_DATA;
    }

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_capsule_section_owner_from_type(uint16_t type,
                                                      enum diag_capsule_section_owner *out_owner)
// clang-format on
{
    enum diag_capsule_section_owner owner = DIAG_CAPSULE_SECTION_OWNER_UNKNOWN;

    if (out_owner == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    switch (type)
    {
        case DIAG_CAPSULE_SECTION_BOOTLOADER_DTC:
            owner = DIAG_CAPSULE_SECTION_OWNER_BOOTLOADER;
            break;

        case DIAG_CAPSULE_SECTION_APPLICATION_DTC:
            owner = DIAG_CAPSULE_SECTION_OWNER_APPLICATION;
            break;

        case DIAG_CAPSULE_SECTION_LIFECYCLE:
        case DIAG_CAPSULE_SECTION_HANDOFF:
        case DIAG_CAPSULE_SECTION_RESET_COUNTERS:
            owner = DIAG_CAPSULE_SECTION_OWNER_SHARED;
            break;

        case DIAG_CAPSULE_SECTION_RESERVED:
            owner = DIAG_CAPSULE_SECTION_OWNER_RESERVED;
            break;

        default:
            owner = DIAG_CAPSULE_SECTION_OWNER_UNKNOWN;
            break;
    }

    *out_owner = owner;
    return DIAG_OK;
}

// clang-format off
enum diag_result diag_capsule_find_section_by_type(const struct diag_capsule_descriptor *descriptor,
                                                   uint16_t type,
                                                   const struct diag_capsule_section **out_section)
// clang-format on
{
    uint16_t i = 0u;

    if (descriptor == NULL || out_section == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    *out_section = NULL;
    if (descriptor->section_count > DIAG_CAPSULE_MAX_SECTIONS)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    for (i = 0u; i < descriptor->section_count; ++i)
    {
        const struct diag_capsule_section *section = &descriptor->sections[i];

        if (section->type == type)
        {
            *out_section = section;
            return DIAG_OK;
        }
    }

    return DIAG_ERROR_NOT_FOUND;
}

// clang-format off
enum diag_result
diag_capsule_find_section_by_owner(const struct diag_capsule_descriptor *descriptor,
                                   enum diag_capsule_section_owner owner,
                                   const struct diag_capsule_section **out_section)
// clang-format on
{
    uint16_t i = 0u;

    if (descriptor == NULL || out_section == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    *out_section = NULL;
    if (descriptor->section_count > DIAG_CAPSULE_MAX_SECTIONS)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    for (i = 0u; i < descriptor->section_count; ++i)
    {
        const struct diag_capsule_section *section = &descriptor->sections[i];
        enum diag_capsule_section_owner    section_owner = DIAG_CAPSULE_SECTION_OWNER_UNKNOWN;
        const enum diag_result             result =
            diag_capsule_section_owner_from_type(section->type, &section_owner);

        if (result != DIAG_OK)
        {
            return result;
        }

        if (section_owner == owner)
        {
            *out_section = section;
            return DIAG_OK;
        }
    }

    return DIAG_ERROR_NOT_FOUND;
}

// clang-format off
enum diag_result
diag_capsule_validate_section_bounds(const struct diag_capsule_descriptor *descriptor,
                                     const struct diag_capsule_section *section)
// clang-format on
{
    size_t payload_start = 0u;

    if (descriptor == NULL || section == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    if (descriptor->section_count > DIAG_CAPSULE_MAX_SECTIONS)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    payload_start = section_table_end(descriptor->section_count);
    if (descriptor->total_length < payload_start)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (section->offset < payload_start)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (section->used_length > section->length)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (range_past_end(section->offset, section->length, descriptor->total_length) != 0)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_capsule_copy_section_payload(const uint8_t *capsule, size_t capsule_length,
                                                   const struct diag_capsule_descriptor *descriptor,
                                                   const struct diag_capsule_section *section,
                                                   uint8_t *out_payload, size_t out_capacity,
                                                   size_t *out_length)
// clang-format on
{
    enum diag_result result = DIAG_OK;

    if (out_length != NULL)
    {
        *out_length = 0u;
    }

    if (capsule == NULL || descriptor == NULL || section == NULL || out_payload == NULL)
    {
        return DIAG_ERROR_INVALID_ARGUMENT;
    }

    result = diag_capsule_validate_section_bounds(descriptor, section);
    if (result != DIAG_OK)
    {
        return result;
    }

    if (descriptor->total_length > capsule_length)
    {
        return DIAG_ERROR_CORRUPT_DATA;
    }

    if (section->used_length > out_capacity)
    {
        return DIAG_ERROR_CAPACITY;
    }

    if (section->used_length != 0u)
    {
        memcpy(out_payload, &capsule[section->offset], section->used_length);
    }

    if (out_length != NULL)
    {
        *out_length = section->used_length;
    }

    return DIAG_OK;
}

// clang-format off
enum diag_result diag_capsule_copy_section_payload_by_type(
    const uint8_t *capsule, size_t capsule_length, const struct diag_capsule_descriptor *descriptor,
    uint16_t type, uint8_t *out_payload, size_t out_capacity, size_t *out_length)
// clang-format on
{
    const struct diag_capsule_section *section = NULL;
    enum diag_result                   result = DIAG_OK;

    if (out_length != NULL)
    {
        *out_length = 0u;
    }

    result = diag_capsule_find_section_by_type(descriptor, type, &section);
    if (result != DIAG_OK)
    {
        return result;
    }

    return diag_capsule_copy_section_payload(capsule, capsule_length, descriptor, section,
                                             out_payload, out_capacity, out_length);
}
