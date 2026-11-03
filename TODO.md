# TODO

## Plan (v1, main window)
- [x] 1. Skin loader (.wsz/.zip/folder, BMP decode, viscolor/pledit/region parsing)
- [x] 2. Skin choice (skins folder via context menu / Preferences, persisted path)
- [x] 3. Static rendering of the main window
- [x] 4. Interaction: button press states, seek/volume/balance sliders, window drag, keys (z x c v b l, arrows, wheel)
- [x] 5. Playback wiring: play/playlist callbacks, transport, shuffle/repeat -> playback order, eject -> Open
- [x] 6. Time digits (click: elapsed/remaining, blink on pause), scrolling title, slider feedback text, kbps/kHz
- [x] 7. Visualizer (spectrum/oscilloscope, viscolor)
- [x] 8. Window: region.txt shape, double size, always on top, windowshade, minimize/close, edge snapping

## Backlog v1
- [x] ~~Embed a default skin~~ — decided: ship no skin (Winamp base skin isn't redistributable); the no-skin placeholder stays.
- [x] Skins folder `<profile>\winamp-skins`, listed in the context menu (Skins submenu).
- [x] Preferences page (Display > Winamp Skins): skin list with preview, folder actions; Options subpage (hotkeys / balance DSP / centre snap, dim titlebars).
- [x] Balance via own DSP ("Winamp Skins balance"), auto-inserted last in the chain on first off-centre use. Lags by the output buffer length.
- [x] Equalizer window (drives fb2k's built-in Equalizer: 10 Winamp bands + preamp mapped onto its 18 bands, synced both ways; built-in presets, .eqf import) and playlist editor (active fb2k playlist).
- [ ] EQ: AUTO button is a toggle only (no per-track presets); no preset saving.
- [ ] Playlist: no playlist switching in the window (shows fb2k's active playlist); no double size.
- [x] Visualizer matches Winamp's classic analyser/scope (verified against recordings); Winamp vis options on the Preferences > Display > Winamp Skins > Visualization page (mode, analyzer style, thin/thick bands, peaks, scope style, refresh rate, falloffs).
- [ ] Skin cursors (*.cur) are ignored.
- [x] Main window title bar shows wrong active/inactive colours when focus changes.
- [x] Dragging the song title (marquee) scrolls the text by hand, like Winamp.
- [ ] Tooltips on controls (main, EQ, playlist windows), like Winamp.
- [ ] Decide on shuffle/repeat: integrate properly with fb2k playback orders (Shuffle albums/folders, Random, Repeat track; repeat is cosmetic while shuffling).
- [x] Winamp logo (lower right of the main window, Winamp opens About): opens Preferences -> Display -> Winamp Skins.
- [ ] Options from Winamp's "Classic Skin Options" prefs tab, for the Winamp Skins page:
  - [x] Dim window titlebars when not in focus (Options page)
  - [x] Dock skinned windows at N pixels (Options page; Shift inverts while dragging)
  - [x] Enable smooth window resizing for resizeable skinned windows (playlist, Options page)
  - [ ] Display tooltips in Winamp's windows (see tooltips item)
  - [ ] Use skinned mouse cursors (see *.cur item)
  - [ ] Display playlist position in the song scroller
  - [x] Always show Winamp "Clutterbar" in main window (Options page)
  - [x] Use doublesize setting on equalizer as well as main window (Options page)
  - [ ] Use skinned font for main window title display (+ alternative font style when 'Double size' is enabled)
  - [ ] Oldschool playlist button behavior
- [ ] Credits: Webamp (sprite layout, EQ presets data), miniz, foobar2000 SDK; show in README / About.
- [ ] Integrate as panels into Default UI
- [ ] Use foobar's playlist view inside Winamp's playlist
- [ ] Add support for cover art

## Backlog later
- [ ] Double click on the visualizer: Winamp opens MilkDrop; decide what fb2k should open (e.g. a vis component / Spectrum popup). Support winamp visualizations.
- [ ] Support Winamp Modern skins (.wal: XML + MAKI scripts, freeform layouts) — essentially a separate engine.
