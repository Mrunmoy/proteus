/// @file
/// Storage adapter contract for explicit persistent diagnostics operations.

#ifndef DIAG_STORAGE_H
#define DIAG_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "diag/compiler.h"
#include "diag/result.h"

DIAG_EXTERN_C_BEGIN

/// Whether the storage adapter provides atomic commit semantics.
enum diag_storage_atomic_commit
{
    /// The adapter does not claim atomic commit behavior.
    DIAG_STORAGE_ATOMIC_COMMIT_NONE = 0,
    /// The adapter owns atomic commit or equivalent power-fail handling.
    DIAG_STORAGE_ATOMIC_COMMIT_ADAPTER = 1
};

/// Whether the storage adapter handles wear leveling.
enum diag_storage_wear_leveling
{
    /// The adapter does not claim wear leveling.
    DIAG_STORAGE_WEAR_LEVELING_NONE = 0,
    /// The adapter owns wear leveling for its backing medium.
    DIAG_STORAGE_WEAR_LEVELING_ADAPTER = 1
};

/// Declared properties of a storage adapter.
struct diag_storage_capabilities
{
    /// Erased byte value for the medium, commonly `0xFF` or `0x00`.
    uint8_t erase_value;
    /// Required write size alignment in bytes; must be nonzero.
    size_t write_alignment;
    /// Atomic commit behavior owned by the adapter.
    enum diag_storage_atomic_commit atomic_commit;
    /// Wear-leveling behavior owned by the adapter.
    enum diag_storage_wear_leveling wear_leveling;
};

/// Storage callback table implemented by the platform or downstream project.
///
/// Callbacks receive `struct diag_storage::user` unchanged. They must not retain
/// pointers to temporary buffers after returning unless the downstream adapter
/// contract explicitly owns that lifetime.
struct diag_storage_ops
{
    /// Load serialized bytes into `buffer`, reporting the number of bytes read.
    enum diag_result (*load)(void *user, uint8_t *buffer, size_t buffer_size, size_t *bytes_read);
    /// Save `size` serialized bytes from `buffer`.
    enum diag_result (*save)(void *user, const uint8_t *buffer, size_t size);
    /// Clear or invalidate the adapter's stored diagnostics payload.
    enum diag_result (*clear)(void *user);
};

/// Storage adapter instance.
struct diag_storage
{
    /// Callback table; all callbacks are required for validation.
    const struct diag_storage_ops *ops;
    /// Opaque adapter state passed to every callback.
    void *user;
    /// Medium and adapter capabilities used for validation.
    struct diag_storage_capabilities capabilities;
    /// Caller-owned staging buffer used by context-level capsule save/load.
    uint8_t *capsule_buffer;
    /// Size of `capsule_buffer` in bytes.
    size_t capsule_buffer_size;
};

/// Opaque diagnostics context initialized with `diag_init()`.
struct diag_context;

/// Attach a storage adapter to an initialized diagnostics context.
///
/// The adapter is copied by value into the context. Callback tables, user state,
/// and capsule buffers must remain valid for as long as the storage feature is
/// used.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or
///         `DIAG_ERROR_NOT_INITIALIZED`.
enum diag_result diag_storage_attach(struct diag_context *ctx, const struct diag_storage *storage);

/// Validate storage capability values.
///
/// @return `DIAG_OK` or `DIAG_ERROR_INVALID_ARGUMENT`.
enum diag_result
diag_storage_validate_capabilities(const struct diag_storage_capabilities *capabilities);

/// Validate that a storage adapter has required callbacks and valid capabilities.
///
/// @return `DIAG_OK` or `DIAG_ERROR_INVALID_ARGUMENT`.
enum diag_result diag_storage_validate(const struct diag_storage *storage);

/// Invoke the adapter load callback after validating arguments.
///
/// `*bytes_read` is set to zero before the callback is called.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or the callback result.
enum diag_result diag_storage_load(const struct diag_storage *storage, uint8_t *buffer,
                                   size_t buffer_size, size_t *bytes_read);

/// Invoke the adapter save callback after validating arguments and write alignment.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or the callback result.
enum diag_result diag_storage_save(const struct diag_storage *storage, const uint8_t *buffer,
                                   size_t size);

/// Invoke the adapter clear callback after validating the adapter.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`, or the callback result.
enum diag_result diag_storage_clear(const struct diag_storage *storage);

/// Save persistent diagnostic state through the attached storage adapter.
///
/// Returns `DIAG_ERROR_INVALID_ARGUMENT` when `ctx` is null. Returns
/// `DIAG_ERROR_NOT_INITIALIZED` when the context is not initialized or storage
/// has not been attached. Returns `DIAG_OK` when no persistent state is dirty.
/// DTC persistence requires `struct diag_storage::capsule_buffer`. Unsupported
/// dirty feature bits return `DIAG_ERROR_NOT_SUPPORTED`.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`,
///         `DIAG_ERROR_NOT_INITIALIZED`, `DIAG_ERROR_CAPACITY`,
///         `DIAG_ERROR_NOT_SUPPORTED`, or a storage/capsule error.
enum diag_result diag_save(struct diag_context *ctx);

/// Load persistent diagnostic state through the attached storage adapter.
///
/// Returns `DIAG_ERROR_INVALID_ARGUMENT` when `ctx` is null. Returns
/// `DIAG_ERROR_NOT_INITIALIZED` when the context is not initialized, storage has
/// not been attached. DTC persistence requires
/// `struct diag_storage::capsule_buffer`.
///
/// @return `DIAG_OK`, `DIAG_ERROR_INVALID_ARGUMENT`,
///         `DIAG_ERROR_NOT_INITIALIZED`, `DIAG_ERROR_CAPACITY`,
///         `DIAG_ERROR_CORRUPT_DATA`, `DIAG_ERROR_NOT_SUPPORTED`, or a storage
///         adapter error.
enum diag_result diag_load(struct diag_context *ctx);

DIAG_EXTERN_C_END

#endif
