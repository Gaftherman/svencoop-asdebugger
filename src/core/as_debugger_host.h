// SPDX-License-Identifier: MIT
#pragma once

#include <angelscript.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Build your script modules, then construct asIDBHostDebugger with their engine,
// an adapter for the registered add-ons, and the directory containing the sources.
// Start the DAP listener and optionally wait for the client to configure it.
// Attach each context before executing it and call Poll between executions.
// Detach contexts before releasing them or destroying the debugger. The engine
// and adapter must outlive the debugger. Drive the API and script execution
// from one host thread, and keep the modules stable while the debugger is active.

BEGIN_AS_NAMESPACE

enum class asIDBInspection
{
    Variables,
    Expression,
    Member
};

struct asIDBArrayView
{
    std::size_t count         = 0;
    int         elementTypeId = 0;
    // Opaque adapter state. The debugger uses only count and elementTypeId.
    const void* storage = nullptr;
    std::size_t stride  = 0;
};

struct asIDBDictionaryEntry
{
    std::string key;
    int         typeId;
    void*       address;
};

class asIDBHostAdapter
{
public:
    virtual ~asIDBHostAdapter() = default;

    virtual bool IsStringType(asITypeInfo* type) const     = 0;
    virtual bool IsArrayType(asITypeInfo* type) const      = 0;
    virtual bool IsDictionaryType(asITypeInfo* type) const = 0;

    // length is the full byte length. For nonzero capacity, write at most
    // capacity - 1 bytes followed by a NUL. Zero capacity queries length only.
    virtual bool ReadString(asITypeInfo* type, const void* object, char* buffer, std::size_t capacity, std::size_t& length, asIDBInspection usage) const = 0;

    virtual bool InspectArray(asITypeInfo* type, const void* object, asIDBArrayView& view, asIDBInspection usage) const = 0;
    // Return an object address for values and a slot address for handles.
    virtual bool ReadArrayElement(const asIDBArrayView& view, std::size_t index, void*& address) const = 0;
    // Optional numeric indexing for host objects such as native vectors.
    virtual bool ReadIndexedFloat(asITypeInfo*, const void*, std::int64_t, float&) const { return false; }

    virtual bool GetDictionarySize(const void* object, std::size_t& count) const = 0;
    // Fill up to capacity entries. Addresses follow the array element convention.
    virtual std::size_t ReadDictionary(const void* object, asIDBDictionaryEntry* entries, std::size_t capacity) const = 0;
};

class asIDBHostDebugger
{
public:
    asIDBHostDebugger(asIScriptEngine* engine, const asIDBHostAdapter& adapter, std::string_view sourceDirectory = ".");
    ~asIDBHostDebugger();

    bool Start(std::string_view host = "127.0.0.1", int port = 9002);
    // Returns when the client sends configurationDone, or false without a listener.
    bool WaitForClient();
    void Poll();

    // These install and clear the context's line and exception callbacks.
    bool AttachContext(asIScriptContext* context);
    void DetachContext(asIScriptContext* context);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

END_AS_NAMESPACE
