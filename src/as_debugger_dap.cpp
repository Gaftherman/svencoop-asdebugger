// MIT Licensed
// see https://github.com/Paril/angelscript-ui-debugger

#include "as_debugger_dap.h"
#include "as_debugger.h"
#include <dap/session.h>
#include <filesystem>
#include <charconv>
#include <chrono>
#include <thread>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <sys/socket.h>
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    include <unistd.h>
#endif

class asIDBDAPClient
{
public:
    asIDBDebugger*                dbg;
    std::atomic_bool              configuration_complete = false;
    std::atomic_bool              terminate              = false;
    std::unique_ptr<dap::Session> session;

    ~asIDBDAPClient()
    {
        if (dbg)
        {
            dbg->SetAction(asIDBAction::Continue);
        }
    }

    asIDBDAPClient(asIDBDebugger* dbg, const std::shared_ptr<dap::ReaderWriter>& socket) :
        dbg(dbg),
        session(dap::Session::create())
    {
        session->setOnInvalidData(dap::kClose);

        session->registerHandler([&](const dap::InitializeRequest& request) {
            dap::InitializeResponse response;
            response.supportsClipboardContext           = true;
            response.supportsConfigurationDoneRequest   = true;
            response.supportsDelayedStackTraceLoading   = true;
            response.supportsEvaluateForHovers          = true;
            response.supportsFunctionBreakpoints        = true;
            response.supportsBreakpointLocationsRequest = true;
            response.supportsLoadedSourcesRequest       = true;
            response.supportsReadMemoryRequest          = true;
            response.supportsWriteMemoryRequest         = true;
            response.supportsDisassembleRequest         = true;
            response.supportsSteppingGranularity        = true;
            response.supportsInstructionBreakpoints     = true;
            response.supportsExceptionInfoRequest       = true;
            response.supportsConditionalBreakpoints     = true;
            response.supportsHitConditionalBreakpoints  = true;
            response.supportsLogPoints                  = true;

            dap::ExceptionBreakpointsFilter allFilter;
            allFilter.filter = "all";
            allFilter.label  = "All Exceptions";
            allFilter.def    = false;

            dap::ExceptionBreakpointsFilter uncaughtFilter;
            uncaughtFilter.filter = "uncaught";
            uncaughtFilter.label  = "Uncaught Exceptions";
            uncaughtFilter.def    = false;

            response.exceptionBreakpointFilters = dap::array<dap::ExceptionBreakpointsFilter>{allFilter, uncaughtFilter};
            return response;
        });

        session->registerHandler([&](const dap::DisconnectRequest& request) { return this->HandleRequest(request); });
        session->registerHandler(
            [&](const dap::SetBreakpointsRequest& request) { return this->HandleRequest(request); });
        session->registerHandler(
            [&](const dap::SetFunctionBreakpointsRequest& request) { return this->HandleRequest(request); });
        session->registerHandler(
            [&](const dap::SetExceptionBreakpointsRequest& request) { return this->HandleRequest(request); });
        session->registerHandler(
            [&](const dap::ConfigurationDoneRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::ThreadsRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::StackTraceRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::ScopesRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::VariablesRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::PauseRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::ContinueRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::StepOutRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::StepInRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::NextRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::EvaluateRequest& request) { return this->HandleRequest(request); });
        session->registerHandler([&](const dap::AttachRequest& request) { return this->HandleRequest(request); });
        session->registerHandler(
            [&](const dap::BreakpointLocationsRequest& request) { return this->HandleRequest(request); });
        session->registerHandler(
            [&](const dap::LoadedSourcesRequest& response) { return this->HandleRequest(response); });
        session->registerHandler(
            [&](const dap::SourceRequest& response) { return this->HandleRequest(response); });
        session->registerHandler(
            [&](const dap::ReadMemoryRequest& response) { return this->HandleRequest(response); });
        session->registerHandler(
            [&](const dap::WriteMemoryRequest& response) { return this->HandleRequest(response); });
        session->registerHandler(
            [&](const dap::DisassembleRequest& response) { return this->HandleRequest(response); });
        session->registerHandler(
            [&](const dap::ExceptionInfoRequest& response) { return this->HandleRequest(response); });

        session->registerSentHandler(
            [&](const dap::ResponseOrError<dap::InitializeResponse>& response) { OnResponseSent(response); });
        session->registerSentHandler(
            [&](const dap::ResponseOrError<dap::ConfigurationDoneResponse>& response) { OnResponseSent(response); });

        session->bind(socket);
    }

    dap::ReadMemoryResponse HandleRequest(const dap::ReadMemoryRequest& request)
    {
        dap::ReadMemoryResponse response{};

        uintptr_t targetAddr = 0;
        if (request.memoryReference.rfind("0x", 0) == 0 || request.memoryReference.rfind("0X", 0) == 0)
        {
            targetAddr = std::strtoull(request.memoryReference.c_str() + 2, nullptr, 16);
        }
        else
        {
            targetAddr = std::strtoull(request.memoryReference.c_str(), nullptr, 10);
        }

        int64_t offset = request.offset.has_value() ? static_cast<int64_t>(request.offset.value()) : 0;
        int64_t count  = request.count;
        if (count <= 0 || targetAddr == 0)
            return response;

        uintptr_t readAddr = targetAddr + offset;
        if (!SafeProbeMemoryReadable(reinterpret_cast<const void*>(readAddr), static_cast<size_t>(count)))
        {
            response.unreadableBytes = count;
            return response;
        }

        std::string buf(static_cast<size_t>(count), '\0');
        std::memcpy(buf.data(), reinterpret_cast<const void*>(readAddr), static_cast<size_t>(count));
        response.address = fmt::format("0x{:x}", readAddr);
        response.data    = base64::to_base64(buf);
        return response;
    }

    dap::WriteMemoryResponse HandleRequest(const dap::WriteMemoryRequest& request)
    {
        dap::WriteMemoryResponse response{};

        uintptr_t targetAddr = 0;
        if (request.memoryReference.rfind("0x", 0) == 0 || request.memoryReference.rfind("0X", 0) == 0)
        {
            targetAddr = std::strtoull(request.memoryReference.c_str() + 2, nullptr, 16);
        }
        else
        {
            targetAddr = std::strtoull(request.memoryReference.c_str(), nullptr, 10);
        }

        int64_t   offset    = request.offset.has_value() ? static_cast<int64_t>(request.offset.value()) : 0;
        uintptr_t writeAddr = targetAddr + offset;

        std::vector<uint8_t> bytes = SafeFromBase64(request.data);
        if (bytes.empty() || writeAddr == 0)
            return response;

        if (!SafeProbeMemoryWritable(reinterpret_cast<void*>(writeAddr), bytes.size()))
        {
            return response;
        }

        std::memcpy(reinterpret_cast<void*>(writeAddr), bytes.data(), bytes.size());
        response.bytesWritten = static_cast<int64_t>(bytes.size());
        response.offset       = offset;
        return response;
    }

    dap::DisassembleResponse HandleRequest(const dap::DisassembleRequest& request)
    {
        dap::DisassembleResponse response{};

        uintptr_t baseAddr = 0;
        if (request.memoryReference.rfind("0x", 0) == 0 || request.memoryReference.rfind("0X", 0) == 0)
        {
            baseAddr = std::strtoull(request.memoryReference.c_str() + 2, nullptr, 16);
        }
        else
        {
            baseAddr = std::strtoull(request.memoryReference.c_str(), nullptr, 10);
        }

        int64_t   byteOffset = request.offset.has_value() ? static_cast<int64_t>(request.offset.value()) : 0;
        uintptr_t startAddr  = baseAddr + byteOffset;

        int64_t count = request.instructionCount > 0 ? static_cast<int64_t>(request.instructionCount) : static_cast<int64_t>(1);
        if (count > 500)
            count = 500;

        asIScriptEngine* engine = (dbg->cache && dbg->cache->ctx) ? dbg->cache->ctx->GetEngine() : nullptr;
        const asDWORD*   bc     = reinterpret_cast<const asDWORD*>(startAddr);

        for (int64_t i = 0; i < count; ++i)
        {
            auto& inst   = response.instructions.emplace_back();
            inst.address = fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(bc));

            if (!SafeProbeMemoryReadable(bc, sizeof(asDWORD)))
            {
                inst.presentationHint = "invalid";
                inst.instruction      = "???";
                break;
            }

            asEBCInstr op = static_cast<asEBCInstr>(*reinterpret_cast<const asBYTE*>(bc));
            if (static_cast<size_t>(op) >= 256 || asBCInfo[op].type == asBCTYPE_INFO)
            {
                inst.presentationHint = "invalid";
                inst.instruction      = fmt::format("unknown 0x{:02x}", static_cast<int>(*reinterpret_cast<const asBYTE*>(bc)));
                bc++;
                continue;
            }

            int instrSize = asBCTypeSize[asBCInfo[op].type];
            if (instrSize <= 0)
                instrSize = 1;

            if (!SafeProbeMemoryReadable(bc, instrSize * sizeof(asDWORD)))
            {
                inst.presentationHint = "invalid";
                inst.instruction      = "???";
                break;
            }

            std::string disasm = asBCInfo[op].name;
            switch (asBCInfo[op].type)
            {
                case asBCTYPE_NO_ARG:
                    break;
                case asBCTYPE_W_ARG:
                case asBCTYPE_wW_ARG:
                case asBCTYPE_rW_ARG:
                    disasm += fmt::format(" r{}", asBC_SWORDARG0(bc));
                    break;
                case asBCTYPE_DW_ARG:
                    if (op == asBC_CALL || op == asBC_CALLSYS || op == asBC_CALLBND)
                    {
                        int funcId = asBC_INTARG(bc);
                        if (engine)
                        {
                            auto targetFunc = engine->GetFunctionById(funcId);
                            if (targetFunc)
                                disasm += fmt::format(" {} ({})", funcId, targetFunc->GetDeclaration());
                            else
                                disasm += fmt::format(" {}", funcId);
                        }
                        else
                            disasm += fmt::format(" {}", funcId);
                    }
                    else if (op == asBC_JMP || op == asBC_JZ || op == asBC_JNZ || op == asBC_JS ||
                             op == asBC_JNS || op == asBC_JP || op == asBC_JNP)
                    {
                        int jmpOffset = asBC_INTARG(bc);
                        disasm += fmt::format(" {:+d} (-> 0x{:x})", jmpOffset, reinterpret_cast<uintptr_t>(bc + jmpOffset));
                    }
                    else
                    {
                        disasm += fmt::format(" 0x{:x}", asBC_DWORDARG(bc));
                    }
                    break;
                case asBCTYPE_rW_DW_ARG:
                case asBCTYPE_wW_DW_ARG:
                case asBCTYPE_W_DW_ARG:
                    disasm += fmt::format(" r{}, 0x{:x}", asBC_SWORDARG0(bc), asBC_DWORDARG(bc));
                    break;
                case asBCTYPE_wW_rW_ARG:
                case asBCTYPE_rW_rW_ARG:
                case asBCTYPE_wW_W_ARG:
                    disasm += fmt::format(" r{}, r{}", asBC_SWORDARG0(bc), asBC_SWORDARG1(bc));
                    break;
                case asBCTYPE_wW_rW_rW_ARG:
                    disasm += fmt::format(" r{}, r{}, r{}", asBC_SWORDARG0(bc), asBC_SWORDARG1(bc), asBC_SWORDARG2(bc));
                    break;
                case asBCTYPE_QW_ARG:
                    disasm += fmt::format(" 0x{:x}", asBC_QWORDARG(bc));
                    break;
                default:
                    disasm += " <operands>";
                    break;
            }

            inst.instruction = disasm;

            std::string rawBytes;
            const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(bc);
            for (int b = 0; b < instrSize * static_cast<int>(sizeof(asDWORD)); ++b)
            {
                rawBytes += fmt::format("{:02x} ", bytePtr[b]);
            }
            if (!rawBytes.empty())
                rawBytes.pop_back();
            inst.instructionBytes = rawBytes;

            bc += instrSize;
        }

        return response;
    }

    dap::ExceptionInfoResponse HandleRequest(const dap::ExceptionInfoRequest& request)
    {
        dap::ExceptionInfoResponse response{};

        response.breakMode   = "always";
        std::string desc = !dbg->last_exception_description.empty() ? dbg->last_exception_description :
            ((dbg->cache && dbg->cache->ctx && dbg->cache->ctx->GetExceptionString()) ? dbg->cache->ctx->GetExceptionString() : "Exception");

        response.description = desc;
        dap::ExceptionDetails details{};
        details.message      = desc;
        details.stackTrace   = dbg->last_exception_callstack;
        response.details     = details;
        response.exceptionId = "exception";

        return response;
    }

    dap::AttachResponse HandleRequest(const dap::AttachRequest& request)
    {
        return dap::AttachResponse{};
    }

    dap::LoadedSourcesResponse HandleRequest(const dap::LoadedSourcesRequest& request)
    {
        dap::LoadedSourcesResponse response{};

        if (dbg->workspace)
            dbg->workspace->CompileScriptSources();

        for (auto& source : dbg->workspace->sections)
        {
            auto& src = response.sources.emplace_back();
            src.name  = source.section;
            if (source.section.rfind("__", 0) == 0 || (dbg->workspace && dbg->workspace->SectionIsVirtual(source.section)))
            {
                src.sourceReference = source.ref;
            }
            else
            {
                auto resolved = dbg->workspace->SectionToPath(source.section);
                std::error_code ec;
                bool isLocal = !resolved.empty() && std::filesystem::exists(resolved, ec);
#ifndef _WIN32
                static bool inDocker = std::filesystem::exists("/.dockerenv", ec) ||
                                       std::filesystem::exists("/home/container", ec);
                if (inDocker || !isLocal)
                    src.sourceReference = source.ref;
                else
                    src.path = resolved;
#else
                if (isLocal)
                    src.path = resolved;
                else
                    src.sourceReference = source.ref;
#endif
            }
        }

        return response;
    }

    dap::ResponseOrError<dap::SourceResponse> HandleRequest(const dap::SourceRequest& request)
    {
        dap::SourceResponse response{};

        if (request.source && request.source->name)
        {
            auto name = request.source->name.value();
            if (dbg->workspace)
            {
                auto content = dbg->workspace->SectionSource(name);
                if (!content.empty())
                {
                    response.content = content;
                    return response;
                }
            }
        }

        if (request.source && request.source->path)
        {
            auto p = request.source->path.value();
            if (dbg->workspace)
            {
                auto sec = dbg->workspace->PathToSection(p);
                if (!sec.empty())
                {
                    auto content = dbg->workspace->SectionSource(sec);
                    if (!content.empty())
                    {
                        response.content = content;
                        return response;
                    }
                }
            }
        }

        if (request.sourceReference > 0 && dbg->workspace)
        {
            for (auto& sec : dbg->workspace->sections)
            {
                if (sec.ref == (uint64_t)request.sourceReference)
                {
                    response.content = dbg->workspace->SectionSource(sec.section);
                    return response;
                }
            }
        }

        return dap::Error("can't find source");
    }

    dap::BreakpointLocationsResponse HandleRequest(const dap::BreakpointLocationsRequest& request)
    {
        dap::BreakpointLocationsResponse response{};

        if (request.source.name.has_value())
        {
            std::scoped_lock lock(dbg->mutex);
            if (dbg->workspace && dbg->workspace->potential_breakpoints.empty())
                dbg->workspace->CompileBreakpointPositions();

            if (auto linecols = dbg->workspace->potential_breakpoints.find(request.source.name.value());
                linecols != dbg->workspace->potential_breakpoints.end())
            {
                // FIXME: STL equal_range maybe?
                for (auto& lc : linecols->second)
                {
                    if (lc.line < request.line)
                        continue;
                    else if (request.endLine.has_value() ? (lc.line > request.endLine.value()) : (lc.line > request.line))
                        continue;
                    else if (request.column.has_value() && lc.col < request.column.value())
                        continue;
                    else if (request.endColumn.has_value() && lc.col > request.endColumn.value())
                        continue;

                    auto& bp = response.breakpoints.emplace_back();

                    bp.line   = lc.line;
                    bp.column = lc.col;
                }
            }
        }

        return response;
    }

    dap::DisconnectResponse HandleRequest(const dap::DisconnectRequest& request)
    {
        terminate = true;
        if (dbg)
        {
            dbg->SetAction(asIDBAction::Continue);
        }
        return dap::DisconnectResponse{};
    }

    dap::SetBreakpointsResponse HandleRequest(const dap::SetBreakpointsRequest& request)
    {
        dap::SetBreakpointsResponse response;

        if (request.source.path)
        {
            auto rel     = dbg->workspace->PathToSection(request.source.path.value());
            auto pathstr = std::filesystem::path(rel).generic_string();

            std::scoped_lock lock(dbg->mutex);

            auto secIt = dbg->workspace->sections.find(pathstr);
            if (secIt == dbg->workspace->sections.end())
            {
                dbg->workspace->AddSection(pathstr);
                secIt = dbg->workspace->sections.find(pathstr);
            }

            std::string_view secKey = (secIt != dbg->workspace->sections.end()) ? std::string_view(secIt->section) : std::string_view(pathstr);
            dbg->breakpoints.erase(secKey);
            dbg->breakpoints.erase(pathstr);

            if (request.breakpoints && !request.breakpoints->empty() && secIt != dbg->workspace->sections.end())
            {
                auto& positions = dbg->workspace->potential_breakpoints[secIt->section];
                auto& bpList    = dbg->breakpoints[secIt->section];
                bpList.clear();

                for (auto& bp : *request.breakpoints)
                {
                    asIDBLineCol closest{-1, -1};

                    // FIXME: there's probably some STL methods that can
                    // speed up this lookup.
                    for (auto& pos : positions)
                    {
                        if (pos.line != bp.line)
                            continue;
                        else if (bp.column.has_value() && pos.col < bp.column.value())
                            continue;

                        closest.line = pos.line;
                        closest.col  = (closest.col == -1) ? pos.col : (std::min)(closest.col, pos.col);
                    }

                    auto& placed_bp = response.breakpoints.emplace_back();

                    if (closest.line == -1)
                    {
                        // Fallback to requested line if positions haven't been pre-indexed
                        closest.line = bp.line;
                        closest.col  = bp.column ? (int)bp.column.value() : 0;
                    }

                    placed_bp.line = closest.line;
                    if (bp.column.has_value())
                        placed_bp.column = closest.col;
                    std::optional<int> colOpt  = bp.column.has_value() ? std::optional<int>((int)closest.col) : std::nullopt;
                    std::string        cond    = bp.condition.has_value() ? std::string(bp.condition.value()) : "";
                    std::string        hitCond = bp.hitCondition.has_value() ? std::string(bp.hitCondition.value()) : "";
                    std::string        logMsg  = bp.logMessage.has_value() ? std::string(bp.logMessage.value()) : "";

                    asIDBBreakpoint newBp;
                    newBp.line         = (int)closest.line;
                    newBp.column       = colOpt;
                    newBp.condition    = cond;
                    newBp.hitCondition = hitCond;
                    newBp.logMessage   = logMsg;
                    newBp.hitCount     = 0;
                    newBp.warnedError  = false;

                    if (!cond.empty())
                    {
                        as_expr::Parser parser(cond);
                        auto res = parser.Parse();
                        if (res.has_value())
                        {
                            newBp.conditionAst = std::move(res.value());
                        }
                        else
                        {
                            newBp.conditionError = res.error();
                        }
                    }
                    if (!hitCond.empty())
                    {
                        newBp.hitRule = as_expr::HitCondition::Parse(hitCond);
                    }
                    if (!logMsg.empty())
                    {
                        newBp.logTemplate = as_expr::LogTemplate::Parse(logMsg);
                    }

                    if (!newBp.conditionError.empty())
                    {
                        placed_bp.verified = false;
                        placed_bp.message  = newBp.conditionError;
                    }
                    else
                    {
                        placed_bp.verified = true;
                    }

                    bpList.push_back(std::move(newBp));
                }
            }
        }

        dbg->UpdateHasWorkAtomic();
        return response;
    }

    dap::SetFunctionBreakpointsResponse HandleRequest(const dap::SetFunctionBreakpointsRequest& request)
    {
        std::scoped_lock                    lock(dbg->mutex);
        dap::SetFunctionBreakpointsResponse response{};

        dbg->function_breakpoints.clear();

        for (auto& fbp : request.breakpoints)
        {
            dbg->function_breakpoints.insert(fbp.name);

            dap::Breakpoint bp{};
            // TODO: validate name
            // TODO: link line/col
            bp.verified = true;
            response.breakpoints.emplace_back(std::move(bp));
        }
        dbg->UpdateHasWorkAtomic();
        return response;
    }

    dap::SetExceptionBreakpointsResponse HandleRequest(const dap::SetExceptionBreakpointsRequest& request)
    {
        std::scoped_lock lock(dbg->mutex);
        dbg->break_on_exceptions = !request.filters.empty();
        return {};
    }

    dap::ConfigurationDoneResponse HandleRequest(const dap::ConfigurationDoneRequest& request)
    {
        return {};
    }

    dap::ThreadsResponse HandleRequest(const dap::ThreadsRequest& request)
    {
        dap::ThreadsResponse response{};
        response.threads.push_back({1, "Main"});
        return response;
    }

    dap::StackTraceResponse HandleRequest(const dap::StackTraceRequest& request)
    {
        std::scoped_lock        lock(dbg->mutex);
        dap::StackTraceResponse response{};

        if (!dbg->cache || !dbg->cache->ctx)
        {
            response.totalFrames   = 1;
            auto& frame            = response.stackFrames.emplace_back();
            frame.id               = -1;
            frame.presentationHint = "label";
            frame.name             = "system func";
            return response;
        }

        dbg->cache->CacheCallstack();

        int64_t start = request.startFrame.has_value() ? (int64_t)(*request.startFrame) : (int64_t)0;
        int64_t levels =
            (request.levels.has_value() && request.levels.value() > 0) ? (int64_t)(*request.levels) : (int64_t)9999;

        response.totalFrames = dbg->cache->call_stack.size();

        for (asUINT i = 0; i < levels && (size_t)(i + start) < dbg->cache->call_stack.size(); i++)
        {
            auto& frame = response.stackFrames.emplace_back();
            auto& stack = dbg->cache->call_stack[i + start];
            frame.id    = stack.id;
            if (stack.scope.offset != SCOPE_SYSTEM)
            {
                frame.line   = stack.row;
                frame.column = stack.column;
                dap::Source src;
                src.name = std::string(stack.section);

                uint64_t secRef = 0;
                if (dbg->workspace)
                {
                    dbg->workspace->AddSection(stack.section);
                    auto it = dbg->workspace->sections.find(stack.section);
                    if (it != dbg->workspace->sections.end())
                        secRef = it->ref;
                }

                bool isVirtual = dbg->workspace && dbg->workspace->SectionIsVirtual(stack.section);
                auto resolved  = dbg->workspace ? dbg->workspace->SectionToPath(stack.section) : std::string(stack.section);

                std::error_code ec;
                bool isLocal = !isVirtual && !resolved.empty() && std::filesystem::exists(resolved, ec);
#ifndef _WIN32
                static bool inDocker = std::filesystem::exists("/.dockerenv", ec) ||
                                       std::filesystem::exists("/home/container", ec);
                if (inDocker || !isLocal)
                {
                    src.sourceReference = secRef > 0 ? (int64_t)secRef : ((int64_t)stack.id + 1000);
                }
                else
                {
                    src.path = resolved;
                }
#else
                if (isLocal)
                {
                    src.path = resolved;
                }
                else
                {
                    src.sourceReference = secRef > 0 ? (int64_t)secRef : ((int64_t)stack.id + 1000);
                }
#endif
                frame.source = std::move(src);
            }
            else
                frame.presentationHint = "label";
            frame.name = stack.declaration;
            if (stack.func && stack.func->GetByteCode())
            {
                frame.instructionPointerReference = fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(stack.func->GetByteCode()));
            }
        }

        return response;
    }

    dap::ResponseOrError<dap::ScopesResponse> HandleRequest(const dap::ScopesRequest& request)
    {
        std::scoped_lock lock(dbg->mutex);

        dap::ScopesResponse response{};

        if (!dbg->cache || !dbg->cache->ctx)
            return response;

        bool found = false;

        for (auto& stack : dbg->cache->call_stack)
        {
            if (stack.id != request.frameId)
                continue;

            asIScriptModule* activeModule = stack.func ? stack.func->GetModule() : nullptr;

            // 1. Locals
            if (!stack.scope.locals->namedProps.empty() ||
                !stack.scope.locals->indexedProps.empty())
            {
                auto& scope              = response.scopes.emplace_back();
                scope.name               = "Locals";
                scope.presentationHint   = "locals";
                scope.namedVariables     = stack.scope.locals->namedProps.size();
                scope.indexedVariables   = stack.scope.locals->indexedProps.size();
                scope.variablesReference = stack.scope.locals->expandRefId.value_or(0);
            }

            // 2. Parameters
            if (!stack.scope.parameters->namedProps.empty() ||
                !stack.scope.parameters->indexedProps.empty())
            {
                auto& scope              = response.scopes.emplace_back();
                scope.name               = "Parameters";
                scope.presentationHint   = "parameters";
                scope.namedVariables     = stack.scope.parameters->namedProps.size();
                scope.indexedVariables   = stack.scope.parameters->indexedProps.size();
                scope.variablesReference = stack.scope.parameters->expandRefId.value_or(0);
            }

            // 3. Temporaries (compiler internal registers)
            if (!stack.scope.registers->namedProps.empty() ||
                !stack.scope.registers->indexedProps.empty())
            {
                auto& scope              = response.scopes.emplace_back();
                scope.name               = "Temporaries";
                scope.presentationHint   = "registers";
                scope.expensive          = true;
                scope.namedVariables     = stack.scope.registers->namedProps.size();
                scope.indexedVariables   = stack.scope.registers->indexedProps.size();
                scope.variablesReference = stack.scope.registers->expandRefId.value_or(0);
            }

            // 4. VM Registers & Execution State
            {
                auto& regScope              = response.scopes.emplace_back();
                regScope.name               = "Registers";
                regScope.presentationHint   = "registers";
                regScope.expensive          = true;
                regScope.namedVariables     = 5;
                regScope.indexedVariables   = 0;
                regScope.variablesReference = 50000 + stack.id;
            }

            // 4. Globals for the currently executing script module
            if (activeModule)
            {
                dbg->cache->CacheGlobalsForModule(activeModule);
                auto modVars = dbg->cache->GetModuleGlobals(activeModule);
                if (modVars && (!modVars->namedProps.empty() || !modVars->indexedProps.empty()))
                {
                    auto&       scope        = response.scopes.emplace_back();
                    const char* mName        = activeModule->GetName();
                    scope.name               = (mName && *mName) ? fmt::format("Globals ({})", mName) : "Globals (Module)";
                    scope.presentationHint   = "globals";
                    scope.namedVariables     = modVars->namedProps.size();
                    scope.indexedVariables   = modVars->indexedProps.size();
                    scope.variablesReference = modVars->expandRefId.value_or(0);
                }
            }

            // 5. Engine Global Properties (g_EngineFuncs, etc.)
            dbg->cache->CacheEngineGlobals();
            auto engVars = dbg->cache->GetEngineGlobals();
            if (engVars && (!engVars->namedProps.empty() || !engVars->indexedProps.empty()))
            {
                auto& scope              = response.scopes.emplace_back();
                scope.name               = "Engine Properties";
                scope.presentationHint   = "globals";
                scope.namedVariables     = engVars->namedProps.size();
                scope.indexedVariables   = engVars->indexedProps.size();
                scope.variablesReference = engVars->expandRefId.value_or(0);
                scope.expensive          = true;
            }

            // 6. Other loaded script modules (if any)
            if (activeModule)
            {
                auto otherMods = dbg->cache->GetOtherModules(activeModule);
                if (otherMods && !otherMods->namedProps.empty())
                {
                    auto& scope              = response.scopes.emplace_back();
                    scope.name               = "Other Modules";
                    scope.presentationHint   = "globals";
                    scope.namedVariables     = otherMods->namedProps.size();
                    scope.indexedVariables   = 0;
                    scope.variablesReference = otherMods->expandRefId.value_or(0);
                    scope.expensive          = true;
                }
            }

            found = true;
            break;
        }

        if (!found)
            return dap::Error{"invalid stack ID"};

        return response;
    }

    dap::ResponseOrError<dap::VariablesResponse> HandleRequest(const dap::VariablesRequest& request)
    {
        std::scoped_lock lock(dbg->mutex);

        if (!dbg->cache || !dbg->cache->ctx)
            return dap::Error("no execution context or cache");

        // Registers Scope handling
        if (request.variablesReference >= 50000 && request.variablesReference < 60000)
        {
            int64_t                    frameId     = request.variablesReference - 50000;
            const asIDBCallStackEntry* targetFrame = nullptr;
            for (const auto& f : dbg->cache->call_stack)
            {
                if (f.id == frameId)
                {
                    targetFrame = &f;
                    break;
                }
            }
            if (!targetFrame)
                return dap::Error("invalid registers frame reference");

            dap::VariablesResponse response{};
            auto                   ctx    = dbg->cache->ctx;
            auto                   func   = targetFrame->func;
            asUINT                 offset = targetFrame->scope.offset;

            auto add_reg = [&](const std::string& name, const std::string& val, const std::string& type, uintptr_t memRef = 0) {
                auto& v              = response.variables.emplace_back();
                v.name               = name;
                v.value              = val;
                v.type               = dap::string(type);
                v.variablesReference = 0;
                if (memRef != 0)
                    v.memoryReference = fmt::format("0x{:x}", memRef);
            };

            void* bc = func ? func->GetByteCode() : nullptr;
            add_reg("PC", fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(bc)), "void*", reinterpret_cast<uintptr_t>(bc));
            add_reg("Function", func ? func->GetDeclaration() : "<unknown>", "asIScriptFunction*");

            const char* stateStr = "UNKNOWN";
            if (ctx)
            {
                switch (ctx->GetState())
                {
                    case asEXECUTION_FINISHED:
                        stateStr = "FINISHED";
                        break;
                    case asEXECUTION_SUSPENDED:
                        stateStr = "SUSPENDED";
                        break;
                    case asEXECUTION_ABORTED:
                        stateStr = "ABORTED";
                        break;
                    case asEXECUTION_EXCEPTION:
                        stateStr = "EXCEPTION";
                        break;
                    case asEXECUTION_PREPARED:
                        stateStr = "PREPARED";
                        break;
                    case asEXECUTION_UNINITIALIZED:
                        stateStr = "UNINITIALIZED";
                        break;
                    case asEXECUTION_ACTIVE:
                        stateStr = "ACTIVE";
                        break;
                    case asEXECUTION_ERROR:
                        stateStr = "ERROR";
                        break;
                }
            }
            add_reg("Context State", stateStr, "asEContextState");

            void* thisPtr = (ctx && offset != SCOPE_SYSTEM) ? ctx->GetThisPointer(offset) : nullptr;
            add_reg("This Pointer", fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(thisPtr)), "void*", reinterpret_cast<uintptr_t>(thisPtr));
            add_reg("Stack Level", fmt::format("{}", offset), "uint32");

            return response;
        }

        auto varit = dbg->cache->variable_refs.find(request.variablesReference);

        if (varit == dbg->cache->variable_refs.end())
            return dap::Error("invalid variablesReference");

        auto varContainer = varit->second.lock();
        if (!varContainer)
            return dap::Error("variable container has expired");

        dap::VariablesResponse response{};

        varContainer->Evaluate();
        varContainer->Expand();

        auto emplace_var = [&](const asIDBVariable::Ptr& local) {
            auto& var = response.variables.emplace_back();
            local->Evaluate();
            var.name = local->identifier.Combine();
            var.type = dap::string(local->typeName);
            if (local->getter)
            {
                dap::VariablePresentationHint hint{};
                hint.lazy            = true;
                var.presentationHint = hint;
                var.value            = "(...)";
            }
            else
            {
                var.value = local->value.empty() ? local->typeName : local->value;
            }
            var.namedVariables     = local->namedProps.size();
            var.indexedVariables   = local->indexedProps.size();
            var.variablesReference = local->expandRefId.value_or(0);
            var.memoryReference    = (local->address.address != nullptr) ?
                fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(local->address.address)) :
                fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(local.get()));
        };

        if (!request.filter.has_value() || request.filter.value() == "named")
            for (auto& var : varContainer->namedProps)
                emplace_var(var);

        if (!request.filter.has_value() || request.filter.value() == "indexed")
        {
            int64_t start = request.start.has_value() ? std::max<int64_t>(0, (int64_t)request.start.value()) : 0;
            int64_t total = (int64_t)varContainer->indexedProps.size();
            int64_t count = request.count.has_value() ? std::max<int64_t>(0, (int64_t)request.count.value()) : total;
            int64_t end   = std::min<int64_t>(start + count, total);

            for (int64_t i = start; i < end; i++)
                emplace_var(varContainer->indexedProps[i]);
        }

        return response;
    }

    dap::PauseResponse HandleRequest(const dap::PauseRequest& request)
    {
        dbg->SetAction(asIDBAction::Pause);
        return {};
    }

    dap::ContinueResponse HandleRequest(const dap::ContinueRequest& request)
    {
        dbg->SetAction(asIDBAction::Continue);
        return {};
    }

    dap::StepOutResponse HandleRequest(const dap::StepOutRequest& request)
    {
        dbg->SetAction(asIDBAction::StepOut);
        return {};
    }

    dap::StepInResponse HandleRequest(const dap::StepInRequest& request)
    {
        dbg->SetAction(asIDBAction::StepInto);
        return {};
    }

    dap::NextResponse HandleRequest(const dap::NextRequest& request)
    {
        dbg->SetAction(asIDBAction::StepOver);
        return {};
    }

    dap::ResponseOrError<dap::EvaluateResponse> HandleRequest(const dap::EvaluateRequest& request)
    {
        std::scoped_lock      lock(dbg->mutex);
        dap::EvaluateResponse response{};

        if (!dbg->cache)
            return response;

        std::optional<int> stack_index = std::nullopt;

        if (request.frameId.has_value())
        {
            for (size_t i = 0; i < dbg->cache->call_stack.size(); i++)
                if (request.frameId.value() == dbg->cache->call_stack[i].id)
                {
                    stack_index = i;
                    break;
                }
        }

        // Try AST parser and evaluator first for expressions, operators, comparisons, arithmetic
        as_expr::Parser parser(request.expression);
        auto astRes = parser.Parse();
        if (astRes.has_value())
        {
            as_expr::Value val = astRes.value()->Evaluate(dbg->cache->ctx, static_cast<asUINT>(stack_index.value_or(0))).DereferenceIfAddress();
            response.type               = dap::string(val.GetTypeName());
            response.result             = val.ToString();
            response.variablesReference = 0;
            if (val.addr)
            {
                response.memoryReference = fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(val.addr));
            }
            return response;
        }

        auto result = dbg->cache->ResolveExpression(request.expression, stack_index);

        if (!result.has_value())
            return dap::Error{result.error().data()};

        auto var = result.value().lock();
        if (!var)
            return dap::Error{"expression variable expired"};

        var->Evaluate();

        response.type               = dap::string(var->typeName);
        response.result             = var->value.empty() ? var->typeName : var->value;
        response.variablesReference = var->expandRefId.value_or(0);
        if (var->address.address)
        {
            response.memoryReference = fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(var->address.address));
        }

        return response;
    }

    void OnResponseSent(const dap::ResponseOrError<dap::InitializeResponse>& response)
    {
        {
            std::scoped_lock lock(dbg->mutex);
            dbg->breakpoints.clear();
        }
        session->send(dap::InitializedEvent());
    }

    void OnResponseSent(const dap::ResponseOrError<dap::ConfigurationDoneResponse>& response)
    {
        configuration_complete = true;
    }
};

asIDBDAPServer::asIDBDAPServer(int port, asIDBDebugger* debugger) :
    port(port),
    dbg(debugger)
{
}

/*virtual*/ asIDBDAPServer::~asIDBDAPServer()
{
    StopServer();
}

static bool CheckPortAvailable(int port)
{
#ifdef _WIN32
    WSADATA wsa;
    int     wsaRes = WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET  s      = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        if (wsaRes == 0)
            WSACleanup();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<u_short>(port));

    int res = ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::closesocket(s);
    if (wsaRes == 0)
        WSACleanup();
    return (res == 0);
#else
    int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0)
        return false;

    int opt = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    int res = ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(s);
    return (res == 0);
#endif
}

void asIDBDAPServer::ClientConnected(const std::shared_ptr<dap::ReaderWriter>& socket)
{
    std::lock_guard<std::mutex> lock(clientMutex);
    client = std::make_shared<asIDBDAPClient>(dbg, socket);
}

void asIDBDAPServer::ClientError(const char* msg)
{
}

void asIDBDAPServer::StartServer()
{
    int  candidatePort = port;
    bool found         = false;

    // Probe requested port, then fallback to candidatePort + 1 up to +10
    for (int offset = 0; offset <= 10; ++offset)
    {
        int p = port + offset;
        if (CheckPortAvailable(p))
        {
            candidatePort = p;
            found         = true;
            break;
        }
    }

    if (!found)
    {
        ASDebug_Notice("WARNING: DAP server could not bind to port %d or fallback ports %d-%d (addresses already in use). DAP server disabled to avoid game hang.",
                       port, port + 1, port + 10);
        actual_port = 0;
        server.reset();
        return;
    }

    actual_port = candidatePort;
    if (actual_port != port)
    {
        ASDebug_Notice("NOTICE: Default DAP port %d is already in use. Automatically switched to fallback port %d.",
                       port, actual_port);
        ASDebug_Notice("NOTICE: To attach from VS Code, set \"port\": %d in your launch.json, or close the application occupying port %d.",
                       actual_port, port);
    }

    server       = dap::net::Server::create();
    bool started = server->start(
        "0.0.0.0", actual_port,
        [&](const std::shared_ptr<dap::ReaderWriter>& socket) { this->ClientConnected(socket); },
        [&](const char* msg) { this->ClientError(msg); });

    if (!started)
    {
        ASDebug_Notice("ERROR: dap::net::Server::start failed on port %d! DAP server disabled.", actual_port);
        actual_port = 0;
        server.reset();
    }
}

void asIDBDAPServer::StopServer()
{
    std::shared_ptr<asIDBDAPClient> c;
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        c = client;
    }
    if (c && c->session)
    {
        dap::TerminatedEvent termEvent;
        c->session->send(termEvent);
        dap::ExitedEvent exitEvent;
        exitEvent.exitCode = 0;
        c->session->send(exitEvent);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        client.reset();
    }
    if (server)
    {
        server->stop();
        server.reset();
    }
    actual_port = 0;
}

void asIDBDAPServer::Tick()
{
    if (!server)
        return;

    std::lock_guard<std::mutex> lock(clientMutex);
    if (client && client->terminate)
        client.reset();
}

void asIDBDAPServer::SendEventToClient(const dap::TypeInfo* typeinfo, const void* event)
{
    if (!server)
        return;

    std::shared_ptr<asIDBDAPClient> c;
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        c = client;
    }
    if (c)
        c->session->send(typeinfo, event);
}