# foo_winamp_skins

<img src="https://www.foobar2000.org/foobarlogo.svg" height="50px"> **+**
<img src="https://upload.wikimedia.org/wikipedia/en/4/41/Winamp_media_player_logomark.png" height="50px">

foobar2000 user interface module that renders classic Winamp 2.x skins (`.wsz`).

Requires foobar2000 2.0 or newer (x86 or x64).

Download `foo_winamp_skins-<version>.fb2k-component` from the [releases page](https://github.com/lesha-dev/foo_winamp_skins/releases)
and open it with foobar2000 (or *Preferences → Components → Install...*), then restart foobar2000.
Select *Winamp Skins* in *Preferences → Display → User interface module*.

You can download skins from the [Winamp Skin Museum](https://skins.webamp.org).
Put them into the skins folder (*Preferences → Display → Winamp Skins*; `<profile>\winamp-skins` by default).

Now you can finally kick a llama's ass in foobar2000!

## Building

See [BUILD.md](BUILD.md).

## License

MIT, see [LICENSE](LICENSE). Third-party code keeps its own license:

| Component | License | Notes |
|---|---|---|
| foobar2000 SDK (incl. pfc) | BSD-style, see `3rdParty/foobar2000_sdk/sdk-license.txt` | linked statically |
| miniz | MIT, see `3rdParty/miniz/LICENSE` | compiled in; its notice must ship with binaries |
| Columns UI SDK | 0BSD-style, see `3rdParty/columns_ui-sdk/LICENCE` | compiled in |

Winamp and foobar2000 are trademarks of their respective owners.

foo_winamp_skins is an independent project and is not affiliated with, sponsored by or endorsed by either.

Winamp skins remain the property of their authors and are not included here.

## Credits

- [miniz](https://github.com/richgel999/miniz) by Rich Geldreich: .wsz archive reading.
- [foobar2000 SDK](https://www.foobar2000.org/SDK) by Peter Pawlowski.
- [Columns UI SDK](https://github.com/reupen/columns_ui-sdk) by Reupen Shah: hosting Columns UI panels.
- Winamp (Nullsoft): its classic skin format and behaviour served as the reference; no Winamp code or assets are included.

Project page: https://github.com/lesha-dev/foo_winamp_skins
