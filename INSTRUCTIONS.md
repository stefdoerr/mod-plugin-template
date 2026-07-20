# Instructions for an LLM continuing this work

You are working in a **MOD plugin template repository**. Your job is
typically one of:

1. **Rename the template** to a fresh plugin name for the user.
2. **Implement DSP** (replace `MyPluginPlugin.cpp`'s passthrough with
   real audio processing the user describes).
3. **Add parameters / a modgui layout** that exposes the DSP knobs.
4. **Cross-build and deploy** to a MOD Dwarf the user has connected.
5. **Release** a prebuilt bundle via `make release version=...`.

Most of the build infrastructure is wired up already. Below are the
hard-won gotchas — read them before you touch the build system, the
modgui, or the Dwarf deploy. Several of these will silently produce a
wrong-looking build with no error message if you trip them.

---

## Build system gotchas

### DPF's `BUILD_CXX_FLAGS` is hard-reset
`dpf/Makefile.base.mk` (around line 347) assigns `BUILD_CXX_FLAGS = …`
unconditionally, then appends `$(CXXFLAGS)` at the end. **User-defined
`-D…` macros must go through `CXXFLAGS`, not `BUILD_CXX_FLAGS`.** The
template's inner Makefile already does this for `MYPLUGIN_BETA=1`. If
you add another conditional compile flag, follow the same pattern.

### `lv2_ttl_generator` only works on the host architecture
DPF's TTL generator `dlopen()`s the plugin `.so` to introspect ports
and emit `manifest.ttl` + `<plugin>.ttl`. It cannot introspect a
cross-compiled aarch64 `.so` from an x86_64 host. The Dwarf cross-build
pipeline (`mod-build/build-plugin.sh`) sidesteps this by doing:

1. native (x86_64) build → produces TTL + modgui bundle layout
2. stash the bundle
3. clean + cross-build the `.so` for aarch64
4. overlay the aarch64 `.so` into the stashed bundle

Don't try to merge these steps — the dlopen will fail with a cryptic
error and produce no TTL.

### Plugin unique-id must be unique
`DistrhoPluginInfo.h` declares `DISTRHO_PLUGIN_UNIQUE_ID` as a 4-char
code (`d_cconst` packs four chars into an int32_t). Hosts use it to
distinguish state between plugins. **If you have multiple plugins (or
the BETA variant), each needs a distinct code.** Conflicts manifest as
"my preset loaded into the wrong plugin" or simply two plugins one of
which is invisible.

### `make all` then `make` doesn't re-run TTL generation
DPF's TTL generator is wired to `make ttl` (called by the top-level
`make all`). If you only re-build the `.so` (e.g. `make -C plugins/...`)
the TTL stays stale and the new ports won't show. Always re-run the
top-level `make` after parameter changes.

### Renaming a parameter requires three places to update
1. Inner `MyPluginPlugin.cpp` — the `Parameter.symbol` in `initParameter()`
2. `modgui.ttl` — the `lv2:symbol` for that port
3. `modgui/icon-myplugin.html` — `mod-port-symbol="..."` on the bound
   widget

If any one is out of sync, MOD-UI will log "No such symbol: ..." and
that knob won't bind to the LV2 port.

---

## Dwarf cross-build / deploy gotchas

### Dropbear has no SFTP — use `scp -O`
The Dwarf runs Dropbear SSH. Modern OpenSSH `scp` (≥ 9.0) defaults to
the SFTP subsystem and fails with "subsystem request failed on channel
0". The `-O` flag forces legacy scp protocol. The template's
`dwarf-deploy` target already does this.

### Two independent lilv plugin caches on the Dwarf
After deploy you must restart BOTH `jack2` AND `mod-ui`. They keep
separate cached views of the plugin world:

- `jack2` hosts `mod-host` internally (appears as `mod-jackd` in logs)
  and caches the plugin world at startup. Without a restart the
  pedalboard fails with "can't get plugin" / "Error adding effect".
- `mod-ui` (the web UI) has its OWN cache for port lists and modgui
  rendering. Without a restart the UI shows the OLD port set and
  reports "No such symbol: …" errors for new ports — even though
  `jack2` picked the new bundle up correctly.

The template's `dwarf-deploy` does `systemctl restart jack2 mod-ui`.

### Plus: hard-refresh the browser
mod-ui's JavaScript front-end caches plugin metadata in the browser.
After a deploy, you need to **hard refresh** the MOD-UI page
(Ctrl-Shift-R) or the user sees the old UI even after the services
restart. The template's deploy step prints a reminder.

### The Dwarf may have two LV2 dirs in scan path
`/root/.lv2/` (per-user, persistent) and sometimes `/usr/lib/lv2/`
(system). If a stale bundle sits in the system path, it can shadow your
fresh `/root/.lv2/` deploy. Have the user SSH in and check:
```
find / -name '<plugin>.so' 2>/dev/null
```
If two paths show up, delete the stale one.

### `build/dwarf/` is the cross-build output, NOT `bin/`
The native host build goes to `bin/<plugin>.lv2/` (x86_64). The Dwarf
cross-build goes to `build/dwarf/<plugin>.lv2/` (aarch64). Don't confuse
the two when packaging release tarballs.

### Container output is owned by root
The cross-build container runs as root; the output bundle would be
unwritable from the host without chown. `build-plugin.sh` chowns
`/out` to `$HOST_UID:$HOST_GID` at the end, which the Makefile passes
in via `-e HOST_UID=$$(id -u) -e HOST_GID=$$(id -g)`. Don't remove this.

### A/B two builds side-by-side on the Dwarf (`make BETA=1 dwarf`)
The `BETA=1` flag builds a variant with a distinct URI / name / unique-id
(`<plugin>-beta`, "… (Beta)") from the *same source*, so it coexists with the
stable plugin instead of replacing it. The Dwarf cross-build honours it:
`make BETA=1 dwarf-build` produces `build/dwarf/<plugin>-beta.lv2` and
`make BETA=1 dwarf-deploy` scp's it alongside the stable bundle. Load both in
one pedalboard to compare a work-in-progress against the known-good build on
the actual hardware (CPU, sound, glitches) — invaluable when a change's effect
can only be judged on-device. Gate experimental behaviour behind the
`#ifdef <PLUGIN>_BETA` macro so stable stays untouched while beta carries the
change; any difference then isolates exactly that change. The flag flows
host→container via `-e BETA=$(BETA)`; `build-plugin.sh` renames the bundle and
passes `BETA=1` to the inner make (which sets the macro).

### Removing or renaming a port breaks saved pedalboards (mod-ui KeyError)
If a plugin is already used in a saved pedalboard and you remove/rename one of its
ports, mod-ui crashes **on boot** while reloading that board:
`KeyError: '<old_symbol>'` in `host.py` `load_pb_plugins` (it looks the saved port
symbol up in the plugin's current ports, and it's gone). The whole web UI fails to
start — the device looks bricked.

**Prevent it — the LV2 way is to never delete a control port, deprecate it.** Keep
the old `lv2:symbol` as a hidden, ignored port (`kParameterIsHidden` → `notOnGUI`),
give it a sensible default, and just don't read it in the DSP. Old pedalboards then
load (the symbol still resolves) and it stays out of the UI.

**Recover a device that's already stuck:** redeploy a build that still declares the
old symbol (e.g. the deprecate-don't-delete fix above) — it boots again, no device
surgery. Otherwise SSH in and move the offending board aside:
`grep -rl '"<old_symbol>"' ~/.pedalboards/`, then
`mv ~/.pedalboards/<name>.pedalboard /tmp/` and `systemctl restart jack2 mod-ui`.

---

## Publishing to Patchstorage

This template also wires up publishing to patchstorage.com's LV2-plugins
platform. This is a separate pipeline from the Dwarf cross-build above,
driven by `patchstorage-build/` (full details in
`patchstorage-build/README.md`).

### Three targets, three ABIs
patchstorage.com's LV2-plugins platform supports exactly three architectures.
Each is cross-built inside **Patchstorage's own prebuilt toolchain image**
(`patchstorage/lv2_builder-<platform>:latest`) rather than a toolchain this
repo maintains, using the same two-phase native-then-cross-compile pattern as
the Dwarf build:

| Target slug | Arch / ABI | glibc |
|---|---|---|
| `linux-amd64` | x86-64, SSE2 | 2.27 |
| `rpi-aarch64` | AArch64 | 2.27 |
| `patchbox-os-arm32` | **32-bit armhf + NEON hard-float** | 2.31 |

### Prerequisites
- Docker
- Python 3 with `requests`, `click`, and `rdflib`:
  `pip install requests click rdflib` (a venv works too — point the build at
  it with `make ... PYTHON=/path/to/venv/bin/python`)
- `jq`
- **No `git submodule update --init` needed for this path.** The Patchstorage
  uploader isn't a submodule here — it's vendored (copied) under
  `patchstorage-build/uploader/`.

### Screenshot is mandatory
Same underlying requirement as MOD Desktop's scanner (see "Screenshots —
REQUIRED, not optional" below): the modgui **screenshot** must be present in
the bundle, or the Patchstorage uploader refuses to publish it.

### Per-plugin metadata: `patchstorage.json`
The repo-root `patchstorage.json` carries the fields the uploader can't infer
from the plugin's `.ttl`:
```json
{
    "source_code_url": "https://github.com/<you>/<yourplugin>",
    "donate_url": null
}
```
Update these placeholders when you fork the template. Keep `source_code_url` /
`donate_url` current if the repo moves or you add a donation link — the
uploader reads this file verbatim.

### Make targets
- `make patchstorage-build` — cross-builds all three bundles into
  `build/patchstorage/<slug>/`.
- `make patchstorage-prepare` — assembles a disposable uploader tree and
  generates `patchstorage.json` + artwork + tarballs under
  `build/ps-upload/dist/` for inspection, without publishing anything.
- `make patchstorage PS_USER=<username>` — runs both of the above, then
  pushes. The uploader prompts for your Patchstorage **password
  interactively**; nothing is stored on disk or in the Makefile.

### Also attached to GitHub releases
`make release` builds and attaches all three Patchstorage bundles (alongside
the Dwarf bundle) to the GitHub release, so `make release version=x.y.z`
produces every downloadable artifact in one pass. The `linux-amd64` asset
**replaces** the old `linux-x86_64` naming.

---

## DSP patterns worth knowing

When the user describes an effect, these patterns are commonly needed.
**None of them are pre-installed** in this template's
`MyPluginPlugin.cpp` — it's a clean passthrough+gain. Add what you need.

### Precompute everything you can at load time — NEVER on the audio thread
`run()` must do as little as possible. **Anything whose result does not depend
on the live input samples should be computed once at load/setup time** — in the
constructor, `activate()`, or a `prepare(sampleRate)` helper — and then only
*read* in `run()`. This includes:
- window functions (Hann/Blackman), FFT twiddle factors, sine/wavetable LUTs;
- filter coefficients, delay-line lengths, dB→linear and frequency tables;
- anything built from `std::sin/cos/exp/pow/log` or `std::sqrt` over a range.

The audio thread has a hard per-block deadline (≈2.67 ms at 128 frames / 48 kHz
on the Dwarf). A "one-time" setup cost hidden in `run()` — even one that only
fires the *first* time a feature is used — blows that deadline and **xruns**.
The transcendental functions are the worst offenders: they're ~10–100× slower
on the Dwarf's in-order ARM core than on an x86 dev machine, so a loop that
looks instant in a host build can be a multi-millisecond spike on-device.

> Real bug from this codebase: a 7200-point Hann window was built with
> `std::cos` lazily on the first freeze (≈70 µs on x86 → est. 1–3 ms on the
> Dwarf → guaranteed xrun / audible pop). Moving it into `prepare()` dropped the
> press cost ~4×. If a value *can* be precomputed, precomputing it is not an
> optimization — it's a correctness requirement for glitch-free audio.

Anything you genuinely must compute per-block (e.g. spreading a large FFT over
several callbacks) should be **chunked** so each block's slice stays tiny — never
do the whole heavy operation in one callback.

### Oscillator / wavetable banks: fixed-point phase, not float radians
A LUT oscillator that stores phase as a **float in radians** is deceptively slow on the
Dwarf's *in-order* ARM core. The classic inner loop —
```cpp
float ph;                            // radians
int   i  = (int)(ph * lutScale);     // float -> int   ┐ FP<->GPR round-trip,
float fr = ph * lutScale - (float)i; //         int -> float ┘ just to get the frac
out = lut[i] + (lut[i+1] - lut[i]) * fr;
ph += dphase;
if (ph >= TWO_PI) ph -= TWO_PI;      // data-dependent wrap branch
```
— has two hazards an out-of-order x86 hides but an in-order core **cannot**: the
`float→int→float` round-trip (which also bounces across the FP/integer register files)
and the data-dependent **wrap branch**. It also won't vectorize — the table lookup is a
gather. So each oscillator is a long serial dependency chain run one at a time, and a
hundred of them can saturate the core even though they're trivial on a desktop.

Use a **fixed-point phase accumulator** — a `uint32_t` whose full range is one cycle:
```cpp
uint32_t ph, inc;                          // inc = freq/fs * 2^32
uint32_t idx = ph >> (32 - LUT_BITS);      // LUT index: a shift, no convert
float    fr  = (ph & FRAC_MASK) * (1.0f / FRAC_SPAN);
out = lut[idx] + (lut[idx+1] - lut[idx]) * fr;
ph += inc;                                 // wraps on overflow — no branch
```
The round-trip and the branch are gone, and the compiler will happily unroll/pipeline it.
For pitch modulation, branch *once per block* on `pitchScale == 1.0` so the common path
stays pure-integer stepping; only the modulated path needs a float multiply + convert.
Measured in this project (Boreas): ~2.2× faster in isolation, and it lifted the safe
partial count on the Dwarf from 64 to 96 at the same CPU. (Same lesson as the precompute
rule above — on a weak in-order core, *latency you can't hide* costs more than op count.)

### Equal-power dry/wet crossfade
For a MIX knob (0 = dry, 1 = wet) that keeps output power constant
across the knob:
```cpp
const float mix     = fMix;
const float dryGain = std::cos(mix * kHalfPi);   // kHalfPi = π/2
const float wetGain = std::sin(mix * kHalfPi);
outL[f] = dryGain * dry + wetGain * wet;
```
Linear `(1-mix) * dry + mix * wet` summing two full-amplitude signals
boosts ~6 dB at MIX = 0.5 for decorrelated signals.

### Output trim knob in dB
Standard `±12 dB` post-mix trim, applied as `10^(level/20)`:
```cpp
const float levelLin = std::pow(10.0f, fLevelDb * (1.0f / 20.0f));
outL[f] *= levelLin;
```

### Equal-power pan
For mapping a position `pos ∈ [-1, +1]` to L/R gains with constant power:
```cpp
const float theta = (pos + 1.0f) * 0.5f * kHalfPi;   // [0, π/2]
const float panL  = std::cos(theta);
const float panR  = std::sin(theta);
```

### Per-string / per-comb saturation
For high-Q resonators (Karplus-Strong, comb filters with feedback near
1) a tanh saturator inside the feedback loop bounds the stored energy
at ±1 regardless of feedback gain — models the physical limit of a
real string (bridge slap / friction). Linear at typical play levels:
```cpp
const float fbSample = std::tanh(fLastOut * fFeedback);   // not just fLastOut * fFeedback
```

### Noise gate (peak envelope + smoother)
```cpp
const float xAbs = std::fabs(x);
if (xAbs > fEnv) fEnv = xAbs;                              // instant attack
else             fEnv = xAbs + (fEnv - xAbs) * fReleaseCoef; // ~80 ms release
const float target = (fEnv > thresholdLin) ? 1.0f : 0.0f;
fGateOpen += (target - fGateOpen) * fSmoothCoef;           // ~8 ms transition
```
Coefficients: `releaseCoef = exp(-1 / (fs * 0.080))`, `smoothCoef =
1 - exp(-1 / (fs * 0.008))`.

If the gate can be turned off (threshold = 0), route that case through the
same smoother with `target = 1.0f` — assigning `fGateOpen = 1.0f` directly
steps the gain in one sample and clicks when the gate was closed.

### DC blocker (one-pole HPF)
```cpp
y = x - prevX + alpha * prevY;     // alpha = 0.996 → fc ≈ 30 Hz at 48k
prevX = x;
prevY = y;
```

### Parameter smoothing (zipper-noise avoidance)
Knob turns and DAW automation change parameters at block boundaries.
For audio-rate-sensitive params (gain, mix, level), interpolate from
old to new across the block:
```cpp
// Per-sample linear ramp
fGain += (fGainTarget - fGain) * fGainSmoothCoef;
// Or per-block linear interp:
const float gainStep = (fGainTarget - fGainCurrent) / frames;
```
Without this you'll get audible clicks on fast knob turns.

### Denormal protection
Feedback loops at long decay produce denormalized floats that are
~100× slower on x86. Add at the top of `activate()` if you have
high-Q feedback:
```cpp
#include <xmmintrin.h>
_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
```
Or sprinkle a tiny DC offset (`1e-25f`) into feedback paths.

---

## Footswitch / button port behaviors (latch / momentary / trigger)

How a boolean control port behaves when a user assigns it to a **hardware
footswitch** on the Dwarf is decided by its **static LV2 port properties** (read
once at load time — a plugin parameter can NOT change it at runtime; see the
mutual-exclusivity note below for why that matters). MOD offers three behaviors:

| Behavior | Port properties | Footswitch acts as |
|---|---|---|
| **Latch** (toggle) | `lv2:toggled` | press flips & holds 0↔1 |
| **Momentary** (held) | `lv2:toggled` + `mod:preferMomentaryOnByDefault` (NOT trigger) | 1 while held, 0 on release |
| **Trigger** (pulse) | `lv2:toggled` + `pprops:trigger` | 1 for one block on press, then auto-off |

Rules of thumb:
- "**Do X on every press**" (stack a layer, tap, re-trigger) → **trigger**.
- "**X only while held**" (hold-to-freeze, momentary boost) → **momentary**.
- Trigger and momentary are **mutually exclusive on one port** — a port is *either*
  pulse-capable *or* hold-capable. A plugin "mode" parameter can't switch a single
  footswitch between them (properties are static). If you need both gestures, expose
  **separate ports** (separate footswitches).

### Emitting them from DPF
- **Trigger**: set the `kParameterIsTrigger` hint — DPF emits `pprops:trigger` +
  `lv2:toggled`. (Officially supported; it includes `kParameterIsBoolean`.)
- **Latch**: `kParameterIsBoolean` → `lv2:toggled`.
- **Momentary**: DPF has **no** hint for `mod:preferMomentaryOnByDefault` (a
  MOD-specific property), so declare the port boolean and **patch the property into
  the generated TTL** after `generate-ttl.sh`. `generate-ttl.sh` rewrites the TTL
  fresh each build, so the patch never accumulates:
  ```make
  ttl: plugin dpf/utils/lv2_ttl_generator
  	@$(CURDIR)/dpf/utils/generate-ttl.sh
  	@for sym in hold clear; do \
  		sed -i "/lv2:symbol \"$$sym\" ;/a\        lv2:portProperty mod:preferMomentaryOnByDefault ;" \
  			"$(BUNDLE)/$(BUNDLE_NAME).ttl"; \
  	done
  ```
  DPF already declares the `mod:` prefix in the generated TTL, so only the property
  line is needed. This runs inside the Dwarf cross-build too (`build-plugin.sh` calls
  `make all`), so the property reaches both bundles.

### Reading them in the plugin
The host delivers 1 then 0; do rising/falling **edge detection** in `run()` (track
the previous value). For a **trigger** footswitch act ONLY on the rising edge — the
auto-off falling edge must not undo the action. For a **momentary** footswitch act on
both edges (press = engage, release = release), or measure how long it stays 1 to
tell a tap from a hold.

## modgui patterns

### Pedal dimensions
Standard MOD pedals are 640 px wide; the height depends on knob count.
This template uses a smaller 320×160 frame to keep the one-knob example
visually proportional — scale up when you add more controls. Pedals
wider than ~700 px scroll horizontally in the pedalboard view.

### Required structural elements
MOD-UI complains visually (broken pedal frame) if these are missing
from `icon-*.html`:
- `<div mod-role="drag-handle">` covering the whole pedal
- `<div mod-role="bypass-light">` for the LED
- `<div mod-role="bypass">` for the footswitch
- The audio I/O `{{#effect.ports.audio.input}}…{{/…}}` loops

### LV2 enum parameters → custom dropdowns
MOD-UI's default film-strip widget renders enums (e.g. a multi-value
selector with named values) badly. The clean pattern is a custom
`<select>` in `icon-*.html` bound via a custom `mod-role`, then in the
JS `start` handler, translate the dropdown's string value to the enum
integer and write it via `funcs.set_port_value`:
```js
const VALUES = ['option-a', 'option-b', 'option-c'];   // matches C++ enum order
$select.on('change', function() {
    funcs.set_port_value('my_enum', VALUES.indexOf(this.value));
});
```
And in the `change` event branch, do the inverse to keep the
`<select>` in sync with preset recall / automation:
```js
$select.val(VALUES[Math.round(event.value)]);
```
Keep a guard flag in `icon.data('suppress-emit', ...)` so updating the
`<select>` programmatically doesn't fire a feedback `set_port_value`.

### Custom pedal frames MUST ship their own knob sprite
The template uses MOD-UI's built-in knob sprites — pick a palette via
`modgui:knob "gold"` (or "yellow", "red", etc.) in `modgui.ttl`. **This
works ONLY while you use MOD-UI's default pedal skin** (the `mod-pedal`
classes + `modgui:model`/`panel`/`color`).

**The moment you switch to a custom pedal frame** (your own
`.<plugin>-pedal` background and layout), MOD-UI stops injecting the knob
sprite. Each knob is a "film" widget that reads its sprite from the CSS
`background-image`; with none, the widget never finishes initialising and
the symptoms are nasty and non-obvious:
- the knobs are **invisible** (only their titles show), and
- clicking one pops **"Parameter value change blocked by the active
  addressing"** — a red herring; nothing is actually addressed (the
  widget's `enabled` flag is set in the same init step that never ran).

So a custom frame **must** ship a knob sprite and reference it in CSS. A
ready-to-use sprite ships at `modgui/knobs/black.png` (horizontal
film-strip, 65 square 128 px frames). Wire it up with an explicit box
size and `background-size` whose height equals the box height (frames are
square, so MOD-UI counts them correctly):
```css
.myplugin-pedal .mod-knob-image {
    width: 64px;
    height: 64px;
    margin: 0 auto;
    background-image: url(/resources/knobs/black.png{{{ns}}});
    background-repeat: no-repeat;
    background-size: auto 64px;   /* == box height; do NOT omit */
}
```
Drop your own film-strip at `modgui/knobs/<name>.png` to replace it (the
Makefile copies the whole `knobs/` directory).

### `/resources/...{{{ns}}}` cache-busting
Static modgui resources are served by MOD-UI's `EffectResource`
handler. The `{{{ns}}}` Mustache token expands to a query string
scoping the request to your plugin's modgui directory and busting the
browser cache when the plugin version changes. Keep it on every
resource URL in the CSS.

### Screenshots — REQUIRED, not optional (omitting them crashes MOD Desktop)
`modgui.ttl` **must** declare both `modgui:screenshot` and `modgui:thumbnail`,
and the referenced PNG files **must** exist under `modgui/` (the Makefile copies
`modgui/*.png` into the bundle automatically).

If you omit them, MOD Desktop fails to start with **"Could not start MOD UI.
Process crashed."** The root cause is in MOD's plugin scanner: `mod-ui`'s
startup `get_all_plugins()` (in `lib/libmod_utils.so`, surfaced through
`modtools/utils.py`) leaves the `screenshot`/`thumbnail` `char*` fields of
`PluginGUI_Mini` **uninitialised** when a modgui doesn't declare them, then
**segfaults** dereferencing that garbage pointer while building the plugin
list. The crash takes down the whole UI, and the accompanying jackd log shows
`Jack main caught signal 15` — a red herring (jackd is just being torn down
after mod-ui dies). Verified on MOD Desktop 0.0.12.

Placeholder PNGs are fine during development (any valid PNG at the declared
paths stops the crash) — replace them with a clean capture taken from inside
MOD Desktop / MOD-UI when you publish.

**Reproduce a scanner crash without the GUI** (fast feedback loop): drive the
bundled scanner directly via ctypes —
```bash
MD=/path/to/mod-desktop
LD_LIBRARY_PATH=$MD/lib LV2_PATH=$MD/lv2:$MD/plugins python3 - <<'PY'
from ctypes import *
u = cdll.LoadLibrary("$MD/lib/libmod_utils.so")
u.get_all_plugins.restype = c_void_p
u.init(); u.get_all_plugins()          # exits 139 (SIGSEGV) if a modgui is poisoned
print("scan OK")
PY
```
A clean exit means every installed plugin's modgui is well-formed.

---

## Customising the modgui look

The template ships with MOD-UI's default skin for the frame, brand
text, bypass light, footswitch, and knob — `stylesheet-myplugin.css`
only POSITIONS those elements; MOD-UI provides their visuals based on
the `modgui:model` / `modgui:panel` / `modgui:color` / `modgui:knob`
strings in `modgui.ttl`. To go custom, override the relevant rule.

**Custom pedal frame:**
```css
.myplugin-pedal {
    border-radius: 14px;
    background:
        radial-gradient(ellipse at 20% 0%, rgba(255, 200, 120, 0.18), transparent 60%),
        linear-gradient(135deg, #3a1f12 0%, #5a3220 45%, #2c170c 100%);
    box-shadow:
        inset 0 0 0 2px rgba(255, 200, 130, 0.18),
        inset 0 0 32px rgba(0, 0, 0, 0.55),
        0 6px 14px rgba(0, 0, 0, 0.55);
    color: #f4e3c2;
    font-family: "nexa", "Lato", "Helvetica Neue", Helvetica, Arial, sans-serif;
}
```

**Custom brand / label text:**
```css
.myplugin-pedal .mod-plugin-brand h1 {
    margin: 0;
    font-size: 11px;
    letter-spacing: 0.3em;
    text-transform: uppercase;
    color: rgba(245, 217, 154, 0.7);
}
.myplugin-pedal .mod-plugin-name h1 {
    margin: 0;
    font-size: 20px;
    letter-spacing: 0.18em;
    text-transform: uppercase;
    color: #f5d99a;
    font-weight: 700;
    text-shadow: 0 1px 1px rgba(0, 0, 0, 0.6);
}
```

**Custom knob sprite** (horizontal film-strip PNG, square frames):
1. Drop the file at `modgui/knobs/<name>.png` — a ready-to-use
   `black.png` (65 frames, 128 px each) ships in the template.
2. The Makefile copies the whole `knobs/` directory by default.
3. Reference it in CSS with an explicit box size **and** `background-size`
   (height == box height). A custom pedal frame REQUIRES this or the knobs
   break — see "Custom pedal frames MUST ship their own knob sprite" above:
   ```css
   .myplugin-pedal .mod-knob-image {
       width: 64px; height: 64px; margin: 0 auto;
       background-image: url(/resources/knobs/<name>.png{{{ns}}});
       background-repeat: no-repeat;
       background-size: auto 64px;
   }
   ```
The `{{{ns}}}` Mustache token is a cache-busting query string MOD-UI
appends; keep it in every resource URL.

**Custom bypass light + footswitch** (drawn entirely in CSS):
```css
.myplugin-pedal .mod-light {
    width: 14px; height: 14px;
    border-radius: 50%;
    background: #401a09;
    border: 1px solid rgba(0, 0, 0, 0.7);
    box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.6);
}
.myplugin-pedal .mod-light.on {
    background: radial-gradient(circle at 30% 30%, #ffd55a, #d27a14 70%, #6e3a04);
    box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.3),
        0 0 8px rgba(255, 180, 60, 0.7);
}
.myplugin-pedal .mod-footswitch {
    width: 24px; height: 24px;
    border-radius: 50%;
    background: linear-gradient(180deg, #4a2a18 0%, #281407 100%);
    border: 1px solid rgba(0, 0, 0, 0.7);
    box-shadow:
        inset 0 1px 2px rgba(255, 200, 130, 0.25),
        inset 0 -2px 4px rgba(0, 0, 0, 0.6),
        0 2px 3px rgba(0, 0, 0, 0.6);
}
```

**Pedal size:** the template is 320×160. For multi-knob layouts go wider
(typical MOD pedal is 640×320 for 5-9 knobs). Wider than ~700 px scrolls
horizontally in the pedalboard view.

---

## Testing the DSP

`tests/test_gain.cpp` is a working, annotated demo. `make test` builds and
runs every `tests/test_*.cpp` under AddressSanitizer — OOB reads/writes in
delay lines and buffers become hard failures instead of latent corruption.
Write a regression test for every DSP bug you fix, and run `make test`
before claiming a fix works.

### The harness — no host required

DPF's `PluginExporter` drives the plugin directly:

```cpp
#include "src/DistrhoPluginInternal.hpp"      // via -Idpf/distrho
USE_NAMESPACE_DISTRHO
static bool requestStub(void*, uint32_t, float) { return true; }

d_nextBufferSize = 512;                        // set BEFORE constructing —
d_nextSampleRate = 48000.0;                    // the Plugin ctor asserts on them
d_nextCanRequestParameterValueChanges = true;
PluginExporter plugin(nullptr, nullptr, requestStub, nullptr);

plugin.setParameterValue(kParamIndex, value);  // plugin-side enum index!
plugin.activate();
plugin.run(inputs, outputs, frames);           // buffers you synthesize
```

Each test is a plain `main()` that prints `FAIL:` lines and returns nonzero —
no test framework. `make test` compiles it together with the plugin source
and `dpf/distrho/src/DistrhoPlugin.cpp`.

### What to assert

- **Simple DSP** (gain, panning, clamping): exact output samples.
- **Resonant / smoothed DSP** (filters, envelopes, feedback): RMS or peak
  over a window after a warm-up run, never single samples. Compare windows
  against each other (ratios) rather than magic absolute numbers.
- **Bug repros:** reconstruct the exact edge condition first and watch the
  test fail before fixing (e.g. scan the float grid for a delay length that
  rounds onto the buffer boundary, then let ASAN catch the OOB read).

### Gotchas

- Parameter indices in the harness are the **plugin-side enum values**; the
  LV2 port numbers in the TTL are offset by the audio ports. Don't mix them.
- Parameter changes take effect on the **next `run()` call** — there is no
  mid-block application in this harness.
- The `PLUGIN_VERSION_*` macros from the VERSION file aren't defined in test
  builds (tests compile the plugin source directly); `getVersion()` falls
  back to 0.0.0, which is harmless.
- Test binaries land in `build/tests/` (already gitignored). Only the
  `.cpp` sources are tracked.

---

## User manual (PDF)

Every plugin ships a beginner-facing PDF manual as a GitHub release asset.
`docs/manual/myplugin-manual.html` is a working, annotated demo — read its
HTML comments before writing one; they carry the conventions inline.

### How it's built and released

- **Source of truth:** `docs/manual/<plugin>-manual.html` — ONE self-contained
  HTML file with inline print CSS. No external stylesheets, fonts, or JS.
- **`make manual`** renders it to `docs/manual/<plugin>-manual.pdf` with
  headless Chrome (`--headless --no-pdf-header-footer --print-to-pdf=...`,
  A4). Needs `google-chrome` on the machine (override with `CHROME=...`).
- **Both files are committed.** The PDF is generated output — never hand-edit
  it; regenerate and re-commit it whenever the HTML changes. `make release`
  attaches the committed PDF to the GitHub release alongside the two bundle
  tarballs.
- **The bundle ships it too:** the `modgui` build step copies the PDF into
  the bundle as `modgui/manual.pdf`, and `modgui.ttl` declares it via
  `modgui:documentation <modgui/manual.pdf>` — mod-ui then shows a
  "documentation" button in the plugin info dialog. The in-bundle name is
  deliberately fixed (`manual.pdf`, not `<plugin>-manual.pdf`) so renaming
  the plugin can't silently break the TTL reference.

### Writing rules — the audience is a musician, not a developer

- The reader uses MOD Desktop or a Dwarf and has no interest in the code or
  the DSP. No FFTs, no port symbols, no build-from-source beyond a one-line
  pointer at the repo. One plain-language sentence may gesture at *how* it
  works; never more.
- Describe every control **by ear** at min / default / max ("very slow, bowed
  swells", not "attack 4 s"). Footswitches first (table), then one knob card
  per knob, each with a numbered badge matching a pin over the pedal
  screenshot.
- Content outline that works, scaled to the plugin's complexity: cover →
  what it does → quick start (numbered steps ending in sound) → the controls
  → recipes (named knob settings + what to listen for) → tips (the mistakes
  every first-timer makes) → installing (Desktop + Dwarf, from the release
  tarballs) → Q&A → specifications.

### Print-CSS gotchas (all annotated in the demo file too)

- `@page { size: A4; margin: 18mm 16mm }` plus `@page :first { margin: 0 }`
  gives a full-bleed cover with normal margins everywhere else.
- The cover div is 210mm × **296mm** — 1mm shy of A4 on purpose; at exactly
  297mm print rounding can spill a blank page 2.
- `section { break-before: page; }` starts each chapter on a fresh page;
  put `break-inside: avoid` on tables, cards, and notes so they never split
  across pages.
- Reference the pedal screenshot by **relative path** into
  `plugins/<Name>/modgui/` — the HTML then only renders correctly from inside
  the repo, which is fine because the PDF is the shipped artifact.

---

## Workflow when the user gives you a task

1. **Read INSTRUCTIONS.md** (this file) and **`Makefile`** before touching
   the build system or modgui. The patterns and gotchas above will
   save you reverse-engineering them.
2. **For DSP work:** edit `plugins/MyPlugin/MyPluginPlugin.cpp`. Add
   parameters via the `ParamIndex` enum and `initParameter()`. Echo
   them in `getParameterValue()` / `setParameterValue()`. Update
   `modgui.ttl` and the icon HTML with the new port symbols.
3. **For modgui work:** edit the three files in `modgui/`. Drop a PNG
   into `modgui/knobs/` and override `.mod-knob-image` in the CSS to
   replace the default MOD knob sprite.
4. **Build + test on desktop first:** `make test` runs the DSP regression
   tests (see "Testing the DSP"); then `make && ./install.sh`. Restart
   MOD Desktop. Drag the plugin onto a pedalboard.
5. **Then test on Dwarf if applicable:** `make dwarf`. Hard-refresh the
   MOD-UI browser tab. Drag the plugin onto a pedalboard.
6. **Release when stable:** `make release version=x.y.z`. Refuses on a
   dirty tree and uploads all bundles (Dwarf plus the three Patchstorage
   targets) plus the PDF manual to a fresh GitHub release. Bumps and
   commits the top-level `VERSION` file before building, so the plugin's
   LV2 version metadata (`getVersion()` → `lv2:minorVersion` /
   `lv2:microVersion` in the TTL) automatically tracks the release tag —
   never hardcode a version in the source. If the manual HTML changed
   since the last release, run `make manual` and commit the regenerated
   PDF first (see "User manual (PDF)" above).
7. **Publish to Patchstorage when ready:** `make patchstorage
   PS_USER=<username>` (see "Publishing to Patchstorage" above). Separate
   from the GitHub release — it pushes to patchstorage.com directly.

If something looks broken on the Dwarf but right on desktop, the cause
is almost always one of:
- service caches (didn't restart `jack2` + `mod-ui`)
- browser cache (didn't hard-refresh)
- stale system-path LV2 bundle shadowing the user-path one
- modgui symbol mismatch between `<plugin>.ttl` and `modgui.ttl`
