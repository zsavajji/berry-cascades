# ChromeBerry

(Formerly Berry Cascades.) A web browser for BlackBerry 10 with a native Cascades interface in the style
of the stock browser, on top of the Chromium engine that
[chromium-for-bb10](https://github.com/sw7ft/chromium-for-bb10) ports to
QNX (Berry Browser's `content_shell`, build 106: Chromium 120).

Berry Browser draws its own toolbar with Skia inside the engine. ChromeBerry
runs the same engine with that toolbar off and puts Cascades around it
instead, laid out like the stock BlackBerry 10 browser: the page fills the
screen, and one dark bar along the bottom holds Back, the address (with
a padlock on https and Stop/Reload) and the ⋮ menu, with a thin blue line
above it while a page loads. Scrolling down slides it away and leaves a
slim bar with the address; scrolling up or a tap on the slim bar brings it
back. A swipe down from the top edge shows a strip with the page's favicon
and title. The engine's window cannot resize, so it reaches down to the slim
bar and Cascades clips it where the bars lie over it.

- **Address** (bottom bar): shows the address without its scheme; tap it to
  type an address or a search, and Go loads it. The first tap selects the
  whole address and the first key replaces it; a second tap places the
  cursor to edit it. While you type, it suggests a search with your search
  engine, then matching bookmarks and history.
- **Search**: DuckDuckGo by default; also DuckDuckGo Lite (the html/ page,
  which works with JavaScript off), Google, Bing and Wikipedia.
- **⋮ menu**, the system's action menu: Forward, Home, Add to
  bookmarks, Bookmarks, History, Find in page, Select text, Paste, Share,
  Copy link, Downloads, Settings, Restart engine.
- **History**: every page visited, newest first, searchable; hold to delete
  one, or clear it all.
- **Long press** on a link or an image: the system's action menu with what
  applies (Open, Copy link, Share link, Copy link text; Open image, Save
  image, Copy image address). In both menus each row shows a key that picks
  it while the menu is open.
- **Text selection**: hold on text to select a word, then drag the handles.
  The bottom bar then copies it, searches for it, shares it, selects all, or
  ends the selection. A tap on the page ends it too. The page script draws
  the selection itself: Chromium's own touch selection crashes this engine
  (as it does Berry Browser), so the script blocks it.
- **Active Frame**: minimized, the browser shows the page's favicon, title
  and address.
- **Ad and tracker blocking**: the engine's own short list (its switch is
  shared with Berry Browser) and the domains of EasyList and EasyPrivacy,
  about 100,000, taken when the app is built (`tools/fetch-blocklist.py`).
  content_shell takes no proxy settings (no `--proxy-pac-url`), so the
  engine holds each request (DevTools `Fetch`) until the app has checked its
  host and failed it or let it go. Only rules for whole domains apply, and a
  page itself is never blocked, only what it loads.
- **Find in page**, with the number of matches and previous/next.
- **Paste** into the page's focused field (a button appears in the bar
  while one is focused).
- **Keyboard**: a text box in the page types as the phone's own text fields
  do, because its keys go through one: an unseen Cascades field takes the
  keyboard while the box has the focus, passes what it types to the page
  (`Input.insertText`) and empties itself. So tapped Alt and Shift with the
  system's indicator, holding them to lock, the symbol key's picker, a held
  key's accents and, on touch-only phones, the on-screen keyboard all work.
  Outside text boxes, keys go to the page as key events (Space scrolls).
- **Trackpad and keyboard swipes** (Q20, Passport) scroll the page, as a
  mouse wheel the engine scrolls off its main thread, and the page glides on
  for a moment after the finger lifts.
- **Downloads**: pages' downloads and saved images go to the shared
  Downloads folder; the Downloads sheet shows progress and opens files.
  Saved images are loaded by the engine (`Network.loadNetworkResource`):
  Qt on BB10 speaks only TLS 1.0, which today's sites refuse.
- **Messages** ("Download complete: …", "Copied") slide in at the top in the
  title strip for a moment: a system toast would be under the page's window.
- **Mobile view** on by default: the engine is a desktop content_shell that
  only sent a mobile user agent, so pages ignored their mobile viewport and
  saw a mouse. DevTools' mobile device mode (`Emulation.setDeviceMetricsOverride`
  with `mobile: true`, plus touch emulation) makes it lay pages out as
  mobile Chrome does. Off with *Request desktop sites*, or in Settings.
  Its client hints said a desktop "content_shell" too, so sites that read
  them (facebook.com) served desktop pages: unless *Request desktop sites*
  is on, they are mobile Chrome's on Android (`Emulation.setUserAgentOverride`).
- **Swipe navigation** off by default: a sideways swipe at the edge of a
  page does not go back or forward. Settings can turn it on. It is
  Chromium's overscroll history navigation, which has no switch in this
  engine build, so the page script sets `overscroll-behavior-x: none` on the
  page's root, which Chromium honours; Browser re-applies it after every
  load so it holds with JavaScript off too.
- **Settings**: search engine, home page, suggestions, swipe navigation,
  keyboard or trackpad scrolling and its speed (shown on a Passport or a
  Classic), desktop sites,
  JavaScript, images, the engine's ad and tracker blocker (its switch file,
  `misc/berry-adblock.disable`, is shared with Berry Browser), dark pages,
  rendering resolution (sharpness only: pages keep their size), frame rate,
  JavaScript memory (256 MB to 1 GB),
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
does about it (the image-decode budget,
CPU-aware raster threads and frame cap, lighter WebAssembly), and what is
left for an engine build (ThinLTO, PGO).

## How it works

```
 ChromeBerry (Cascades, Qt 4.8)             content_shell.exe (Chromium)
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
  bar. It also leaves the keyboard and the Navigator to the Cascades
  process: the window takes touches but never the keyboard
  (`SCREEN_SENSITIVITY_NO_FOCUS`), which Screen otherwise gives to the
  window last touched.
- `src/devtools.cpp` speaks the DevTools protocol over a small WebSocket
  client (Qt 4 has none): `Page.navigate`, `Page.getNavigationHistory`,
  `Page.navigateToHistoryEntry`, `Page.reload`, `Page.stopLoading`, and
  `Input.insertText`, `Input.dispatchKeyEvent` and `Input.dispatchMouseEvent`
  for the keyboard and the trackpad. Page events keep the address bar, title
  and Back/Forward up to date.
- `assets/page.js` runs in every page. It reports touches (so the address
  bar gives up the keyboard), long presses, text selection and focused
  fields through a DevTools binding, and draws the selection handles. It also
  keeps `target=_blank` links and `window.open()` in the one window, because
  the engine's extra windows have no UI here. Page event listeners only run
  with JavaScript on, so long-press menus, selection and find need it.
- `src/downloads.cpp` sets the engine's download folder through the browser
  target (`Browser.setDownloadBehavior`), and writes saved images, which the
  engine loads for it.
- `src/history.cpp` keeps the visited pages in `history.json`.
- The engine's window is drawn above the Cascades window (Cascades shows
  through only where `Browser::setPageClip` cuts it, under the bars), so it
  is hidden while the menu, a sheet, a dialog or the suggestions are
  showing, and while the address is edited (the text magnifier is drawn
  above the bar). Under the menus and the address, a snapshot of the page
  stands in for it, read from the engine's window (`screen_read_window`) at
  once: `Page.captureScreenshot`, the fallback, waits for the engine's main
  thread, which stalls for seconds on heavy pages.

## The engine

This repository is the app; the engine comes from elsewhere:

- **The binary**: Berry Browser's `content_shell`, from the releases of
  [sw7ft/berry-browser](https://github.com/sw7ft/berry-browser) (build 106),
  fetched and checked by `tools/fetch-engine.sh`.
- **The port**: Chromium for QNX/BB10, sw7ft's
  [chromium-for-bb10](https://github.com/sw7ft/chromium-for-bb10). This
  repository's history began as a branch of a fork of it.

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
# -> build/chromeberry.bar
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
then sideload `build/chromeberry.bar` with Sachesi or
DDPB. It installs next to Berry Browser.

## Status

The app, the shim and the package build cleanly with the 10.3.1 SDK
(the QML imports `bb.cascades 1.4`), and the QML passes a syntax check
(Qt 6's `qmllint`, which knows QML but not Cascades' types). It runs on a
BlackBerry Q10 with OS 10.3.3: the page, taps, the bars, menus, selection,
downloads, the blocklist and the keyboard (symbol key included) are tried
there. A Q20's owner found the trackpad's scroll too fast at 1× (its
slider goes from 0.25× to 1.5×, 0.5× at first). Not tried yet: the touch
keyboard's swipes (Passport) and the on-screen keyboard of a touch-only
phone (Z30).

If the page is turned or taps land in the wrong place on another phone,
Settings, Engine window has Rotation and Tap mapping (*Source*, *Window*,
*Display*); both apply after Restart engine.

The Q10 cuts some key codes to their low byte: Shift (`0xF0E1`) comes as
`á`, Alt (`0xF0E9`) as `é`, the symbol key (`0xF0D3`) as `Ó`, Delete
(`0xF0FF`) as `ÿ`. A key whose cap is a code, or a modifier's byte with no
cap of its own, is taken for the code (`pageKey` in `assets/main.qml`, as
in [term51](https://github.com/zsavajji/term51)).

The engine log is `/accounts/1000/shared/misc/chromeberry-engine.log`
(the **misc** folder in File Manager), or `engine.log` in the app's data
folder when the app has no shared-files access. Settings → Engine log shows
its path and its last 24 KB, and can copy it. Besides the engine's output it
has the app's own lines (`[chromeberry …]`): each start with its size, port
and flags, the DevTools connection, and the exit code if the engine stops.
*Detailed engine log* adds Chromium's own logging. Past 2 MB the log keeps
its newest megabyte.

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
- **Blocklist round trip.** Each request waits for the app's answer over
  DevTools, a few milliseconds while the app is responsive. With blocking off
  in Settings, requests go straight out.
- **Size changes.** The engine cannot resize its window, so a new web view
  size (e.g. the action bar's) applies at the next engine restart.
  The keyboard covers the page instead of shrinking it; the bar rises
  above it while the address or find is typed.
- **Nothing over the page.** The engine's window has to lie above the
  Cascades window: below it (a negative Z-order, as `ForeignWindowControl`
  is meant to be used) the page shows through but Cascades gets its
  touches. So no system toast, magnifier or Cascades overlay can show over a
  live page: messages go in the title strip, and the page's snapshot stands
  in for it under the menus and the address.

## Licences

The `.bar` carries `blocklist.txt`, made from
[EasyList and EasyPrivacy](https://easylist.to) by The EasyList authors, dual
licensed under the GNU GPL v3 and CC BY-SA 3.0. It is generated when the app
is built and is not part of this repository.
