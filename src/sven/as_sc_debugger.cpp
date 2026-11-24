#include "as_sc_debugger.h"
#include "asdebugger.h"
#include "sven_host_adapter.h"

namespace
{
const SvenHostAdapter hostAdapter;
}

asSCDebugger::asSCDebugger(asIDBWorkspace* workspace) :
    asIDBDAPDebugger(workspace, hostAdapter)
{
}

void asSCDebugger::OnDebugBreak(asIScriptContext* ctx)
{
    if (ASDebug_IsVerbose())
        ASDebug_Log("DebugBreak ENTERED! ctx=%p", ctx);
}
