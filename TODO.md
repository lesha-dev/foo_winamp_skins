# TODO

## Backlog
- [ ] EQ: AUTO button is a toggle only (no per-track presets, keyed by file path, applied on track start). Preset load/save is via .feq files.
- [ ] Find a way to hide Default UI Playlist View's column header
- [ ] Find a way to make Default UI Playlist View's rows as short as Winamp's (13 px)
- [ ] Double click on the visualizer: Winamp opens MilkDrop; decide what fb2k should open (e.g. a vis component / Spectrum popup). Support winamp visualizations.
- [ ] Support Winamp Modern skins (.wal: XML + MAKI scripts, freeform layouts) — essentially a separate engine.
- [ ] After installing, the license is accepted twice
- [ ] Get rid of webamp license

## Done
- [x] 1. Skin loader (.wsz/.zip/folder, BMP decode, viscolor/pledit/region parsing)
- [x] 2. Skin choice (skins folder via context menu / Preferences, persisted path)
- [x] 3. Static rendering of the main window
- [x] 4. Interaction: button press states, seek/volume/balance sliders, window drag, keys (z x c v b l, arrows, wheel)
- [x] 5. Playback wiring: play/playlist callbacks, transport, shuffle/repeat -> playback order, eject -> Open
- [x] 6. Time digits (click: elapsed/remaining, blink on pause), scrolling title, slider feedback text, kbps/kHz
- [x] 7. Visualizer (spectrum/oscilloscope, viscolor)
- [x] 8. Window: region.txt shape, double size, always on top, windowshade, minimize/close, edge snapping
- [x] ~~Embed a default skin~~ — decided: ship no skin (Winamp base skin isn't redistributable); the no-skin placeholder stays.
- [x] Skins folder `<profile>\winamp-skins`, listed in the context menu (Skins submenu).
- [x] Preferences page (Display > Winamp Skins): skin list with preview, folder actions; Options subpage (hotkeys / balance DSP / centre snap, dim titlebars).
- [x] Balance via own DSP ("Winamp Skins balance"), auto-inserted last in the chain on first off-centre use. Lags by the output buffer length.
- [x] Equalizer window (drives fb2k's built-in Equalizer: 10 Winamp bands + preamp mapped onto its 18 bands, synced both ways; built-in presets) and playlist editor (active fb2k playlist).
- [x] Playlist switching: Playlists submenu in the context menu (sets fb2k's active playlist).
- [x] Visualizer matches Winamp's classic analyser/scope (verified against recordings); Winamp vis options on the Preferences > Display > Winamp Skins > Visualization page (mode, analyzer style, thin/thick bands, peaks, scope style, refresh rate, falloffs).
- [x] Skin cursors (*.cur) are ignored.
- [x] Main window title bar shows wrong active/inactive colours when focus changes.
- [x] Dragging the song title (marquee) scrolls the text by hand, like Winamp.
- [x] Tooltips on controls (main, EQ, playlist windows), like Winamp; unimplemented features say "(doesn't work)".
- [x] Shuffle/repeat: buttons open menus of fb2k playback orders (None/Random/Shuffle tracks/albums/folders; None/Repeat playlist/track); lit when such an order is active.
- [x] Winamp logo (lower right of the main window, Winamp opens About): opens Preferences -> Display -> Winamp Skins.
- [x] Options from Winamp's "Classic Skin Options" prefs tab, for the Winamp Skins page:
  - [x] Dim window titlebars when not in focus (Options page)
  - [x] Dock skinned windows at N pixels (Options page; Shift inverts while dragging)
  - [x] Enable smooth window resizing for resizeable skinned windows (playlist, Options page)
  - [x] Display tooltips in Winamp's windows (Options page)
  - [x] Use skinned mouse cursors (see *.cur item)
  - [x] Display playlist position in the song scroller (via Options > Title formatting: main title, playlist entry, playlist shade title)
  - [x] Always show Winamp "Clutterbar" in main window (Options page)
  - [x] Use doublesize setting on equalizer as well as main window (Options page)
  - [x] Use skinned font for main window title display (+ alternative font style when 'Double size' is enabled)
  - [x] Oldschool playlist button behavior (Options page; default is Winamp's click-to-open)
- [x] Use foobar's playlist view inside Winamp's playlist (playlist menu switch "Use foobar's Playlist View", also Elements > Playlist View)
- [x] Add support for cover art (Album Art Viewer via the Elements menu)
- [x] Elements menu: any Default UI element in a Winamp generic window (gen.bmp frame, pledit.bmp fallback)
- [x] Credits: Webamp (sprite layout, EQ presets data), miniz, foobar2000 SDK; show in README / About.
- [x] Readme
- [x] Support Easter egg main window header
