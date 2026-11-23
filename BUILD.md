# Building foo_winamp_skins

## Layout

- `src/` — component sources (include paths are relative to `src/`, e.g. `"common/config.h"`):
  - `common/` — shared code: component registration, settings, playback/EQ/balance glue, docking, hosted foobar2000 elements, resources
  - `classic/` — classic `.wsz` skins: skin loader, windows, renderer, preferences pages
- `tests/` — skin tests: `skintest.py` (driver), `pngimg.py`; see *Skin tests* below
- `unix/harness/` — skin test tools (not shipped)
  - `winamp_capture.exe <winamp_dir> <out_dir> [--wait secs] [--list file] [--modes plan.txt] [--tracks folder] [--close-running] [skin.wal ...]` runs a portable Winamp (one with `paths.ini`) per skin and saves its skin windows as PNGs plus `windows.txt` (with their transparency: each window is taken from the screen over a black and a white backdrop placed right below it, the window made topmost meanwhile; alpha and colour follow from the two; a window not wholly on a monitor is saved opaque, marked `(opaque)`); a new capture replaces the skin's old one. With `--modes`, each planned click runs a fresh Winamp, clicks the skin's own button, waits that step's settle time and saves the window in its other mode (`M<n>_*.png`, `modes.txt`). A skin that declares `skin.about.group` (marked `#about` in the `--modes` plan) also gets `about_WxH.png`: the tool opens Preferences > Skins > Modern Skins > Current skin and saves that group; for other skins (or without a plan) Preferences isn't opened. Start-up prompts (the portable-player question for a removable drive) are answered No. A running Winamp (Winamp is single-instance) is waited for until it's closed, or asked to close with `--close-running`; the Settings found at the start are put back at the end. `--tracks`: the tracks state (see *Skin tests*) in one more start, saved as `T<NN>_*.png` and `tracks.txt`. `WINAMP_CAPTURE_TIMES=1` prints each phase's time. Keep captures outside the repo (they contain skin and Winamp art), e.g. `..\skintest\winamp_captures\`.
  - `fb2k_capture.exe <fb2k_dir> <profile_template> <out_dir> [--wait secs] [--modes plan.txt] [--tracks folder] skin ...` runs a portable test foobar2000 per skin: replaces its `profile` with a copy of the template, starts it with `/winamp_skin_clock:<time> "/winamp_skin:<skin>"` (the time of Winamp's screenshot of the state that start is for, from the plan's `#clock` lines, less the settle time and a second for loading, then set exactly after the load: clocks show Winamp's time with no change left to animate; 12:00:00 without one), brings the main window to the front and saves every window of the component (with the UI elements it hosts drawn in, and its transparency as `winamp_capture`) plus `windows.txt` in `winamp_capture`'s format; other visible windows of the process (an error popup) are listed as `other`. It waits for the component's skin-loaded signal (an event set by `/winamp_skin:`), then the skin's settle time from the plan (`#settle`; `--wait` without one, 1 s by default); after a planned click, that step's settle time. `--modes`: the click plan (`M<n>_*.png`, `modes.txt`, as `winamp_capture`), entries run on in the same foobar2000 while they click only windows no earlier entry changed, else from a fresh start; for skins the plan marks `#about`: Preferences opened on Current skin (`/winamp_skin_prefs:about`) and its about area saved as `about_WxH.png`. `--tracks`: the tracks state set up by `foo_skintest` (needs it in the profile; `/skintest_tracks:<folder>`, `/skintest_pos:<ms>` from the plan's `#tracks_pos`), every window saved as `T<NN>_*.png` with `tracks.txt`: in the running foobar2000 when no mode capture changed a window there (`/winamp_skin_vis:off` and the tracks forwarded to it: no second start, about a tenth faster), else in one more start from the tracks profile (`--tracks-profile`). Tracks added to a running foobar2000 leave a skin's playlist button bar black (Juke_Box_1950's note), a UI bug the capture keeps. A running foobar2000 of that install is closed first; others are left alone.
- `3rdParty/` — all dependencies (not in git; copy manually):
  - `foobar2000_sdk/` — foobar2000 SDK 2026-10-01 (unmodified)
  - `columns_ui-sdk/` — Columns UI SDK 8.1.0 (unmodified; only `ui_extension.cpp` is compiled)
  - `miniz/` — miniz 11.0.2 amalgamated `miniz.c` / `miniz.h` + `LICENSE` (compiled into the component)
- `msbuild/`, `Directory.Build.props` — forces toolset v145 (v143 under VS 2022) on all projects, SDK included
- `unix/` — Linux cross-build (CMake + clang-cl + xwin) for compile checks, plus its package/deploy scripts
- `scripts/` — Windows packaging

## Versions used

| Dependency / tool | Version | Source |
|---|---|---|
| foobar2000 SDK | 2026-10-01 (`FOOBAR2000_SDK_VERSION 20261001`) | https://www.foobar2000.org/SDK |
| Columns UI SDK | 8.1.0 (tag `v8.1.0`) | https://github.com/reupen/columns_ui-sdk |
| miniz | 11.0.2 (amalgamated release) | https://github.com/richgel999/miniz/releases |
| foobar2000 (test target) | 2.26.2, portable x64 + x86 | https://www.foobar2000.org/download |
| Visual Studio | 2026 (18.x), toolset v145 | |
| clang-cl / lld-link | 21.1.8 (Ubuntu 26.04 packages) | apt `clang lld llvm` |
| CMake / Ninja | 4.2.3 / 1.13.2 | apt |
| xwin | 0.10.0 | https://github.com/Jake-Shadle/xwin |
| MSVC CRT + ATL (xwin, manifest 18) | 14.51.36244 | |
| Windows SDK (xwin) | 10.0.28000 | |

## Skin tests

`tests/skintest.py` (Python 3, no packages) checks the skins listed in `..\skintest\skins.txt`. Run it from WSL
(`python3 tests/skintest.py ...`) or Windows (`scripts\skintest.bat ...`); the tools it runs come from
`unix/build-x64` (build them in WSL: `cmake --build unix/build-x64`). Skin art stays outside the repo:
`..\skintest\`: the skin list with accepted results (`skins.txt`), Winamp captures (`winamp_captures\`), goldens, accepted harness reports, foobar2000 test installs, last run.

Checks ("gates") per skin:

| Gate | What must hold | Skins |
|---|---|---|
| `golden` | the harness render of every window and layout the skin declares is identical to the accepted one (outside `mask.golden.<window>` in `skins.txt`: looping animations whose frame the engine's timing moves) | modern |
| `winamp` | each Winamp capture (windows open at start, every window and layout clicks reach, the about page) is the same as the harness render (0%, masks aside); the accepted % (`winamp.<window>`) is only shown, to tell a regression from an improvement | modern |
| `batch` | the harness report (warnings, script errors, unsupported attributes) has no lines that aren't in the accepted one (`..\skintest\batch\<skin>.txt`) | modern |
| `fb2k:x64`, `fb2k:x86` | the DLL in a test foobar2000, the same windows, layouts (replaying the Winamp capture's click plan) and the about page in Preferences: each identical to the accepted capture (masks and tolerance from `skins.txt`), and differing from Winamp by no more than `fb2k.<window>` | both |

Playlist editors are left out of the harness and of every Winamp comparison (foobar2000's playlist differs from
Winamp's by design); they are checked only against their own accepted foobar2000 capture. Windows and layouts:
`modern_harness --modes` clicks every visible object in a fresh engine, two clicks deep, and records what opens
or switches (TOGGLE/SWITCH buttons, double-click actions, the skin's scripts) as a click plan, kept with the
Winamp capture as `plan.txt`; classic skins get windowshade of the main window, equalizer and playlist editor (the playlist editor's also in the tracks state: `tracks:pledit:shade`). Every window the
skin declares is open at start in both programs (Winamp: its saved `activated` windows in `studio.xnf`;
foobar2000: our saved open windows in a per-run copy of the profile template), so all of them are tested, not
just the default ones. The plan also lists looping animations (`#animated`: the 4x4 blocks their frames differ at, as rects; masked in
foobar2000 comparisons, which run in real time), embedded web pages and component buckets (`#browser`, `#bucket`: masked in Winamp
comparisons; Winamp shows the page in Internet Explorer and its plug-ins in the Thinger; a Thinger with the skin's own contents, `wndtype` other than `buck`, is compared) and whether the skin
declares an about page (`#about`: only then is it captured and
compared). `recapture --plan-only` refreshes those lines without running Winamp.

Every skin is also captured with tracks loaded (window keys `tracks:<window>`): the first four `.mp3` files of
`..\vis-test\` (`SKINTEST_TRACKS` to change), the third played for 0.3 s (as `winamp_capture` lets Winamp play until its stream info is in plus 250 ms: VU-driven parts such as 3Sat's speaker or Go_Army's headlights then have a level), paused and moved to 0:05 (classic skins at 0:03.08, the plan's `#tracks_pos`: there Winamp's whole-second seek thumb and ours land on the same pixel), the first selected, the second
focused but not selected, the fourth neither, so main windows show the track header and playlists every row
state. Winamp is driven by its IPC and clicks in the classic playlist editor (`winamp_capture --tracks`);
foobar2000 by `foo_skintest.dll` (`unix/harness/foo_skintest.cpp`, a test-only component installed into the
test profiles: `/skintest_tracks:<folder>`). Classic playlists are compared with Winamp too (modern ones only
with their accepted capture); the classic time display, which blinks while paused, is caught shown by both capture tools: watched from the pause (no settle wait for classic skins), caught as it switches to shown (its last two digit cells differ: `0` and the paused second), in Winamp its second appearance after the pause. Both programs use
the same settings: `fb2k-setup` copies Winamp's Settings.clean values (title bitmap font, playlist font and size,
time display, double size, volume) into ours, title scrolling is off in both, and the main title format is Winamp's (classic: our default, `<n>. <artist> - <title> (<length>)  ***  `; modern: `<artist> - <title>`).

A window against Winamp passes only at 0% (outside masks); any difference fails and writes its sheet to `runs/diffs`, whatever was
accepted. The accepted % is shown next to it (worse, improved or as accepted). Goldens may change when results against Winamp
improve. A skin with `status = pending` is reported but never fails a run.

| Command | |
|---|---|
| `check [skin ...]` | harness gates (seconds); `--fb2k x64\|x86\|both` adds foobar2000 (~5 s per skin and arch, idle and with tracks; takes over the desktop) |
| `fb2k [x64\|x86\|both] [skin ...]` | foobar2000 gates only |
| `show <skin> [window] [--gate g]` | zoomed crops of the last run's differences (reference above, ours below) and their positions |
| `accept <skin ...\|--all> [--gate golden,winamp,batch,fb2k] [--note text]` | makes the last run's results the expected ones |
| `add <skin> ...` | captures in Winamp (other modes too), adds as `pending`, checks (with foobar2000 x64 when set up; `--no-fb2k`: without) |
| `recapture [skin ...]` | captures in Winamp again |
| `stabilize [skin ...] [--arch x64] [--runs 3]` | captures in foobar2000 several times; what changes by itself becomes `mask.fb2k.<window>` (a few spots) or `tolerance.fb2k.<window>` (an animation) |
| `fb2k-setup [x64\|x86\|both]` | copies `..\foobar2000_x64` / `_x32` (without profile) to `..\skintest\fb2k-<arch>\install` and makes a classic and a modern profile template from `..\foobar2000_x64\profile`'s settings (no playlists, library, other components, or the modern UI's per-skin state: every skin starts as on first use), selecting Winamp Skins / Winamp Modern Skins, with Winamp's settings where both programs have them |
| `list` | the skins and their status |

Exit code 0: passed; 1: a check failed or an accepted skin has unaccepted results; 2: setup problem (missing
tool, capture or test install). Skin names starting with `-` (`-Flo-`) go after `--`.

- After a change: `check`; after UI-layer or classic changes and before a release: `check --fb2k both`.
- foobar2000 and Winamp runs need the desktop: leave it alone meanwhile. An `info ... brought to the front` line
  means another program kept the focus, so title bars may be drawn inactive (classic skins then fail on them).
- Captures and harness renders keep their transparency, and comparisons count alpha (premultiplied colour and
  alpha, each channel within 24): window shapes, holes and edges are compared too. Sheets and `show` crops show
  transparency over a gray checkerboard.
- Clocks: `winamp_capture` records when it takes each screenshot (`t=HH:MM:SS.mmm` in `windows.txt`, `modes.txt`,
  `tracks.txt`); `fb2k_capture` sets our test clock to that time for each state (`#clock` plan lines from `skintest`,
  forwarded as `/winamp_skin_clock:` just before the settle wait), and the harness starts its simulated clock
  there, so skins' clocks show the same time as in Winamp. The mouse pointer is parked in the virtual screen's
  top-left corner before every start (no tooltips or hover images in captures).
- The visualizer: the spectrum without tracks, off in the tracks state (starting a track it plays a loading animation
  that the paused state freezes at any frame). Winamp: `studio.xnf` "Spectrum analyzer mode" (modern skins) and
  `Winamp.ini` `sa` (classic); foobar2000: `/winamp_skin_vis:off` to the running one, or a second profile for the
  tracks state (`fb2k_capture --tracks-profile`) with our `sa_mode` / `vis_mode` off; the harness: `SA=` (default spectrum).
  Modern skins with a vis that keeps its own mode (`mode="1"` in the XML, a script's `setMode`) ignore a start with the
  mode off in Winamp (a click changes only the vis clicked), so where `modern_harness --modes` finds clicks on the
  visualizers that turn every shown Vis off, the plan lists them (`#vis_off<TAB>skin<TAB>steps`, steps as the click plan's):
  both capture tools start the tracks state with it on, as the start state, and click it off before adding the tracks.
  Skins without the line (no clickable vis, nothing on, or clicks that never turn it off: -Flo-, 3Sat; Max_Payne's vis
  is under a layer, which takes the clicks in Winamp too) keep the settings.
- Media Library, Video and AVS windows (Winamp's own, or a skin's frame titled for them) are not tested: Winamp
  starts with them closed (as foobar2000, so the skins' buttons for them match), and the capture tools list any shown in `windows.txt` as `skipped` without a screenshot, the harness doesn't render them.
- Every check that finds a window differing from its golden or from Winamp writes a sheet, current | reference |
  difference (red: differing pixels, blue: masked, magenta: outside one image), each labelled, to
  `..\skintest\runs\diffs\[<skin>]_<window>_<src>_<dst>.png` (src `engine`: the harness render, or `fb2k-x64`/`fb2k-x86`; dst `golden` or `winamp`); a window that doesn't differ has none (old sheets of the
  checked skins and gates are removed first).
  Modern skins' playlists, never checked against Winamp, still get their sheet against Winamp's playlist in
  `runs/diffs/playlists` (same names), for a look.
- On a failure: `show <skin>`; fix it, or `accept <skin> --gate <gate>` when the change is intended.
- A newly supported skin: `add <skin>`, review its numbers and `show` crops, `accept <skin>`. If its
  foobar2000 captures change between runs, `stabilize <skin>`, then `fb2k both <skin>` and `accept`.
- foobar2000 results depend on the profile templates (window sizes, options): after `fb2k-setup`, run
  `check --fb2k both` and `accept --all --gate fb2k`.
- Window names (keys in `skins.txt`, golden and diff file names) are the same in every gate: `main`, `eq`,
  `pledit` (classic), else the skin's container id (`thinger`, `ctsbig`; Winamp and foobar2000 windows are matched
  to it by title through the plan's `#container` lines), `<id>:<layout>` for a mode capture (`main:shade`),
  `prefs-about` for the about page in Preferences, `tracks:` before any of them for the tracks state, and
  `pledit-frame` for the harness's stand-in of a Winamp window.

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
cmake -G Ninja -B build-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_ARCH=x86_64 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
cmake -G Ninja -B build-x86 -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake -DXWIN_ARCH=x86 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
ninja -C build-x64 && ninja -C build-x86
./deploy.sh    # -> ../foobar2000_x64, ../foobar2000_x32
```

(`CMAKE_TRY_COMPILE_CONFIGURATION=Release`: xwin has no debug CRT for CMake's compiler check. The toolchain uses `/opt/xwin` when it exists, else `~/.xwin`. Several WSL distros sharing this tree: link `/opt/xwin` to each one's sysroot (`sudo ln -sfn ~/.xwin /opt/xwin`) so the build dirs hold the same paths whichever configures them; a build dir configured with another path keeps it in its cache: delete `CMakeCache.txt` and `CMakeFiles/` and configure again.)

Source lists come from `foo_winamp_skins.vcxproj`; add files there only.
Windows headers included with non-lowercase names need an entry in `unix/cmake/case_shim.txt`.
