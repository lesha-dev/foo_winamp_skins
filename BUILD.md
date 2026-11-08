# Building foo_winamp_skins

## Layout

- `src/` — component sources
- `3rdParty/` — all dependencies (not in git; copy manually):
  - `foobar2000_sdk/` — foobar2000 SDK 2026-10-01 (unmodified)
  - `miniz/` — miniz 11.0.2 amalgamated `miniz.c` / `miniz.h` + `LICENSE` (compiled into the component)
- `msbuild/`, `Directory.Build.props` — forces toolset v145 (v143 under VS 2022) on all projects, SDK included
- `unix/` — Linux cross-build (CMake + clang-cl + xwin) for compile checks, plus its package/deploy scripts
- `scripts/` — Windows packaging

## Versions used

| Dependency / tool | Version | Source |
|---|---|---|
| foobar2000 SDK | 2026-10-01 (`FOOBAR2000_SDK_VERSION 20261001`) | https://www.foobar2000.org/SDK |
| miniz | 11.0.2 (amalgamated release) | https://github.com/richgel999/miniz/releases |
| foobar2000 (test target) | 2.26.2, portable x64 + x86 | https://www.foobar2000.org/download |
| Visual Studio | 2026 (18.x), toolset v145 | |
| clang-cl / lld-link | 21.1.8 (Ubuntu 26.04 packages) | apt `clang lld llvm` |
| CMake / Ninja | 4.2.3 / 1.13.2 | apt |
| xwin | 0.10.0 | https://github.com/Jake-Shadle/xwin |
| MSVC CRT + ATL (xwin, manifest 18) | 14.51.36244 | |
| Windows SDK (xwin) | 10.0.28000 | |

## Build (Windows)

VS 2026, workload *Desktop development with C++* + *C++ ATL for v145*.
Open `foo_winamp_skins.sln`, or run `scripts\release.bat` to rebuild Release x86 + x64 and pack `out\foo_winamp_skins-<ver>.fb2k-component`.

`scripts\redeploy.bat [x64|x86]` closes the portable foobar2000, copies the newest build (WSL or VS) into it and restarts it.
Post-build copies the DLL into `..\foobar2000_x64` / `..\foobar2000_x32` portable installs if present;
F5 launches that foobar2000.

## Build (Linux, compile check)

Requires clang-cl, lld-link, llvm-rc/lib/mt, cmake, ninja and an xwin sysroot in `~/.xwin`
(`xwin --accept-license --arch x86,x86_64 --include-atl --manifest-version 18 splat --output ~/.xwin`).

```sh
cd unix
cmake -G Ninja -B build-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_ARCH=x86_64
cmake -G Ninja -B build-x86 -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_ARCH=x86
ninja -C build-x64 && ninja -C build-x86
./deploy.sh    # -> ../foobar2000_x64, ../foobar2000_x32
```

Source lists come from `foo_winamp_skins.vcxproj`; add files there only.
Windows headers included with non-lowercase names need an entry in `unix/cmake/case_shim.txt`.
