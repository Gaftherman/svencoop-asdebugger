// SPDX-License-Identifier: MIT
#include "sven_host_adapter.h"
#include "as_safe_memory.h"
#include <asext_api.h>
#include <std_string.h>
#include <algorithm>
#include <format>

namespace
{
int32_t SafeReadInt32(const void* ptr)
{
    if (!ptr)
        return 0;
#if defined(_WIN32)
    __try
    {
        return *reinterpret_cast<const int32_t*>(ptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
#else
    if (!SafeProbeMemoryReadable(ptr, sizeof(int32_t)))
        return 0;
    return *reinterpret_cast<const int32_t*>(ptr);
#endif
}

inline bool SafeReadCString(const void* ptr, char* out_buf, size_t buf_size, size_t& out_len, bool& out_truncated)
{
    if (!ptr)
        return false;
#if defined(_WIN32)
    __try
    {
        const CString* cstr = reinterpret_cast<const CString*>(ptr);
        size_t         len  = cstr->m_len;
        if (len > 10000000)
            return false;
        if (len == 0)
        {
            out_len       = 0;
            out_truncated = false;
            if (buf_size > 0)
                out_buf[0] = '\0';
            return true;
        }
        const char* p = cstr->m_ptr;
        if (!p)
            return false;
        out_len = len;
        if (buf_size == 0)
            return true;
        size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
        out_truncated  = (len >= buf_size);
        for (size_t i = 0; i < to_copy; i++)
            out_buf[i] = p[i];
        out_buf[to_copy] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    const CString* cstr = reinterpret_cast<const CString*>(ptr);
    if (!SafeProbeMemoryReadable(cstr, sizeof(CString)))
        return false;
    size_t len = cstr->m_len;
    if (len > 10000000)
        return false;
    if (len == 0)
    {
        out_len       = 0;
        out_truncated = false;
        if (buf_size > 0)
            out_buf[0] = '\0';
        return true;
    }
    const char* p = cstr->m_ptr;
    if (!p || !SafeProbeMemoryReadable(p, len))
        return false;
    out_len = len;
    if (buf_size == 0)
        return true;
    size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
    out_truncated  = (len >= buf_size);
    std::memcpy(out_buf, p, to_copy);
    out_buf[to_copy] = '\0';
    return true;
#endif
}

inline bool SafeReadStdString(const void* ptr, char* out_buf, size_t buf_size, size_t& out_len, bool& out_truncated)
{
    if (!ptr)
        return false;
#if defined(_WIN32)
    __try
    {
        const std_string* strPtr = reinterpret_cast<const std_string*>(ptr);
        size_t            len    = strPtr->len();
        if (len > 10000000)
            return false;
        if (len == 0)
        {
            out_len       = 0;
            out_truncated = false;
            if (buf_size > 0)
                out_buf[0] = '\0';
            return true;
        }
        const char* c_str = const_cast<std_string*>(strPtr)->c_str();
        if (!c_str)
            return false;
        out_len = len;
        if (buf_size == 0)
            return true;
        size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
        out_truncated  = (len >= buf_size);
        for (size_t i = 0; i < to_copy; i++)
            out_buf[i] = c_str[i];
        out_buf[to_copy] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    const std_string* strPtr = reinterpret_cast<const std_string*>(ptr);
    if (!SafeProbeMemoryReadable(strPtr, sizeof(std_string)))
        return false;
    size_t len = strPtr->len();
    if (len > 10000000)
        return false;
    if (len == 0)
    {
        out_len       = 0;
        out_truncated = false;
        if (buf_size > 0)
            out_buf[0] = '\0';
        return true;
    }
    const char* c_str = const_cast<std_string*>(strPtr)->c_str();
    if (!c_str || !SafeProbeMemoryReadable(c_str, len))
        return false;
    out_len = len;
    if (buf_size == 0)
        return true;
    size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
    out_truncated  = (len >= buf_size);
    std::memcpy(out_buf, c_str, to_copy);
    out_buf[to_copy] = '\0';
    return true;
#endif
}

inline bool SafeInspectScriptArray(const void* ptr, size_t& out_count, void*& out_data, int& out_elemTypeId, int& out_elemSize)
{
    if (!ptr)
        return false;
#if defined(_WIN32)
    __try
    {
        const CScriptArray* arr = reinterpret_cast<const CScriptArray*>(ptr);
        CScriptArrayBuffer* buf = arr->m_buffer;
        out_elemTypeId          = arr->m_ElementTypeId;
        out_elemSize            = arr->m_unk5;

        if (!buf)
        {
            out_count = 0;
            out_data  = nullptr;
            return true;
        }

        int count = buf->m_size;
        if (count < 0 || count > 10000000)
            return false;

        out_count = static_cast<size_t>(count);
        out_data  = buf->data();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    if (!SafeProbeMemoryReadable(ptr, sizeof(CScriptArray)))
        return false;
    const CScriptArray* arr = reinterpret_cast<const CScriptArray*>(ptr);
    CScriptArrayBuffer* buf = arr->m_buffer;
    out_elemTypeId          = arr->m_ElementTypeId;
    out_elemSize            = arr->m_unk5;

    if (!buf)
    {
        out_count = 0;
        out_data  = nullptr;
        return true;
    }

    if (!SafeProbeMemoryReadable(buf, sizeof(int) * 2))
        return false;

    int count = buf->m_size;
    if (count < 0 || count > 10000000)
        return false;

    out_count = static_cast<size_t>(count);
    out_data  = buf->data();
    return true;
#endif
}

struct DictEntryRaw
{
    const CString* keyObj;
    void*          node;
};

#ifdef _WIN32
static bool SafeGetDictSize(CScriptDictionary* dict, unsigned int& out_count)
{
    __try
    {
        if (!dict || !ASEXT_CScriptDictionary_GetSize)
            return false;
        out_count = ASEXT_CScriptDictionary_GetSize(dict);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static size_t SafeIterateDict(CScriptDictionary* dict, DictEntryRaw* out_entries, size_t max_entries)
{
    __try
    {
        if (!dict || !ASEXT_CScriptDictionary_begin || !ASEXT_CScriptDictionary_end ||
            !ASEXT_CScriptDictionary_CIterator_operator_NE || !ASEXT_CScriptDictionary_CIterator_operator_PP ||
            !ASEXT_CScriptDictionary_CIterator_GetKey)
            return 0;

        CScriptDictionary_CIterator it{}, itend{};
        ASEXT_CScriptDictionary_begin(dict, &it);
        ASEXT_CScriptDictionary_end(dict, &itend);

        size_t count = 0;
        while (ASEXT_CScriptDictionary_CIterator_operator_NE(&it, &itend) && count < max_entries)
        {
            out_entries[count].keyObj = ASEXT_CScriptDictionary_CIterator_GetKey(&it);
            out_entries[count].node   = it.cur;
            count++;
            ASEXT_CScriptDictionary_CIterator_operator_PP(&it);
        }
        return count;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}
#else

static bool SafeGetDictSize(CScriptDictionary* dict, unsigned int& out_count)
{
    if (!dict || !ASEXT_CScriptDictionary_GetSize)
        return false;
    out_count = ASEXT_CScriptDictionary_GetSize(dict);
    return true;
}

static size_t SafeIterateDict(CScriptDictionary* dict, DictEntryRaw* out_entries, size_t max_entries)
{
    if (!dict || !ASEXT_CScriptDictionary_begin || !ASEXT_CScriptDictionary_end ||
        !ASEXT_CScriptDictionary_CIterator_operator_NE || !ASEXT_CScriptDictionary_CIterator_operator_PP ||
        !ASEXT_CScriptDictionary_CIterator_GetKey)
        return 0;

    CScriptDictionary_CIterator it{}, itend{};
    ASEXT_CScriptDictionary_begin(dict, &it);
    ASEXT_CScriptDictionary_end(dict, &itend);

    size_t count = 0;
    while (ASEXT_CScriptDictionary_CIterator_operator_NE(&it, &itend) && count < max_entries)
    {
        out_entries[count].keyObj = ASEXT_CScriptDictionary_CIterator_GetKey(&it);
        out_entries[count].node   = it.cur;
        count++;
        ASEXT_CScriptDictionary_CIterator_operator_PP(&it);
    }
    return count;
}
#endif

} // namespace

bool SvenHostAdapter::IsStringType(asITypeInfo* type) const
{
    return type && type->GetName() && std::string_view(type->GetName()) == "string";
}

bool SvenHostAdapter::IsArrayType(asITypeInfo* type) const
{
    return type && type->GetName() && (std::string_view(type->GetName()) == "array" || (type->GetFlags() & asOBJ_TEMPLATE));
}

bool SvenHostAdapter::IsDictionaryType(asITypeInfo* type) const
{
    return type && type->GetName() && std::string_view(type->GetName()) == "dictionary";
}

bool SvenHostAdapter::ReadString(asITypeInfo* type, const void* object, char* buffer, size_t capacity, size_t& length, asIDBInspection usage) const
{
    const size_t typeSize  = type ? type->GetSize() : 0;
    bool         truncated = false;
    if (usage == asIDBInspection::Variables &&
        typeSize != 36 && typeSize != sizeof(CString) &&
        (typeSize == 24 || typeSize == sizeof(std_string)))
    {
        return SafeReadStdString(object, buffer, capacity, length, truncated) ||
            SafeReadCString(object, buffer, capacity, length, truncated);
    }
    return SafeReadCString(object, buffer, capacity, length, truncated) ||
        SafeReadStdString(object, buffer, capacity, length, truncated);
}

bool SvenHostAdapter::InspectArray(asITypeInfo* type, const void* object, asIDBArrayView& view, asIDBInspection usage) const
{
    if (usage == asIDBInspection::Member && (!type || std::string_view(type->GetName()) != "array"))
        return false;

    void* data        = nullptr;
    int   elementSize = 0;
    if (!SafeInspectScriptArray(object, view.count, data, view.elementTypeId, elementSize))
        return false;

    auto engine = type ? type->GetEngine() : nullptr;
    if (usage == asIDBInspection::Variables && view.elementTypeId <= 0 && type)
        view.elementTypeId = type->GetSubTypeId();

    view.storage = data;
    view.stride  = sizeof(void*);
    if (!(view.elementTypeId & asTYPEID_MASK_OBJECT))
    {
        if (elementSize > 0 && elementSize <= 64)
            view.stride = static_cast<size_t>(elementSize);
        else if (engine)
        {
            const int primitiveSize = engine->GetSizeOfPrimitiveType(view.elementTypeId);
            if (primitiveSize > 0)
                view.stride = static_cast<size_t>(primitiveSize);
        }
    }
    return true;
}

bool SvenHostAdapter::ReadArrayElement(const asIDBArrayView& view, size_t index, void*& address) const
{
    if (!view.storage || index >= view.count)
        return false;

    auto slot = static_cast<const char*>(view.storage) + index * view.stride;
    // Value objects and handles both use pointer-sized slots. Return the object
    // for a value element and preserve the slot for a handle element.
    if ((view.elementTypeId & asTYPEID_MASK_OBJECT) && !(view.elementTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)))
        address = SafeReadPointer(slot);
    else
        address = const_cast<char*>(slot);
    return true;
}

bool SvenHostAdapter::ReadIndexedFloat(asITypeInfo* type, const void* object, int64_t index, float& value) const
{
    if (!type || std::string_view(type->GetName()) != "Vector" || index < 0 || index >= 3)
        return false;

    value = static_cast<const float*>(object)[index];
    return true;
}

bool SvenHostAdapter::GetDictionarySize(const void* object, size_t& count) const
{
    unsigned int size = 0;
    if (!SafeGetDictSize(static_cast<CScriptDictionary*>(const_cast<void*>(object)), size))
        return false;

    count = size;
    return true;
}

size_t SvenHostAdapter::ReadDictionary(const void* object, asIDBDictionaryEntry* entries, size_t capacity) const
{
    std::vector<DictEntryRaw> raw(capacity);
    const size_t              count   = SafeIterateDict(static_cast<CScriptDictionary*>(const_cast<void*>(object)), raw.data(), raw.size());
    size_t                    written = 0;
    for (size_t i = 0; i < count; i++)
    {
        char        key[257];
        size_t      length    = 0;
        bool        truncated = false;
        std::string keyName;
        if (SafeReadCString(raw[i].keyObj, key, sizeof(key), length, truncated))
            keyName.assign(key, std::min(length, sizeof(key) - 1));
        else
            keyName = std::format("entry_{}", i);

        auto node = static_cast<char*>(raw[i].node);
        if (node && SafeProbeMemoryReadable(node + 0x30, 0x10))
        {
            const int typeId  = SafeReadInt32(node + 0x38);
            void*     address = node + 0x30;
            if ((typeId & asTYPEID_MASK_OBJECT) && !(typeId & asTYPEID_OBJHANDLE))
                address = SafeReadPointer(address);

            entries[written++] = {std::move(keyName), typeId, address};
        }
    }
    return written;
}
