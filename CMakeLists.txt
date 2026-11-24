cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure formatting without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(ASDebuggerFormat LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()

set(asdebugger_sven_default OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(asdebugger_sven_default ON)
endif()
option(ASDEBUGGER_BUILD_SVEN_PLUGIN "Build the 32-bit Sven Co-op plugin" ${asdebugger_sven_default})
option(ASDEBUGGER_STD_ADAPTER "Build the vanilla AngelScript add-on adapter" OFF)
option(ASDEBUGGER_BUILD_TESTS "Build the debugger tests" ${ASDEBUGGER_BUILD_SVEN_PLUGIN})
set(ASDEBUGGER_ANGELSCRIPT_TARGET "" CACHE STRING "AngelScript target supplied by the embedding project")
set(ASDEBUGGER_STD_ADDON_DIR "" CACHE PATH "AngelScript sdk/add_on directory for the standard adapter")

if(ASDEBUGGER_BUILD_SVEN_PLUGIN)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metamod/cmake/Platform.cmake"
       OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asext/cmake/SDK.cmake")
        message(FATAL_ERROR "Initialize dependencies with git submodule update --init --recursive.")
    endif()
    set(METAMOD_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/metamod")
    set(ASEXT_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/asext")
    include("${METAMOD_SOURCE_PATH}/cmake/Platform.cmake")
endif()

project(asdebugger VERSION 1.5.0 LANGUAGES CXX)

if(ASDEBUGGER_BUILD_SVEN_PLUGIN)
    enable_language(C)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
    endif()
    include("${METAMOD_SOURCE_PATH}/cmake/Build.cmake")
    mmfg_configure_install_layout()
    add_subdirectory(metamod)
    add_subdirectory(asext)
endif()

if(ASDEBUGGER_ANGELSCRIPT_TARGET)
    set(asdebugger_angelscript "${ASDEBUGGER_ANGELSCRIPT_TARGET}")
elseif(TARGET angelscript)
    set(asdebugger_angelscript angelscript)
elseif(ASDEBUGGER_BUILD_SVEN_PLUGIN AND NOT ASDEBUGGER_STD_ADAPTER)
    # Sven supplies the VM at runtime via its own SDK ABI.
    set(asdebugger_angelscript AngelScript::Headers)
else()
    message(FATAL_ERROR "Add an AngelScript target before asdebugger or set ASDEBUGGER_ANGELSCRIPT_TARGET.")
endif()
if(NOT TARGET "${asdebugger_angelscript}")
    message(FATAL_ERROR "AngelScript target '${asdebugger_angelscript}' does not exist.")
endif()

include(FetchContent)
FetchContent_Declare(
    cppdap
    GIT_REPOSITORY https://github.com/google/cppdap.git
    GIT_TAG main
    GIT_SUBMODULES "third_party/json"
    GIT_SHALLOW TRUE
)
set(CPPDAP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(cppdap)
FetchContent_GetProperties(cppdap SOURCE_DIR cppdap_source)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/patch_cppdap.py" "${cppdap_source}"
    COMMAND_ERROR_IS_FATAL ANY
)
find_package(Threads REQUIRED)

function(asdebugger_add_core target angelscript_target)
    add_library(${target} STATIC
        src/core/as_debugger.cpp
        src/core/as_debugger_dap.cpp
        src/core/as_debugger_host.cpp
        src/core/as_helpers.cpp
    )
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
    target_link_libraries(${target} PUBLIC "${angelscript_target}" cppdap PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(${target} PRIVATE ws2_32)
    endif()
    if(ASDEBUGGER_BUILD_SVEN_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND angelscript_target STREQUAL "AngelScript::Headers")
        target_compile_options(${target} PRIVATE -m32 -fvisibility=hidden -march=i686 -mtune=generic -msse -msse2)
    endif()
endfunction()

asdebugger_add_core(asdebugger_core "${asdebugger_angelscript}")

if(ASDEBUGGER_STD_ADAPTER)
    if(NOT EXISTS "${ASDEBUGGER_STD_ADDON_DIR}/scriptarray/scriptarray.h"
       OR NOT EXISTS "${ASDEBUGGER_STD_ADDON_DIR}/scriptdictionary/scriptdictionary.h")
        message(FATAL_ERROR "Set ASDEBUGGER_STD_ADDON_DIR to the vanilla AngelScript sdk/add_on directory.")
    endif()
    target_sources(asdebugger_core PRIVATE src/core/adapters/std_host_adapter.cpp)
    target_include_directories(asdebugger_core PRIVATE
        "${ASDEBUGGER_STD_ADDON_DIR}/scriptarray"
        "${ASDEBUGGER_STD_ADDON_DIR}/scriptdictionary"
    )
endif()

if(ASDEBUGGER_BUILD_SVEN_PLUGIN)
    set(asdebugger_plugin_core_target asdebugger_core)
    if(NOT asdebugger_angelscript STREQUAL "AngelScript::Headers" OR ASDEBUGGER_STD_ADAPTER)
        set(asdebugger_plugin_core_target asdebugger_sven_core)
        asdebugger_add_core(${asdebugger_plugin_core_target} AngelScript::Headers)
    endif()

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
    target_link_libraries(asdebugger PRIVATE ${asdebugger_plugin_core_target} ASExt::SDK)
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
    install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
    install(FILES metamod/LICENSE DESTINATION licenses RENAME metamod-LICENSE)
    install(FILES asext/LICENSE DESTINATION licenses RENAME asext-LICENSE)

    include(cmake/FormatValidation.cmake)
endif()

if(ASDEBUGGER_BUILD_TESTS)
    enable_testing()
    add_executable(host-adapter-test tests/host_adapter.cpp)
    target_link_libraries(host-adapter-test PRIVATE asdebugger_core)
    add_test(NAME host-adapter-test COMMAND host-adapter-test)

    if(ASDEBUGGER_BUILD_SVEN_PLUGIN)
        add_executable(plugin-smoke tests/plugin_smoke.cpp)
        target_compile_features(plugin-smoke PRIVATE cxx_std_17)
        target_link_libraries(plugin-smoke PRIVATE ASExt::SDK ${CMAKE_DL_LIBS})
        if(NOT WIN32)
            target_compile_definitions(plugin-smoke PRIVATE PLATFORM_POSIX LINUX _LINUX)
        endif()
        add_test(NAME plugin-smoke COMMAND plugin-smoke "$<TARGET_FILE:asdebugger>")
    endif()
endif()
