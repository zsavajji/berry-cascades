# Speed on BlackBerry 10 hardware

What limits this browser, what the app now does about it, and what is left
for an engine build. Sources: Berry Browser's own measurements on a Passport
(`deploy/CACHING_PERF_NOTES.md`, `deploy/THINLTO.md`, `deploy/PGO.md` and the
commit log on `berry-v3` in sw7ft/chromium-for-bb10) and webmee's
`docs/performance.md`.

## The hardware

| Phone | SoC | CPU | GPU | RAM |
|---|---|---|---|---|
| Passport | Snapdragon 801 | 4 × Krait 400, 2.26 GHz | Adreno 330 | 3 GB |
| Classic (Q20), Z30 | Snapdragon S4 Pro | 2 × Krait, 1.7 GHz | Adreno 320 | 2 GB |
| Q10, Q5, Z10, Z3 | Snapdragon S4 Plus / 400 | 2 × Krait, 1.2–1.5 GHz | Adreno 225 / 305 | 1.5–2 GB |

All 32-bit ARMv7. Berry's finding (notes §7): on heavy sites the multi-second
stalls are single-threaded JavaScript at the Krait's ceiling. No V8 flag,
process model or GC setting changed them. What helps is running less
JavaScript, not re-downloading or re-decoding, and keeping the other cores
free for the main thread.

## What the app does (0.3.0)

| Change | Why | Source |
|---|---|---|
| **Pauses the page while the app is minimized** (`Page.setWebLifecycleState` frozen on thumbnail/invisible, active on fullscreen; Settings can turn it off) | The shim keeps the engine away from the Navigator, so Berry's own background throttling (notes §12) never fired: a minimized browser kept running scripts and animations. Frozen pages run no script, timers or frames. | notes §12; checked against desktop Chromium |
| **128 MB decoded-image budget** (`--enable-features=ImageDecodeConfigurableFeature:DecodedImageWorkingSetBudgetBytesForLowEndDevice/134217728`) | Low-end mode cuts it to 32 MB, so images are dropped and re-decoded on every scroll. | berry-v3 a1a2948 (build 107) |
| **Raster threads = CPU cores** (4 on quad-core, 2 on dual-core) | Four raster threads on a dual-core Q10 compete with the main thread. | notes §6 |
| **Frame cap by CPU**: 45 fps on quad-core, 30 on dual-core; Settings → Frame rate cap | Fewer frames on animated pages leave the main thread more time. | notes §6, launcher `QNX_MAX_FPS` |
| **Lighter WebAssembly** (Settings; `--no-wasm-tier-up --wasm-lazy-validation`) | Tier-up recompiles hot WASM on every core just as web apps start. Berry's WhatsApp app ships this way. Off by default. | notes §18a |
| **Lists load only while their sheet is open** (history, bookmarks, tabs, downloads) | They were rebuilt on every page visit and download-progress event, on the UI thread, while the page loaded. | — |
| **Snapshot of the page under the menu and dialogs** (`Page.captureScreenshot`, JPEG) | The engine window must be hidden under Cascades UI; showing a picture of it keeps the stock look and costs one small JPEG. | — |

Already on before 0.3.0 (Berry's launcher defaults): GPU rasterization,
low-end device mode, accessibility off, a 256 MB HTTP and media cache, mobile
user agent, render resolution below panel resolution, and the engine's
built-in ad/tracker blocking (on unless `BERRY_ADBLOCK=0`). Settings →
JavaScript off and Load images off are the biggest levers on a slow page; the
DuckDuckGo Lite search works without JavaScript.

## What needs an engine build (your toolchain)

The bundled engine is Berry Browser **build 106**, the newest one published
(sw7ft/berry-browser releases), so it has the first two below. Upstream's
`berry-v3` branch has reached build 109, but no binary has been released:

1. **ThinLTO (builds 95+).** 96 → 86 MB, fewer worst-case load stalls.
   Needs only GN args (`use_lld = true`, `use_thin_lto = true` on top of
   `out/qnx-arm/args.gn`) and four QNX fixes, all committed on `berry-v3`.
   Runbook: `deploy/THINLTO.md`. About 2.5 h on 24 cores.
2. **PGO (builds 97+)**, on top of ThinLTO: about 5–10% on native hot paths
   (Skia, net, compositor). Their `berry-qnx-arm.profdata` is **not in the
   repository**, so you would build the instrumented engine
   (`chrome_pgo_phase = 1`), browse with it on a phone to collect profiles,
   merge them, and build phase 2. Runbook: `deploy/PGO.md`.
3. Engine fixes after build 106 that the launcher cannot reach: the hang
   detector (b7bf89c, two days after the release). The telemetry-blocklist
   stall fix (8fd8aa0) landed the same day as the release, so it may not be
   in it; this app does not pass that blocklist, so it is not affected.

To use a newer engine here, package it as Berry Browser does and point
`tools/fetch-engine.sh` at that `.bar`. The app only depends on command-line
flags that build 84 already accepts, and on the engine's DevTools port.

Berry's engine starts its DevTools server, which this app drives it
through, only when `QNX_DEVTOOLS` is set (`shell_browser_main_parts.cc` on
`berry-v3`; `--remote-debugging-port` alone is not enough). Builds 84 and
106 both read it, and the app sets it.

Tried upstream and ruled out: multi-process (unstable), always-Sparkplug,
fewer raster threads plus lower resolution against the JS stall (within
noise), PartitionAlloc as malloc (hangs at start).
