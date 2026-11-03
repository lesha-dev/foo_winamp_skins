# TODO

## Plan (v1, main window)
- [x] 1. Skin loader (.wsz/.zip/folder, BMP decode, viscolor/pledit/region parsing)
- [x] 2. Skin choice (context menu Load/Unload, persisted path)
- [x] 3. Static rendering of the main window
- [x] 4. Interaction: button press states, seek/volume/balance sliders, window drag, keys (z x c v b l, arrows, wheel)
- [x] 5. Playback wiring: play/playlist callbacks, transport, shuffle/repeat -> playback order, eject -> Open
- [x] 6. Time digits (click: elapsed/remaining, blink on pause), scrolling title, slider feedback text, kbps/kHz
- [x] 7. Visualizer (spectrum/oscilloscope, viscolor)
- [x] 8. Window: region.txt shape, double size, always on top, windowshade, minimize/close, edge snapping

## Backlog
- [x] ~~Embed a default skin~~ — decided: ship no skin (Winamp base skin isn't redistributable); the no-skin placeholder stays.
- [x] Skins folder `<profile>\winamp-skins`, listed in the context menu (Skins submenu).
- [x] Preferences page (Display > Winamp Skins): skin list with preview, folder actions, hotkeys / balance DSP / centre snap options.
- [x] Balance via own DSP ("Winamp Skins balance"), auto-inserted last in the chain on first off-centre use. Lags by the output buffer length.
- [x] Equalizer window (drives fb2k's built-in Equalizer: 10 Winamp bands + preamp mapped onto its 18 bands, synced both ways; built-in presets, .eqf import) and playlist editor (active fb2k playlist).
- [ ] EQ: AUTO button is a toggle only (no per-track presets); no preset saving.
- [ ] Playlist: no playlist switching in the window (shows fb2k's active playlist); no double size.
- [ ] Visualizer tuning: dB range / falloff speeds are guesses; no Winamp vis options (fire/line bars, dot oscilloscope, falloff, refresh rate).
- [ ] Main menu (File/Edit/...) is only reachable via right-click; consider Alt shortcuts / options button behaviour like Winamp.
- [ ] Skin cursors (*.cur) are ignored.
- [ ] Consider removing Winamp hotkeys (z x c v b l, arrows, Ctrl+D/A/W) entirely and relying on fb2k's keyboard shortcuts only.
- [ ] Decide on shuffle/repeat: integrate properly with fb2k playback orders (Shuffle albums/folders, Random, Repeat track; repeat is cosmetic while shuffling).
- [ ] Support Winamp Modern skins (.wal: XML + MAKI scripts, freeform layouts) — essentially a separate engine.
- [x] Winamp logo (lower right of the main window, Winamp opens About): opens Preferences -> Display -> Winamp Skins.
- [ ] Versioning: decide on a scheme and bump `src/version.h` per release.
- [ ] Credits: Webamp (sprite layout, EQ presets data), miniz, foobar2000 SDK; show in README / About.
