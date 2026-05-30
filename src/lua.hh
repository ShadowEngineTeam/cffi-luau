#ifndef LUA_HH
#define LUA_HH

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "platform.hh"
#include "util.hh"

#if defined(FFI_DIAGNOSTIC_PRAGMA_CLANG)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(FFI_DIAGNOSTIC_PRAGMA_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

/* Luau public headers.
 *
 * Unlike reference Lua, Luau's API has C++ linkage by default (unless the VM
 * was compiled with LUAU_EXTERN_C). We therefore include the headers directly
 * and must NOT wrap them in `extern "C"`, or the declarations would not match
 * the compiled VM and linking would fail. lualib.h pulls in the auxiliary
 * library too (Luau has no separate lauxlib.h).
 */
#include <lua.h>
#include <lualib.h>

#if defined(FFI_DIAGNOSTIC_PRAGMA_CLANG)
#pragma clang diagnostic pop
#elif defined(FFI_DIAGNOSTIC_PRAGMA_GCC)
#pragma GCC diagnostic pop
#endif

/* Luau does not define LUA_VERSION_NUM. At the C API level it is closest to
 * Lua 5.1: numbers have no integer subtype in the base language, there are no
 * bit operators, no to-be-closed variables, no __pairs/__ipairs, etc. We
 * present it as 5.1 so that all the version-gated code elsewhere in cffi
 * resolves to the 5.1-compatible paths, which is exactly the feature subset
 * Luau supports. Luau-specific divergences are bridged with the shims below.
 */
#ifndef LUA_VERSION_NUM
#define LUA_VERSION_NUM 501
#endif

/* ----------------------------------------------------------------------------
 * C API compatibility shims for Luau
 * ------------------------------------------------------------------------- */

/* Luau's closure-pushing primitives require a debug name (and arity); the rest
 * of cffi uses the classic 2-/3-argument forms. Re-point the classic spellings
 * at lua_pushcclosurek so existing call sites stay unchanged.
 */
#ifdef lua_pushcfunction
#undef lua_pushcfunction
#endif
#define lua_pushcfunction(L, fn) lua_pushcclosurek(L, fn, "cffi", 0, nullptr)

#ifdef lua_pushcclosure
#undef lua_pushcclosure
#endif
#define lua_pushcclosure(L, fn, nup) lua_pushcclosurek(L, fn, "cffi", nup, nullptr)

/* Luau has no integer subtype in the base language; numbers are doubles. The
 * 5.3+ "seamless integer" paths are compiled out via LUA_VERSION_NUM anyway,
 * but cffi still references lua_isinteger unconditionally in a few places.
 */
#ifdef lua_isinteger
#undef lua_isinteger
#endif
#define lua_isinteger(L, idx) (0)

/* Reference refs: Luau exposes lua_ref/lua_unref (registry only; lua_ref does
 * NOT pop its argument). cffi uses the auxlib spellings against
 * LUA_REGISTRYINDEX, so provide those with the classic 5.1 semantics.
 *
 * IMPORTANT: lua_unref in Luau is a non-allocating, barrier-free write to the
 * registry freelist (see VM/src/lapi.cpp). That makes it safe to call from a
 * userdata destructor, which is how cffi releases its refs during GC. Calling
 * any allocating / VM-reentrant API (lua_pcall, pushing values, etc.) from a
 * destructor is NOT safe and must be avoided.
 */
static inline int luaL_ref(lua_State *L, int t) {
    assert(t == LUA_REGISTRYINDEX);
    (void)t;
    int r = lua_ref(L, -1);
    lua_pop(L, 1);
    return r;
}

static inline void luaL_unref(lua_State *L, int t, int ref) {
    (void)t;
    lua_unref(L, ref);
}

/* Auxiliary helpers present in the 5.1 reference library but absent in Luau. */

static inline void luaL_setmetatable(lua_State *L, char const *tname) {
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);
}

static inline void *luaL_testudata(lua_State *L, int ud, char const *tname) {
    void *p = lua_touserdata(L, ud);
    if (!p || !lua_getmetatable(L, ud)) {
        return nullptr;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, tname);
    if (lua_rawequal(L, -1, -2)) {
        lua_pop(L, 2);
        return p;
    }
    lua_pop(L, 2);
    return nullptr;
}

static inline std::size_t lua_rawlen(lua_State *L, int index) {
    return std::size_t(lua_objlen(L, index));
}

static inline void luaL_newlib(lua_State *L, luaL_Reg const l[]) {
    lua_newtable(L);
    luaL_register(L, nullptr, l);
}

namespace lua {

static constexpr int CFFI_CTYPE_TAG = -128;
static constexpr char const CFFI_CDATA_MT[] = "cffi_cdata_handle";
static constexpr char const CFFI_LIB_MT[] = "cffi_lib_handle";
static constexpr char const CFFI_DECL_STOR[] = "cffi_decl_stor";
static constexpr char const CFFI_PARSER_STATE[] = "cffi_parser_state";

/* Luau userdata tags.
 *
 * Luau does not support __gc metamethods. cffi-managed userdata are therefore
 * allocated with a tag (lua_newuserdatatagged) and a C destructor registered
 * for that tag with lua_setuserdatadtor. These tags are global to the
 * lua_State; if the embedding host already uses these particular tag numbers
 * for its own userdata, override them at compile time.
 *
 * Storage that only needs a C++ destructor (the declaration store and parser
 * state) instead uses lua_newuserdatadtor and needs no dedicated tag.
 */
#ifndef CFFI_CDATA_UTAG
#define CFFI_CDATA_UTAG 70
#endif
#ifndef CFFI_CLIB_UTAG
#define CFFI_CLIB_UTAG 71
#endif

static constexpr int CDATA_UTAG = CFFI_CDATA_UTAG;
static constexpr int CLIB_UTAG = CFFI_CLIB_UTAG;

template<typename T>
static T *touserdata(lua_State *L, int index) {
    return static_cast<T *>(lua_touserdata(L, index));
}

static inline int type_error(lua_State *L, int narg, char const *tname) {
    lua_pushfstring(
        L, "%s expected, got %s", tname, lua_typename(L, lua_type(L, narg))
    );
    luaL_argcheck(L, false, narg, lua_tostring(L, -1));
    return 0;
}

static inline void mark_cdata(lua_State *L) {
    luaL_setmetatable(L, CFFI_CDATA_MT);
}

static inline void mark_lib(lua_State *L) {
    luaL_setmetatable(L, CFFI_LIB_MT);
}

/* Luau (like Lua 5.1/5.2) does not expose its maximum userdata alignment, so
 * use the conservative classic union.
 */
union user_align_t { void *p; double d; long l; };

} /* namespace lua */

#define LUA_BUG_MSG(L, msg) \
    lua_pushfstring(L, "%s:%s: bug: %s", __FILE__, __LINE__, msg)

#endif /* LUA_HH */
