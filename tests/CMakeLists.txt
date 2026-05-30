# Test support library. Built shared so the FFI can dlopen/LoadLibrary it at
# runtime via cffi.load() (its symbols are exported with __declspec(dllexport)
# / default visibility from testlib.cc). The "" prefix keeps the name as
# testlib.dll / testlib.so (no "lib" prefix) on all platforms.
add_library(testlib SHARED testlib.cc)
set_target_properties(testlib PROPERTIES PREFIX "")
if(MSVC)
    target_compile_definitions(testlib PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(testlib PRIVATE -fno-exceptions -fno-rtti)
endif()

# Luau host that runs a single test file (see host.cc).
add_executable(cffi_test_host host.cc)
target_include_directories(cffi_test_host PRIVATE
    "${LUAU_SOURCE_DIR}/VM/include"
    "${LUAU_SOURCE_DIR}/Compiler/include"
    "${LUAU_SOURCE_DIR}/Common/include"
)
target_link_libraries(cffi_test_host PRIVATE cffi Luau.Compiler Luau.VM)

# Test cases. These mirror the original suite minus metatype54 (which targets
# Lua 5.4 semantics that Luau, being ~5.1 at the C API level, does not have).
set(CFFI_TEST_CASES
    simple
    abi
    variadic
    simple_pass
    struct_array
    struct_pass
    struct_union_array_fields
    unionval
    globals
    copy_fill
    dump_string
    callbacks
    table_init
    parameterized
    scalar
    redir
    callconv
    cexpr
    redef
    cast
    istype
    metatype
)

foreach(case_name ${CFFI_TEST_CASES})
    add_test(
        NAME ${case_name}
        COMMAND cffi_test_host
            "${CMAKE_CURRENT_SOURCE_DIR}/${case_name}.lua"
            "$<TARGET_FILE:testlib>"
    )
    # host.cc exits 77 when a test calls skip_test()
    set_tests_properties(${case_name} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
