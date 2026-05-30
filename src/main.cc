/* Entry point for cffi-luau.
 *
 * Luau has no native module loader (no require()/package.loadlib), so unlike
 * the original cffi-lua this is not a dynamically loadable module: it is meant
 * to be statically linked into a host application that embeds Luau.
 *
 * After creating your lua_State and opening the standard libraries, call
 * luaopen_cffi(L). It leaves the cffi module table on top of the stack and
 * returns 1, so a typical host registers it as a global like this:
 *
 *     lua_pushcfunction(L, luaopen_cffi, "luaopen_cffi");
 *     lua_call(L, 0, 1);
 *     lua_setglobal(L, "ffi");
 */

#include "lua.hh"

#if defined(__GNUC__) && (__GNUC__ >= 4)
#  define CFFI_LUA_EXPORT __attribute__((visibility("default")))
#else
#  define CFFI_LUA_EXPORT
#endif

void ffi_module_open(lua_State *L);

extern "C" CFFI_LUA_EXPORT int luaopen_cffi(lua_State *L) {
    ffi_module_open(L);
    return 1;
}
