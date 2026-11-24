// SPDX-License-Identifier: MIT
#include "as_debugger_session.h"
#include <chrono>
#include <thread>

BEGIN_AS_NAMESPACE

asIDBDAPDebugger::asIDBDAPDebugger(asIDBWorkspace* workspace, const asIDBHostAdapter& adapter, bool requireClient) :
    asIDBDebugger(workspace, adapter),
    m_requireClient(requireClient)
{
}

asIDBDAPDebugger::~asIDBDAPDebugger()
{
    Resume();
}

std::string asIDBDAPDebugger::FetchSource(const char* section)
{
    if (workspace && section)
        return workspace->SectionSource(section);
    return {};
}

void asIDBDAPDebugger::SendLogOutput(const std::string& msg)
{
    if (dap_server)
    {
        dap::OutputEvent outEvent;
        outEvent.category = "console";
        outEvent.output   = msg;
        dap_server->SendEventToClient(outEvent);
    }
}

void asIDBDAPDebugger::Suspend()
{
    {
        std::scoped_lock lock(m_suspendMutex);
        m_suspended = true;
    }
    if (m_requireClient && (!dap_server || !dap_server->ClientConfigured()))
    {
        Resume();
        return;
    }

    if (dap_server)
    {
        dap::StoppedEvent event;
        bool              isException = is_exception_break || (cache && cache->ctx && cache->ctx->GetState() == asEXECUTION_EXCEPTION);

        if (isException)
        {
            event.reason      = "exception";
            std::string exStr = !last_exception_description.empty() ? last_exception_description :
                                                                      ((cache && cache->ctx && cache->ctx->GetExceptionString()) ? cache->ctx->GetExceptionString() : "Exception occurred");

            event.description = exStr;
            event.text        = exStr;

            int         col     = 0;
            const char* section = nullptr;
            int         row     = (cache && cache->ctx) ? cache->ctx->GetLineNumber(0, &col, &section) : 0;

            dap::OutputEvent outEvent;
            outEvent.category = "stderr";
            outEvent.output   = fmt::format("[AngelScript Exception] {} in '{}':{}\n", exStr, section ? section : "unknown", row);
            dap_server->SendEventToClient(outEvent);
        }
        else
        {
            switch (last_break_action)
            {
                case asIDBAction::Pause:
                    event.reason = "pause";
                    break;
                case asIDBAction::StepInto:
                case asIDBAction::StepOver:
                case asIDBAction::StepOut:
                    event.reason = "step";
                    break;
                default:
                    event.reason = "breakpoint";
                    break;
            }
        }

        event.threadId          = 1;
        event.allThreadsStopped = true;
        dap_server->SendEventToClient(event);
    }

    std::unique_lock<std::recursive_mutex> lock(m_suspendMutex);
    while (m_suspended.load())
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(15), [this] {
            return !m_suspended.load();
        });

        lock.unlock();
        if (dap_server)
        {
            dap_server->Tick();
            if (m_requireClient && !dap_server->ClientConfigured())
                Resume();
        }
        lock.lock();
    }
}

void asIDBDAPDebugger::Resume()
{
    {
        std::scoped_lock lock(m_suspendMutex);
        m_suspended        = false;
        is_exception_break = false;
        last_exception_description.clear();
        last_exception_callstack.clear();
    }
    m_cv.notify_all();
}

std::unique_ptr<asIDBCache> asIDBDAPDebugger::CreateCache(asIScriptContext* ctx)
{
    return std::make_unique<asIDBCache>(*this, ctx);
}

struct asIDBHostDebugger::Impl
{
    asIScriptEngine*                engine;
    asIDBFileWorkspace              workspace;
    asIDBDAPDebugger                debugger;
    std::unique_ptr<asIDBDAPServer> server;

    Impl(asIScriptEngine* engine, const asIDBHostAdapter& adapter, std::string_view sourceDirectory) :
        engine(engine),
        workspace(sourceDirectory, {engine}),
        debugger(&workspace, adapter, true)
    {
    }
};

asIDBHostDebugger::asIDBHostDebugger(asIScriptEngine* engine, const asIDBHostAdapter& adapter, std::string_view sourceDirectory) :
    impl(std::make_unique<Impl>(engine, adapter, sourceDirectory))
{
}

asIDBHostDebugger::~asIDBHostDebugger() = default;

bool asIDBHostDebugger::Start(std::string_view host, int port)
{
    if (!impl->engine || host.empty() || port < 1 || port > 65535 || impl->server)
        return false;

    auto server = std::make_unique<asIDBDAPServer>(port, &impl->debugger, std::string(host), false,
                                                  asIDBDAPServer::Notice{}, true);
    if (!server->StartServer())
        return false;

    impl->debugger.dap_server = server.get();
    impl->server              = std::move(server);
    return true;
}

bool asIDBHostDebugger::WaitForClient()
{
    if (!impl->server)
        return false;

    while (!impl->server->ClientConfigured())
    {
        Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return true;
}

void asIDBHostDebugger::Poll()
{
    if (impl->server)
        impl->server->Tick();

    auto&            dbg = impl->debugger;
    std::scoped_lock lock(dbg.mutex);
    auto*            context = dbg.stepping_context;
    if (context && context->GetState() != asEXECUTION_SUSPENDED && context->GetState() != asEXECUTION_ACTIVE)
    {
        dbg.action               = asIDBAction::None;
        dbg.stepping_context     = nullptr;
        dbg.stepping_function_id = -1;
        dbg.UpdateHasWorkAtomic();
    }
}

bool asIDBHostDebugger::AttachContext(asIScriptContext* context)
{
    if (!context || context->GetEngine() != impl->engine || context->GetState() == asEXECUTION_EXCEPTION)
        return false;
    if (impl->debugger.HookContext(context, true))
        return true;

    DetachContext(context);
    return false;
}

void asIDBHostDebugger::DetachContext(asIScriptContext* context)
{
    if (!context || context->GetEngine() != impl->engine)
        return;

    context->ClearLineCallback();
    context->ClearExceptionCallback();

    auto&            dbg = impl->debugger;
    std::scoped_lock lock(dbg.mutex);
    if (context == dbg.stepping_context)
    {
        dbg.action               = asIDBAction::None;
        dbg.stepping_context     = nullptr;
        dbg.stepping_function_id = -1;
        dbg.UpdateHasWorkAtomic();
    }
}

END_AS_NAMESPACE
