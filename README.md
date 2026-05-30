# cffi-luau

A `libffi`-based C FFI for [Luau](https://luau.org), ported from
[cffi-lua](https://github.com/q66/cffi-lua). It aims to be mostly compatible
with the LuaJIT FFI, but written from scratch on top of `libffi`, so it works
across many operating systems and CPU architectures.

This fork targets **Luau exclusively** and builds with **CMake**. The original
project supports reference Lua 5.1–5.5 and LuaJIT via Meson; this one drops the
multi-version machinery and adapts the C++ to Luau's C API.

## How it differs from cffi-lua

Luau is, at the C API level, close to Lua 5.1, but it diverges in ways that
matter for an FFI. The port handles these as follows:

- **No native module loader.** Luau has no `require()`/`package.loadlib` for C
  modules, so this is built as a **static library** that you link into a host
  embedding Luau. There is no loadable `.so`/`.dll` module. Call
  `luaopen_cffi(L)` yourself (see *Embedding*).
- **No `__gc` metamethods.** Luau frees userdata via per-tag C destructors
  registered with `lua_setuserdatadtor` (and `lua_newuserdatadtor` for
  state-free storage), not `__gc`. cffi-luau uses these for all internal
  cleanup. Consequences:
  - **User finalizers do not run.** `cffi.gc(cd, fn)` and a `__gc` field in a
    `cffi.metatype` are accepted but the finalizer is **never invoked** — Luau
    cannot safely run Lua code during collection. Free such resources
    explicitly instead. (The registry slot is still released, so there is no
    leak from the FFI's side; only your finalizer is skipped.)
  - Callbacks are still released explicitly via `callback:free()`, as before.
- **Numbers are doubles.** Luau's base language has no integer subtype, so
  Lua-number ↔ C-integer conversions go through `double`. Use 64-bit integer
  **cdata** when you need full width; passing a plain Lua number as a 64-bit C
  integer is limited to what a `double`/`int` can represent.
- **`extern "C"` is not used** around the Luau headers — Luau's API has C++
  linkage by default (we do not enable `LUAU_EXTERN_C`).

Everything else — the C parser, type system, struct/union/array handling,
calling conventions, callbacks, `cffi.load` of shared libraries, and the
non-`__gc` metamethods (`__index`, `__add`, `__call`, …) — works as in the
upstream project.

## Notable differences from LuaJIT

(inherited from cffi-lua)

- Equality comparisons against `nil` always result in `false`
- Equality comparisons between `cdata` and Lua values are always `false`
- Passing unions (or structs containing unions) is not supported on all platforms
- Bitfields are not supported
- Several new API extensions

Use `cffi.nullptr` instead of comparing against `nil`. See `docs/` for the
full API and semantics, and `STATUS.md` for feature status.

## Dependencies

- A C++17 compiler (Luau requires C++17)
- CMake ≥ 3.19
- A Luau source checkout (built from source via `add_subdirectory`)
- `libffi` (fetched and built automatically via `FetchContent`)

## Building

cffi-luau is a static library. Point it at your Luau checkout and configure:

```sh
cmake -S . -B build -DLUAU_SOURCE_DIR=/path/to/luau
cmake --build build
```

`libffi` is pulled in automatically with `FetchContent` and built statically.
Relevant options:

| Option | Default | Meaning |
| --- | --- | --- |
| `LUAU_SOURCE_DIR` | `../luau` | Path to the Luau source tree |
| `CFFI_BUILD_TESTS` | `ON` | Build the Luau test host and CTest cases |
| `CFFI_USE_SYSTEM_LIBFFI` | `OFF` | Use an installed libffi (pkg-config / `find_library`) instead of fetching |
| `CFFI_LIBFFI_GIT_REPOSITORY` | blade-lang/ffi | Git URL of the CMake libffi fork to fetch |
| `CFFI_LIBFFI_GIT_TAG` | `main` | Tag/branch/commit of that fork |

If you already have a CMake-buildable `libffi` or a system one, set
`-DCFFI_USE_SYSTEM_LIBFFI=ON`, or override the repository/tag to pin a fork.

## Embedding

Link the `cffi` target into your host and open the module after the standard
libraries:

```cpp
#include <lua.h>
#include <lualib.h>

extern "C" int luaopen_cffi(lua_State *L);

lua_State *L = luaL_newstate();
luaL_openlibs(L);

lua_pushcfunction(L, luaopen_cffi, "luaopen_cffi");
lua_call(L, 0, 1);          // leaves the cffi table on the stack
lua_setglobal(L, "ffi");    // now usable from Luau as `ffi`
```

In CMake:

```cmake
add_subdirectory(cffi-luau)   # provides the `cffi` target
target_link_libraries(your_host PRIVATE cffi)
```

Userdata tags `70` and `71` are used by default for cdata and library handles
(Luau requires tags for destructors). If your host already uses those tags,
override them: `-DCMAKE_CXX_FLAGS="-DCFFI_CDATA_UTAG=... -DCFFI_CLIB_UTAG=..."`.

## Testing

The suite is the upstream one (minus the Lua-5.4-specific `metatype54`), run by
a small embedded Luau host (`tests/host.cc`) instead of a standalone
interpreter, since Luau has no `require`/`os.exit`/`package` to drive it.

```sh
cmake -S . -B build -DLUAU_SOURCE_DIR=/path/to/luau
cmake --build build
ctest --test-dir build --output-on-failure
```

Each case runs as a separate process; a test that calls `skip_test()` exits
`77` and is reported as skipped. You can also run a single case directly:

```sh
build/tests/cffi_test_host tests/simple.lua build/tests/testlib.<dll|so>
```

## Credits

This is a port of [cffi-lua](https://github.com/q66/cffi-lua) by Daniel "q66"
Kolesa and contributors. See `COPYING.md` and `.mailmap` for licensing and
authorship of the original work.
