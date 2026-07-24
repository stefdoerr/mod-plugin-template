# MOD Plugin Template

A starting point for building audio plugins with
[DPF](https://github.com/DISTRHO/DPF) and shipping them **everywhere from one
source**: [MOD Desktop](https://mod.audio/desktop/) (Linux x86-64),
[MOD Dwarf](https://mod.audio/dwarf/) (aarch64 hardware), Raspberry Pi
(via Patchstorage), and desktop DAWs on Linux / Windows / macOS as **LV2, VST3,
and CLAP**.

Includes a working passthrough+gain example, a MOD pedalboard GUI (modgui), a
self-contained cross-build pipeline for the Dwarf + Patchstorage, and a release
flow that publishes prebuilt bundles as GitHub release assets — the desktop
VST3/CLAP binaries for all three OSes are built by GitHub Actions and attached to
the same release.

**[Quick start](#quick-start) · [Renaming](#renaming) · [Build & release reference](DEVELOPERS.md)**

## Quick start

```bash
git clone --recurse-submodules https://github.com/YOU/yourplugin.git
cd yourplugin
make                              # build bin/myplugin.{lv2,vst3,clap}
./install.sh                      # install the LV2 into MOD Desktop's plugin dir
```

Restart MOD Desktop and the plugin appears under brand **"myplugin"** as
**"My Plugin"**. To try it in a desktop DAW, copy the `.vst3` / `.clap` from
`bin/` into `~/.vst3` / `~/.clap`.

## Renaming

Change the four variables at the top of the [`Makefile`](Makefile):

```makefile
PLUGIN          := myplugin
BRAND           := myplugin
LABEL           := My Plugin
PLUGIN_URI_BASE := http://myplugin.local/plugins
```

Then rename:
- `plugins/MyPlugin/` → `plugins/YourPlugin/`
- `MyPluginPlugin.cpp` → `YourPluginPlugin.cpp` (update `FILES_DSP` in
  the inner Makefile, the class name, and the `Makefile -C` path)
- `modgui/icon-myplugin.html`, `stylesheet-myplugin.css`,
  `script-myplugin.js`, `knobs/myplugin-knob.png`
- All `myplugin` strings in `modgui.ttl` and `DistrhoPluginInfo.h`
- All `MYPLUGIN_BETA` / `MyPlugin` / etc. macro references

It's mechanical — `grep -ri myplugin` will show every spot. Reading
[`INSTRUCTIONS.md`](INSTRUCTIONS.md) first will save you time if you delegate the
rename to an LLM.

## Building, publishing & layout

The full build-target reference, cross-compiling for the Dwarf, publishing to
Patchstorage, the project layout, and how local builds and CI split the release all
live in **[DEVELOPERS.md](DEVELOPERS.md)**. For LLM-assisted work, point your agent at
[`INSTRUCTIONS.md`](INSTRUCTIONS.md).

## Acknowledgements

- [DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF) — the
  cross-platform LV2/VST/CLAP framework powering the plugin
- [MOD Audio](https://mod.audio) — for the Dwarf, MOD Desktop,
  mod-plugin-builder, and the modgui design

## License

This template is released under [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)
— effectively public domain, no attribution required. Fork it, rename
it, use it however you want.

DPF is ISC-licensed (see [`dpf/LICENSE`](dpf/LICENSE)).
