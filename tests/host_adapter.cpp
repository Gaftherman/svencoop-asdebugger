// SPDX-License-Identifier: MIT
#include "as_debugger.h"
#include <iostream>

#ifdef AS_USE_NAMESPACE
using namespace AngelScript;
#endif

namespace
{
class FakeAdapter final : public asIDBHostAdapter
{
public:
    bool stringType = true;
    bool IsStringType(asITypeInfo*) const override { return stringType; }
    bool IsArrayType(asITypeInfo*) const override { return true; }
    bool IsDictionaryType(asITypeInfo*) const override { return true; }

    bool ReadString(asITypeInfo*, const void*, char* buffer, std::size_t capacity, std::size_t& length, asIDBInspection) const override
    {
        constexpr std::string_view text = "from adapter";
        length                          = text.size();
        if (capacity)
            buffer[text.copy(buffer, capacity - 1)] = '\0';
        return true;
    }

    bool InspectArray(asITypeInfo*, const void* object, asIDBArrayView& view, asIDBInspection) const override
    {
        view = {2, asTYPEID_INT32, object};
        return true;
    }

    bool ReadArrayElement(const asIDBArrayView& view, std::size_t index, void*& address) const override
    {
        address = const_cast<int*>(static_cast<const int*>(view.storage)) + index;
        return true;
    }

    bool GetDictionarySize(const void*, std::size_t& count) const override
    {
        count = 7;
        return true;
    }

    std::size_t ReadDictionary(const void*, asIDBDictionaryEntry*, std::size_t) const override { return 0; }
};

class TestDebugger final : public asIDBDebugger
{
public:
    explicit TestDebugger(const asIDBHostAdapter& adapter) : asIDBDebugger(nullptr, adapter) {}
    std::string FetchSource(const char*) override { return {}; }

protected:
    void                        Suspend() override {}
    void                        Resume() override {}
    std::unique_ptr<asIDBCache> CreateCache(asIScriptContext*) override { return {}; }
};
} // namespace

int main()
{
    FakeAdapter  adapter;
    TestDebugger debugger(adapter);
    int          values[]{10, 27};
    auto         variable = std::make_shared<asIDBVariable>(debugger);
    variable->address     = {0, false, values};

    asIDBArrayTypeEvaluator array;
    array.Evaluate(variable);
    if (variable->value != "2 elements" || !variable->expandable)
    {
        std::cerr << "Array evaluation did not use the host adapter\n";
        return 1;
    }

    asIDBDictionaryTypeEvaluator dictionary;
    dictionary.Evaluate(variable);
    if (variable->value != "7 entries" || !variable->expandable)
    {
        std::cerr << "Dictionary evaluation did not use the host adapter\n";
        return 1;
    }

    auto text = as_expr::Value::MakeAddress(values, 0, false, nullptr, adapter);
    if (text.ToString() != "from adapter")
    {
        std::cerr << "Expression string evaluation did not use the host adapter\n";
        return 1;
    }

    adapter.stringType = false;
    as_expr::IndexAccessNode index(
        std::make_shared<as_expr::LiteralNode>(as_expr::Value::MakeAddress(values, 0, false, nullptr, adapter)),
        std::make_shared<as_expr::LiteralNode>(as_expr::Value::MakeInt(1)));
    auto element = index.Evaluate(nullptr, 0, adapter);
    if (element.kind != as_expr::ValueKind::Int || element.iVal != 27)
    {
        std::cerr << "Expression indexing did not use the host adapter\n";
        return 1;
    }
}
