// SPDX-License-Identifier: MIT
#pragma once

#include "as_debugger.h"
#include "as_debugger_dap.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

BEGIN_AS_NAMESPACE

class asIDBDAPDebugger : public asIDBDebugger
{
public:
    asIDBDAPServer* dap_server = nullptr;

    asIDBDAPDebugger(asIDBWorkspace* workspace, const asIDBHostAdapter& adapter, bool requireClient = false);
    virtual ~asIDBDAPDebugger() override;

    virtual std::string FetchSource(const char* section) override;
    virtual void        SendLogOutput(const std::string& msg) override;

protected:
    virtual void                        Suspend() override;
    virtual void                        Resume() override;
    virtual std::unique_ptr<asIDBCache> CreateCache(asIScriptContext* ctx) override;

private:
    const bool                  m_requireClient;
    std::recursive_mutex        m_suspendMutex;
    std::condition_variable_any m_cv;
    std::atomic_bool            m_suspended = false;
};

END_AS_NAMESPACE
