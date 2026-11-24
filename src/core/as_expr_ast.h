// SPDX-License-Identifier: MIT
#pragma once

#include "as_debugger_host.h"
#include "as_safe_memory.h"
#include "as_helpers.h"
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm>
#include <optional>

BEGIN_AS_NAMESPACE

namespace as_expr
{

enum class ValueKind
{
    Null,
    Bool,
    Int,
    UInt,
    Float,
    String,
    Address
};

struct Value
{
    ValueKind        kind = ValueKind::Null;
    bool             bVal = false;
    int64_t          iVal = 0;
    uint64_t         uVal = 0;
    double           fVal = 0.0;
    std::string      sVal;

    // For Address kind:
    void*            addr     = nullptr;
    int              typeId   = 0;
    bool             isHandle = false;
    asIScriptEngine* engine   = nullptr;
    const asIDBHostAdapter* adapter  = nullptr;

    static Value MakeNull()
    {
        Value v;
        v.kind = ValueKind::Null;
        return v;
    }

    static Value MakeBool(bool b)
    {
        Value v;
        v.kind = ValueKind::Bool;
        v.bVal = b;
        return v;
    }

    static Value MakeInt(int64_t i)
    {
        Value v;
        v.kind = ValueKind::Int;
        v.iVal = i;
        return v;
    }

    static Value MakeUInt(uint64_t u)
    {
        Value v;
        v.kind = ValueKind::UInt;
        v.uVal = u;
        return v;
    }

    static Value MakeFloat(double f)
    {
        Value v;
        v.kind = ValueKind::Float;
        v.fVal = f;
        return v;
    }

    static Value MakeString(std::string s)
    {
        Value v;
        v.kind = ValueKind::String;
        v.sVal = std::move(s);
        return v;
    }

    static Value MakeAddress(void* ptr, int typeId, bool isHandle, asIScriptEngine* engine, const asIDBHostAdapter& adapter)
    {
        Value v;
        v.kind     = ValueKind::Address;
        v.addr     = ptr;
        v.typeId   = typeId;
        v.isHandle = isHandle;
        v.engine   = engine;
        v.adapter  = &adapter;
        return v;
    }

    void* ResolvePointer() const
    {
        if (kind == ValueKind::Null || !addr)
            return nullptr;
        if (isHandle || (typeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)))
        {
            return SafeReadPointer(addr);
        }
        return addr;
    }

    bool IsTruthy() const
    {
        switch (kind)
        {
            case ValueKind::Null:
                return false;
            case ValueKind::Bool:
                return bVal;
            case ValueKind::Int:
                return iVal != 0;
            case ValueKind::UInt:
                return uVal != 0;
            case ValueKind::Float:
                return fVal != 0.0 && !std::isnan(fVal);
            case ValueKind::String:
                return !sVal.empty() && sVal != "false" && sVal != "0";
            case ValueKind::Address:
                return ResolvePointer() != nullptr;
        }
        return false;
    }

    bool ToBool() const
    {
        return IsTruthy();
    }

    std::string GetTypeName() const
    {
        switch (kind)
        {
            case ValueKind::Null:
                return "null";
            case ValueKind::Bool:
                return "bool";
            case ValueKind::Int:
                return "int64";
            case ValueKind::UInt:
                return "uint64";
            case ValueKind::Float:
                return "double";
            case ValueKind::String:
                return "string";
            case ValueKind::Address:
                if (engine && typeId != 0)
                {
                    if (auto ti = engine->GetTypeInfoById(typeId))
                        return ti->GetName();
                }
                return "object";
        }
        return "unknown";
    }

    asITypeInfo* GetTypeInfo() const
    {
        return engine && typeId ? engine->GetTypeInfoById(typeId) : nullptr;
    }

    bool IsScriptString() const
    {
        return adapter && adapter->IsStringType(GetTypeInfo());
    }

    bool ReadString(const void* object, char* buffer, size_t capacity, size_t& length) const
    {
        return adapter && adapter->ReadString(GetTypeInfo(), object, buffer, capacity, length, asIDBInspection::Expression);
    }

    std::string ToString() const
    {
        switch (kind)
        {
            case ValueKind::Null:
                return "null";
            case ValueKind::Bool:
                return bVal ? "true" : "false";
            case ValueKind::Int:
                return std::to_string(iVal);
            case ValueKind::UInt:
                return std::to_string(uVal);
            case ValueKind::Float:
            {
                std::ostringstream ss;
                ss << fVal;
                return ss.str();
            }
            case ValueKind::String:
                return sVal;
            case ValueKind::Address:
            {
                void* p = ResolvePointer();
                if (!p)
                    return "null";

                if (IsScriptString())
                {
                    char   buf[512];
                    size_t len = 0;
                    if (ReadString(p, buf, sizeof(buf), len))
                        return std::string(buf, std::min(len, sizeof(buf) - 1));
                }

                std::ostringstream ss;
                ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(p);
                return ss.str();
            }
        }
        return "null";
    }

    Value DereferenceIfAddress() const
    {
        if (kind != ValueKind::Address || !addr)
            return *this;

        void* p = ResolvePointer();
        if (!p)
            return MakeNull();

        // If it's a primitive type, read value directly:
        switch (typeId & asTYPEID_MASK_SEQNBR)
        {
            case asTYPEID_BOOL:
                return MakeBool(*reinterpret_cast<const bool*>(p));
            case asTYPEID_INT8:
                return MakeInt(*reinterpret_cast<const int8_t*>(p));
            case asTYPEID_INT16:
                return MakeInt(*reinterpret_cast<const int16_t*>(p));
            case asTYPEID_INT32:
                return MakeInt(*reinterpret_cast<const int32_t*>(p));
            case asTYPEID_INT64:
                return MakeInt(*reinterpret_cast<const int64_t*>(p));
            case asTYPEID_UINT8:
                return MakeUInt(*reinterpret_cast<const uint8_t*>(p));
            case asTYPEID_UINT16:
                return MakeUInt(*reinterpret_cast<const uint16_t*>(p));
            case asTYPEID_UINT32:
                return MakeUInt(*reinterpret_cast<const uint32_t*>(p));
            case asTYPEID_UINT64:
                return MakeUInt(*reinterpret_cast<const uint64_t*>(p));
            case asTYPEID_FLOAT:
                return MakeFloat(*reinterpret_cast<const float*>(p));
            case asTYPEID_DOUBLE:
                return MakeFloat(*reinterpret_cast<const double*>(p));
        }

        // Check if string type
        if (engine)
        {
            asITypeInfo* ti = engine->GetTypeInfoById(typeId);
            if (ti)
            {
                if (ti->GetFlags() & asOBJ_ENUM)
                {
                    return MakeInt(*reinterpret_cast<const int*>(p));
                }
                if (IsScriptString())
                {
                    char   buf[512];
                    size_t len = 0;
                    if (ReadString(p, buf, sizeof(buf), len))
                        return MakeString(std::string(buf, std::min(len, sizeof(buf) - 1)));
                }
            }
        }

        return *this;
    }
};

class Node
{
public:
    virtual ~Node() = default;
    virtual Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const = 0;
};

using NodePtr = std::shared_ptr<Node>;

class LiteralNode : public Node
{
    Value m_val;
public:
    explicit LiteralNode(Value v) : m_val(std::move(v)) {}
    Value Evaluate(asIScriptContext*, asUINT, const asIDBHostAdapter&) const override
    {
        return m_val;
    }
};

class IdentifierNode : public Node
{
    std::string m_name;
    std::string m_ns;
public:
    IdentifierNode(std::string name, std::string ns = "") :
        m_name(std::move(name)), m_ns(std::move(ns)) {}

    Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const override
    {
        if (!ctx || stackLevel >= ctx->GetCallstackSize())
            return Value::MakeNull();

        asIScriptEngine* engine = ctx->GetEngine();

        // Step A: Local variables and parameters (in reverse order for scoping)
        if (m_ns.empty())
        {
            if (m_name == "this")
            {
                void* thisPtr = ctx->GetThisPointer(stackLevel);
                int   thisId  = ctx->GetThisTypeId(stackLevel);
                if (thisPtr && thisId)
                    return Value::MakeAddress(thisPtr, thisId, false, engine, adapter);
                return Value::MakeNull();
            }

            for (int i = ctx->GetVarCount(stackLevel) - 1; i >= 0; --i)
            {
                if (!ctx->IsVarInScope(i, stackLevel))
                    continue;
                const char* vName  = nullptr;
                int         typeId = 0;
                ctx->GetVar(i, stackLevel, &vName, &typeId);
                if (vName && m_name == vName)
                {
                    void* addr = ctx->GetAddressOfVar(i, stackLevel);
                    bool  isH  = (typeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
                    return Value::MakeAddress(addr, typeId, isH, engine, adapter).DereferenceIfAddress();
                }
            }

            // Step B: this properties fallback
            void* thisPtr = ctx->GetThisPointer(stackLevel);
            int   thisId  = ctx->GetThisTypeId(stackLevel);
            if (thisPtr && thisId && engine)
            {
                Value thisVal   = Value::MakeAddress(thisPtr, thisId, false, engine, adapter);
                Value memberVal = ResolveMember(thisVal, m_name, engine, adapter);
                if (memberVal.kind != ValueKind::Null || memberVal.addr != nullptr)
                    return memberVal.DereferenceIfAddress();
            }
        }

        // Step C: Module Globals
        asIScriptFunction* func = ctx->GetFunction(stackLevel);
        asIScriptModule*   mod  = func ? func->GetModule() : nullptr;
        if (mod)
        {
            const asUINT gCount = mod->GetGlobalVarCount();
            for (asUINT i = 0; i < gCount; ++i)
            {
                const char* gName   = nullptr;
                const char* gNs     = nullptr;
                int         gTypeId = 0;
                mod->GetGlobalVar(i, &gName, &gNs, &gTypeId);
                if (gName && m_name == gName)
                {
                    if (m_ns.empty() || (gNs && m_ns == gNs))
                    {
                        void* addr = mod->GetAddressOfGlobalVar(i);
                        bool  isH  = (gTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
                        return Value::MakeAddress(addr, gTypeId, isH, engine, adapter).DereferenceIfAddress();
                    }
                }
            }

            // Search Module Enums
            for (asUINT e = 0; e < mod->GetEnumCount(); ++e)
            {
                asITypeInfo* enumType = mod->GetEnumByIndex(e);
                if (!enumType)
                    continue;
                if (!m_ns.empty() && m_ns != enumType->GetName())
                    continue;
                for (asUINT v = 0; v < enumType->GetEnumValueCount(); ++v)
                {
                    asIDBEnumValue val   = 0;
                    const char* vName = enumType->GetEnumValueByIndex(v, &val);
                    if (vName && m_name == vName)
                        return Value::MakeInt(val);
                }
            }
        }

        // Step D: Engine Globals & Enums
        if (engine)
        {
            for (asUINT g = 0; g < engine->GetGlobalPropertyCount(); ++g)
            {
                const char* propName = nullptr;
                const char* propNs   = nullptr;
                int         propType = 0;
                void*       propAddr = nullptr;
                engine->GetGlobalPropertyByIndex(g, &propName, &propNs, &propType, nullptr, nullptr, &propAddr);
                if (propName && m_name == propName)
                {
                    if (m_ns.empty() || (propNs && m_ns == propNs))
                    {
                        bool isH = (propType & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
                        return Value::MakeAddress(propAddr, propType, isH, engine, adapter).DereferenceIfAddress();
                    }
                }
            }

            for (asUINT e = 0; e < engine->GetEnumCount(); ++e)
            {
                asITypeInfo* enumType = engine->GetEnumByIndex(e);
                if (!enumType)
                    continue;
                if (!m_ns.empty() && m_ns != enumType->GetName())
                    continue;
                for (asUINT v = 0; v < enumType->GetEnumValueCount(); ++v)
                {
                    asIDBEnumValue val   = 0;
                    const char* vName = enumType->GetEnumValueByIndex(v, &val);
                    if (vName && m_name == vName)
                        return Value::MakeInt(val);
                }
            }
        }

        return Value::MakeNull();
    }

    static Value ResolveMember(const Value& objVal, const std::string& memberName, asIScriptEngine* engine, const asIDBHostAdapter& adapter);
};

enum class UnaryOp
{
    Not,
    Neg,
    Pos,
    BitNot,
    HandleOf
};

class UnaryOpNode : public Node
{
    UnaryOp m_op;
    NodePtr m_child;
public:
    UnaryOpNode(UnaryOp op, NodePtr child) : m_op(op), m_child(std::move(child)) {}

    Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const override
    {
        Value v = m_child->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();
        switch (m_op)
        {
            case UnaryOp::Not:
                return Value::MakeBool(!v.IsTruthy());
            case UnaryOp::Neg:
                if (v.kind == ValueKind::Float)
                    return Value::MakeFloat(-v.fVal);
                if (v.kind == ValueKind::UInt)
                    return Value::MakeInt(-static_cast<int64_t>(v.uVal));
                return Value::MakeInt(-v.iVal);
            case UnaryOp::Pos:
                return v;
            case UnaryOp::BitNot:
                return Value::MakeInt(~v.iVal);
            case UnaryOp::HandleOf:
                return v;
        }
        return Value::MakeNull();
    }
};

enum class BinaryOp
{
    // Logical
    Or,
    And,
    // Bitwise
    BitOr,
    BitXor,
    BitAnd,
    // Equality & Identity
    Equal,
    NotEqual,
    Is,
    NotIs,
    // Relational
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    // Shifts
    Shl,
    Shr,
    // Additive & Multiplicative
    Add,
    Sub,
    Mul,
    Div,
    Mod
};

class BinaryOpNode : public Node
{
    BinaryOp m_op;
    NodePtr  m_left;
    NodePtr  m_right;
public:
    BinaryOpNode(BinaryOp op, NodePtr left, NodePtr right) :
        m_op(op), m_left(std::move(left)), m_right(std::move(right)) {}

    Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const override
    {
        // Short-circuiting logical operators:
        if (m_op == BinaryOp::Or)
        {
            Value l = m_left->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();
            if (l.IsTruthy())
                return Value::MakeBool(true);
            Value r = m_right->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();
            return Value::MakeBool(r.IsTruthy());
        }
        if (m_op == BinaryOp::And)
        {
            Value l = m_left->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();
            if (!l.IsTruthy())
                return Value::MakeBool(false);
            Value r = m_right->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();
            return Value::MakeBool(r.IsTruthy());
        }

        Value l = m_left->Evaluate(ctx, stackLevel, adapter);
        Value r = m_right->Evaluate(ctx, stackLevel, adapter);

        // Handle identity: 'is' and '!is'
        if (m_op == BinaryOp::Is || m_op == BinaryOp::NotIs)
        {
            void* pL = (l.kind == ValueKind::Address) ? l.ResolvePointer() : nullptr;
            void* pR = (r.kind == ValueKind::Address) ? r.ResolvePointer() : nullptr;
            bool same = (pL == pR);
            return Value::MakeBool(m_op == BinaryOp::Is ? same : !same);
        }

        l = l.DereferenceIfAddress();
        r = r.DereferenceIfAddress();

        // Equality '==' and '!='
        if (m_op == BinaryOp::Equal || m_op == BinaryOp::NotEqual)
        {
            bool eq = false;
            if (l.kind == ValueKind::Null || r.kind == ValueKind::Null)
            {
                eq = (l.kind == ValueKind::Null && r.kind == ValueKind::Null);
            }
            else if (l.kind == ValueKind::String || r.kind == ValueKind::String)
            {
                eq = (l.ToString() == r.ToString());
            }
            else if (l.kind == ValueKind::Float || r.kind == ValueKind::Float)
            {
                double dL = (l.kind == ValueKind::Float) ? l.fVal : (l.kind == ValueKind::UInt ? static_cast<double>(l.uVal) : static_cast<double>(l.iVal));
                double dR = (r.kind == ValueKind::Float) ? r.fVal : (r.kind == ValueKind::UInt ? static_cast<double>(r.uVal) : static_cast<double>(r.iVal));
                eq = (dL == dR);
            }
            else if (l.kind == ValueKind::Bool && r.kind == ValueKind::Bool)
            {
                eq = (l.bVal == r.bVal);
            }
            else
            {
                eq = (l.iVal == r.iVal);
            }
            return Value::MakeBool(m_op == BinaryOp::Equal ? eq : !eq);
        }

        // Relational '<', '<=', '>', '>='
        if (m_op == BinaryOp::Less || m_op == BinaryOp::LessEqual ||
            m_op == BinaryOp::Greater || m_op == BinaryOp::GreaterEqual)
        {
            if (l.kind == ValueKind::String && r.kind == ValueKind::String)
            {
                if (m_op == BinaryOp::Less) return Value::MakeBool(l.sVal < r.sVal);
                if (m_op == BinaryOp::LessEqual) return Value::MakeBool(l.sVal <= r.sVal);
                if (m_op == BinaryOp::Greater) return Value::MakeBool(l.sVal > r.sVal);
                if (m_op == BinaryOp::GreaterEqual) return Value::MakeBool(l.sVal >= r.sVal);
            }
            double dL = (l.kind == ValueKind::Float) ? l.fVal : (l.kind == ValueKind::UInt ? static_cast<double>(l.uVal) : static_cast<double>(l.iVal));
            double dR = (r.kind == ValueKind::Float) ? r.fVal : (r.kind == ValueKind::UInt ? static_cast<double>(r.uVal) : static_cast<double>(r.iVal));
            if (m_op == BinaryOp::Less) return Value::MakeBool(dL < dR);
            if (m_op == BinaryOp::LessEqual) return Value::MakeBool(dL <= dR);
            if (m_op == BinaryOp::Greater) return Value::MakeBool(dL > dR);
            if (m_op == BinaryOp::GreaterEqual) return Value::MakeBool(dL >= dR);
        }

        // Add '+' (supports string concatenation and numbers)
        if (m_op == BinaryOp::Add)
        {
            if (l.kind == ValueKind::String || r.kind == ValueKind::String)
            {
                return Value::MakeString(l.ToString() + r.ToString());
            }
            if (l.kind == ValueKind::Float || r.kind == ValueKind::Float)
            {
                double dL = (l.kind == ValueKind::Float) ? l.fVal : static_cast<double>(l.iVal);
                double dR = (r.kind == ValueKind::Float) ? r.fVal : static_cast<double>(r.iVal);
                return Value::MakeFloat(dL + dR);
            }
            return Value::MakeInt(l.iVal + r.iVal);
        }

        // Arithmetic '-', '*', '/', '%'
        if (m_op == BinaryOp::Sub)
        {
            if (l.kind == ValueKind::Float || r.kind == ValueKind::Float)
                return Value::MakeFloat((l.kind == ValueKind::Float ? l.fVal : l.iVal) - (r.kind == ValueKind::Float ? r.fVal : r.iVal));
            return Value::MakeInt(l.iVal - r.iVal);
        }
        if (m_op == BinaryOp::Mul)
        {
            if (l.kind == ValueKind::Float || r.kind == ValueKind::Float)
                return Value::MakeFloat((l.kind == ValueKind::Float ? l.fVal : l.iVal) * (r.kind == ValueKind::Float ? r.fVal : r.iVal));
            return Value::MakeInt(l.iVal * r.iVal);
        }
        if (m_op == BinaryOp::Div)
        {
            if (l.kind == ValueKind::Float || r.kind == ValueKind::Float)
            {
                double divisor = (r.kind == ValueKind::Float ? r.fVal : r.iVal);
                return Value::MakeFloat(divisor != 0.0 ? ((l.kind == ValueKind::Float ? l.fVal : l.iVal) / divisor) : 0.0);
            }
            return Value::MakeInt(r.iVal != 0 ? (l.iVal / r.iVal) : 0);
        }
        if (m_op == BinaryOp::Mod)
        {
            return Value::MakeInt(r.iVal != 0 ? (l.iVal % r.iVal) : 0);
        }

        // Bitwise ops
        if (m_op == BinaryOp::BitAnd) return Value::MakeInt(l.iVal & r.iVal);
        if (m_op == BinaryOp::BitOr)  return Value::MakeInt(l.iVal | r.iVal);
        if (m_op == BinaryOp::BitXor) return Value::MakeInt(l.iVal ^ r.iVal);
        if (m_op == BinaryOp::Shl)    return Value::MakeInt(l.iVal << (r.iVal & 63));
        if (m_op == BinaryOp::Shr)    return Value::MakeInt(l.iVal >> (r.iVal & 63));

        return Value::MakeNull();
    }
};

class MemberAccessNode : public Node
{
    NodePtr     m_target;
    std::string m_member;
public:
    MemberAccessNode(NodePtr target, std::string member) :
        m_target(std::move(target)), m_member(std::move(member)) {}

    Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const override
    {
        Value obj = m_target->Evaluate(ctx, stackLevel, adapter);
        return IdentifierNode::ResolveMember(obj, m_member, ctx ? ctx->GetEngine() : nullptr, adapter).DereferenceIfAddress();
    }
};

class IndexAccessNode : public Node
{
    NodePtr m_target;
    NodePtr m_index;
public:
    IndexAccessNode(NodePtr target, NodePtr index) :
        m_target(std::move(target)), m_index(std::move(index)) {}

    Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const override
    {
        Value obj = m_target->Evaluate(ctx, stackLevel, adapter);
        Value idx = m_index->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();

        void* p = obj.ResolvePointer();
        if (!p)
            return Value::MakeNull();

        // 1. String indexing str[i] -> returns char code
        if (obj.kind == ValueKind::String || obj.IsScriptString())
        {
            std::string s = (obj.kind == ValueKind::String) ? obj.sVal : obj.ToString();
            int64_t i = idx.iVal;
            if (i >= 0 && static_cast<size_t>(i) < s.size())
            {
                return Value::MakeInt(static_cast<uint8_t>(s[static_cast<size_t>(i)]));
            }
            return Value::MakeNull();
        }

        asIDBArrayView array;
        if (adapter.InspectArray(obj.GetTypeInfo(), p, array, asIDBInspection::Expression))
        {
            void* element = nullptr;
            if (idx.iVal < 0 || static_cast<uint64_t>(idx.iVal) >= array.count ||
                !adapter.ReadArrayElement(array, static_cast<size_t>(idx.iVal), element))
                return Value::MakeNull();

            const bool isHandle = (array.elementTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
            return Value::MakeAddress(element, array.elementTypeId, isHandle, obj.engine, adapter).DereferenceIfAddress();
        }

        float value = 0.0f;
        if (adapter.ReadIndexedFloat(obj.GetTypeInfo(), p, idx.iVal, value))
            return Value::MakeFloat(value);

        return Value::MakeNull();
    }
};

class CastNode : public Node
{
    std::string m_typeName;
    NodePtr     m_child;
public:
    CastNode(std::string typeName, NodePtr child) :
        m_typeName(std::move(typeName)), m_child(std::move(child)) {}

    Value Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const override
    {
        Value v = m_child->Evaluate(ctx, stackLevel, adapter);
        if (!ctx || !ctx->GetEngine())
            return v;

        asIScriptEngine* engine = ctx->GetEngine();
        asITypeInfo* targetType = engine->GetTypeInfoByName(m_typeName.c_str());
        if (targetType)
        {
            v.typeId = targetType->GetTypeId();
        }
        return v;
    }
};

inline Value IdentifierNode::ResolveMember(const Value& objVal, const std::string& memberName, asIScriptEngine* engine, const asIDBHostAdapter& adapter)
{
    void* objPtr = objVal.ResolvePointer();
    if (!objPtr || !engine || objVal.typeId == 0)
        return Value::MakeNull();

    // Zero-argument inspection whitelist on containers:
    if (memberName == "length" || memberName == "size" || memberName == "Length" || memberName == "Size" || memberName == "getSize")
    {
        size_t len = 0;
        if (objVal.IsScriptString() && objVal.ReadString(objPtr, nullptr, 0, len))
            return Value::MakeInt(static_cast<int64_t>(len));

        auto           type = objVal.GetTypeInfo();
        asIDBArrayView array;
        if (adapter.InspectArray(type, objPtr, array, asIDBInspection::Member))
            return Value::MakeInt(static_cast<int64_t>(array.count));
    }
    if (memberName == "isEmpty" || memberName == "IsEmpty")
    {
        size_t len = 0;
        if (objVal.IsScriptString() && objVal.ReadString(objPtr, nullptr, 0, len))
            return Value::MakeBool(len == 0);
    }

    asITypeInfo* type = engine->GetTypeInfoById(objVal.typeId);
    if (!type)
        return Value::MakeNull();

    // 1. Direct Script Object Properties
    if (objVal.typeId & asTYPEID_SCRIPTOBJECT)
    {
        asIScriptObject* sObj = reinterpret_cast<asIScriptObject*>(objPtr);
        for (asUINT p = 0; p < type->GetPropertyCount(); ++p)
        {
            const char* propName = nullptr;
            int         propTypeId = 0;
            type->GetProperty(p, &propName, &propTypeId);
            if (propName && memberName == propName)
            {
                void* propAddr = sObj->GetAddressOfProperty(p);
                bool  isH = (propTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
                return Value::MakeAddress(propAddr, propTypeId, isH, engine, adapter);
            }
        }

        // Backing field conventions: m_<name>, m_<CapitalName>, m_sz<Name>
        std::string capName = memberName;
        if (!capName.empty()) capName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(capName[0])));
        std::string candidate1 = "m_" + memberName;
        std::string candidate2 = "m_" + capName;
        std::string candidate3 = "m_sz" + capName;

        for (asUINT p = 0; p < type->GetPropertyCount(); ++p)
        {
            const char* propName = nullptr;
            int         propTypeId = 0;
            type->GetProperty(p, &propName, &propTypeId);
            if (propName && (candidate1 == propName || candidate2 == propName || candidate3 == propName ||
                             PathEqualsCaseInsensitive(memberName, propName)))
            {
                void* propAddr = sObj->GetAddressOfProperty(p);
                bool  isH = (propTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
                return Value::MakeAddress(propAddr, propTypeId, isH, engine, adapter);
            }
        }
    }
    else
    {
        // 2. C++ Registered native type properties
        for (asUINT p = 0; p < type->GetPropertyCount(); ++p)
        {
            const char* propName = nullptr;
            int         propTypeId = 0;
            int         offset = 0;
            int         compositeOffset = 0;
            bool        isCompositeIndirect = false;
            type->GetProperty(p, &propName, &propTypeId, nullptr, nullptr, &offset, nullptr, nullptr, &compositeOffset, &isCompositeIndirect);
            if (propName && (memberName == propName || PathEqualsCaseInsensitive(memberName, propName)))
            {
                uint8_t* base = reinterpret_cast<uint8_t*>(objPtr);
                void* propAddr = nullptr;
                if (isCompositeIndirect)
                {
                    uint8_t** indPtr = reinterpret_cast<uint8_t**>(base + compositeOffset);
                    if (indPtr && *indPtr)
                        propAddr = *indPtr + offset;
                }
                else
                {
                    propAddr = base + offset + compositeOffset;
                }
                if (propAddr)
                {
                    bool isH = (propTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) != 0;
                    return Value::MakeAddress(propAddr, propTypeId, isH, engine, adapter);
                }
            }
        }
    }

    return Value::MakeNull();
}

// Pratt / Recursive-Descent Expression Parser
class Parser
{
    std::string_view m_src;
    size_t           m_pos = 0;
    std::string      m_error;

    char Peek() const
    {
        return m_pos < m_src.size() ? m_src[m_pos] : '\0';
    }

    char Get()
    {
        return m_pos < m_src.size() ? m_src[m_pos++] : '\0';
    }

    void SkipWhitespace()
    {
        while (m_pos < m_src.size() && (m_src[m_pos] == ' ' || m_src[m_pos] == '\t' ||
                                        m_src[m_pos] == '\r' || m_src[m_pos] == '\n'))
        {
            m_pos++;
        }
    }

    bool Match(std::string_view kw)
    {
        SkipWhitespace();
        if (m_src.substr(m_pos).starts_with(kw))
        {
            m_pos += kw.size();
            return true;
        }
        return false;
    }

public:
    explicit Parser(std::string_view src) : m_src(src) {}

    const std::string& error() const { return m_error; }

    NodePtr ParseExpression()
    {
        return ParseLogicalOr();
    }

    asIDBExpected<NodePtr> Parse()
    {
        SkipWhitespace();
        if (m_src.empty())
        {
            return asIDBExpected<NodePtr>("Empty expression");
        }
        NodePtr node = ParseExpression();
        SkipWhitespace();
        if (!node)
        {
            return asIDBExpected<NodePtr>(!m_error.empty() ? std::string_view(m_error) : std::string_view("Failed to parse expression"));
        }
        if (m_pos < m_src.size())
        {
            return asIDBExpected<NodePtr>("Unexpected trailing tokens in expression");
        }
        return node;
    }

private:
    NodePtr ParseLogicalOr()
    {
        NodePtr node = ParseLogicalAnd();
        while (Match("||"))
        {
            NodePtr right = ParseLogicalAnd();
            node = std::make_shared<BinaryOpNode>(BinaryOp::Or, node, right);
        }
        return node;
    }

    NodePtr ParseLogicalAnd()
    {
        NodePtr node = ParseBitwiseOr();
        while (Match("&&"))
        {
            NodePtr right = ParseBitwiseOr();
            node = std::make_shared<BinaryOpNode>(BinaryOp::And, node, right);
        }
        return node;
    }

    NodePtr ParseBitwiseOr()
    {
        NodePtr node = ParseBitwiseXor();
        while (Peek() == '|' && m_src.substr(m_pos, 2) != "||")
        {
            Get();
            NodePtr right = ParseBitwiseXor();
            node = std::make_shared<BinaryOpNode>(BinaryOp::BitOr, node, right);
        }
        return node;
    }

    NodePtr ParseBitwiseXor()
    {
        NodePtr node = ParseBitwiseAnd();
        while (Match("^"))
        {
            NodePtr right = ParseBitwiseAnd();
            node = std::make_shared<BinaryOpNode>(BinaryOp::BitXor, node, right);
        }
        return node;
    }

    NodePtr ParseBitwiseAnd()
    {
        NodePtr node = ParseEquality();
        while (Peek() == '&' && m_src.substr(m_pos, 2) != "&&")
        {
            Get();
            NodePtr right = ParseEquality();
            node = std::make_shared<BinaryOpNode>(BinaryOp::BitAnd, node, right);
        }
        return node;
    }

    NodePtr ParseEquality()
    {
        NodePtr node = ParseRelational();
        while (true)
        {
            if (Match("=="))
            {
                NodePtr right = ParseRelational();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Equal, node, right);
            }
            else if (Match("!="))
            {
                NodePtr right = ParseRelational();
                node = std::make_shared<BinaryOpNode>(BinaryOp::NotEqual, node, right);
            }
            else if (Match("!is"))
            {
                NodePtr right = ParseRelational();
                node = std::make_shared<BinaryOpNode>(BinaryOp::NotIs, node, right);
            }
            else if (Match("is"))
            {
                NodePtr right = ParseRelational();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Is, node, right);
            }
            else
            {
                break;
            }
        }
        return node;
    }

    NodePtr ParseRelational()
    {
        NodePtr node = ParseShift();
        while (true)
        {
            if (Match("<="))
            {
                NodePtr right = ParseShift();
                node = std::make_shared<BinaryOpNode>(BinaryOp::LessEqual, node, right);
            }
            else if (Match(">="))
            {
                NodePtr right = ParseShift();
                node = std::make_shared<BinaryOpNode>(BinaryOp::GreaterEqual, node, right);
            }
            else if (Peek() == '<' && m_src.substr(m_pos, 2) != "<<")
            {
                Get();
                NodePtr right = ParseShift();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Less, node, right);
            }
            else if (Peek() == '>' && m_src.substr(m_pos, 2) != ">>")
            {
                Get();
                NodePtr right = ParseShift();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Greater, node, right);
            }
            else
            {
                break;
            }
        }
        return node;
    }

    NodePtr ParseShift()
    {
        NodePtr node = ParseAdditive();
        while (true)
        {
            if (Match("<<"))
            {
                NodePtr right = ParseAdditive();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Shl, node, right);
            }
            else if (Match(">>"))
            {
                NodePtr right = ParseAdditive();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Shr, node, right);
            }
            else
            {
                break;
            }
        }
        return node;
    }

    NodePtr ParseAdditive()
    {
        NodePtr node = ParseMultiplicative();
        while (true)
        {
            if (Match("+"))
            {
                NodePtr right = ParseMultiplicative();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Add, node, right);
            }
            else if (Match("-"))
            {
                NodePtr right = ParseMultiplicative();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Sub, node, right);
            }
            else
            {
                break;
            }
        }
        return node;
    }

    NodePtr ParseMultiplicative()
    {
        NodePtr node = ParseUnary();
        while (true)
        {
            if (Match("*"))
            {
                NodePtr right = ParseUnary();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Mul, node, right);
            }
            else if (Match("/"))
            {
                NodePtr right = ParseUnary();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Div, node, right);
            }
            else if (Match("%"))
            {
                NodePtr right = ParseUnary();
                node = std::make_shared<BinaryOpNode>(BinaryOp::Mod, node, right);
            }
            else
            {
                break;
            }
        }
        return node;
    }

    NodePtr ParseUnary()
    {
        SkipWhitespace();
        if (Match("!"))
            return std::make_shared<UnaryOpNode>(UnaryOp::Not, ParseUnary());
        if (Match("~"))
            return std::make_shared<UnaryOpNode>(UnaryOp::BitNot, ParseUnary());
        if (Match("-"))
            return std::make_shared<UnaryOpNode>(UnaryOp::Neg, ParseUnary());
        if (Match("+"))
            return std::make_shared<UnaryOpNode>(UnaryOp::Pos, ParseUnary());
        if (Match("@"))
            return std::make_shared<UnaryOpNode>(UnaryOp::HandleOf, ParseUnary());

        if (Match("cast<"))
        {
            size_t close = m_src.find('>', m_pos);
            if (close != std::string_view::npos)
            {
                std::string typeName(m_src.substr(m_pos, close - m_pos));
                m_pos = close + 1;
                SkipWhitespace();
                if (Match("("))
                {
                    NodePtr inner = ParseExpression();
                    Match(")");
                    return std::make_shared<CastNode>(typeName, inner);
                }
            }
        }

        return ParsePostfix();
    }

    NodePtr ParsePostfix()
    {
        NodePtr node = ParsePrimary();
        while (true)
        {
            SkipWhitespace();
            if (Match("."))
            {
                std::string member = ParseIdentifierName();
                // Check if followed by () method call
                SkipWhitespace();
                if (Match("("))
                {
                    Match(")"); // Whitelist methods like .length()
                }
                node = std::make_shared<MemberAccessNode>(node, member);
            }
            else if (Match("["))
            {
                NodePtr index = ParseExpression();
                Match("]");
                node = std::make_shared<IndexAccessNode>(node, index);
            }
            else
            {
                break;
            }
        }
        return node;
    }

    NodePtr ParsePrimary()
    {
        SkipWhitespace();
        if (Match("("))
        {
            NodePtr node = ParseExpression();
            Match(")");
            return node;
        }

        if (Match("true"))  return std::make_shared<LiteralNode>(Value::MakeBool(true));
        if (Match("false")) return std::make_shared<LiteralNode>(Value::MakeBool(false));
        if (Match("null"))  return std::make_shared<LiteralNode>(Value::MakeNull());

        // String Literal "..."
        if (Peek() == '"')
        {
            Get();
            std::string s;
            while (Peek() && Peek() != '"')
            {
                char c = Get();
                if (c == '\\' && Peek())
                {
                    char esc = Get();
                    if (esc == 'n') s += '\n';
                    else if (esc == 'r') s += '\r';
                    else if (esc == 't') s += '\t';
                    else s += esc;
                }
                else
                {
                    s += c;
                }
            }
            Match("\"");
            return std::make_shared<LiteralNode>(Value::MakeString(std::move(s)));
        }

        // Hex number: 0x...
        if (m_pos + 1 < m_src.size() && m_src[m_pos] == '0' && (m_src[m_pos + 1] == 'x' || m_src[m_pos + 1] == 'X'))
        {
            m_pos += 2;
            size_t start = m_pos;
            while (std::isxdigit(static_cast<unsigned char>(Peek())))
                Get();
            uint64_t val = std::strtoull(m_src.data() + start, nullptr, 16);
            return std::make_shared<LiteralNode>(Value::MakeInt(static_cast<int64_t>(val)));
        }

        // Decimal number (int or float)
        if (std::isdigit(static_cast<unsigned char>(Peek())))
        {
            size_t start = m_pos;
            bool isFloat = false;
            while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '.')
            {
                if (Peek() == '.') isFloat = true;
                Get();
            }
            if (Peek() == 'f' || Peek() == 'F')
            {
                isFloat = true;
                Get();
            }
            if (isFloat)
            {
                double d = std::strtod(m_src.data() + start, nullptr);
                return std::make_shared<LiteralNode>(Value::MakeFloat(d));
            }
            int64_t val = std::strtoll(m_src.data() + start, nullptr, 10);
            return std::make_shared<LiteralNode>(Value::MakeInt(val));
        }

        // Identifier (supports Scoped Identifiers like DynamicEquipmentType::Undefined)
        std::string ident = ParseIdentifierName();
        std::string ns;
        SkipWhitespace();
        if (Match("::"))
        {
            ns = std::move(ident);
            ident = ParseIdentifierName();
        }

        return std::make_shared<IdentifierNode>(ident, ns);
    }

    std::string ParseIdentifierName()
    {
        SkipWhitespace();
        size_t start = m_pos;
        while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_')
        {
            Get();
        }
        return std::string(m_src.substr(start, m_pos - start));
    }
};

// Hit-Condition Pre-compiled Rule
enum class HitOp
{
    AlwaysTrue,
    Exact,
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Modulo
};

struct HitCondition
{
    HitOp op     = HitOp::AlwaysTrue;
    int   target = 0;

    static HitCondition Parse(std::string_view cond)
    {
        HitCondition hc;
        while (!cond.empty() && (cond.front() == ' ' || cond.front() == '\t'))
            cond.remove_prefix(1);
        if (cond.empty())
            return hc;

        if (cond.starts_with("%"))
        {
            hc.op = HitOp::Modulo;
            hc.target = std::atoi(cond.data() + 1);
            return hc;
        }
        if (cond.starts_with("=="))
        {
            hc.op = HitOp::Equal;
            hc.target = std::atoi(cond.data() + 2);
            return hc;
        }
        if (cond.starts_with("!="))
        {
            hc.op = HitOp::NotEqual;
            hc.target = std::atoi(cond.data() + 2);
            return hc;
        }
        if (cond.starts_with(">="))
        {
            hc.op = HitOp::GreaterEqual;
            hc.target = std::atoi(cond.data() + 2);
            return hc;
        }
        if (cond.starts_with(">"))
        {
            hc.op = HitOp::Greater;
            hc.target = std::atoi(cond.data() + 1);
            return hc;
        }
        if (cond.starts_with("<="))
        {
            hc.op = HitOp::LessEqual;
            hc.target = std::atoi(cond.data() + 2);
            return hc;
        }
        if (cond.starts_with("<"))
        {
            hc.op = HitOp::Less;
            hc.target = std::atoi(cond.data() + 1);
            return hc;
        }

        // Bare number N: hitCount == N (exact)
        hc.op = HitOp::Exact;
        hc.target = std::atoi(cond.data());
        return hc;
    }

    bool Evaluate(int hitCount) const
    {
        switch (op)
        {
            case HitOp::AlwaysTrue:   return true;
            case HitOp::Exact:        return hitCount == target;
            case HitOp::Equal:        return hitCount == target;
            case HitOp::NotEqual:     return hitCount != target;
            case HitOp::Greater:      return hitCount > target;
            case HitOp::GreaterEqual: return hitCount >= target;
            case HitOp::Less:         return hitCount < target;
            case HitOp::LessEqual:    return hitCount <= target;
            case HitOp::Modulo:       return target > 0 && (hitCount % target == 0);
        }
        return true;
    }
};

// Logpoint Pre-compiled Template
struct LogTemplate
{
    struct Segment
    {
        bool        isExpr = false;
        std::string text;
        NodePtr     ast;
    };

    std::vector<Segment> segments;

    bool empty() const
    {
        return segments.empty();
    }

    static LogTemplate Parse(std::string_view msg)
    {
        LogTemplate lt;
        size_t i = 0;
        while (i < msg.size())
        {
            if (msg[i] == '{')
            {
                size_t close = msg.find('}', i + 1);
                if (close != std::string_view::npos)
                {
                    std::string token(msg.substr(i + 1, close - i - 1));
                    Parser p(token);
                    Segment seg;
                    seg.isExpr = true;
                    seg.ast = p.ParseExpression();
                    lt.segments.push_back(std::move(seg));
                    i = close + 1;
                    continue;
                }
            }

            size_t nextOpen = msg.find('{', i);
            if (nextOpen == std::string_view::npos)
                nextOpen = msg.size();

            Segment seg;
            seg.isExpr = false;
            seg.text = std::string(msg.substr(i, nextOpen - i));
            lt.segments.push_back(std::move(seg));
            i = nextOpen;
        }
        return lt;
    }

    std::string Evaluate(asIScriptContext* ctx, asUINT stackLevel, const asIDBHostAdapter& adapter) const
    {
        std::string res;
        for (const auto& seg : segments)
        {
            if (seg.isExpr && seg.ast)
            {
                Value v = seg.ast->Evaluate(ctx, stackLevel, adapter).DereferenceIfAddress();
                res += v.ToString();
            }
            else
            {
                res += seg.text;
            }
        }
        return res;
    }
};

} // namespace as_expr

END_AS_NAMESPACE
