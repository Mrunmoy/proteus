#ifndef DIAG_CONTEXT_INTERNAL_H
#define DIAG_CONTEXT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "diag/features.h"

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

#include "diag/context.h"

enum diag_context_state_flag
{
    DIAG_CONTEXT_STATE_INITIALIZED = 1u << 0u,
    DIAG_CONTEXT_STATE_DTC_ATTACHED = 1u << 1u,
    DIAG_CONTEXT_STATE_IDENTITY_ATTACHED = 1u << 2u,
    DIAG_CONTEXT_STATE_LIFECYCLE_ATTACHED = 1u << 3u,
    DIAG_CONTEXT_STATE_STORAGE_ATTACHED = 1u << 4u,
    DIAG_CONTEXT_STATE_TRANSPORT_ATTACHED = 1u << 5u
};

struct diag_context
{
    uint32_t state_flags;
    uint32_t dirty_flags;
#if DIAG_FEATURE_DTC
    struct diag_dtc_config dtc;
    size_t dtc_count;
#endif
#if DIAG_FEATURE_IDENTITY
    struct diag_identity identity;
#endif
#if DIAG_FEATURE_LIFECYCLE
    struct diag_lifecycle_config lifecycle;
    enum diag_reset_reason last_reset_reason;
    uint32_t reset_count;
    uint32_t abnormal_reset_count;
    uint32_t lifecycle_dirty_flags;
#endif
#if DIAG_FEATURE_STORAGE
    struct diag_storage storage;
#endif
#if DIAG_FEATURE_TRANSPORT
    struct diag_transport transport;
#endif
};

static inline int diag_context_has_state(const struct diag_context *ctx, uint32_t state_flags)
{
    return (ctx->state_flags & state_flags) == state_flags;
}

static inline void diag_context_set_state(struct diag_context *ctx, uint32_t state_flags)
{
    ctx->state_flags |= state_flags;
}

static inline void diag_context_clear_state(struct diag_context *ctx, uint32_t state_flags)
{
    ctx->state_flags &= ~state_flags;
}

static inline void diag_context_mark_dirty(struct diag_context *ctx, uint32_t dirty_flags)
{
    ctx->dirty_flags |= dirty_flags;
}

static inline void diag_context_clear_dirty(struct diag_context *ctx, uint32_t dirty_flags)
{
    ctx->dirty_flags &= ~dirty_flags;
}

// Portable C99 alignment probe: the offset of a member placed after a single
// char equals that member type's alignment. Avoids __alignof__, which is a
// GNU/Clang extension and would not compile under MSVC C mode.
struct diag_align_probe_context
{
    char head;
    struct diag_context member;
};

struct diag_align_probe_storage
{
    char head;
    struct diag_context_storage member;
};

typedef char diag_context_storage_size_check
    [(sizeof(struct diag_context) <= DIAG_CONTEXT_STORAGE_SIZE) ? 1 : -1];

// Caller-owned storage must also be aligned strictly enough for the private
// context, not just large enough.
// clang-format off
typedef char diag_context_storage_align_check
    [(offsetof(struct diag_align_probe_context, member) <=
      offsetof(struct diag_align_probe_storage, member))
         ? 1
         : -1];
// clang-format on

#endif
