cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure formatting without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(ASDebuggerFormat LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()

option(ASDEBUGGER_BUILD_TESTS "Build the debugger tests" ON)
# The host-agnostic DAP core lives in angelscript-dap and is shared with the AngelScript CLI.
set(ASDEBUGGER_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/angelscript-dap"
    CACHE PATH "Directory of the host-agnostic debugger core")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metamod/cmake/Platform.cmake"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asext/cmake/SDK.cmake"
   OR NOT EXISTS "${ASDEBUGGER_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Initialize dependencies with git submodule update --init --recursive.")
endif()
set(METAMOD_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/metamod")
set(ASEXT_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/asext")
include("${METAMOD_SOURCE_PATH}/cmake/Platform.cmake")

project(asdebugger VERSION 1.5.0 LANGUAGES CXX)

enable_language(C)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
include("${METAMOD_SOURCE_PATH}/cmake/Build.cmake")
mmfg_configure_install_layout()
add_subdirectory(metamod)
add_subdirectory(asext)

# Sven supplies the VM at runtime through its own SDK ABI, so the core only needs the headers.
set(ASDEBUGGER_ANGELSCRIPT_TARGET AngelScript::Headers)
set(ASDEBUGGER_STD_ADAPTER OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ASDEBUGGER_CORE_COMPILE_OPTIONS -m32 -fvisibility=hidden -march=i686 -mtune=generic -msse -msse2)
endif()
add_subdirectory("${ASDEBUGGER_CORE_DIR}" angelscript-debugger-core)

set(ASDEBUGGER_SOURCES
    src/sven/asdebugger.cpp
    src/sven/as_sc_debugger.cpp
    src/sven/as_sc_workspace.cpp
    src/sven/dllapi.cpp
    src/sven/engine_api.cpp
    src/sven/engine_hook.cpp
    src/sven/h_export.cpp
    src/sven/meta_api.cpp
    src/sven/server_hook.cpp
    src/sven/sven_host_adapter.cpp
)

if(WIN32)
    list(APPEND ASDEBUGGER_SOURCES src/sven/dllmain.cpp)
endif()

add_library(asdebugger SHARED ${ASDEBUGGER_SOURCES})
mmfg_configure_target(asdebugger)
target_link_libraries(asdebugger PRIVATE asdebugger_core ASExt::SDK)
target_compile_definitions(asdebugger PRIVATE ASDEBUGGER_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_compile_definitions(asdebugger PRIVATE NOMINMAX)
    target_link_libraries(asdebugger PRIVATE ws2_32 dbghelp)
    target_link_options(asdebugger PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/sven/asdebugger.def")
else()
    target_compile_options(asdebugger PRIVATE -fexceptions)
    target_link_libraries(asdebugger PRIVATE pthread dl)
endif()

mmfg_assign_source_groups(asdebugger)
install(FILES assets/plugins.ini.example DESTINATION addons/metamod)
install(FILES examples/asdebugger_example.as DESTINATION scripts/plugins)
install(FILES README.md LICENSE docs/THIRD_PARTY.md DESTINATION .)
install(FILES metamod/LICENSE DESTINATION licenses RENAME metamod-LICENSE)
install(FILES asext/LICENSE DESTINATION licenses RENAME asext-LICENSE)

include(cmake/FormatValidation.cmake)

if(ASDEBUGGER_BUILD_TESTS)
    enable_testing()
    add_executable(plugin-smoke tests/plugin_smoke.cpp)
    target_compile_features(plugin-smoke PRIVATE cxx_std_17)
    target_link_libraries(plugin-smoke PRIVATE ASExt::SDK ${CMAKE_DL_LIBS})
    if(NOT WIN32)
        target_compile_definitions(plugin-smoke PRIVATE PLATFORM_POSIX LINUX _LINUX)
    endif()
    add_test(NAME plugin-smoke COMMAND plugin-smoke "$<TARGET_FILE:asdebugger>")
endif()
