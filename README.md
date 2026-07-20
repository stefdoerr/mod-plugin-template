# MOD Plugin Template

A starting point for building LV2 audio plugins targeting
[MOD Desktop](https://mod.audio/desktop/) (Linux x86_64) and
[MOD Dwarf](https://mod.audio/dwarf/) (aarch64 hardware) using
[DPF](https://github.com/DISTRHO/DPF).

Includes a working passthrough+gain example, a MOD pedalboard GUI
(modgui), a self-contained cross-build pipeline for the Dwarf, and a
release flow that publishes prebuilt bundles as GitHub release assets.

## Quick start

```bash
git clone --recurse-submodules https://github.com/YOU/yourplugin.git
cd yourplugin
make                              # build bin/myplugin.lv2
./install.sh                      # install into MOD Desktop's plugin dir
```

Restart MOD Desktop and the plugin appears under brand **"myplugin"** as
**"My Plugin"**.

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

## Build targets

| Command                       | What it does |
|-------------------------------|--------------|
| `make`                        | Build `bin/myplugin.lv2` (Linux x86_64) |
| `make beta`                   | Build `bin/myplugin-beta.lv2` (side-by-side variant) |
| `./install.sh`                | Copy to MOD Desktop's user-plugin dir |
| `BETA=1 ./install.sh`         | Install the beta variant |
| `make install`                | Copy to `/usr/lib/lv2/` (use `sudo`) |
| `make dwarf-image`            | One-time: build aarch64 cross-toolchain image (~30-60 min) |
| `make dwarf-build`            | Cross-compile → `build/dwarf/myplugin.lv2` (~10 s) |
| `make dwarf-deploy`           | scp the bundle to a connected Dwarf + restart services |
| `make dwarf`                  | Cross-build + deploy in one step |
| `make test`                   | Build + run the DSP regression tests in `tests/` under ASAN |
| `make manual`                 | Render `docs/manual/*.html` → PDF via headless Chrome (commit both) |
| `make patchstorage-build`     | Cross-build the three Patchstorage bundles into `build/patchstorage/<slug>/` |
| `make patchstorage-prepare`   | Assemble + inspect the upload payload under `build/ps-upload/dist/` |
| `make patchstorage PS_USER=<username>` | Build, prepare, and push to patchstorage.com (password prompted) |
| `make release version=0.0.1`  | Bump `VERSION`, build, package, tag, push, and `gh release create` with all bundles + the manual PDF attached |
| `make clean`                  | Delete `bin/`, `build/` |

The `dwarf-*` targets need Docker. `make patchstorage*` targets also need
Docker (see "Publishing to Patchstorage" below). `make release` needs the
`gh` CLI authenticated to the GitHub repo.

### Publishing to Patchstorage
This template also wires up publishing to [patchstorage.com](https://patchstorage.com)'s
LV2-plugins platform (`linux-amd64`, `rpi-aarch64`, and `patchbox-os-arm32` targets):

- `make patchstorage-build` — cross-build all three bundles
- `make patchstorage-prepare` — assemble + inspect the upload payload before publishing
- `make patchstorage PS_USER=<username>` — build, prepare, and push (password prompted
  interactively)

Fill in `patchstorage.json` at the repo root (`source_code_url` / `donate_url`)
when you fork the template. See
[`patchstorage-build/README.md`](patchstorage-build/README.md) for prerequisites and details.

## Project layout

```
.
├── plugins/MyPlugin/             — the DPF plugin (C++ DSP + modgui)
│   ├── MyPluginPlugin.cpp        — DSP code; replace with your plugin
│   ├── DistrhoPluginInfo.h       — LV2 identity (stable + beta)
│   ├── modgui/                   — MOD pedalboard GUI (HTML/CSS/JS/sprite)
│   ├── modgui.ttl                — MOD GUI declaration
│   └── Makefile                  — DPF inner build glue (BETA=1 retags here)
├── docs/manual/                  — beginner PDF manual (annotated demo; HTML source + generated PDF)
├── tests/                        — host-less DSP regression tests (annotated demo; `make test`)
├── dpf/                          — DISTRHO Plugin Framework (git submodule)
├── mod-build/                    — Self-contained Dwarf cross-build setup
│   ├── Dockerfile                — vendored MPB Dockerfile, builds aarch64 toolchain
│   ├── build-plugin.sh           — runs inside the container; native TTL + aarch64 .so
│   └── README.md                 — Dwarf cross-build walkthrough
├── patchstorage-build/           — Patchstorage cross-build + publish pipeline
│   ├── build-target.sh           — runs inside Patchstorage's toolchain image
│   ├── prepare.sh                — assembles the uploader working tree
│   ├── uploader/                 — vendored Patchstorage uploader (see PROVENANCE)
│   └── README.md                 — Patchstorage publishing walkthrough
├── patchstorage.json             — per-plugin metadata (source_code_url, donate_url)
├── Makefile                      — top-level build + install + Dwarf + release
├── install.sh                    — MOD Desktop installer
├── README.md                     — this file
└── INSTRUCTIONS.md                     — instructions for an LLM continuing this work
```

## Why local releases (not CI)?

The `make release` target builds all bundles **on your machine** and
uploads them via `gh release create`. The Dwarf cross-toolchain takes
~30–60 min to assemble from scratch and is hard to cache reliably on a
fresh GitHub Actions runner. Locally the image is already there and
`make dwarf-build` is ~10 s, so doing the publish from the developer
machine is faster and simpler than wiring up CI with image caching.

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
