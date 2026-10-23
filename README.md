# Berry Cascades

A web browser for BlackBerry 10 with a native Cascades interface: an address
bar and an action bar on top of the Chromium engine that
[chromium-for-bb10](https://github.com/zsavajji/chromium-for-bb10) ports to
QNX (Berry Browser's `content_shell`, build 84).

Berry Browser draws its own toolbar with Skia inside the engine. Berry
Cascades runs the same engine with that toolbar off and puts Cascades
around it instead:

- **Address bar** (top): type an address or a search; Go loads it. The
  spinner shows while a page loads.
- **Action bar**: Back, Reload/Stop, Forward; in the menu, Home, Bookmarks,
  Add bookmark, Share link, Copy link, Settings, Restart engine.
- **Bookmarks** sheet: tap to open, hold to delete.
- **Settings** sheet: search engine (DuckDuckGo, Google, Bing), home page,
  desktop sites, rendering resolution, HTTPS certificate checks, and two
  engine-window corrections (below).
- Other apps can open `http`/`https` links in it.

## How it works

```
 Berry Cascades (Cascades, Qt 4.8)          content_shell.exe (Chromium)
 ┌──────────────────────────────┐   spawn   ┌──────────────────────────┐
 │ main.qml: TextField, actions │ ────────► │ BERRY_APP_MODE=1 (no     │
 │ ForeignWindowControl ◄───────┼─ window ──│ Skia toolbar)            │
 │ Browser (C++)                │   group   │ LD_PRELOAD=              │
 │   DevTools client ───────────┼─ CDP ws ─►│   libberryembed.so       │
 └──────────────────────────────┘ 127.0.0.1 └──────────────────────────┘
```

- `src/browser.cpp` starts the engine as a child process once the web view's
  size is known, with Berry Browser's launcher flags
  (`deploy/berry-shell-bar/launcher.c` on `berry-v3`), its render size scaled
  from the view, and `--remote-debugging-port` on 127.0.0.1.
- `shim/berry_embed.c` is preloaded into the engine. The engine's
  `qnx_screen` window makes itself a top-level window with its own group; the
  shim makes it a child window that joins the Cascades window group with the
  `ForeignWindowControl`'s `windowId`, so Cascades places it under the address
  bar. It also leaves keyboard focus and the Navigator to the Cascades
  process.
- `src/devtools.cpp` speaks the DevTools protocol over a small WebSocket
  client (Qt 4 has none): `Page.navigate`, `Page.getNavigationHistory`,
  `Page.navigateToHistoryEntry`, `Page.reload`, `Page.stopLoading`, and
  `Input.dispatchKeyEvent` for the hardware keyboard. Page events keep the
  address bar, title and Back/Forward up to date.
- Touches go straight to the engine's window. A page script reports them
  through a DevTools binding so the address bar gives up the keyboard.
  The script also keeps `target=_blank` links and `window.open()` in the one
  window, because the engine's extra windows have no UI here.
- The engine's window is drawn above the Cascades window, so it is hidden
  while the menu or a sheet is open.

## Building

The build runs in a Docker image with the BlackBerry 10 Native SDK 10.3.1
(qcc, qmake, Cascades, `blackberry-nativepackager`). By default that is
[`yamsergey/bb10-ndk:0.6`](https://hub.docker.com/r/yamsergey/bb10-ndk) from
Docker Hub; set `BB10_NDK_IMAGE` to use another.

```sh
tools/build.sh
# -> build/io.github.zsavajji.berrycascades.bar
```

`tools/build.sh` first runs `tools/fetch-engine.sh`, which downloads Berry
Browser build 84 from chromium-for-bb10, checks its sha256, and unpacks the
engine into `engine/`. To use a `.bar` you already have:

```sh
tools/fetch-engine.sh path/to/BerryBrowserV3-3.0.2-build84.bar
```

[blackberry10-toolchain](https://github.com/XIAZY/blackberry10-toolchain)'s
image has the same Qt 4.8 `moc` and Cascades libraries, but the project here
is built with the SDK's own qmake spec.

`tools/make-icons.py` redraws the icons in `assets/` (needs Pillow).

## Installing

The package is unsigned, like Berry Browser's: turn on Development Mode,
then sideload `build/io.github.zsavajji.berrycascades.bar` with Sachesi or
DDPB. It installs next to Berry Browser.

## Status

The app, the shim and the package build cleanly with the 10.3.1 SDK. Its
DevTools sequence was checked against desktop Chromium: handshake, binding,
navigation, history, typing. **It has not been run on a BlackBerry yet.** These parts can
only be confirmed on a phone:

- **The child window.** Joining the Cascades group should make the engine
  appear under the address bar. If the page is turned, try Settings, Engine
  window, Rotation. Berry Browser's own window needs 90° as a top-level window.
  As a child window, 0° is the guess.
- **Taps.** The Screen headers do not say which space a child window's touch
  positions are in. By default the shim uses the buffer position
  (`SOURCE_POSITION`). If taps land in the wrong place, try Tap mapping
  *Window* or *Display*. Both settings apply after Restart engine.
- **Keys.** The hardware keyboard reaches the page through
  `Input.dispatchKeyEvent`, using Cascades' `KeyEvent.key` (sys/keycodes.h for
  special keys).

The engine writes its log to `/accounts/1000/shared/misc/berry-cascades-engine.log`
(or `engine.log` in the app's data folder without shared-files access).

### Trade-offs

- **One page.** No tabs; new-window links open in place.
- **Certificates.** Like Berry Browser, the engine skips HTTPS certificate
  verification by default (`--ignore-certificate-errors`), because it is slow
  and unreliable on QNX. Traffic is still encrypted, but a man in the middle
  would not be detected. Settings, *Verify HTTPS certificates* turns the
  checks back on.
- **DevTools port.** The engine listens on a random port in 9300–9799 on
  127.0.0.1 only. Other apps on the phone could connect to it while the
  browser runs. Berry Browser's launcher leaves it off because it marks the
  browser as automated (`navigator.webdriver`); the engine's own page script
  forces that back to false.
- **Size changes.** The engine cannot resize its window, so a new web view
  size (e.g. the action bar's) applies at the next engine restart.
  The keyboard covers the page instead of shrinking it.
