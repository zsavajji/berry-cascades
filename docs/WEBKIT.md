# A WebKit engine for ChromeBerry

Chromium (Berry Browser's `content_shell`) is a 96 MB binary that takes from a
few seconds up to minutes to start on a Krait phone. This note looks at
replacing it with WPE WebKit, the embedded WebKit port that
[webmee](https://github.com/zsavajji/webmee) already runs on the Nokia N9,
a far weaker device (1 × 1 GHz Cortex-A8, 1 GB RAM, PowerVR SGX530). It records what was checked
in WebKit's sources, which version to target, and a build plan that can run
on your own machine.

## Options

| | Verdict |
|---|---|
| **BB10's own WebKit** (Cascades `WebView`, the stock browser's engine, `AppleWebKit/537.35+`) | Dead end. 2014-era JavaScript (no ES2015+), and the root store predates today's CAs, so many HTTPS sites fail and the WebView offers no way around it. |
| **WPE WebKit, ported to QNX** | Recommended. Current engine, built for embedding, with a pluggable backend. webmee proves the dependency stack and the backend approach on 32-bit ARM. |
| **QtWebKit 5.212** | No. Needs a Qt 5 port for BB10 first, and its engine is older than 2.44. |
| Servo, Ladybird, NetSurf | No QNX support, or no JavaScript (NetSurf). |

## Which WPE version: newer than 2.44, yes

webmee is pinned to 2.44 because it is the last release that rasterises with
Cairo on the CPU, and the N9's SGX530 driver cannot compile Skia's shaders.
That does not apply here: BB10 phones have Adreno 225/305/320/330 GPUs, the
generation Chrome for Android ran Skia GPU rasterisation on. On a dual-core
Krait, drawing on the GPU is what frees the CPU for JavaScript, the same
lesson as Berry's `--enable-gpu-rasterization`.

Checked in each release's CMake files:

| | 2.44.4 | 2.48.7 | 2.50.6 | 2.52.6 | **2.54.2** (current) |
|---|---|---|---|---|---|
| Rasteriser | Cairo (CPU) | Skia | Skia | Skia | Skia |
| Compiler minimum | GCC 10.2 / Clang | GCC 11.2 / Clang | GCC 12.2 / Clang | GCC 12.2 / Clang | GCC 12.2 / Clang |
| libwpe backend API (webmee's) | yes | yes | yes | `ENABLE_WPE_LEGACY_API` | `ENABLE_WPE_LEGACY_API` |
| WPEPlatform | dev only | dev only | dev only | dev only | **on by default** |
| `OS(QNX)` in WTF/JSC | yes | yes | yes | yes | yes |

Skia picks GPU painting when it has a GL context and otherwise paints on CPU
worker threads. Environment variables tune both (`WEBKIT_SKIA_CPU_PAINTING_THREADS`,
`WEBKIT_SKIA_GPU_PAINTING_THREADS`, `WEBKIT_SKIA_MSAA_SAMPLE_COUNT`), so a
misbehaving Adreno 2xx driver can be worked around without a rebuild.

**Target 2.54 with the legacy libwpe API** (`ENABLE_WPE_LEGACY_API=ON`,
`ENABLE_WPE_PLATFORM=OFF`). The legacy API lets the backend hand the web
process an EGL native window, which on BB10 is a QNX Screen child window.
That is the zero-copy path this app already proved with Chromium (the
engine's window joins the Cascades window group). WPEPlatform's buffer
sharing is built around DMA-BUF, which QNX does not have, or shared memory,
which means a CPU copy per frame. The legacy API turned into an option in
2.52, so it may be removed later; a WPEPlatform backend is the follow-up
once the port works.

## What WebKit already knows about QNX

Upstream WebKit kept QNX support, recently for QNX 7/8 on ARM64 and x86-64:

- `wtf/PlatformOS.h` defines `OS(QNX)` from `__QNX__`; CMake recognises the
  `QCC` compiler.
- `wtf/StackBounds.cpp` reads thread stacks from `__tls()` on QNX.
- `JavaScriptCore/runtime/MachineContext.h` reads the frame pointer, PC and
  argument registers from QNX signal contexts, but **only for ARM64 and
  x86-64**. BB10 needs the ARMv7 cases (`cpu.gpr[ARM_REG_FP]`,
  `cpu.gpr[ARM_REG_PC]`, `cpu.gpr[ARM_REG_R0..]`): four small additions.
- `wtf/posix/ThreadingPOSIX.cpp` and `wtf/unix/MemoryPressureHandlerUnix.cpp`
  have QNX branches.

Gaps found:

- **The ARMv7 JIT is enabled only on Linux** (`WebKitFeatures.cmake`:
  `WTF_CPU_ARM AND WTF_OS_LINUX AND ARM_THUMB2_DETECTED`). On QNX it falls
  through to JIT off. Needs that condition widened, plus QNX's instruction
  cache flush (`msync(addr, len, MS_INVALIDATE_ICACHE)`) where the ARMv7
  assembler uses Linux's `cacheflush` syscall. BB10 permits executable
  mappings for apps: V8's JIT runs in Berry Browser.
- The **C++ toolchain**. This is the real blocker, see below.

## The toolchain

| Toolchain | C++ | Enough for WebKit 2.5x? |
|---|---|---|
| BB10 NDK 10.3.1 | GCC 4.6.3, Dinkumware C++ (`libcpp`) | no |
| [XIAZY/blackberry10-toolchain](https://github.com/XIAZY/blackberry10-toolchain) | Clang 19 driver over the SDK sysroot; C++ as `gnu++98`, no exceptions/RTTI, SDK `libcpp` | compiler yes, library no |
| Berry Browser's (`/root/qnx800`, `arm-blackberry-qnx8eabi`) | Clang 17 + GCC 9.3's libstdc++ | no: WebKit needs C++20 library features GCC 9's libstdc++ lacks (e.g. `std::span`) |

So the missing piece is **a C++20 standard library for BB10**. Two ways:

1. **LLVM libc++ (+ libc++abi), built for `armv7`/QNX against the BB10 sysroot**
   with XIAZY's driver flags (`-mcpu=cortex-a9 -mfloat-abi=softfp`,
   `-D__QNX__ -D__QNXNTO__`, link with `ld -m armnto` and
   `--dynamic-linker /usr/lib/ldqnx.so.2`). WebKit builds with
   `-fno-exceptions -fno-rtti`, which keeps the runtime small. libc++
   has no QNX configuration, so expect a small `__config_site` and shims for
   what BB10's libc lacks (locale functions most likely; `_LIBCPP_HAS_LOCALIZATION=0`
   is an option, since WTF has its own text handling). Static linking avoids
   shipping a second C++ runtime.
2. **GCC 12+ for `arm-blackberry-qnx8eabi`**, built the way Berry built 9.3.
   More work up front, and GCC's QNX support has thinned out.

Recommended: 1, on top of XIAZY's toolchain image, which already has Clang
19, compiler-rt and the armnto linker.

## Plan

Each milestone ends with something to run on the phone (over SSH, e.g. from
term51).

**M0. C++20 on BB10.** Build libc++/libc++abi for BB10 with Clang 19 (XIAZY
image). Test program: `std::span`, `<bit>`, concepts, `std::atomic`,
`std::thread`/`std::mutex`, `<chrono>`. Gives the list of libc functions BB10
lacks (link errors) and the shims needed.

**M1. JavaScriptCore alone** (`-DPORT=JSCOnly`, `-DENABLE_JIT=ON`,
`-DUSE_SYSTEM_MALLOC=ON` first). Only ICU as a dependency. Produces the
`jsc` shell, which exercises WTF on QNX, threads, signals (GC thread
suspension via `MachineContext`), and the ARMv7 JIT. Benchmark it against
Berry's V8 on the same phone before going further: this is the go/no-go point.

**M2. Dependencies.** webmee's `deps/` scripts, retargeted to QNX: zlib,
libffi, pcre2, GLib, ICU, libpng, libjpeg-turbo, libwebp, FreeType, HarfBuzz,
fontconfig (with BB10's `/usr/fonts`), expat, libxml2, SQLite, OpenSSL,
libpsl, nghttp2, libsoup 3, glib-networking, brotli, woff2, libepoxy,
libxkbcommon, libwpe. Skia is bundled with WebKit. DNS: Berry found QNX's
`getaddrinfo` unreliable and ships its own resolver; GLib's resolver may need
the same.

**M3. WPE WebKit + a QNX Screen backend.** The libwpe backend's renderer
target gives the web process an EGL window: a Screen child window joined to
the Cascades group, as `shim/berry_embed.c` does for Chromium today. Touch
from Screen goes to the window's owner (the web process), so it has to be
forwarded to the view backend in the engine process. webmee's
`backend/n9-wpe-backend.c` (X11) is the template.

**M4. Engine host and integration.** An engine process with one WebKit view
speaking webmee's line protocol (`docs/protocol.md`: `load`, `back`,
`touch`, `text`, `key`, `url`, `title`, `progress`, `nav`, `ime`, `link`, …)
over stdio. In this app, `Browser` gets an engine interface with two
implementations: the current DevTools one for Chromium and a protocol one for
WPE. The Cascades UI stays as it is.

## What to expect (estimates, not measurements)

- **Start-up:** a WPE view starts in about a second, against Chromium's
  seconds-to-a-minute on these phones.
- **Size:** webmee's whole 2.44 tree with every dependency is about 50 MB
  stripped; Skia adds some. Chromium alone is 96 MB.
- **Memory:** webmee measured a web process at 116–127 MB on heavy pages,
  well under what single-process Chromium needs.
- **JavaScript:** JSC on 32-bit ARM has its baseline and DFG JITs but not
  FTL; V8 on 32-bit ARM has TurboFan. Peak JS may be lower than Chromium's;
  webmee's finding that heavy pages are main-thread bound, not JIT bound,
  applies here too. M1 measures it before anything else is built.
- **Media:** WPE's video needs GStreamer. Leave it off at first (webmee did);
  YouTube stays with the Chromium engine until then.
