#include "browser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QUrl>

#include <bb/cascades/Application>
#include <bb/cascades/Window>
#include <bb/system/Clipboard>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

namespace {

const char kWindowId[] = "berryEngineWindow";
const char kSharedMisc[] = "/accounts/1000/shared/misc";
const int kProbeMs = 500;
const int kProbeAttempts = 120;  // the engine takes 5-20 s to come up
const int kWatchMs = 3000;
const qint64 kMaxLogBytes = 2 * 1024 * 1024;

// Fraction of the web view's pixels the engine renders, before the window
// scales it up; Chromium on a Krait CPU needs the help. Indexed by the
// "quality" setting.
const double kRenderScales[] = {0.375, 0.5, 0.75, 1.0};

// Run in every page. One window, one page: links and window.open() that
// would open a new window navigate this one, since the engine's extra windows
// have no UI here. And touches are reported through the __berryTouched
// binding, since they go straight to the engine's window and Cascades would
// otherwise leave the keyboard with the address bar.
const char kTouchBinding[] = "__berryTouched";
const char kPageScript[] =
    "(function(){"
    "if(window.__berryPage)return;window.__berryPage=true;"
    "window.open=function(u){if(u){location.href=new URL(u,location.href).href;}return window;};"
    "document.addEventListener('click',function(e){"
    "var a=e.target&&e.target.closest?e.target.closest('a[target]'):null;"
    "if(a&&a.target!=='_self'&&a.href){a.target='_self';}"
    "},true);"
    "var last=0;"
    "document.addEventListener('touchstart',function(){"
    "var now=Date.now();"
    "if(now-last>500&&window.__berryTouched){last=now;window.__berryTouched('');}"
    "},{capture:true,passive:true});"
    "})();";

// Settings that are engine flags: changing one takes an engine restart.
bool isEngineSetting(const QString &key)
{
    return key == "quality" || key == "desktop" || key == "verifyCerts"
        || key == "rotation" || key == "touchMode";
}

}  // namespace

Browser::Browser(QObject *parent)
    : QObject(parent)
    , m_settings(QDir::homePath() + "/settings.ini", QSettings::IniFormat)
    , m_engine(0)
    , m_devtools(new DevTools(this))
    , m_probeAttempts(0)
    , m_width(0)
    , m_height(0)
    , m_stopping(false)
    , m_restartNeeded(false)
    , m_engineState("idle")
    , m_loading(false)
    , m_historyIndex(-1)
{
    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(400);
    connect(&m_startTimer, SIGNAL(timeout()), this, SLOT(launchIfIdle()));
    connect(&m_probe, SIGNAL(timeout()), this, SLOT(probeEngine()));
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(150);
    connect(&m_historyTimer, SIGNAL(timeout()), this, SLOT(refreshHistory()));

    bb::system::InvokeManager *invokes = new bb::system::InvokeManager(this);
    connect(invokes, SIGNAL(invoked(const bb::system::InvokeRequest&)),
            this, SLOT(invoked(const bb::system::InvokeRequest&)));

    connect(m_devtools, SIGNAL(targetsListed(QVariantList)), this, SLOT(targetsListed(QVariantList)));
    connect(m_devtools, SIGNAL(targetsFailed()), this, SLOT(targetsFailed()));
    connect(m_devtools, SIGNAL(attached()), this, SLOT(attached()));
    connect(m_devtools, SIGNAL(detached()), this, SLOT(detached()));
    connect(m_devtools, SIGNAL(replied(int, QString, QVariantMap)),
            this, SLOT(replied(int, QString, QVariantMap)));
    connect(m_devtools, SIGNAL(failed(int, QString, QString)),
            this, SLOT(failed(int, QString, QString)));
    connect(m_devtools, SIGNAL(event(QString, QVariantMap)),
            this, SLOT(protocolEvent(QString, QVariantMap)));
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
        return QString(kSharedMisc) + "/berry-cascades-engine.log";
    return QDir::homePath() + "/engine.log";
}

void Browser::launch()
{
    const QString dir = engineDir();
    if (!QFile::exists(dir + "/content_shell.exe")) {
        setEngineState("failed");
        emit message(tr("The browser engine is missing from this package."));
        return;
    }

    int quality = m_settings.value("quality", -1).toInt();
    if (quality < 0 || quality > 3)
        quality = qMin(m_width, m_height) >= 1080 ? 0 : 1;
    const double scale = kRenderScales[quality];
    const int renderWidth = qMax(240, int(m_width * scale + 0.5));
    const int renderHeight = qMax(240, int(m_height * scale + 0.5));
    const int port = 9300 + (qrand() % 500);

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
    env.insert("QNX_SCREEN_ROTATION", QString::number(m_settings.value("rotation", 0).toInt()));
    env.insert("QNX_INPUT_POLL_MS", "1");
    env.insert("QNX_MAX_FPS", "45");
    env.insert("QNX_GPU_PROBE", "0");
    env.insert("BERRY_APP_MODE", "1");
    env.insert("BERRY_EMBED_GROUP", bb::cascades::Application::instance()->mainWindow()->groupId());
    env.insert("BERRY_EMBED_ID", kWindowId);
    env.insert("BERRY_EMBED_ROTATION", QString::number(m_settings.value("rotation", 0).toInt()));
    static const char *const touchModes[] = {"source", "window", "display"};
    const int touchMode = m_settings.value("touchMode", 0).toInt();
    env.insert("BERRY_EMBED_TOUCH", touchModes[touchMode >= 0 && touchMode < 3 ? touchMode : 0]);

    const QString log = logPath();
    if (QFileInfo(log).size() > kMaxLogBytes)
        QFile::remove(log);

    QString startUrl = m_pendingUrl.isEmpty() ? homeUrl() : m_pendingUrl;
    m_pendingUrl.clear();

    if (m_engine) {
        m_engine->disconnect(this);
        m_engine->deleteLater();
    }
    m_engine = new QProcess(this);
    m_engine->setProcessEnvironment(env);
    m_engine->setWorkingDirectory(QDir::homePath());
    m_engine->setStandardOutputFile(log, QIODevice::Append);
    m_engine->setStandardErrorFile(log, QIODevice::Append);
    connect(m_engine, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(engineFinished(int, QProcess::ExitStatus)));
    connect(m_engine, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(engineError(QProcess::ProcessError)));

    m_devtools->setPort(port);
    QStringList args = engineArguments(startUrl);
    args.insert(args.size() - 1, QString("--remote-debugging-port=%1").arg(port));
    m_engine->start(dir + "/content_shell.exe", args);

    m_url = startUrl;
    m_title.clear();
    m_historyIndex = -1;
    m_historyIds.clear();
    m_mainFrameId.clear();
    emit pageChanged();
    emit historyChanged();
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
         << "--disable-accelerated-video-decode"
         << "--disable-logging";
    // The engine's own certificate checks are slow and race-prone on QNX, so
    // Berry Browser skips them; Settings can turn them back on.
    if (!m_settings.value("verifyCerts", false).toBool())
        args << "--ignore-certificate-errors";
    args << "--use-fake-ui-for-media-stream"
         << "--use-fake-device-for-media-stream"
         << "--disable-quic"
         << "--disable-renderer-accessibility"
         << "--ozone-platform=qnx_screen"
         << "--use-gl=egl"
         << "--ignore-gpu-blocklist"
         << "--enable-gpu-rasterization"
         << "--num-raster-threads=4"
         << "--force-device-scale-factor=1"
         << "--disable-features=NetworkServiceDedicatedThread,MojoIpcz,Translate,"
            "OptimizationHints,MediaRouter,PreconnectToSearch,BackForwardCache,"
            "AutofillServerCommunication,HeavyAdPrivacyMitigations,"
            "InterestFeedContentSuggestions,FedCm,PrivacySandboxAdsAPIs,"
            "SharedStorageAPI,PrivateAggregationApi"
         << "--enable-low-end-device-mode"
         << "--disk-cache-size=268435456"
         << "--media-cache-size=268435456";
    if (!m_settings.value("desktop", false).toBool())
        args << "--use-mobile-user-agent";
    args << "--js-flags=--max-old-space-size=256"
         << "--blink-settings=hyperlinkAuditingEnabled=false,spellcheckEnabledByDefault=false"
         << "--remote-debugging-address=127.0.0.1"
         << startUrl;
    return args;
}

void Browser::engineFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
    m_probe.stop();
    m_devtools->close();
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
    if (m_engineState == "starting" && ++m_probeAttempts > kProbeAttempts) {
        m_probe.stop();
        setEngineState("failed");
        emit message(tr("The browser engine did not answer. Its log is %1").arg(logPath()));
        return;
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
        if (target.value("type").toString() != "page")
            continue;
        const QString id = target.value("id").toString();
        if (first.isEmpty())
            first = id;
    }
    if (first.isEmpty())
        return;
    if (first == m_devtools->targetId() && (m_devtools->isAttached() || m_devtools->isConnecting()))
        return;
    m_devtools->attach(first);
}

void Browser::targetsFailed()
{
}

void Browser::attached()
{
    setEngineState("running");
    m_probe.start(kWatchMs);
    m_devtools->call("Page.enable");
    QVariantMap binding;
    binding["name"] = QString(kTouchBinding);
    m_devtools->call("Runtime.addBinding", binding);
    QVariantMap script;
    script["source"] = QString(kPageScript);
    m_devtools->call("Page.addScriptToEvaluateOnNewDocument", script);
    QVariantMap now;
    now["expression"] = QString(kPageScript);
    m_devtools->call("Runtime.evaluate", now);
    m_devtools->call("Page.getFrameTree");
    refreshHistory();
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

void Browser::replied(int id, const QString &method, const QVariantMap &result)
{
    Q_UNUSED(id);
    if (method == "Page.getFrameTree") {
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
            emit bookmarksChanged();
        }
    }
}

void Browser::failed(int id, const QString &method, const QString &message)
{
    Q_UNUSED(id);
    if (method == "Page.navigate")
        emit this->message(message);
}

void Browser::protocolEvent(const QString &method, const QVariantMap &params)
{
    const QString frameId = params.value("frameId").toString();
    const bool mainFrame = m_mainFrameId.isEmpty() || frameId == m_mainFrameId;
    if (method == "Page.frameStartedLoading") {
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
            m_historyTimer.start();
        }
    } else if (method == "Page.navigatedWithinDocument"
               || method == "Page.domContentEventFired"
               || method == "Page.loadEventFired") {
        m_historyTimer.start();
    } else if (method == "Runtime.bindingCalled") {
        if (params.value("name").toString() == kTouchBinding)
            emit pageTouched();
    } else if (method == "Inspector.targetCrashed") {
        setLoading(false);
        emit message(tr("The page crashed."));
    }
}

void Browser::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    emit loadingChanged();
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

QString Browser::fixUp(const QString &input) const
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return QString();
    const QString lower = text.toLower();
    if (lower.contains("://") || lower.startsWith("about:") || lower.startsWith("data:"))
        return text;
    if (!text.contains(' ')) {
        if (lower.startsWith("localhost") || lower.startsWith("127.") || lower.startsWith("192.168."))
            return "http://" + text;
        if (text.contains('.') && !text.startsWith('.') && !text.endsWith('.'))
            return "https://" + text;
    }
    return searchUrl(text);
}

QString Browser::searchUrl(const QString &query) const
{
    const QString engine = m_settings.value("search", "duckduckgo").toString();
    QString base = "https://duckduckgo.com/?q=";
    if (engine == "google")
        base = "https://www.google.com/search?q=";
    else if (engine == "bing")
        base = "https://www.bing.com/search?q=";
    return base + QString::fromAscii(QUrl::toPercentEncoding(query));
}

// ---- keys ----------------------------------------------------------------

void Browser::keyEvent(const QString &type, const QVariantMap &fields)
{
    QVariantMap params = fields;
    params["type"] = type;
    m_devtools->call("Input.dispatchKeyEvent", params);
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

// ---- bookmarks and settings ---------------------------------------------

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

void Browser::copyUrl()
{
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert("text/plain", m_url.toUtf8());
    emit message(tr("Link copied"));
}

QVariant Browser::setting(const QString &key) const
{
    if (key == "quality") {
        int quality = m_settings.value("quality", -1).toInt();
        if (quality < 0 || quality > 3)
            quality = qMin(m_width, m_height) >= 1080 ? 0 : 1;
        return quality;
    }
    if (key == "rotation" || key == "touchMode")
        return m_settings.value(key, 0).toInt();
    if (key == "desktop" || key == "verifyCerts")
        return m_settings.value(key, false).toBool();
    if (key == "search")
        return m_settings.value(key, "duckduckgo").toString();
    return m_settings.value(key).toString();
}

void Browser::setSetting(const QString &key, const QVariant &value)
{
    if (setting(key) == value)
        return;
    m_settings.setValue(key, value);
    if (isEngineSetting(key) && !m_restartNeeded) {
        m_restartNeeded = true;
        emit restartNeededChanged();
    }
}
