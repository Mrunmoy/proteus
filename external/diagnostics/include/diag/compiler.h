/// @file
/// Compiler portability helpers used by public storage declarations.

#ifndef DIAG_COMPILER_H
#define DIAG_COMPILER_H

/// Prefix form of an alignment attribute for caller-owned public storage.
///
/// The macro expands to C11 `_Alignas`, a compiler-specific attribute, or
/// nothing when the compiler has no supported alignment syntax.
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define DIAG_ALIGNAS_PREFIX(alignment) _Alignas(alignment)
/// Suffix form of an alignment attribute for caller-owned public storage.
#define DIAG_ALIGNAS_SUFFIX(alignment)
#elif defined(_MSC_VER)
#define DIAG_ALIGNAS_PREFIX(alignment) __declspec(align(alignment))
#define DIAG_ALIGNAS_SUFFIX(alignment)
#elif defined(__GNUC__) || defined(__clang__)
#define DIAG_ALIGNAS_PREFIX(alignment)
#define DIAG_ALIGNAS_SUFFIX(alignment) __attribute__((aligned(alignment)))
#else
#define DIAG_ALIGNAS_PREFIX(alignment)
#define DIAG_ALIGNAS_SUFFIX(alignment)
#endif

/// Branch prediction hint for hot-path conditions that are expected to be true.
///
/// This is a hint only. It preserves the expression value and falls back to the
/// expression unchanged on compilers without a supported builtin.
#if defined(__GNUC__) || defined(__clang__)
#define DIAG_LIKELY(expression) __builtin_expect(!!(expression), 1)
/// Branch prediction hint for cold-path conditions that are expected to be false.
#define DIAG_UNLIKELY(expression) __builtin_expect(!!(expression), 0)
#else
#define DIAG_LIKELY(expression) (expression)
#define DIAG_UNLIKELY(expression) (expression)
#endif

/// Begin C linkage for public headers when included from C++.
#ifdef __cplusplus
#define DIAG_EXTERN_C_BEGIN                                                                        \
    extern "C"                                                                                     \
    {
/// End C linkage for public headers when included from C++.
#define DIAG_EXTERN_C_END }
#else
#define DIAG_EXTERN_C_BEGIN
#define DIAG_EXTERN_C_END
#endif

#endif
