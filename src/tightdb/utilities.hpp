/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_UTILITIES_HPP
#define TIGHTDB_UTILITIES_HPP

#include <cstdlib>
#include "assert.hpp"
#ifdef _MSC_VER
    #include <win32/types.h>
    #include <win32/stdint.h>
    #include <intrin.h>
#endif

#if defined(__GNUC__)
	#define TIGHTDB_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
	#define TIGHTDB_INLINE __forceinline
#elif defined(__HP_aCC)
	#define TIGHTDB_INLINE inline __attribute__((always_inline))
#elif defined(__xlC__ )
	#define TIGHTDB_INLINE inline 
#else
	#error TEXT("Compiler version not detectable")
#endif

#if (defined(__X86__) || defined(__i386__) || defined(i386) || defined(_M_IX86) || defined(__386__) || defined(__x86_64__) || defined(_M_X64))
    #define TIGHTDB_X86X64
#endif

#if defined _LP64 || defined __LP64__ || defined __64BIT__ || _ADDR64 || defined _WIN64 || defined __arch64__ || __WORDSIZE == 64 || (defined __sparc && defined __sparcv9) || defined __x86_64 || defined __amd64 || defined __x86_64__ || defined _M_X64 || defined _M_IA64 || defined __ia64 || defined __IA64__
    #define TIGHTDB_PTR_64
#endif

#if defined(TIGHTDB_PTR_64) && defined(TIGHTDB_X86X64) 
    #define TIGHTDB_COMPILER_SSE  // Compiler supports SSE 4.2 thorugh __builtin_ accessors or back-end assembler 
#endif

namespace tightdb {

extern char sse_support; // 1 = sse42, 0 = sse3, -2 = cpu does not support sse

template <int version>TIGHTDB_INLINE bool cpuid_sse()
{
    TIGHTDB_STATIC_ASSERT(version == 30 || version == 42, "Only SSE 3 and 42 supported for detection");
    if(version == 30)
        return (sse_support >= 0);
    else if(version == 42)
        return (sse_support > 0);   // faster than == 1 (0 requres no immediate operand)
}

typedef struct {
    unsigned long long remainder;
    unsigned long long remainder_len;
    unsigned long long b_val;
    unsigned long long a_val;
    unsigned long long result;
} checksum_t;

size_t to_ref(int64_t v);
size_t to_size_t(int64_t v);
void cpuid_init();
unsigned long long checksum(unsigned char* data, size_t len);
void checksum_rolling(unsigned char* data, size_t len, checksum_t* t);
void* round_up(void* p, size_t align);
void* round_down(void* p, size_t align);
size_t round_up(size_t p, size_t align);
size_t round_down(size_t p, size_t align);
void checksum_init(checksum_t* t);

}

#endif // TIGHTDB_UTILITIES_HPP

