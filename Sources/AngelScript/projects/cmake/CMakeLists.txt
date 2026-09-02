cmake_minimum_required(VERSION 3.10)
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS TRUE)
cmake_policy(SET CMP0003 NEW)

project(Angelscript)

set(ANGELSCRIPT_SOURCE
        ../../source/as_atomic.cpp
        ../../source/as_builder.cpp
        ../../source/as_bytecode.cpp
        ../../source/as_callfunc.cpp
        ../../source/as_callfunc_x86.cpp
        ../../source/as_callfunc_x64_gcc.cpp
        ../../source/as_callfunc_x64_msvc.cpp
        ../../source/as_compiler.cpp
        ../../source/as_configgroup.cpp
        ../../source/as_context.cpp
        ../../source/as_datatype.cpp
        ../../source/as_gc.cpp
        ../../source/as_generic.cpp
        ../../source/as_globalproperty.cpp
        ../../source/as_memory.cpp
        ../../source/as_module.cpp
        ../../source/as_objecttype.cpp
        ../../source/as_outputbuffer.cpp
        ../../source/as_parser.cpp
        ../../source/as_restore.cpp
        ../../source/as_scriptcode.cpp
        ../../source/as_scriptengine.cpp
        ../../source/as_scriptfunction.cpp
        ../../source/as_scriptnode.cpp
        ../../source/as_scriptobject.cpp
        ../../source/as_string.cpp
        ../../source/as_string_util.cpp
        ../../source/as_thread.cpp
        ../../source/as_tokenizer.cpp
        ../../source/as_typeinfo.cpp
        ../../source/as_variablescope.cpp
)

# Enable optimized x64/IA64 MASM assembly (ml64.exe)
# MASM (ml64.exe) only supports x64 and IA64. ARM64 requires armasm64.exe (different tool).
# MSVC_CXX_ARCHITECTURE_ID can be "X64", "x64", "IA64", "ia64", "amd64", "ARM64", "arm64" etc. depending on CMake/MSVC version.
# Use CMAKE_CL_64 (reliable for all 64-bit compilers) but explicitly exclude ARM64 (case-insensitive).
if(MSVC AND CMAKE_CL_64 AND NOT MSVC_CXX_ARCHITECTURE_ID MATCHES "^(ARM64|arm64)")
	enable_language(ASM_MASM)
	if(CMAKE_ASM_MASM_COMPILER_WORKS)
		set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_x64_msvc_asm.asm)
	else()
		message(FATAL_ERROR "MASM x64/IA64 target requires a working assembler")
	endif()
endif()

if(ANDROID)
        enable_language(ASM)
        if(CMAKE_ASM_COMPILER_WORKS)
                set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_arm.cpp ../../source/as_callfunc_arm_gcc.S)
        else()
                message(FATAL ERROR "Android target requires a working assembler")
        endif(CMAKE_ASM_COMPILER_WORKS)
endif()

# Add ARM64 support for macOS (Apple Silicon M1/M2/M3)
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        enable_language(ASM)
        if(CMAKE_ASM_COMPILER_WORKS)
                set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_arm64.cpp ../../source/as_callfunc_arm64_xcode.S)
                message(STATUS "Building AngelScript with ARM64 support for macOS")
        else()
                message(FATAL_ERROR "macOS ARM64 target requires a working assembler")
        endif()
endif()

# Add ARM64 support for Linux
if(UNIX AND NOT APPLE AND NOT ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        enable_language(ASM)
        if(CMAKE_ASM_COMPILER_WORKS)
                set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_arm64.cpp ../../source/as_callfunc_arm64_gcc.S)
                message(STATUS "Building AngelScript with ARM64 support for Linux")
        else()
                message(FATAL_ERROR "Linux ARM64 target requires a working assembler")
        endif()
endif()

# ARM64 Windows: Hybrid generic calling convention approach
# The as_callfunc_arm64.cpp requires as_callfunc_arm64_msvc.asm assembly (armasm64.exe compiler).
# Since armasm64.exe is not available in standard CMake + MSVC setups, we use a hybrid approach:
# 1. AngelScript: Enable AS_MAX_PORTABILITY for internal generic calling convention support
# 2. Script Bindings: Use SPADES_ASCC_* macros to select asCALL_GENERIC on ARM64
# This combination avoids the "Not supported" error that AS_MAX_PORTABILITY alone causes,
# because script functions explicitly use the generic calling convention.
# Performance impact: ~5-10% slower than native calling conventions on ARM64.
include(${CMAKE_SOURCE_DIR}/cmake/DetectARM64.cmake)
if(IS_ARM64)
	add_definitions(-DAS_MAX_PORTABILITY)
	message(STATUS "[AngelScript] Enabling AS_MAX_PORTABILITY for Windows ARM64")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    enable_language(ASM)
    if(CMAKE_ASM_COMPILER_WORKS)
        set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_riscv64.cpp ../../source/as_callfunc_riscv64_gcc.S)
    else()
        message(FATAL ERROR "RISC-V 64 target requires a working assembler")
    endif()
endif()

set(ANGELSCRIPT_HEADERS
        ../../include/angelscript.h
        ../../source/as_array.h
        ../../source/as_builder.h
        ../../source/as_bytecode.h
        ../../source/as_callfunc.h
        ../../source/as_compiler.h
        ../../source/as_config.h
        ../../source/as_configgroup.h
        ../../source/as_context.h
        ../../source/as_criticalsection.h
        ../../source/as_datatype.h
        ../../source/as_debug.h
        ../../source/as_generic.h
        ../../source/as_map.h
        ../../source/as_memory.h
        ../../source/as_module.h
        ../../source/as_namespace.h
        ../../source/as_objecttype.h
        ../../source/as_outputbuffer.h
        ../../source/as_parser.h
        ../../source/as_property.h
        ../../source/as_restore.h
        ../../source/as_scriptcode.h
        ../../source/as_scriptengine.h
        ../../source/as_scriptfunction.h
        ../../source/as_scriptnode.h
        ../../source/as_scriptobject.h
        ../../source/as_string.h
        ../../source/as_string_util.h
        ../../source/as_texts.h
        ../../source/as_thread.h
        ../../source/as_tokendef.h
        ../../source/as_tokenizer.h
        ../../source/as_typeinfo.h
        ../../source/as_variablescope.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_definitions("-D_CRT_SECURE_NO_WARNINGS -DANGELSCRIPT_EXPORT -D_LIB")

# Fix x64 issues on Linux
if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" AND NOT APPLE)
        add_definitions(-fPIC)
endif()

add_library(Angelscript STATIC ${ANGELSCRIPT_SOURCE} ${ANGELSCRIPT_HEADERS})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

find_package(Threads)
target_link_libraries(Angelscript ${CMAKE_THREAD_LIBS_INIT})

if(MSVC)
        set_target_properties(Angelscript PROPERTIES COMPILE_FLAGS "/MP")
endif(MSVC)

set(RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../../bin)

