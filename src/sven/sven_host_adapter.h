// SPDX-License-Identifier: MIT
#pragma once

#include "as_debugger_host.h"

class SvenHostAdapter final : public asIDBHostAdapter
{
public:
    bool        IsStringType(asITypeInfo* type) const override;
    bool        IsArrayType(asITypeInfo* type) const override;
    bool        IsDictionaryType(asITypeInfo* type) const override;
    bool        ReadString(asITypeInfo* type, const void* object, char* buffer, std::size_t capacity, std::size_t& length, asIDBInspection usage) const override;
    bool        InspectArray(asITypeInfo* type, const void* object, asIDBArrayView& view, asIDBInspection usage) const override;
    bool        ReadArrayElement(const asIDBArrayView& view, std::size_t index, void*& address) const override;
    bool        ReadIndexedFloat(asITypeInfo* type, const void* object, std::int64_t index, float& value) const override;
    bool        GetDictionarySize(const void* object, std::size_t& count) const override;
    std::size_t ReadDictionary(const void* object, asIDBDictionaryEntry* entries, std::size_t capacity) const override;
};
