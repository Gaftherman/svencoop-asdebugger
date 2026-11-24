#pragma once

#include "as_debugger_session.h"

class asSCDebugger : public asIDBDAPDebugger
{
public:
    explicit asSCDebugger(asIDBWorkspace* workspace);

protected:
    void OnDebugBreak(asIScriptContext* ctx) override;
};
