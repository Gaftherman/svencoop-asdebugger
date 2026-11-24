// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <unistd.h>
#endif

inline bool SafeProbeMemoryReadable(const void* ptr, size_t size)
{
    if (!ptr || size == 0)
        return false;
#if defined(_WIN32)
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t end   = start + size;
    if (end < start)
        return false;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t                 current = start;
    while (current < end)
    {
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0)
            return false;

        if (mbi.State != MEM_COMMIT)
            return false;

        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;

        constexpr DWORD readableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & readableFlags))
            return false;

        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= current)
            break;
        current = next;
    }
    return true;
#else
    uintptr_t start    = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t end      = start + size;
    long      pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        pageSize = 4096;
    uintptr_t page = start & ~(pageSize - 1);
    while (page < end)
    {
        unsigned char vec = 0;
        if (mincore(reinterpret_cast<void*>(page), pageSize, &vec) != 0)
            return false;
        page += pageSize;
    }
    return true;
#endif
}

inline bool SafeProbeMemoryWritable(void* ptr, size_t size)
{
    if (!ptr || size == 0)
        return false;
#if defined(_WIN32)
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t end   = start + size;
    if (end < start)
        return false;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t                 current = start;
    while (current < end)
    {
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0)
            return false;

        if (mbi.State != MEM_COMMIT)
            return false;

        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;

        constexpr DWORD writableFlags = PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & writableFlags))
            return false;

        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= current)
            break;
        current = next;
    }
    return true;
#else
    return SafeProbeMemoryReadable(ptr, size);
#endif
}

inline void* SafeReadPointer(const void* ptr)
{
    if (!ptr)
        return nullptr;
#if defined(_WIN32)
    __try
    {
        return *reinterpret_cast<void* const*>(ptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
#else
    if (!SafeProbeMemoryReadable(ptr, sizeof(void*)))
        return nullptr;
    return *reinterpret_cast<void* const*>(ptr);
#endif
}

inline std::vector<uint8_t> SafeFromBase64(std::string_view input)
{
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::vector<uint8_t> out;
    out.reserve(input.size() * 3 / 4);
    int val = 0, valb = -8;
    for (char ch : input)
    {
        uint8_t c = static_cast<uint8_t>(ch);
        if (c == '=')
            break;
        if (T[c] == -1)
            continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}
