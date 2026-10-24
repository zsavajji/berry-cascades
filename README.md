# Berry Cascades

A web browser for BlackBerry 10 with a native Cascades interface in the style
of the stock browser, on top of the Chromium engine that
[chromium-for-bb10](https://github.com/zsavajji/chromium-for-bb10) ports to
QNX (Berry Browser's `content_shell`, build 106: Chromium 120).

Berry Browser draws its own toolbar with Skia inside the engine. Berry
Cascades runs the same engine with that toolbar off and puts Cascades around
it instead, laid out like the stock BlackBerry 10 browser: the page fills the
screen, and one dark bar along the bottom holds Back, the address (with
a padlock on https and Stop/Reload) and the ⋮ menu, with a thin blue line
above it while a page loads. Scrolling down slides it away and leaves a
slim bar with the address; scrolling up or a tap on the slim bar brings it
back. A swipe down from the top edge shows a strip with the page's favicon
and title. The engine's window cannot resize, so it reaches down to the slim
bar and Cascades clips it where the bars lie over it.

- **Address** (bottom bar): shows the address without its scheme; tap it to
  type an address or a search, and Go loads it. While you type, it suggests
  a search with your search engine, then matching bookmarks and history.
- **Search**: DuckDuckGo by default; also DuckDuckGo Lite (the html/ page,
  which works with JavaScript off), Google, Bing and Wikipedia.
- **⋮ menu**, sliding in from the right: Forward, Home, Add to
  bookmarks, Bookmarks, History, Find in page, Select text, Paste, Share,
  Copy link, Downloads, Settings, Restart engine.
- **History**: every page visited, newest first, searchable; hold to delete
  one, or clear it all.
- **Long press** on a link or an image: Open, Copy link,
  Share link, Copy link text; Open image, Save image, Copy image address.
- **Text selection**: hold on text to select a word, then drag the handles.
  The bottom bar then copies it, searches for it, shares it, selects all, or
  ends the selection. A tap on the page ends it too. The page script draws
  the selection itself: Chromium's own touch selection crashes this engine
  (as it does Berry Browser), so the script blocks it.
- **Active Frame**: minimized, the browser shows the page's favicon, title
  and address.
- **Find in page**, with the number of matches and previous/next.
- **Paste** into the page's focused field (a button appears in the bar
  while one is focused), and the hardware keyboard types into the page.
- **Downloads**: pages' downloads and saved images go to the shared
  Downloads folder; the Downloads sheet shows progress and opens files.
- **Mobile view** on by default: the engine is a desktop content_shell that
  only sent a mobile user agent, so pages ignored their mobile viewport and
  saw a mouse. DevTools' mobile device mode (`Emulation.setDeviceMetricsOverride`
  with `mobile: true`, plus touch emulation) makes it lay pages out as
  mobile Chrome does. Off with *Request desktop sites*, or in Settings.
- **Swipe navigation** off by default: a sideways swipe at the edge of a
  page does not go back or forward. Settings can turn it on. It is
  Chromium's overscroll history navigation, which has no switch in this
  engine build, so the page script sets `overscroll-behavior-x: none` on the
  page's root, which Chromium honours; Browser re-applies it after every
  load so it holds with JavaScript off too.
- **Settings**: search engine, home page, suggestions, swipe navigation, desktop sites,
  JavaScript, images, the engine's ad and tracker blocker (its switch file,
  `misc/berry-adblock.disable`, is shared with Berry Browser), dark pages,
  rendering resolution (sharpness only: pages keep their size), frame rate,
  JavaScript memory (256/384/512 MB),
  HTTPS certificate checks, clear history/cache/cookies, two engine-window
  corrections (below), and Performance: Berry Browser's engine defaults,
  each one a choice (GPU drawing, painting threads, low-end mode, the 256 MB
  cache, service workers, privacy-sandbox cuts, HTTP/3, HTTP/1.1 only).
- Other apps can open `http`/`https` links in it.

The features beyond navigation follow
[webmee](https://github.com/zsavajji/webmee)'s list (its Harmattan shell
around WPE): search engines, history suggestions,
long-press menus, downloads, content settings and storage.
Not carried over: HTTP authentication and saved passwords, and page width.

## Speed

See [docs/SPEED.md](docs/SPEED.md): what limits the Krait phones, what the app
does about it (pausing pages when minimized, the image-decode budget,
CPU-aware raster threads and frame cap, lighter WebAssembly), and what is
left for an engine build (ThinLTO, PGO).

## A lighter engine

See [docs/WEBKIT.md](docs/WEBKIT.md): replacing Chromium with WPE WebKit
(2.54, Skia on the GPU), what WebKit already supports on QNX, the C++
toolchain gap, and a milestone plan.

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
- `assets/page.js` runs in every page. It reports touches (so the address
  bar gives up the keyboard), long presses, text selection and focused
  fields through a DevTools binding, and draws the selection handles. It also
  keeps `target=_blank` links and `window.open()` in the one window, because
  the engine's extra windows have no UI here. Page event listeners only run
  with JavaScript on, so long-press menus, selection and find need it.
- `src/downloads.cpp` sets the engine's download folder through the browser
  target (`Browser.setDownloadBehavior`), and saves images itself with Qt,
  trusting the engine's root certificates.
- `src/history.cpp` keeps the visited pages in `history.json`.
- The engine's window is drawn above the Cascades window, so it is hidden
  while the menu, a sheet, a dialog or the suggestions are showing. Under
  the menu and dialogs, a snapshot of the page (`Page.captureScreenshot`)
  stands in for it.

## The engine

This repository is the app; the engine comes from elsewhere:

- **The binary**: Berry Browser's `content_shell`, from the releases of
  [sw7ft/berry-browser](https://github.com/sw7ft/berry-browser) (build 106),
  fetched and checked by `tools/fetch-engine.sh`.
- **The port**: Chromium for QNX/BB10, sw7ft's
  [chromium-for-bb10](https://github.com/sw7ft/chromium-for-bb10) and its fork
  [zsavajji/chromium-for-bb10](https://github.com/zsavajji/chromium-for-bb10),
  whose `berry-cascades` branch this repository's history comes from.

The app talks to the engine only through command-line flags, environment
variables (`QNX_DEVTOOLS`, `QNX_SCREEN_*`, `BERRY_*`), the DevTools protocol
and the window shim in `shim/`, so any Berry Browser build that keeps those
should work here.

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
Browser build 106 from [sw7ft/berry-browser](https://github.com/sw7ft/berry-browser)'s
releases, checks its sha256, and unpacks the engine into `engine/`. To use a `.bar` you already have:

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

The app, the shim and the package build cleanly with the 10.3.1 SDK, and the
QML passes a syntax check. Against desktop Chromium with emulated touches:
the DevTools sequence (handshake, binding, navigation, history, typing),
`page.js` (tap, long press on text, link and image, dragging a selection
handle, ending a selection, focused fields, find, select all, `window.open`)
and the download flow. **It has not been run on a BlackBerry yet.** These parts can
only be confirmed on a phone:

- **The child window.** Joining the Cascades group should make the engine
  appear above the bottom bar. If the page is turned, try Settings, Engine
  window, Rotation. Berry Browser's own window needs 90° as a top-level window.
  As a child window, 0° is the guess.
- **Taps.** The Screen headers do not say which space a child window's touch
  positions are in. By default the shim uses the buffer position
  (`SOURCE_POSITION`). If taps land in the wrong place, try Tap mapping
  *Window* or *Display*. Both settings apply after Restart engine.
- **Keys.** The hardware keyboard reaches the page through
  `Input.dispatchKeyEvent`, using Cascades' `KeyEvent.key` (sys/keycodes.h for
  special keys).

The engine log is `/accounts/1000/shared/misc/berry-cascades-engine.log`
(the **misc** folder in File Manager), or `engine.log` in the app's data
folder when the app has no shared-files access. Settings → Engine log shows
its path and its last 24 KB, and can copy it. Besides the engine's output it
has the app's own lines (`[berry-cascades …]`): each start with its size, port
and flags, the DevTools connection, and the exit code if the engine stops.
*Detailed engine log* adds Chromium's own logging.

### Trade-offs

- **One page at a time.** No tabs: new-window links open in place, and the
  next start reopens the last page.
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
