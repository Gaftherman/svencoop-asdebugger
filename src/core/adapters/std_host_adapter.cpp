// SPDX-License-Identifier: MIT
#include "std_host_adapter.h"
#include <scriptarray.h>
#include <scriptdictionary.h>
#include <algorithm>

BEGIN_AS_NAMESPACE

namespace
{
bool IsNativeType(asITypeInfo* type, std::string_view name, asQWORD flags)
{
    return type && (type->GetFlags() & flags) == flags &&
        !(type->GetFlags() & asOBJ_SCRIPT_OBJECT) && type->GetName() == name;
}
} // namespace

bool StdHostAdapter::IsStringType(asITypeInfo* type) const
{
    return IsNativeType(type, "string", asOBJ_VALUE);
}

bool StdHostAdapter::IsArrayType(asITypeInfo* type) const
{
    return IsNativeType(type, "array", asOBJ_REF | asOBJ_TEMPLATE);
}

bool StdHostAdapter::IsDictionaryType(asITypeInfo* type) const
{
    return IsNativeType(type, "dictionary", asOBJ_REF | asOBJ_GC);
}

bool StdHostAdapter::ReadString(asITypeInfo* type, const void* object, char* buffer, std::size_t capacity, std::size_t& length, asIDBInspection) const
{
    if (!object || !IsStringType(type))
        return false;

    const auto& value = *static_cast<const std::string*>(object);
    length            = value.size();
    if (capacity != 0)
        buffer[value.copy(buffer, capacity - 1)] = '\0';
    return true;
}

bool StdHostAdapter::InspectArray(asITypeInfo* type, const void* object, asIDBArrayView& view, asIDBInspection) const
{
    if (!object || !IsArrayType(type))
        return false;

    const auto* array = static_cast<const CScriptArray*>(object);
    view              = {array->GetSize(), array->GetElementTypeId(), array, 0};
    return true;
}

bool StdHostAdapter::ReadArrayElement(const asIDBArrayView& view, std::size_t index, void*& address) const
{
    if (!view.storage || index >= view.count)
        return false;

    const auto* array = static_cast<const CScriptArray*>(view.storage);
    // At already resolves value objects and returns the slot for handles.
    address = const_cast<void*>(array->At(static_cast<asUINT>(index)));
    return true;
}

bool StdHostAdapter::GetDictionarySize(const void* object, std::size_t& count) const
{
    if (!object)
        return false;

    count = static_cast<const CScriptDictionary*>(object)->GetSize();
    return true;
}

std::size_t StdHostAdapter::ReadDictionary(const void* object, asIDBDictionaryEntry* entries, std::size_t capacity) const
{
    if (!object)
        return 0;

    const auto& dictionary = *static_cast<const CScriptDictionary*>(object);
    std::size_t count      = 0;
    for (auto it = dictionary.begin(); count < capacity && it != dictionary.end(); ++it)
        entries[count++] = {it.GetKey(), it.GetTypeId(), const_cast<void*>(it.GetAddressOfValue())};
    return count;
}

END_AS_NAMESPACE
