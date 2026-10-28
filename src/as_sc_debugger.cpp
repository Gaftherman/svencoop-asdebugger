#include "as_sc_debugger.h"
#include <chrono>
#include <thread>

asSCDebugger::asSCDebugger(asIDBWorkspace* workspace) :
    asIDBDebugger(workspace)
{
}

asSCDebugger::~asSCDebugger()
{
    Resume();
}

std::string asSCDebugger::FetchSource(const char* section)
{
    if (workspace && section)
        return workspace->SectionSource(section);
    return {};
}

void asSCDebugger::SendLogOutput(const std::string& msg)
{
    if (dap_server)
    {
        dap::OutputEvent outEvent;
        outEvent.category = "console";
        outEvent.output   = msg;
        dap_server->SendEventToClient(outEvent);
    }
}

void asSCDebugger::Suspend()
{
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
    m_suspended = true;

    while (m_suspended.load())
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(15), [this] {
            return !m_suspended.load();
        });

        lock.unlock();
        if (dap_server)
            dap_server->Tick();
        lock.lock();
    }
}

void asSCDebugger::Resume()
{
    {
        std::scoped_lock lock(m_suspendMutex);
        m_suspended = false;
        is_exception_break = false;
        last_exception_description.clear();
        last_exception_callstack.clear();
    }
    m_cv.notify_all();
}

std::unique_ptr<asIDBCache> asSCDebugger::CreateCache(asIScriptContext* ctx)
{
    return std::make_unique<asIDBCache>(*this, ctx);
}
