# foo_winamp_skins

foobar2000 user interface module that renders classic Winamp 2.x skins (`.wsz`).

Select it in *Preferences → Display → User interface module*.

## Layout

- `src/` — component sources
- `3rdParty/` — all dependencies (not in git; copy manually):
  - `foobar2000_sdk/` — foobar2000 SDK 2026-10-01 (unmodified)
  - `WTL/` — WTL 10.0.10320 headers
- `msbuild/`, `Directory.Build.props` — forces toolset v145 (v143 under VS 2022) on all projects, SDK included
- `unix/` — Linux cross-build (CMake + clang-cl + xwin) for compile checks, plus its package/deploy scripts
- `scripts/` — Windows packaging

## Versions used

| Dependency / tool | Version | Source |
|---|---|---|
| foobar2000 SDK | 2026-10-01 (`FOOBAR2000_SDK_VERSION 20261001`) | https://www.foobar2000.org/SDK |
| WTL | 10.0.10320 | https://sourceforge.net/projects/wtl/ |
| foobar2000 (test target) | 2.26.2, portable x64 + x86 | https://www.foobar2000.org/download |
| Visual Studio | 2026 (18.x), toolset v145 | |
| clang-cl / lld-link | 21.1.8 (Ubuntu 26.04 packages) | apt `clang lld llvm` |
| CMake / Ninja | 4.2.3 / 1.13.2 | apt |
| xwin | 0.10.0 | https://github.com/Jake-Shadle/xwin |
| MSVC CRT + ATL (xwin, manifest 18) | 14.51.36244 | |
| Windows SDK (xwin) | 10.0.28000 | |
| 7-Zip | 26.00 | apt `7zip` |

## Build (Windows)

VS 2026, workload *Desktop development with C++* + *C++ ATL for v145*.
Open `foo_winamp_skins.sln`, build Release x64 and Release x86, then `scripts\package.ps1`.

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
./package.sh   # -> out/foo_winamp_skins-<ver>.fb2k-component
./deploy.sh    # -> ../foobar2000_x64, ../foobar2000_x32
```

Source lists come from `foo_winamp_skins.vcxproj`; add files there only.
Windows headers included with non-lowercase names need an entry in `unix/cmake/case_shim.txt`.

## License

MIT, see [LICENSE](LICENSE). Third-party code keeps its own license:

| Component | License | Notes |
|---|---|---|
| foobar2000 SDK (incl. pfc) | BSD-style, see `3rdParty/foobar2000_sdk/sdk-license.txt` | linked statically |
| miniz | MIT, see `3rdParty/miniz/LICENSE` | compiled in; its notice must ship with binaries |
| Webamp | MIT, see `licenses/webamp-LICENSE.txt` | EQ presets data; its notice ships with binaries |
| WTL | MS-PL, see `3rdParty/WTL/MS-PL.txt` | not used yet |

Winamp skins are the property of their authors and are not part of this project.

## Credits

- [Webamp](https://github.com/captbaritone/webamp) by Jordan Eldredge: sprite layout cross-checks, EQ presets data.
- [miniz](https://github.com/richgel999/miniz) by Rich Geldreich: .wsz archive reading.
- [foobar2000 SDK](https://www.foobar2000.org/SDK) by Peter Pawlowski.
- Winamp (Nullsoft): its classic skin format and behaviour served as the reference; no Winamp code or assets are included.

Project page: https://github.com/lesha-dev/foo_winamp_skins
