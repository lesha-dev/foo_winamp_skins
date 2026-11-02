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
- [ ] Embed a default skin in the DLL (resource) so something renders when no skin is set or loading fails.
      Needs a skin we may redistribute: Winamp's base skin is Nullsoft's copyright, so use a freely licensed one or draw our own.
- [ ] Skins folder: scan a dedicated folder (e.g. `<profile>\winamp-skins`) and list its skins in the UI for selection.
- [ ] Balance slider is visual only: fb2k core has no balance. Options: our own DSP, or hide the effect.
- [ ] EQ / PL buttons do nothing until the equalizer and playlist windows exist.
- [ ] Visualizer tuning: dB range / falloff speeds are guesses; no Winamp vis options (fire/line bars, dot oscilloscope, falloff, refresh rate).
- [ ] Main menu (File/Edit/...) is only reachable via right-click; consider Alt shortcuts / options button behaviour like Winamp.
- [ ] Skin cursors (*.cur) are ignored.
- [ ] Consider removing Winamp hotkeys (z x c v b l, arrows, Ctrl+D/A/W) entirely and relying on fb2k's keyboard shortcuts only.
- [ ] Decide on shuffle/repeat: integrate properly with fb2k playback orders (Shuffle albums/folders, Random, Repeat track; repeat is cosmetic while shuffling).
