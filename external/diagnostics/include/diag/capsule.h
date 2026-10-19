/// @file
/// Versioned serialized diagnostics capsule helpers.

#ifndef DIAG_CAPSULE_H
#define DIAG_CAPSULE_H

#include <stddef.h>
#include <stdint.h>

#include "diag/compiler.h"
#include "diag/result.h"

DIAG_EXTERN_C_BEGIN

/// Current supported capsule schema version.
#define DIAG_CAPSULE_SCHEMA_VERSION (1u)
/// Maximum number of sections accepted in a capsule descriptor.
#define DIAG_CAPSULE_MAX_SECTIONS (8u)
/// Encoded capsule header size in bytes.
#define DIAG_CAPSULE_HEADER_SIZE (24u)
/// Encoded section table entry size in bytes.
#define DIAG_CAPSULE_SECTION_ENTRY_SIZE (16u)
/// Little-endian capsule magic value used to identify diagnostics capsules.
#define DIAG_CAPSULE_MAGIC (0x50434744u)

/// Well-known capsule section types.
enum diag_capsule_section_type
{
    /// DTC payload owned by bootloader firmware.
    DIAG_CAPSULE_SECTION_BOOTLOADER_DTC = 1,
    /// DTC payload owned by application firmware.
    DIAG_CAPSULE_SECTION_APPLICATION_DTC = 2,
    /// Shared lifecycle payload.
    DIAG_CAPSULE_SECTION_LIFECYCLE = 3,
    /// Volatile or platform-mediated handoff payload descriptor.
    DIAG_CAPSULE_SECTION_HANDOFF = 4,
    /// Reset counter payload.
    DIAG_CAPSULE_SECTION_RESET_COUNTERS = 5,
    /// Reserved section type marker.
    DIAG_CAPSULE_SECTION_RESERVED = 0xFFFFu
};

/// Write ownership class derived from a section type.
enum diag_capsule_section_owner
{
    /// Unknown or future section type.
    DIAG_CAPSULE_SECTION_OWNER_UNKNOWN = 0,
    /// Section belongs to the bootloader bank.
    DIAG_CAPSULE_SECTION_OWNER_BOOTLOADER = 1,
    /// Section belongs to the application bank.
    DIAG_CAPSULE_SECTION_OWNER_APPLICATION = 2,
    /// Section is shared and should be modified only by explicit policy.
    DIAG_CAPSULE_SECTION_OWNER_SHARED = 3,
    /// Reserved section owner.
    DIAG_CAPSULE_SECTION_OWNER_RESERVED = 4
};

/// Decoded capsule section table entry.
///
/// Offsets and lengths describe byte ranges inside the serialized capsule, not
/// raw C structure layouts. `used_length` is the active payload byte count and
/// must not exceed `length`.
struct diag_capsule_section
{
    /// Section type, usually one of `enum diag_capsule_section_type`.
    uint16_t type;
    /// Section-specific payload schema version.
    uint16_t version;
    /// Byte offset from the start of the serialized capsule.
    uint32_t offset;
    /// Allocated section length in bytes.
    uint32_t length;
    /// Used payload length in bytes.
    uint32_t used_length;
};

/// Decoded capsule header and bounded section table.
struct diag_capsule_descriptor
{
    /// Capsule schema version; currently `DIAG_CAPSULE_SCHEMA_VERSION`.
    uint16_t schema_version;
    /// Number of valid entries in `sections`.
    uint16_t section_count;
    /// Total serialized capsule length in bytes.
    uint32_t total_length;
    /// Caller-managed generation counter for commit ordering.
    uint32_t generation;
    /// CRC-32 over serialized bytes after the fixed capsule header.
    uint32_t content_crc32;
    /// Fixed-capacity section table.
    struct diag_capsule_section sections[DIAG_CAPSULE_MAX_SECTIONS];
};

// clang-format off
/// Compute the capsule CRC-32 used by schema version 1.
///
/// A null data pointer is valid only when `length` is zero. The implementation
/// uses the standard reflected CRC-32 polynomial.
///
/// @return CRC-32 value for the supplied byte range.
uint32_t diag_capsule_crc32(const uint8_t *data, size_t length);

/// Encode a schema-version-1 capsule header and section table.
///
/// The descriptor is validated before writing header bytes. Payload bytes are
/// owned by the caller; this function does not populate section payload data.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_CAPACITY`.
enum diag_result diag_capsule_encode_v1(uint8_t *buffer, size_t capacity,
                                        const struct diag_capsule_descriptor *descriptor,
                                        size_t *encoded_length);

/// Decode and validate a serialized capsule descriptor.
///
/// The decoder rejects unsupported schema versions, oversized section counts,
/// out-of-bounds or overlapping sections, and CRC mismatches before returning
/// `DIAG_OK`.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_CORRUPT_DATA`.
enum diag_result diag_capsule_decode(const uint8_t *buffer, size_t length,
                                     struct diag_capsule_descriptor *out_descriptor);

/// Map a section type to its ownership class.
///
/// @return `DIAG_OK` or `DIAG_ERROR_INVALID_ARGUMENT`.
enum diag_result diag_capsule_section_owner_from_type(uint16_t type,
                                                      enum diag_capsule_section_owner *out_owner);

/// Find the first section with a matching type.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`,
///         or `DIAG_ERROR_CORRUPT_DATA`.
enum diag_result diag_capsule_find_section_by_type(const struct diag_capsule_descriptor *descriptor,
                                                   uint16_t type,
                                                   const struct diag_capsule_section **out_section);

/// Find the first section with a matching ownership class.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`,
///         or `DIAG_ERROR_CORRUPT_DATA`.
enum diag_result
diag_capsule_find_section_by_owner(const struct diag_capsule_descriptor *descriptor,
                                   enum diag_capsule_section_owner owner,
                                   const struct diag_capsule_section **out_section);

/// Validate one section's bounds against a decoded descriptor.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_CORRUPT_DATA`.
enum diag_result
diag_capsule_validate_section_bounds(const struct diag_capsule_descriptor *descriptor,
                                     const struct diag_capsule_section *section);

/// Copy the used payload bytes for a validated section.
///
/// Only `section->used_length` bytes are copied. `*out_length` is set to zero
/// before validation when the pointer is provided.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, `DIAG_ERROR_CAPACITY`,
///         or `DIAG_ERROR_CORRUPT_DATA`.
enum diag_result diag_capsule_copy_section_payload(const uint8_t *capsule, size_t capsule_length,
                                                   const struct diag_capsule_descriptor *descriptor,
                                                   const struct diag_capsule_section *section,
                                                   uint8_t *out_payload, size_t out_capacity,
                                                   size_t *out_length);

/// Find a section by type and copy its used payload bytes.
///
/// @return `DIAG_OK`, `DIAG_ERROR_NOT_FOUND`, `DIAG_ERROR_INVALID_ARGUMENT`,
///         `DIAG_ERROR_CAPACITY`, or `DIAG_ERROR_CORRUPT_DATA`.
enum diag_result diag_capsule_copy_section_payload_by_type(
    const uint8_t *capsule, size_t capsule_length, const struct diag_capsule_descriptor *descriptor,
    uint16_t type, uint8_t *out_payload, size_t out_capacity, size_t *out_length);
// clang-format on

DIAG_EXTERN_C_END

#endif
