#include "browser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QProcessEnvironment>
#include <QThread>
#include <QUrl>
#include <QtCore/qmath.h>

#include <bb/cascades/Application>
#include <bb/cascades/ForeignWindowControl>
#include <bb/cascades/Image>
#include <bb/cascades/Window>
#include <bb/data/JsonDataAccess>
#include <bb/system/Clipboard>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

#include <bb/ImageData>
#include <bb/device/HardwareInfo>
#include <bb/UIToolkitSupport>
#include <bb/utility/ImageConverter>
#include <bps/virtualkeyboard.h>
#include <errno.h>
#include <screen/screen.h>

#include "downloads.h"
#include "history.h"

namespace {

const char kWindowId[] = "berryEngineWindow";
const char kSharedMisc[] = "/accounts/1000/shared/misc";
const char kBinding[] = "__berryEvent";
const int kProbeMs = 500;
// A cold start can take 30-90 s on a Passport (Berry's HARDENING.md), more on
// a dual-core phone. While the process runs, the app keeps waiting and only
// says so at these marks.
const int kSlowStartProbes = 120;      // 60 s
const int kVerySlowStartProbes = 360;  // 3 min
const int kWatchMs = 3000;
const qint64 kMaxLogBytes = 2 * 1024 * 1024;

// The glide after a trackpad or touch-keyboard swipe: its speed decays with
// this time constant, so a flick travels about speed × kGlideMs further (a
// guess, to tune on a Q20 and a Passport). A finger that rested longer than
// kStillMs before lifting leaves no glide; moves further apart than
// kSwipeGapMs belong to separate swipes.
const double kGlideMs = 250;
const qint64 kStillMs = 80;
const qint64 kSwipeGapMs = 100;
const double kMinGlide = 0.05;  // wheel pixels per ms

// Fraction of the web view's pixels the engine renders, before the window
// scales it up; Chromium on a Krait CPU needs the help. Indexed by the
// "quality" setting.
const double kRenderScales[] = {0.375, 0.5, 0.75, 1.0};

// The rendering resolution a phone starts at: 3/8 from 1080 pixels up (the
// Passport's 1440), else 1/2. It also sets how big pages look: the other
// resolutions change the device scale factor, not the page's CSS size.
int defaultQuality(int width, int height)
{
    return qMin(width, height) >= 1080 ? 0 : 1;
}

// The engine's ad and tracker blocker is on unless this file exists. The
// engine reads it itself, so the switch is shared with Berry Browser.
const char kAdblockOff[] = "/accounts/1000/shared/misc/berry-adblock.disable";

// The "search" setting: key, name, address the encoded terms are appended to.
// DuckDuckGo Lite is the html/ endpoint, which works without JavaScript.
struct SearchEngine { const char *key; const char *name; const char *url; };
const SearchEngine kSearchEngines[] = {
    {"duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q="},
    {"ddglite", "DuckDuckGo Lite", "https://html.duckduckgo.com/html/?q="},
    {"google", "Google", "https://www.google.com/search?q="},
    {"bing", "Bing", "https://www.bing.com/search?q="},
    {"wikipedia", "Wikipedia", "https://en.m.wikipedia.org/w/index.php?search="},
};

const SearchEngine &searchEngine(const QString &key)
{
    for (unsigned i = 0; i < sizeof(kSearchEngines) / sizeof(kSearchEngines[0]); ++i) {
        if (key == kSearchEngines[i].key)
            return kSearchEngines[i];
    }
    return kSearchEngines[0];
}

// Settings that are engine flags: changing one takes an engine restart.
bool isEngineSetting(const QString &key)
{
    return key == "quality" || key == "desktop" || key == "verifyCerts"
        || key == "rotation" || key == "touchMode" || key == "javascript"
        || key == "images" || key == "fps" || key == "wasmLite"
        || key == "scrollbars" || key == "detailedLog" || key == "adblock"
        || key == "darkPages" || key == "heap" || key == "gpu" || key == "lowEnd"
        || key == "diskCache" || key == "serviceWorkers" || key == "privacyCuts"
        || key == "quic" || key == "http1" || key == "rasterThreads";
}

// Emulation.setUserAgentOverride for a mobile Chrome on Android. The engine
// sends the mobile user agent, but its client hints (Sec-CH-UA-*,
// navigator.userAgentData) are content_shell's: brand "content_shell",
// platform "Unknown", not mobile, and sites that read them (facebook.com)
// serve their desktop pages. Chromium 120, the engine's version: change
// with the engine.
QVariantMap mobileAgent()
{
    QVariantList brands;
    QVariantMap brand;
    brand["brand"] = QString("Chromium");
    brand["version"] = QString("120");
    brands << brand;
    brand["brand"] = QString("Not_A Brand");
    brand["version"] = QString("8");
    brands << brand;
    QVariantMap hints;
    hints["brands"] = brands;
    hints["platform"] = QString("Android");
    hints["platformVersion"] = QString("10.0.0");
    hints["architecture"] = QString("");
    hints["model"] = QString("");
    hints["mobile"] = true;
    QVariantMap params;
    params["userAgent"] = QString("Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 "
                                  "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36");
    params["platform"] = QString("Linux armv7l");
    params["userAgentMetadata"] = hints;
    return params;
}

QString jsString(const QString &s)
{
    QString out = s;
    out.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n").replace('\r', "\\r");
    out.replace(QChar(0x2028), "\\u2028").replace(QChar(0x2029), "\\u2029");
    return '"' + out + '"';
}

}  // namespace

Browser::Browser(History *history, Downloads *downloads, QObject *parent)
    : QObject(parent)
    , m_history(history)
    , m_downloads(downloads)
    , m_settings(QDir::homePath() + "/settings.ini", QSettings::IniFormat)
    , m_engine(0)
    , m_devtools(new DevTools(this))
    , m_probeAttempts(0)
    , m_port(0)
    , m_width(0)
    , m_height(0)
    , m_stopping(false)
    , m_restartNeeded(false)
    , m_engineState("idle")
    , m_loading(false)
    , m_historyIndex(-1)
    , m_selecting(false)
    , m_editing(false)
    , m_progress(0)
    , m_snapshotId(-1)
    , m_titleHeight(0)
    , m_slimHeight(0)
    , m_fullHeight(0)
    , m_barsLift(0)
    , m_barsIn(1)
    , m_titleIn(0)
    , m_barsFrom(1)
    , m_titleFrom(0)
    , m_barsTo(1)
    , m_titleTo(0)
    , m_keyboardShown(false)
    , m_keyboardHeight(0)
    , m_scrollVX(0)
    , m_scrollVY(0)
    , m_iconCount(0)
{
    subscribe(virtualkeyboard_get_domain());
    virtualkeyboard_request_events(0);
    QFile script(QCoreApplication::applicationDirPath() + "/assets/page.js");
    if (script.open(QIODevice::ReadOnly))
        m_pageScript = QString::fromUtf8(script.readAll());

    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(400);
    connect(&m_startTimer, SIGNAL(timeout()), this, SLOT(launchIfIdle()));

    // A swipe down from the top edge shows the title strip (Cascades' own
    // application menu is off, see main.cpp).
    m_toolkit = new bb::UIToolkitSupport(this);
    connect(m_toolkit, SIGNAL(swipedDown(bb::SwipeDownMode::Type)), this, SLOT(swipeFromTop()));

    m_barsTimer.setInterval(16);
    connect(&m_barsTimer, SIGNAL(timeout()), this, SLOT(stepBars()));
    m_glideTimer.setInterval(16);
    connect(&m_glideTimer, SIGNAL(timeout()), this, SLOT(glide()));
    connect(&m_probe, SIGNAL(timeout()), this, SLOT(probeEngine()));
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(150);
    connect(&m_historyTimer, SIGNAL(timeout()), this, SLOT(refreshHistory()));
    m_progressTimer.setInterval(300);
    connect(&m_progressTimer, SIGNAL(timeout()), this, SLOT(creepProgress()));
    m_snapshotTimer.setSingleShot(true);
    m_snapshotTimer.setInterval(700);
    connect(&m_snapshotTimer, SIGNAL(timeout()), this, SLOT(snapshotTimedOut()));
    connect(this, SIGNAL(pageChanged()), this, SIGNAL(bookmarkedChanged()));
    connect(this, SIGNAL(bookmarksChanged()), this, SIGNAL(bookmarkedChanged()));

    // The engine cannot see the Navigator (the shim keeps it away), so it
    // does not know when the app is minimized; pause the page from here.
    bb::cascades::Application *app = bb::cascades::Application::instance();
    connect(app, SIGNAL(fullscreen()), this, SLOT(appVisible()));
    connect(app, SIGNAL(thumbnail()), this, SLOT(appHidden()));
    connect(app, SIGNAL(invisible()), this, SLOT(appHidden()));

    bb::system::InvokeManager *invokes = new bb::system::InvokeManager(this);
    connect(invokes, SIGNAL(invoked(const bb::system::InvokeRequest&)),
            this, SLOT(invoked(const bb::system::InvokeRequest&)));

    connect(m_devtools, SIGNAL(targetsListed(QVariantList)), this, SLOT(targetsListed(QVariantList)));
    connect(m_devtools, SIGNAL(attached()), this, SLOT(attached()));
    connect(m_devtools, SIGNAL(detached()), this, SLOT(detached()));
    connect(m_devtools, SIGNAL(replied(int, QString, QVariantMap)),
            this, SLOT(replied(int, QString, QVariantMap)));
    connect(m_devtools, SIGNAL(failed(int, QString, QString)),
            this, SLOT(failed(int, QString, QString)));
    connect(m_devtools, SIGNAL(event(QString, QVariantMap)),
            this, SLOT(protocolEvent(QString, QVariantMap)));
    connect(m_downloads, SIGNAL(fetch(QString, QString)), this, SLOT(fetchForDownload(QString, QString)));
    connect(m_downloads, SIGNAL(log(QString)), this, SLOT(downloadsLog(QString)));
}

Browser::~Browser()
{
    if (m_engine && m_engine->state() != QProcess::NotRunning) {
        m_engine->disconnect(this);
        m_engine->terminate();
        if (!m_engine->waitForFinished(2000))
            m_engine->kill();
    }
}

QString Browser::windowId() const
{
    return kWindowId;
}

// ---- engine process ------------------------------------------------------

// The web view's size in pixels. The first launch waits for the layout to
// settle; later sizes (the keyboard, the action bar) apply at the next
// restart, since the engine cannot resize its window.
void Browser::startEngine(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    m_width = width;
    m_height = height;
    if (m_engineState == "idle")
        m_startTimer.start();
}

void Browser::launchIfIdle()
{
    if (m_engineState == "idle")
        launch();
}

void Browser::restartEngine()
{
    m_restartNeeded = false;
    emit restartNeededChanged();
    if (m_pendingUrl.isEmpty() && !m_url.isEmpty())
        m_pendingUrl = m_url;
    if (m_engine && m_engine->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_devtools->close();
        m_downloads->disconnectEngine();
        m_engine->terminate();
        QTimer::singleShot(3000, m_engine, SLOT(kill()));
    } else if (m_width > 0) {
        launch();
    }
}

QString Browser::engineDir() const
{
    return QCoreApplication::applicationDirPath() + "/engine";
}

QString Browser::logPath() const
{
    QFileInfo shared(kSharedMisc);
    if (shared.isDir() && shared.isWritable())
        return QString(kSharedMisc) + "/chromeberry-engine.log";
    return QDir::homePath() + "/engine.log";
}

// The log keeps its newer half past kMaxLogBytes. The engine appends to it
// for as long as it runs, so its writes go on at the new end.
static void trimLog(const QString &path)
{
    QFile file(path);
    if (file.size() <= kMaxLogBytes || !file.open(QIODevice::ReadOnly))
        return;
    file.seek(file.size() - kMaxLogBytes / 2);
    const QByteArray tail = file.readAll();
    file.close();
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(tail);
}

// The app's own lines in the engine log, between the engine's.
void Browser::log(const QString &line) const
{
    trimLog(logPath());
    QFile file(logPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(QString("[chromeberry %1] %2\n")
                       .arg(QTime::currentTime().toString("hh:mm:ss"), line).toUtf8());
}

QString Browser::logTail() const
{
    QFile file(logPath());
    if (!file.open(QIODevice::ReadOnly))
        return tr("No log yet at %1").arg(logPath());
    const qint64 keep = 24 * 1024;
    if (file.size() > keep)
        file.seek(file.size() - keep);
    return QString::fromUtf8(file.readAll());
}

void Browser::launch()
{
    const QString dir = engineDir();
    if (!QFile::exists(dir + "/content_shell.exe")) {
        log("engine missing: " + dir + "/content_shell.exe");
        setEngineState("failed");
        emit message(tr("The browser engine is missing from this package."));
        return;
    }

    const int quality = setting("quality").toInt();
    const double scale = kRenderScales[quality];
    const int renderWidth = qMax(240, int(m_width * scale + 0.5));
    const int renderHeight = qMax(240, int(m_height * scale + 0.5));
    m_port = 9300 + (qrand() % 500);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString libraryPath = dir;
    if (!env.value("LD_LIBRARY_PATH").isEmpty())
        libraryPath += ":" + env.value("LD_LIBRARY_PATH");
    env.insert("LD_LIBRARY_PATH", libraryPath);
    env.insert("LD_PRELOAD", QCoreApplication::applicationDirPath() + "/lib/libberryembed.so");
    env.insert("QNX_CA_BUNDLE", dir + "/root_store.certs");
    env.insert("QNX_SCREEN_WIDTH", QString::number(renderWidth));
    env.insert("QNX_SCREEN_HEIGHT", QString::number(renderHeight));
    env.insert("QNX_SCREEN_OUTPUT_WIDTH", QString::number(m_width));
    env.insert("QNX_SCREEN_OUTPUT_HEIGHT", QString::number(m_height));
    env.insert("QNX_SCREEN_ROTATION", setting("rotation").toString());
    env.insert("QNX_INPUT_POLL_MS", "1");
    // Dual-core phones (Q10, Q5, Z10, Z3) get fewer raster threads and a
    // lower frame cap than the quad-core Passport, Classic and Z30.
    const int cores = qMax(1, QThread::idealThreadCount());
    int fps = setting("fps").toInt();
    if (fps <= 0)
        fps = cores >= 4 ? 45 : 30;
    env.insert("QNX_MAX_FPS", QString::number(fps));
    env.insert("QNX_GPU_PROBE", "0");
    // Berry's engine starts its DevTools server only when asked by this, not
    // by --remote-debugging-port alone (shell_browser_main_parts.cc on
    // berry-v3), and DevTools is how this app drives it.
    env.insert("QNX_DEVTOOLS", "1");
    env.insert("BERRY_APP_MODE", "1");
    env.insert("BERRY_EMBED_GROUP", bb::cascades::Application::instance()->mainWindow()->groupId());
    env.insert("BERRY_EMBED_ID", kWindowId);
    env.insert("BERRY_EMBED_ROTATION", setting("rotation").toString());
    static const char *const touchModes[] = {"source", "window", "display"};
    const int touchMode = setting("touchMode").toInt();
    env.insert("BERRY_EMBED_TOUCH", touchModes[touchMode >= 0 && touchMode < 3 ? touchMode : 0]);
    if (setting("detailedLog").toBool())
        env.insert("QNX_NAV_DEBUG", "1");

    const QString logFile = logPath();
    trimLog(logFile);

    QString startUrl = m_pendingUrl;
    if (startUrl.isEmpty())
        startUrl = m_settings.value("lastUrl").toString();
    if (startUrl.isEmpty())
        startUrl = homeUrl();
    m_pendingUrl.clear();

    if (m_engine) {
        m_engine->disconnect(this);
        m_engine->deleteLater();
    }
    m_engine = new QProcess(this);
    m_engine->setProcessEnvironment(env);
    m_engine->setWorkingDirectory(QDir::homePath());
    m_engine->setStandardOutputFile(logFile, QIODevice::Append);
    m_engine->setStandardErrorFile(logFile, QIODevice::Append);
    connect(m_engine, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(engineFinished(int, QProcess::ExitStatus)));
    connect(m_engine, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(engineError(QProcess::ProcessError)));

    m_devtools->setPort(m_port);
    QStringList args = engineArguments(startUrl);
    args.insert(args.size() - 1, QString("--remote-debugging-port=%1").arg(m_port));
    log(QString("starting engine: view %1x%2, render %3x%4, %5 cores, %6 fps, DevTools port %7, group %8")
            .arg(m_width).arg(m_height).arg(renderWidth).arg(renderHeight)
            .arg(cores).arg(fps).arg(m_port).arg(env.value("BERRY_EMBED_GROUP")));
    log("arguments: " + args.join(" "));
    m_engine->start(dir + "/content_shell.exe", args);

    m_url = startUrl;
    m_title.clear();
    m_historyIndex = -1;
    m_historyIds.clear();
    m_mainFrameId.clear();
    emit pageChanged();
    emit historyChanged();
    if (m_selecting || m_editing) {
        m_selecting = m_editing = false;
        emit selectionChanged();
        emit editingChanged();
    }
    rememberPage();
    setLoading(true);
    setEngineState("starting");
    m_probeAttempts = 0;
    m_probe.start(kProbeMs);
}

QStringList Browser::engineArguments(const QString &startUrl) const
{
    // Berry Browser's launcher flags (deploy/berry-shell-bar/launcher.c on
    // berry-v3), minus its marker-file switches.
    QStringList args;
    args << "--no-sandbox" << "--no-zygote" << "--single-process"
         << "--disable-background-networking"
         << "--disable-background-timer-throttling"
         << "--disable-renderer-backgrounding"
         << "--disable-backgrounding-occluded-windows"
         << "--disable-client-side-phishing-detection"
         << "--disable-default-apps"
         << "--disable-domain-reliability"
         << "--disable-hang-monitor"
         << "--disable-prompt-on-repost"
         << "--disable-sync"
         << "--disable-translate"
         << "--disable-notifications"
         << "--disable-smooth-scrolling"
         << "--no-first-run"
         << "--no-default-browser-check"
         << "--disable-component-update"
         << "--autoplay-policy=no-user-gesture-required"
         << "--disable-accelerated-video-decode";
    // Chromium's own logging costs CPU; Settings can turn it on to diagnose.
    if (setting("detailedLog").toBool())
        args << "--enable-logging=stderr";
    else
        args << "--disable-logging";
    // A touch device scrolls by dragging; desktop scrollbars only take room.
    if (!setting("scrollbars").toBool())
        args << "--hide-scrollbars";
    // The engine's own certificate checks are slow and race-prone on QNX, so
    // Berry Browser skips them; Settings can turn them back on.
    if (!setting("verifyCerts").toBool())
        args << "--ignore-certificate-errors";
    args << "--use-fake-ui-for-media-stream"
         << "--use-fake-device-for-media-stream"
         << "--disable-renderer-accessibility"
         << "--ozone-platform=qnx_screen"
         << QString("--force-device-scale-factor=%1").arg(
                kRenderScales[setting("quality").toInt()] / kRenderScales[defaultQuality(m_width, m_height)],
                0, 'g', 4);
    // Settings > Performance: Berry Browser's defaults, each one a choice.
    if (!setting("quic").toBool())
        args << "--disable-quic";  // breaks video CDNs on QNX (Berry)
    if (setting("http1").toBool())
        args << "--disable-http2";
    if (setting("gpu").toBool())
        args << "--use-gl=egl" << "--ignore-gpu-blocklist" << "--enable-gpu-rasterization";
    else
        // --disable-gpu alone still makes the renderer wait ~14 s for a GPU
        // channel that never comes (Berry, build 3786306).
        args << "--disable-gpu" << "--disable-gpu-compositing";
    int rasterThreads = setting("rasterThreads").toInt();
    if (rasterThreads <= 0)
        rasterThreads = qBound(1, QThread::idealThreadCount(), 4);
    args << QString("--num-raster-threads=%1").arg(rasterThreads);
    // The first two keep the engine working on QNX; the rest are services
    // a phone does not need.
    QStringList off = QStringList() << "NetworkServiceDedicatedThread" << "MojoIpcz"
        << "Translate" << "OptimizationHints" << "MediaRouter" << "PreconnectToSearch"
        << "BackForwardCache" << "AutofillServerCommunication" << "HeavyAdPrivacyMitigations"
        << "InterestFeedContentSuggestions";
    if (setting("privacyCuts").toBool())
        off << "FedCm" << "PrivacySandboxAdsAPIs" << "SharedStorageAPI" << "PrivateAggregationApi";
    if (!setting("serviceWorkers").toBool())
        off << "ServiceWorker";
    args << "--disable-features=" + off.join(",");
    if (setting("lowEnd").toBool())
        args << "--enable-low-end-device-mode"
             // ...but keep the 128MB decoded-image budget, which low-end mode
             // cuts to 32MB, so images are not re-decoded on every scroll
             // (Berry Browser build 107; a field-trial parameter).
             << "--enable-features=ImageDecodeConfigurableFeature:"
                "DecodedImageWorkingSetBudgetBytesForLowEndDevice/134217728";
    if (setting("diskCache").toBool())
        args << "--disk-cache-size=268435456" << "--media-cache-size=268435456";
    if (!setting("desktop").toBool())
        args << "--use-mobile-user-agent";
    QString blink = "hyperlinkAuditingEnabled=false,spellcheckEnabledByDefault=false";
    if (!setting("javascript").toBool())
        blink += ",scriptEnabled=false";
    if (!setting("images").toBool())
        blink += ",imagesEnabled=false";
    if (setting("darkPages").toBool())
        blink += ",forceDarkModeEnabled=true";
    // WebAssembly stays on its baseline compiler: tier-up recompiles hot
    // code on every core right when the page needs them (Berry's WhatsApp
    // app ships this way).
    args << QString("--js-flags=--max-old-space-size=%1%2")
                .arg(setting("heap").toInt())
                .arg(setting("wasmLite").toBool() ? " --no-wasm-tier-up --wasm-lazy-validation" : "")
         << "--blink-settings=" + blink
         << "--remote-debugging-address=127.0.0.1"
         << startUrl;
    return args;
}

void Browser::engineFinished(int exitCode, QProcess::ExitStatus status)
{
    log(QString("engine exited: %1, code %2%3")
            .arg(status == QProcess::CrashExit ? "crashed" : "normal exit").arg(exitCode)
            .arg(m_stopping ? " (restart)" : ""));
    if (!m_stopping && m_engineState == "starting")
        emit message(tr("The browser engine stopped while starting. Its log is %1").arg(logPath()));
    m_probe.stop();
    m_devtools->close();
    m_downloads->disconnectEngine();
    if (m_stopping) {
        m_stopping = false;
        launch();
        return;
    }
    setLoading(false);
    setEngineState("stopped");
}

void Browser::engineError(QProcess::ProcessError error)
{
    log("engine process error: " + (m_engine ? m_engine->errorString() : QString::number(error)));
    if (error == QProcess::FailedToStart) {
        m_probe.stop();
        setEngineState("failed");
        emit message(tr("The browser engine could not start."));
    }
}

void Browser::setEngineState(const QString &state)
{
    if (state == m_engineState)
        return;
    m_engineState = state;
    emit engineStateChanged();
}

// ---- DevTools ------------------------------------------------------------

void Browser::probeEngine()
{
    if (m_engineState == "starting") {
        ++m_probeAttempts;
        if (m_probeAttempts == kSlowStartProbes || m_probeAttempts == kVerySlowStartProbes) {
            log(QString("no DevTools answer on port %1 after %2 s; engine still running")
                    .arg(m_port).arg(m_probeAttempts * kProbeMs / 1000));
            emit message(m_probeAttempts == kSlowStartProbes
                ? tr("The engine is still starting. The first start can take a minute or two.")
                : tr("The engine is taking unusually long. Its log is %1").arg(logPath()));
        }
    }
    m_devtools->listTargets();
}

void Browser::targetsListed(const QVariantList &targets)
{
    // The engine lists its newest window first. Follow it when it is not the
    // one attached, e.g. after a page opened a window despite the script.
    QString first;
    foreach (const QVariant &t, targets) {
        const QVariantMap target = t.toMap();
        if (target.value("type").toString() == "page") {
            first = target.value("id").toString();
            break;
        }
    }
    if (first.isEmpty())
        return;
    if (first == m_devtools->targetId() && (m_devtools->isAttached() || m_devtools->isConnecting()))
        return;
    m_devtools->attach(first);
}

void Browser::attached()
{
    const bool firstAttach = m_engineState != "running";
    if (firstAttach)
        log(QString("connected to DevTools on port %1 after %2 s")
                .arg(m_port).arg(m_probeAttempts * kProbeMs / 1000.0, 0, 'f', 1));
    setEngineState("running");
    m_probe.start(kWatchMs);
    m_devtools->call("Page.enable");
    // With Berry's own blocker (on unless misc/berry-adblock.disable), the
    // EasyList and EasyPrivacy domains: content_shell takes no proxy script,
    // so the engine holds each request until protocolEvent() lets it go.
    if (setting("adblock").toBool()) {
        if (m_blocklist.isEmpty()) {
            QFile file(QCoreApplication::applicationDirPath() + "/blocklist.txt");
            if (file.open(QIODevice::ReadOnly))
                m_blocklist = file.readAll().split('\n').toSet();
            log(QString("content blocklist: %1 domains").arg(m_blocklist.size()));
        }
        if (!m_blocklist.isEmpty())
            m_devtools->call("Fetch.enable");
    }
    QVariantMap binding;
    binding["name"] = QString(kBinding);
    m_devtools->call("Runtime.addBinding", binding);
    QVariantMap script;
    script["source"] = m_pageScript;
    m_devtools->call("Page.addScriptToEvaluateOnNewDocument", script);
    evaluate(m_pageScript, QString());
    evaluate("'engine user agent: ' + navigator.userAgent + ' ' + JSON.stringify(navigator.userAgentData)",
             "log");
    if (!setting("desktop").toBool())
        m_devtools->call("Emulation.setUserAgentOverride", mobileAgent());
    m_devtools->call("Page.getFrameTree");
    applyMobileView();
    applySwipe();
    refreshHistory();
    if (firstAttach)
        m_downloads->connectEngine(m_port);
    if (!m_pendingUrl.isEmpty()) {
        const QString url = m_pendingUrl;
        m_pendingUrl.clear();
        navigate(url);
    }
}

void Browser::detached()
{
    if (m_engineState == "running" && !m_stopping)
        m_probe.start(kProbeMs);
}

void Browser::refreshHistory()
{
    m_devtools->call("Page.getNavigationHistory");
}

int Browser::evaluate(const QString &expression, const QString &purpose)
{
    QVariantMap params;
    params["expression"] = expression;
    params["returnByValue"] = true;
    const int id = m_devtools->call("Runtime.evaluate", params);
    if (id >= 0 && !purpose.isEmpty())
        m_evaluations.insert(id, purpose);
    return id;
}

void Browser::replied(int id, const QString &method, const QVariantMap &result)
{
    if (m_saves.contains(id)) {
        // "Save image": the engine's answer, then the data a part at a time.
        const QStringList save = m_saves.take(id);
        if (method == "Network.loadNetworkResource") {
            const QVariantMap resource = result.value("resource").toMap();
            const int status = resource.value("httpStatusCode").toInt();
            if (resource.value("success").toBool() && status < 400)
                readSaved(save.at(0), resource.value("stream").toString());
            else
                m_downloads->finish(save.at(0), status >= 400 ? QString("HTTP %1").arg(status)
                                                              : resource.value("netErrorName").toString());
            return;
        }
        const QByteArray data = result.value("base64Encoded").toBool()
            ? QByteArray::fromBase64(result.value("data").toByteArray())
            : result.value("data").toString().toUtf8();
        const bool eof = result.value("eof").toBool();
        if (m_downloads->append(save.at(0), data) && !eof) {
            readSaved(save.at(0), save.at(1));
            return;
        }
        QVariantMap params;
        params["handle"] = save.at(1);
        m_devtools->call("IO.close", params);
        if (eof)
            m_downloads->finish(save.at(0), QString());
    } else if (method == "Runtime.evaluate") {
        const QString purpose = m_evaluations.take(id);
        const QVariant value = result.value("result").toMap().value("value");
        if (purpose == "copy") {
            copyText(value.toString());
            clearSelection();
        } else if (purpose == "search") {
            const QString text = value.toString().simplified();
            clearSelection();
            if (!text.isEmpty())
                navigate(searchUrl(text));
        } else if (purpose == "find") {
            const QVariantMap found = value.toMap();
            emit findResult(found.value("found").toBool(), found.value("count").toInt());
        } else if (purpose == "log") {
            log(value.toString());
        }
    } else if (method == "Page.captureScreenshot") {
        if (id != m_snapshotId)
            return;
        m_snapshotId = -1;
        m_snapshotTimer.stop();
        emit snapshotReady(setSnapshot(bb::utility::ImageConverter::decode(
            QByteArray::fromBase64(result.value("data").toByteArray()), bb::PixelFormat::RGBX)));
    } else if (method == "Page.getFrameTree") {
        m_mainFrameId = result.value("frameTree").toMap().value("frame").toMap().value("id").toString();
    } else if (method == "Page.getNavigationHistory") {
        const QVariantList entries = result.value("entries").toList();
        const int index = result.value("currentIndex").toInt();
        m_historyIds.clear();
        foreach (const QVariant &e, entries)
            m_historyIds.append(e.toMap().value("id"));
        m_historyIndex = index;
        emit historyChanged();
        if (index >= 0 && index < entries.size()) {
            const QVariantMap entry = entries.at(index).toMap();
            m_url = entry.value("url").toString();
            m_title = entry.value("title").toString();
            emit pageChanged();
            rememberPage();
            if (!m_loading)
                m_history->visit(m_url, m_title);
        }
    }
}

void Browser::failed(int id, const QString &method, const QString &message)
{
    m_evaluations.remove(id);
    if (m_saves.contains(id))
        m_downloads->finish(m_saves.take(id).at(0), message);
    log(QString("DevTools refused %1: %2").arg(method, message));
    if (method == "Page.navigate")
        emit this->message(message);
}

void Browser::protocolEvent(const QString &method, const QVariantMap &params)
{
    const QString frameId = params.value("frameId").toString();
    const bool mainFrame = m_mainFrameId.isEmpty() || frameId == m_mainFrameId;
    if (method == "Fetch.requestPaused") {
        // A domain on the content blocklist, or under one; not pages themselves.
        QVariantMap reply;
        reply["requestId"] = params.value("requestId");
        const QString host = QUrl(params.value("request").toMap().value("url").toString()).host();
        QByteArray name = host.toLatin1();
        bool blocked = false;
        if (params.value("resourceType").toString() != "Document") {
            while (!blocked && name.contains('.')) {
                blocked = m_blocklist.contains(name);
                name = name.mid(name.indexOf('.') + 1);
            }
        }
        if (blocked) {
            log("blocked " + host);
            reply["errorReason"] = QString("BlockedByClient");
            m_devtools->call("Fetch.failRequest", reply);
        } else {
            m_devtools->call("Fetch.continueRequest", reply);
        }
    } else if (method == "Page.frameStartedLoading") {
        if (mainFrame)
            setLoading(true);
    } else if (method == "Page.frameStoppedLoading") {
        if (mainFrame) {
            setLoading(false);
            m_historyTimer.start();
        }
    } else if (method == "Page.frameNavigated") {
        const QVariantMap frame = params.value("frame").toMap();
        if (!frame.contains("parentId")) {
            m_mainFrameId = frame.value("id").toString();
            m_url = frame.value("url").toString();
            emit pageChanged();
            setIcon(QString());
            if (m_loading)
                setProgress(qMax(m_progress, 0.3));
            m_historyTimer.start();
            // A new document has no bindings until they are added again.
            QVariantMap binding;
            binding["name"] = QString(kBinding);
            m_devtools->call("Runtime.addBinding", binding);
            if (m_selecting || m_editing) {
                m_selecting = m_editing = false;
                m_selectionText.clear();
                emit selectionChanged();
                emit editingChanged();
            }
        }
    } else if (method == "Page.domContentEventFired" || method == "Page.loadEventFired") {
        // The page's load event ends the bar: frameStoppedLoading waits for
        // every frame, and an ad frame that never finishes would hold it.
        if (method == "Page.loadEventFired")
            setLoading(false);
        else if (m_loading)
            setProgress(qMax(m_progress, 0.7));
        m_historyTimer.start();
        applySwipe();
    } else if (method == "Page.navigatedWithinDocument") {
        m_historyTimer.start();
    } else if (method == "Runtime.bindingCalled") {
        if (params.value("name").toString() == kBinding) {
            bb::data::JsonDataAccess json;
            const QVariant event = json.loadFromBuffer(params.value("payload").toString().toUtf8());
            if (!json.hasError())
                pageEvent(event.toMap());
        }
    } else if (method == "Inspector.targetCrashed") {
        setLoading(false);
        emit message(tr("The page crashed. Reload to try again."));
    }
}

// What assets/page.js reports.
void Browser::pageEvent(const QVariantMap &event)
{
    const QString type = event.value("t").toString();
    if (type == "touch") {
        m_glideTimer.stop();  // a finger on the page stops a glide
        emit pageTouched();
    } else if (type == "menu") {
        emit contextMenu(event.value("link").toString(), event.value("text").toString(),
                         event.value("image").toString());
    } else if (type == "select" || type == "selection") {
        m_selecting = true;
        m_selectionText = event.value("text").toString();
        emit selectionChanged();
    } else if (type == "selectionEnd") {
        m_selecting = false;
        m_selectionText.clear();
        emit selectionChanged();
    } else if (type == "editing") {
        // Logged to tell why a field gets key events instead of typed text.
        const bool on = event.value("on").toBool();
        log(QString("focus: %1%2").arg(event.value("what").toString(), on ? ", typed as text" : ""));
        if (on != m_editing) {
            m_editing = on;
            emit editingChanged();
        }
    } else if (type == "scroll") {
        emit pageScrolled(event.value("down").toBool());
    } else if (type == "icon") {
        saveIcon(event.value("data").toString());
    }
}

void Browser::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    if (loading) {
        m_glideTimer.stop();  // the next page starts still
        setProgress(0.08);
        m_progressTimer.start();
    } else {
        m_progressTimer.stop();
        setProgress(1.0);
    }
    emit loadingChanged();
}

// DevTools reports no load progress, only milestones (navigated, DOM ready,
// loaded); between them the bar creeps towards 90% as the stock one does.
void Browser::creepProgress()
{
    setProgress(m_progress + (0.9 - m_progress) * 0.06);
}

void Browser::setProgress(double progress)
{
    if (qAbs(progress - m_progress) < 0.005)
        return;
    m_progress = progress;
    emit progressChanged();
}

// Read from the page's window: the engine's own screenshot waits for its main
// thread, which stalls for seconds on heavy pages.
void Browser::snapshot()
{
    if (grabPage()) {
        emit snapshotReady(true);
        return;
    }
    if (m_snapshotId >= 0)
        return;
    QVariantMap params;
    params["format"] = QString("jpeg");
    params["quality"] = 55;
    m_snapshotId = m_devtools->call("Page.captureScreenshot", params);
    if (m_snapshotId < 0)
        emit snapshotReady(false);
    else
        m_snapshotTimer.start();
}

void Browser::snapshotTimedOut()
{
    m_snapshotId = -1;
    log("the engine's screenshot is late");
    emit snapshotReady(false);
}

bool Browser::grabPage()
{
    bb::cascades::ForeignWindowControl *control = qobject_cast<bb::cascades::ForeignWindowControl *>(m_pageView);
    screen_window_t window = control ? control->windowHandle() : 0;
    screen_context_t context = 0;
    int size[2] = {0, 0};
    if (!window || screen_get_window_property_pv(window, SCREEN_PROPERTY_CONTEXT, (void **)&context) != 0
        || !context || screen_get_window_property_iv(window, SCREEN_PROPERTY_SIZE, size) != 0
        || size[0] <= 0 || size[1] <= 0)
        return false;
    screen_pixmap_t pixmap = 0;
    if (screen_create_pixmap(&pixmap, context) != 0)
        return false;
    int format = SCREEN_FORMAT_RGBX8888;
    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ;
    screen_buffer_t buffer = 0;
    unsigned char *pixels = 0;
    int stride = 0;
    bool ok = false;
    if (screen_set_pixmap_property_iv(pixmap, SCREEN_PROPERTY_FORMAT, &format) == 0
        && screen_set_pixmap_property_iv(pixmap, SCREEN_PROPERTY_USAGE, &usage) == 0
        && screen_set_pixmap_property_iv(pixmap, SCREEN_PROPERTY_BUFFER_SIZE, size) == 0
        && screen_create_pixmap_buffer(pixmap) == 0
        && screen_get_pixmap_property_pv(pixmap, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buffer) == 0
        && screen_read_window(window, buffer, 0, 0, 0) == 0
        && screen_get_buffer_property_pv(buffer, SCREEN_PROPERTY_POINTER, (void **)&pixels) == 0
        && screen_get_buffer_property_iv(buffer, SCREEN_PROPERTY_STRIDE, &stride) == 0 && pixels) {
        // Screen's RGBX8888 is B, G, R, X in memory; ImageData's RGBX is R, G, B, X.
        bb::ImageData image(bb::PixelFormat::RGBX, size[0], size[1]);
        for (int y = 0; y < size[1]; ++y) {
            const unsigned char *from = pixels + y * stride;
            unsigned char *to = image.pixels() + y * image.bytesPerLine();
            for (int x = 0; x < size[0]; ++x, from += 4, to += 4) {
                to[0] = from[2];
                to[1] = from[1];
                to[2] = from[0];
                to[3] = 255;
            }
        }
        ok = setSnapshot(image);
    } else {
        log(QString("cannot read the page's window: %1").arg(strerror(errno)));
    }
    screen_destroy_pixmap(pixmap);
    return ok;
}

// Handed to Cascades as it is: no file to write and decode again.
bool Browser::setSnapshot(const bb::ImageData &image)
{
    if (!image.isValid())
        return false;
    m_snapshotImage = QVariant::fromValue(bb::cascades::Image(image));
    return true;
}

void Browser::appVisible()
{
    log("app in full screen");
}

void Browser::appHidden()
{
    log("app minimized or the screen off");
}

// ---- navigation ----------------------------------------------------------

// Another app opened a link here.
void Browser::invoked(const bb::system::InvokeRequest &request)
{
    QString uri = request.uri().toString();
    if (uri.isEmpty())
        uri = QString::fromUtf8(request.data()).trimmed();
    if (!uri.isEmpty())
        open(uri);
}

void Browser::open(const QString &input)
{
    const QString url = fixUp(input);
    if (!url.isEmpty())
        navigate(url);
}

void Browser::navigate(const QString &url)
{
    m_url = url;
    emit pageChanged();
    rememberPage();
    if (!m_devtools->isAttached()) {
        m_pendingUrl = url;
        if (m_engineState == "stopped" || m_engineState == "failed")
            restartEngine();
        return;
    }
    QVariantMap params;
    params["url"] = url;
    m_devtools->call("Page.navigate", params);
    setLoading(true);
}

void Browser::back()
{
    if (!canGoBack())
        return;
    QVariantMap params;
    params["entryId"] = m_historyIds.at(m_historyIndex - 1);
    m_devtools->call("Page.navigateToHistoryEntry", params);
}

void Browser::forward()
{
    if (!canGoForward())
        return;
    QVariantMap params;
    params["entryId"] = m_historyIds.at(m_historyIndex + 1);
    m_devtools->call("Page.navigateToHistoryEntry", params);
}

void Browser::reload()
{
    if (m_devtools->call("Page.reload") >= 0)
        setLoading(true);
    else if (m_engineState == "stopped" || m_engineState == "failed")
        restartEngine();
}

void Browser::stop()
{
    m_devtools->call("Page.stopLoading");
    setLoading(false);
}

void Browser::home()
{
    navigate(homeUrl());
}

QString Browser::homeUrl() const
{
    const QString custom = fixUp(m_settings.value("homepage").toString());
    if (!custom.isEmpty())
        return custom;
    return QUrl::fromLocalFile(QCoreApplication::applicationDirPath()
                               + "/assets/start.html").toString()
        + "?engine=" + setting("search").toString();
}

bool Browser::looksLikeAddress(const QString &input) const
{
    const QString text = input.trimmed();
    const QString lower = text.toLower();
    if (lower.contains("://") || lower.startsWith("about:") || lower.startsWith("data:"))
        return true;
    if (text.contains(' '))
        return false;
    if (lower.startsWith("localhost") || lower.startsWith("127.") || lower.startsWith("192.168."))
        return true;
    return text.contains('.') && !text.startsWith('.') && !text.endsWith('.');
}

QString Browser::fixUp(const QString &input) const
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return QString();
    if (!looksLikeAddress(text))
        return searchUrl(text);
    const QString lower = text.toLower();
    if (lower.contains("://") || lower.startsWith("about:") || lower.startsWith("data:"))
        return text;
    if (lower.startsWith("localhost") || lower.startsWith("127.") || lower.startsWith("192.168."))
        return "http://" + text;
    return "https://" + text;
}

QString Browser::searchName() const
{
    return searchEngine(setting("search").toString()).name;
}

QString Browser::searchUrl(const QString &query) const
{
    return QString(searchEngine(setting("search").toString()).url)
        + QString::fromAscii(QUrl::toPercentEncoding(query.trimmed()));
}

QVariantList Browser::suggest(const QString &text) const
{
    const QString needle = text.trimmed().toLower();
    QVariantList out;
    QStringList seen;
    if (needle.isEmpty())
        return out;
    foreach (const QVariant &b, bookmarks()) {
        QVariantMap row = b.toMap();
        const QString url = row.value("url").toString();
        if (out.size() < 3 && (url.toLower().contains(needle)
                               || row.value("title").toString().toLower().contains(needle))) {
            row["kind"] = QString("bookmark");
            out.append(row);
            seen.append(url);
        }
    }
    foreach (const QVariant &h, m_history->matches(text, 8)) {
        QVariantMap row = h.toMap();
        if (out.size() >= 6)
            break;
        if (seen.contains(row.value("url").toString()))
            continue;
        row["kind"] = QString("history");
        out.append(row);
    }
    return out;
}

// ---- the page's window and icon ------------------------------------------

// "Save image": Qt here speaks only TLS 1.0, so the engine loads the address
// as the page would and hands over a stream to read.
void Browser::fetchForDownload(const QString &id, const QString &url)
{
    QVariantMap options;
    options["disableCache"] = false;
    options["includeCredentials"] = true;
    QVariantMap params;
    params["frameId"] = m_mainFrameId;
    params["url"] = url;
    params["options"] = options;
    const int call = m_devtools->call("Network.loadNetworkResource", params);
    if (call < 0)
        m_downloads->finish(id, tr("the engine is not running"));
    else
        m_saves.insert(call, QStringList() << id << QString());
}

void Browser::readSaved(const QString &id, const QString &stream)
{
    QVariantMap params;
    params["handle"] = stream;
    params["size"] = 262144;
    const int call = m_devtools->call("IO.read", params);
    if (call < 0)
        m_downloads->finish(id, tr("the engine stopped"));
    else
        m_saves.insert(call, QStringList() << id << stream);
}

void Browser::setBarHeights(int title, int slim, int full)
{
    m_titleHeight = title;
    m_slimHeight = slim;
    m_fullHeight = full;
}

void Browser::showBars(QObject *view, bool bars, bool title, bool animate)
{
    m_pageView = view;
    m_barsFrom = m_barsIn;
    m_titleFrom = m_titleIn;
    m_barsTo = bars ? 1 : 0;
    m_titleTo = title ? 1 : 0;
    m_barsClock.start();
    if (animate)
        m_barsTimer.start();
    else
        m_barsClock.invalidate();
    stepBars();
}

void Browser::liftBars(int pixels)
{
    m_barsLift = pixels;
    stepBars();
}

void Browser::stepBars()
{
    const int kSlideMs = 200;
    const double t = m_barsClock.isValid() ? qMin(1.0, m_barsClock.elapsed() / double(kSlideMs)) : 1.0;
    const double eased = 1 - (1 - t) * (1 - t);  // ease out
    m_barsIn = m_barsFrom + (m_barsTo - m_barsFrom) * eased;
    m_titleIn = m_titleFrom + (m_titleTo - m_titleFrom) * eased;
    emit barsMoved();
    if (t >= 1)
        m_barsTimer.stop();
    // The full bar slides by its own height and covers the slim bar first;
    // the page is cut where it rises above the slim bar.
    setPageClip(qRound(m_titleIn * m_titleHeight),
                qMax(0, qRound(m_barsIn * m_fullHeight) - m_slimHeight) + m_barsLift);
}

// The on-screen keyboard (a touch-only phone's, the Passport's top row):
// shown, hidden, or a new height.
void Browser::event(bps_event_t *event)
{
    if (bps_event_get_domain(event) != virtualkeyboard_get_domain())
        return;
    const int code = bps_event_get_code(event);
    if (code == VIRTUALKEYBOARD_EVENT_VISIBLE)
        m_keyboardShown = true;
    else if (code == VIRTUALKEYBOARD_EVENT_HIDDEN)
        m_keyboardShown = false;
    int height = 0;
    if (m_keyboardShown)
        virtualkeyboard_get_height(&height);
    if (height == m_keyboardHeight)
        return;
    m_keyboardHeight = height;
    log(QString("on-screen keyboard: %1 px").arg(height));
    emit keyboardHeightChanged();
}

void Browser::setPageClip(int top, int bottom)
{
    bb::cascades::ForeignWindowControl *control = qobject_cast<bb::cascades::ForeignWindowControl *>(m_pageView);
    screen_window_t window = control ? control->windowHandle() : 0;
    if (!window)
        return;
    // CLIP_* are the parent's to set on a child window. Window pixels are
    // assumed here; if the engine's buffer pixels are meant instead, the cut
    // comes out scaled by the rendering resolution (see the log line).
    int size[2] = {0, 0};
    screen_get_window_property_iv(window, SCREEN_PROPERTY_SIZE, size);
    int position[2] = {0, top};
    int clip[2] = {size[0], qMax(1, size[1] - top - bottom)};
    screen_set_window_property_iv(window, SCREEN_PROPERTY_CLIP_POSITION, position);
    screen_set_window_property_iv(window, SCREEN_PROPERTY_CLIP_SIZE, clip);
    screen_context_t context = 0;
    if (screen_get_window_property_pv(window, SCREEN_PROPERTY_CONTEXT, (void **)&context) == 0 && context)
        screen_flush_context(context, 0);
    if (!m_barsTimer.isActive())
        log(QString("page clip: window %1x%2, showing y %3..%4").arg(size[0]).arg(size[1])
                .arg(top).arg(top + clip[1]));
}

void Browser::swipeFromTop()
{
    log("swipe down from the top edge");
    emit swipedDown();
}

void Browser::saveIcon(const QString &dataUrl)
{
    const QByteArray png = QByteArray::fromBase64(dataUrl.mid(dataUrl.indexOf(',') + 1).toLatin1());
    const QString old = QDir::homePath() + QString("/icon-%1.png").arg(m_iconCount);
    const QString path = QDir::homePath() + QString("/icon-%1.png").arg(++m_iconCount);
    QFile::remove(old);
    QFile file(path);
    if (!png.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(png) == png.size())
        setIcon(QUrl::fromLocalFile(path).toString());
}

void Browser::setIcon(const QString &icon)
{
    if (icon == m_icon)
        return;
    m_icon = icon;
    emit iconChanged();
}

// ---- the last page, which the next start opens --------------------------

void Browser::rememberPage()
{
    if (!m_url.isEmpty() && m_settings.value("lastUrl").toString() != m_url)
        m_settings.setValue("lastUrl", m_url);
}

// ---- keys, editing, selection, find ----------------------------------------

void Browser::keyEvent(const QString &type, const QVariantMap &fields)
{
    QVariantMap params = fields;
    params["type"] = type;
    m_devtools->call("Input.dispatchKeyEvent", params);
}

void Browser::typeText(const QString &text)
{
    if (!m_devtools->isAttached())
        return;
    QVariantMap params;
    params["text"] = text;
    m_devtools->call("Input.insertText", params);
}

void Browser::sendKey(int key)
{
    if (!m_devtools->isAttached())
        return;

    // Keys without text: sys/keycodes.h values (KEYCODE_PC_KEYS + n) and
    // the ASCII control codes Cascades reports for some of them.
    struct Special { int key; int alt; const char *name; int vk; const char *text; };
    static const Special specials[] = {
        {0xF008, 8, "Backspace", 8, 0},
        {0xF00D, 13, "Enter", 13, "\r"},
        {0xF009, 9, "Tab", 9, 0},
        {0xF01B, 27, "Escape", 27, 0},
        {0xF051, -1, "ArrowLeft", 37, 0},
        {0xF052, -1, "ArrowUp", 38, 0},
        {0xF053, -1, "ArrowRight", 39, 0},
        {0xF054, -1, "ArrowDown", 40, 0},
        {0xF050, -1, "Home", 36, 0},
        {0xF057, -1, "End", 35, 0},
        {0xF055, -1, "PageUp", 33, 0},
        {0xF056, -1, "PageDown", 34, 0},
        {0xF0FF, 127, "Delete", 46, 0},
    };
    for (unsigned i = 0; i < sizeof(specials) / sizeof(specials[0]); ++i) {
        const Special &s = specials[i];
        if (key != s.key && key != s.alt && !(s.vk == 13 && key == 10))
            continue;
        QVariantMap fields;
        fields["key"] = QString(s.name);
        fields["code"] = QString(s.name);
        fields["windowsVirtualKeyCode"] = s.vk;
        fields["nativeVirtualKeyCode"] = s.vk;
        if (s.text) {
            fields["text"] = QString(s.text);
            keyEvent("keyDown", fields);
        } else {
            keyEvent("rawKeyDown", fields);
        }
        fields.remove("text");
        keyEvent("keyUp", fields);
        return;
    }

    if (key < 0x20 || key >= 0xE000)
        return;
    const QString text = QString(QChar(key));
    int vk = 0;
    const QChar upper = QChar(key).toUpper();
    if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9') || key == ' ')
        vk = upper.unicode();
    QVariantMap fields;
    fields["key"] = text;
    fields["text"] = text;
    fields["unmodifiedText"] = text;
    fields["windowsVirtualKeyCode"] = vk;
    fields["nativeVirtualKeyCode"] = vk;
    keyEvent("keyDown", fields);
    fields.remove("text");
    fields.remove("unmodifiedText");
    keyEvent("keyUp", fields);
}

void Browser::paste()
{
    bb::system::Clipboard clipboard;
    const QString text = QString::fromUtf8(clipboard.value("text/plain"));
    if (text.isEmpty()) {
        emit message(tr("The clipboard has no text"));
        return;
    }
    QVariantMap params;
    params["text"] = text;
    m_devtools->call("Input.insertText", params);
}

// A mouse wheel over the middle of the page: the engine scrolls it off its
// main thread, which stalls for seconds on heavy pages.
void Browser::scrollPage(double dx, double dy, bool pixels)
{
    const QString input = pixels ? "keyboardScroll" : "trackpadScroll";
    if (!m_devtools->isAttached() || m_width <= 0 || !setting(input).toBool())
        return;
    // The Q20 has not been tried: the log says whether its swipes arrive.
    static bool logged[2];
    if (!logged[pixels]) {
        logged[pixels] = true;
        log(pixels ? "touch keyboard scrolls the page" : "trackpad scrolls the page");
    }
    const double kWheelPerUnit = 4;  // too fast at 1× on a Q20
    const double css = kRenderScales[defaultQuality(m_width, m_height)];  // CSS px per window px
    const double scale = setting(input + "Speed").toDouble() * (pixels ? css : kWheelPerUnit);
    const double wheelX = dx * scale;
    const double wheelY = dy * scale;

    // The swipe's speed, for the glide after it (endScroll), smoothed over its
    // last moves. A pause, or a glide still running, starts a new swipe.
    const qint64 ms = m_scrollClock.isValid() ? m_scrollClock.restart() : -1;
    if (ms < 0)
        m_scrollClock.start();
    if (ms < 0 || ms > kSwipeGapMs || m_glideTimer.isActive()) {
        m_scrollVX = 0;
        m_scrollVY = 0;
    } else {
        const double t = qMax<qint64>(ms, 4);
        m_scrollVX = 0.6 * wheelX / t + 0.4 * m_scrollVX;
        m_scrollVY = 0.6 * wheelY / t + 0.4 * m_scrollVY;
    }
    m_glideTimer.stop();
    wheel(wheelX, wheelY);
}

void Browser::endScroll()
{
    if (!m_scrollClock.isValid() || m_scrollClock.elapsed() > kStillMs
            || qAbs(m_scrollVX) + qAbs(m_scrollVY) < kMinGlide)
        return;
    // Neither phone has been tried: the log says whether the lift arrives.
    static bool logged;
    if (!logged) {
        logged = true;
        log(QString("scroll glides on at %1, %2 px/ms").arg(m_scrollVX).arg(m_scrollVY));
    }
    m_scrollClock.restart();
    m_glideTimer.start();
}

// One step of the glide. The speed decays exponentially, so each step moves
// the distance it covers in that time: speed × kGlideMs × (1 - what is left).
void Browser::glide()
{
    if (!m_devtools->isAttached()) {
        m_glideTimer.stop();
        return;
    }
    const double ms = qMax<qint64>(1, m_scrollClock.restart());
    const double left = qExp(-ms / kGlideMs);
    wheel(m_scrollVX * kGlideMs * (1 - left), m_scrollVY * kGlideMs * (1 - left));
    m_scrollVX *= left;
    m_scrollVY *= left;
    if (qAbs(m_scrollVX) + qAbs(m_scrollVY) < kMinGlide)
        m_glideTimer.stop();
}

void Browser::wheel(double dx, double dy)
{
    const double css = kRenderScales[defaultQuality(m_width, m_height)];  // CSS px per window px
    QVariantMap params;
    params["type"] = QString("mouseWheel");
    params["x"] = m_width * css / 2;
    params["y"] = m_height * css / 2;
    params["deltaX"] = dx;
    params["deltaY"] = dy;
    m_devtools->call("Input.dispatchMouseEvent", params);
}

bool Browser::hasTrackpad() const
{
    return bb::device::HardwareInfo().isTrackpadDevice();
}

void Browser::noteTouchKeyboard()
{
    if (hasTouchKeyboard())
        return;
    m_settings.setValue("touchKeyboard", true);
    log("touch keyboard found");
    emit hasTouchKeyboardChanged();
}

void Browser::selectAll()
{
    evaluate("window.__berry&&__berry.selectAll()", QString());
}

void Browser::clearSelection()
{
    evaluate("window.__berry&&__berry.clearSelection()", QString());
    if (m_selecting) {
        m_selecting = false;
        m_selectionText.clear();
        emit selectionChanged();
    }
}

void Browser::copySelection()
{
    evaluate("window.__berry?__berry.selectionText():''", "copy");
}

void Browser::searchSelection()
{
    evaluate("window.__berry?__berry.selectionText():''", "search");
}

void Browser::find(const QString &text, bool backwards)
{
    evaluate(QString("window.__berry?__berry.find(%1,%2):{found:false,count:0}")
                 .arg(jsString(text), backwards ? "true" : "false"),
             "find");
}

// Mobile view: the engine is a desktop content_shell that only sends a mobile
// user agent. DevTools' mobile device mode makes it lay pages out as mobile
// Chrome does: <meta name=viewport> honoured, pages without one laid out at
// 980 px and zoomed to fit, (pointer: coarse) and no hover, so sites serve
// their touch layouts. Width and height 0 keep the view's real size. Off with
// "Request desktop sites", or in Settings.
void Browser::applyMobileView()
{
    if (setting("mobileView").toBool() && !setting("desktop").toBool()) {
        QVariantMap metrics;
        metrics["width"] = 0;
        metrics["height"] = 0;
        metrics["deviceScaleFactor"] = 0;
        metrics["mobile"] = true;
        m_devtools->call("Emulation.setDeviceMetricsOverride", metrics);
        QVariantMap touch;
        touch["enabled"] = true;
        touch["maxTouchPoints"] = 5;
        m_devtools->call("Emulation.setTouchEmulationEnabled", touch);
    } else {
        m_devtools->call("Emulation.clearDeviceMetricsOverride");
        QVariantMap touch;
        touch["enabled"] = false;
        m_devtools->call("Emulation.setTouchEmulationEnabled", touch);
    }
}

// Swipe to go back/forward, as the "swipeNavigation" setting says.
void Browser::applySwipe()
{
    evaluate(QString("window.__berry&&__berry.setSwipe(%1)")
                 .arg(setting("swipeNavigation").toBool() ? "true" : "false"),
             QString());
}

void Browser::stopFind()
{
    evaluate("getSelection().removeAllRanges()", QString());
}

// ---- bookmarks, clipboard, storage, settings ------------------------------

void Browser::copyText(const QString &text)
{
    if (text.isEmpty())
        return;
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert("text/plain", text.toUtf8());
    emit message(tr("Copied"));
}

void Browser::copyUrl()
{
    copyText(m_url);
}

QVariantList Browser::bookmarks() const
{
    return m_settings.value("bookmarks").toList();
}

bool Browser::bookmarked() const
{
    foreach (const QVariant &b, bookmarks()) {
        if (b.toMap().value("url").toString() == m_url)
            return true;
    }
    return false;
}

void Browser::addBookmark()
{
    if (m_url.isEmpty() || bookmarked())
        return;
    QVariantMap bookmark;
    bookmark["url"] = m_url;
    bookmark["title"] = m_title.isEmpty() ? m_url : m_title;
    QVariantList list = bookmarks();
    list.prepend(bookmark);
    m_settings.setValue("bookmarks", list);
    emit bookmarksChanged();
    emit message(tr("Bookmark added"));
}

void Browser::removeBookmark(const QString &url)
{
    QVariantList list = bookmarks();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).toMap().value("url").toString() == url)
            list.removeAt(i);
    }
    m_settings.setValue("bookmarks", list);
    emit bookmarksChanged();
}

void Browser::clearCache()
{
    if (m_devtools->call("Network.clearBrowserCache") >= 0)
        emit message(tr("Cache cleared"));
}

void Browser::clearCookies()
{
    if (m_devtools->call("Network.clearBrowserCookies") >= 0)
        emit message(tr("Cookies cleared"));
}

QVariant Browser::setting(const QString &key) const
{
    if (key == "quality") {
        int quality = m_settings.value("quality", -1).toInt();
        if (quality < 0 || quality > 3)
            quality = defaultQuality(m_width, m_height);
        return quality;
    }
    if (key == "rotation" || key == "touchMode" || key == "fps" || key == "rasterThreads")
        return m_settings.value(key, 0).toInt();
    if (key == "heap") {
        // V8's old-space cap in MB. Above 512 the 32-bit address space,
        // shared with graphics and network, can run out (Berry's "risky").
        const int heap = m_settings.value(key, 256).toInt();
        return heap == 384 || heap == 512 || heap == 768 || heap == 1024 ? heap : 256;
    }
    // Times page pixels per pixel the finger slides on the touch keyboard (on
    // a Passport 1 was about half the speed it should be), and times 4 CSS
    // pixels per trackpad unit (on a Q20 1 is too fast).
    if (key == "keyboardScrollSpeed")
        return qBound(0.5, m_settings.value(key, 2.0).toDouble(), 3.0);
    if (key == "trackpadScrollSpeed")
        return qBound(0.25, m_settings.value(key, 0.5).toDouble(), 1.5);
    if (key == "adblock")
        return !QFile::exists(kAdblockOff);
    if (key == "desktop" || key == "verifyCerts" || key == "swipeNavigation" || key == "wasmLite"
        || key == "scrollbars" || key == "detailedLog" || key == "darkPages" || key == "quic"
        || key == "http1")
        return m_settings.value(key, false).toBool();
    if (key == "javascript" || key == "images" || key == "suggestions"
        || key == "mobileView" || key == "gpu" || key == "lowEnd" || key == "diskCache"
        || key == "serviceWorkers" || key == "privacyCuts" || key == "keyboardScroll" || key == "trackpadScroll")
        return m_settings.value(key, true).toBool();
    if (key == "search")
        return QString(searchEngine(m_settings.value(key, "duckduckgo").toString()).key);
    return m_settings.value(key).toString();
}

void Browser::setSetting(const QString &key, const QVariant &value)
{
    if (setting(key) == value)
        return;
    if (key == "adblock") {
        QFile off(kAdblockOff);
        if (value.toBool())
            off.remove();
        else
            off.open(QIODevice::WriteOnly);
    } else {
        m_settings.setValue(key, value);
    }
    if (key == "search")
        emit searchChanged();
    if (key == "swipeNavigation")
        applySwipe();
    if (key == "mobileView" && m_devtools->isAttached()) {
        applyMobileView();
        reload();
    }
    if (isEngineSetting(key) && !m_restartNeeded) {
        m_restartNeeded = true;
        emit restartNeededChanged();
    }
}
