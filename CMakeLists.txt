# cffi-luau: a libffi-based C FFI for Luau.
#
# This is a CMake port of cffi-lua, narrowed to target Luau exclusively.
# It builds a static library (`cffi`) that exposes the `luaopen_cffi` entry
# point; a host that embeds Luau links it in and calls luaopen_cffi(L).
#
# Luau has no native module loader (no require/package.loadlib), so unlike the
# original project this is NOT a loadable shared module -- it is meant to be
# statically linked into your Luau host.

cmake_minimum_required(VERSION 3.19)

project(cffi-luau LANGUAGES C CXX VERSION 0.2.4)

set(CMAKE_CXX_STANDARD 17)          # Luau requires C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

# Where the Luau source tree lives. Defaults to a sibling checkout.
set(LUAU_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../luau"
    CACHE PATH "Path to the Luau source tree (built via add_subdirectory)")

# libffi is fetched as a CMake-buildable fork. Override these to use a
# different fork/commit, or set CFFI_USE_SYSTEM_LIBFFI to find an installed one.
option(CFFI_USE_SYSTEM_LIBFFI "Use an installed libffi instead of FetchContent" OFF)
set(CFFI_LIBFFI_GIT_REPOSITORY "https://github.com/blade-lang/ffi.git"
    CACHE STRING "Git URL of the CMake-buildable libffi fork to fetch")
set(CFFI_LIBFFI_GIT_TAG "main"
    CACHE STRING "Git tag/branch/commit of the libffi fork to fetch")

option(CFFI_BUILD_TESTS "Build the Luau test host and register CTest cases" ON)

# ---------------------------------------------------------------------------
# Endianness (platform.hh requires exactly one of these to be defined)
# ---------------------------------------------------------------------------

include(TestBigEndian)
test_big_endian(CFFI_IS_BIG_ENDIAN)
if(CFFI_IS_BIG_ENDIAN)
    set(CFFI_ENDIAN_DEFINE FFI_BIG_ENDIAN)
else()
    set(CFFI_ENDIAN_DEFINE FFI_LITTLE_ENDIAN)
endif()

# ---------------------------------------------------------------------------
# Luau
# ---------------------------------------------------------------------------

if(NOT EXISTS "${LUAU_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Luau source not found at '${LUAU_SOURCE_DIR}'.\n"
        "Pass -DLUAU_SOURCE_DIR=/path/to/luau when configuring.")
endif()

# We consume Luau's C++ API directly (its symbols have C++ linkage by default),
# so do NOT enable LUAU_EXTERN_C. We only need the VM and, for the test host,
# the compiler -- skip Luau's CLI and tests.
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(LUAU_EXTERN_C OFF CACHE BOOL "" FORCE)

add_subdirectory("${LUAU_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/luau" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# libffi
# ---------------------------------------------------------------------------

if(CFFI_USE_SYSTEM_LIBFFI)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBFFI IMPORTED_TARGET libffi)
    endif()
    if(TARGET PkgConfig::LIBFFI)
        add_library(cffi_libffi ALIAS PkgConfig::LIBFFI)
    else()
        find_library(LIBFFI_LIBRARY NAMES ffi libffi REQUIRED)
        find_path(LIBFFI_INCLUDE_DIR NAMES ffi.h REQUIRED)
        add_library(cffi_libffi INTERFACE)
        target_link_libraries(cffi_libffi INTERFACE ${LIBFFI_LIBRARY})
        target_include_directories(cffi_libffi INTERFACE ${LIBFFI_INCLUDE_DIR})
    endif()
else()
    include(FetchContent)
    # Force a static libffi so the FFI is self-contained.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libffi
        GIT_REPOSITORY "${CFFI_LIBFFI_GIT_REPOSITORY}"
        GIT_TAG        "${CFFI_LIBFFI_GIT_TAG}"
    )
    FetchContent_MakeAvailable(libffi)

    # The fork sets include paths globally (no target_include_directories), so
    # attach them to the target ourselves and expose it under a stable alias.
    # Cover both source and generated (binary) include locations.
    target_include_directories(libffi PUBLIC
        "${libffi_SOURCE_DIR}"
        "${libffi_SOURCE_DIR}/include"
        "${libffi_BINARY_DIR}"
        "${libffi_BINARY_DIR}/include"
    )
    add_library(cffi_libffi ALIAS libffi)
endif()

# ---------------------------------------------------------------------------
# cffi-luau
# ---------------------------------------------------------------------------

set(CFFI_SOURCES
    src/util.cc
    src/ffilib.cc
    src/parser.cc
    src/ast.cc
    src/lib.cc
    src/ffi.cc
    src/main.cc
)

add_library(cffi STATIC ${CFFI_SOURCES})

target_include_directories(cffi PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${LUAU_SOURCE_DIR}/VM/include"
    "${LUAU_SOURCE_DIR}/Common/include"
)

target_compile_definitions(cffi PRIVATE ${CFFI_ENDIAN_DEFINE})

target_link_libraries(cffi PUBLIC Luau.VM cffi_libffi)

if(MSVC)
    # cffi does not use C++ exceptions or RTTI, but we leave the MSVC defaults
    # alone (mismatched /EH between objects is harmless and avoids surprises).
    target_compile_options(cffi PRIVATE /W3)
    target_compile_definitions(cffi PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # The codebase is written to build without exceptions/RTTI (as upstream).
    target_compile_options(cffi PRIVATE
        -Wall -Wextra -fno-exceptions -fno-rtti
        -fvisibility=hidden)
endif()

# ---------------------------------------------------------------------------
# Tests (a tiny Luau host that runs the tests/*.lua suite)
# ---------------------------------------------------------------------------

if(CFFI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
