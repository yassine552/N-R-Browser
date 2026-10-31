#include "WebPage.h"

#include "AdBlockClient.h"
#include "AdblockBackend.h"
#include "AppConfig.h"
#include "BackgroundStore.h"
#include "BrowserApplication.h"
#include "BrowserWindow.h"
#include "HistoryStore.h"
#include "TopSitesStore.h"

#include <QAuthenticator>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrlQuery>
#include <QWebEngineCertificateError>
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

using namespace std::chrono_literals;

namespace {
const char kCosmeticScriptName[] = "nor-cosmetic-filters";
const char kGenericScriptName[] = "nor-generic-cosmetic";

// Injected at document creation, before content parses: hides elements
// matched by the engine's "##" selector rules for this exact URL.
// CSS-only by design — no scriptlets from filter lists are ever executed.
const char kCosmeticScriptTemplate[] = R"NORJS((function () {
    try {
        var selectors = %1;
        if (!selectors || !selectors.length) return;
        var css = selectors.join(',') + '{display:none!important;}';
        function inject() {
            if (document.getElementById('nor-cosmetic-style')) return;
            var s = document.createElement('style');
            s.id = 'nor-cosmetic-style';
            s.textContent = css;
            (document.head || document.documentElement).appendChild(s);
        }
        if (document.documentElement) inject();
        else document.addEventListener('DOMContentLoaded', inject);
    } catch (e) { /* never break the page */ }
})();)NORJS";
} // namespace

WebPage::WebPage(QWebEngineProfile *profile, BrowserWindow *window, QObject *parent)
    : QWebEnginePage(profile, parent)
    , m_window(window)
{
    connect(this, &QWebEnginePage::certificateError,
            this, &WebPage::onCertificateError);
    connect(this, &QWebEnginePage::permissionRequested,
            this, &WebPage::onPermissionRequested);
    connect(this, &QWebEnginePage::fullScreenRequested,
            this, &WebPage::onFullScreenRequested);
    connect(this, &QWebEnginePage::authenticationRequired,
            this, &WebPage::onAuthenticationRequired);
    connect(this, &QWebEnginePage::windowCloseRequested,
            this, &WebPage::onWindowCloseRequested);
    connect(this, &QWebEnginePage::loadFinished, this, &WebPage::onLoadFinished);
    connect(this, &QWebEnginePage::titleChanged, this, &WebPage::onTitleChanged);
    connect(this, &QWebEnginePage::urlChanged, this, &WebPage::onUrlChanged);
}

WebPage::~WebPage() = default;

QString WebPage::permissionKey(QWebEnginePermission::PermissionType type)
{
    switch (type) {
    case QWebEnginePermission::PermissionType::MediaAudioCapture:
        return QStringLiteral("media-audio");
    case QWebEnginePermission::PermissionType::MediaVideoCapture:
        return QStringLiteral("media-video");
    case QWebEnginePermission::PermissionType::MediaAudioVideoCapture:
        return QStringLiteral("media-audio-video");
    case QWebEnginePermission::PermissionType::DesktopVideoCapture:
        return QStringLiteral("desktop-video");
    case QWebEnginePermission::PermissionType::DesktopAudioVideoCapture:
        return QStringLiteral("desktop-audio-video");
    case QWebEnginePermission::PermissionType::MouseLock:
        return QStringLiteral("mouse-lock");
    case QWebEnginePermission::PermissionType::Notifications:
        return QStringLiteral("notifications");
    case QWebEnginePermission::PermissionType::Geolocation:
        return QStringLiteral("geolocation");
    case QWebEnginePermission::PermissionType::ClipboardReadWrite:
        return QStringLiteral("clipboard");
    case QWebEnginePermission::PermissionType::LocalFontsAccess:
        return QStringLiteral("local-fonts");
    default:
        return QStringLiteral("unknown");
    }
}

QString WebPage::permissionDisplayName(QWebEnginePermission::PermissionType type)
{
    switch (type) {
    case QWebEnginePermission::PermissionType::MediaAudioCapture:
        return tr("your microphone");
    case QWebEnginePermission::PermissionType::MediaVideoCapture:
        return tr("your camera");
    case QWebEnginePermission::PermissionType::MediaAudioVideoCapture:
        return tr("your camera and microphone");
    case QWebEnginePermission::PermissionType::DesktopVideoCapture:
        return tr("your screen");
    case QWebEnginePermission::PermissionType::DesktopAudioVideoCapture:
        return tr("your screen and audio");
    case QWebEnginePermission::PermissionType::MouseLock:
        return tr("your mouse pointer");
    case QWebEnginePermission::PermissionType::Notifications:
        return tr("notifications");
    case QWebEnginePermission::PermissionType::Geolocation:
        return tr("your location");
    case QWebEnginePermission::PermissionType::ClipboardReadWrite:
        return tr("your clipboard");
    case QWebEnginePermission::PermissionType::LocalFontsAccess:
        return tr("your local fonts");
    default:
        return tr("a device");
    }
}

void WebPage::onCertificateError(const QWebEngineCertificateError &error)
{
    // Certificate errors are never accepted automatically. When the error
    // is user-overridable we ask; otherwise the connection is rejected.
    // (The error object is implicitly shared: a copy can resolve it.)
    QWebEngineCertificateError localError = error;
    const QString url = error.url().host();
    // Automated runs (smoke tests) set this property to answer every
    // certificate error with the safe default without a modal dialog.
    // It can only make the browser more restrictive, never less.
    if (QCoreApplication::instance()->property("norAutoRejectCertificates").toBool()) {
        localError.rejectCertificate();
        return;
    }
    if (!error.isOverridable()) {
        QMessageBox::warning(m_window, tr("Secure connection failed"),
                             tr("The connection to %1 was rejected:\n%2")
                                 .arg(url, error.description()));
        localError.rejectCertificate();
        return;
    }
    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Security warning"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<b>%1</b> uses an invalid security certificate.").arg(url));
    box.setInformativeText(tr("%1\n\nYour connection is not private right now. "
                              "Visiting this site could allow attackers to "
                              "impersonate it or read your traffic.")
                               .arg(error.description()));
    QPushButton *proceed = box.addButton(tr("Proceed anyway"), QMessageBox::DestructiveRole);
    QPushButton *back = box.addButton(tr("Go back (recommended)"), QMessageBox::RejectRole);
    box.setDefaultButton(back);
    box.exec();
    if (box.clickedButton() == proceed)
        localError.acceptCertificate();
    else
        localError.rejectCertificate();
}

void WebPage::onPermissionRequested(QWebEnginePermission permission)
{
    const QUrl origin = permission.origin();
    const QString key = permissionKey(permission.permissionType());

    // Stored decision?
    const QString stored = m_window ? m_window->storedPermission(origin.toString(), key)
                                    : QString();
    if (stored == QLatin1String("allow")) {
        permission.grant();
        return;
    }
    if (stored == QLatin1String("deny")) {
        permission.deny();
        return;
    }

    const QString what = permissionDisplayName(permission.permissionType());
    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Permission request"));
    box.setText(tr("<b>%1</b> wants to use %2.")
                    .arg(origin.host().isEmpty() ? origin.toString() : origin.host(), what));
    QCheckBox *remember = new QCheckBox(tr("Remember for this site"), &box);
    box.setCheckBox(remember);
    QPushButton *allow = box.addButton(tr("Allow"), QMessageBox::YesRole);
    QPushButton *block = box.addButton(tr("Block"), QMessageBox::NoRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(block);
    box.exec();
    QAbstractButton *clicked = box.clickedButton();
    if (clicked == cancel) {
        permission.deny();
        return;
    }
    const bool granted = clicked == allow;
    if (remember->isChecked()) {
        m_window->storePermission(origin.toString(), key,
                                  granted ? QStringLiteral("allow") : QStringLiteral("deny"));
    }
    granted ? permission.grant() : permission.deny();
}

void WebPage::onFullScreenRequested(QWebEngineFullScreenRequest request)
{
    request.accept();
    emit fullScreenToggled(request.toggleOn());
}

void WebPage::onAuthenticationRequired(const QUrl &requestUrl, QAuthenticator *authenticator)
{
    QDialog dialog(m_window);
    dialog.setWindowTitle(tr("Authentication required"));
    QFormLayout *form = new QFormLayout(&dialog);
    const QString realm = authenticator->realm().isEmpty() ? requestUrl.host()
                                                           : authenticator->realm();
    form->addRow(new QLabel(tr("Site \"%1\" asks for authentication.").arg(realm), &dialog));
    QLineEdit *user = new QLineEdit(&dialog);
    QLineEdit *pass = new QLineEdit(&dialog);
    pass->setEchoMode(QLineEdit::Password);
    form->addRow(tr("User name:"), user);
    form->addRow(tr("Password:"), pass);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        authenticator->setUser(user->text());
        authenticator->setPassword(pass->text());
    } else {
        authenticator->setUser(QString());
        authenticator->setPassword(QString());
    }
}

void WebPage::onWindowCloseRequested()
{
    if (m_window)
        m_window->closeCurrentTab();
}

void WebPage::onLoadFinished(bool ok)
{
    // FAILED loads: (1) the engine may keep the previous document's
    // <title> (titleChanged never fires for the error page) — flag it so
    // the window title and tab label never announce a page the viewport
    // is not showing; (2) a failed navigation must never enter the
    // browsing history: "I typed it and it did not load" is not a visit,
    // and it must not become a Top Site either.
    m_titleStale = !ok;
    if (ok) {
        recordHistoryIfAllowed();
        m_lastRecordedUrl = url();
    }
    // DOM is ready: apply generic (domain-unrestricted) hiding selectors
    // for the classes/ids actually present on the page.
    applyGenericCosmeticFilters();
}

void WebPage::onTitleChanged(const QString &title)
{
    m_titleStale = false; // a fresh title belongs to the current document
    if (!m_lastRecordedUrl.isEmpty() && m_lastRecordedUrl == url()) {
        if (m_window && m_window->historyStore())
            m_window->historyStore()->updateTitleForUrl(url().toString(), title);
    }
}

void WebPage::recordHistoryIfAllowed()
{
    if (!m_window || !m_window->historyStore())
        return;
    if (profile() && profile()->isOffTheRecord())
        return; // private sessions never touch the history database
    const QUrl u = url();
    const QString scheme = u.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return; // nor://newtab, about:, downloads etc. are not history
    m_window->historyStore()->addVisit(u.toString(), title());
}

bool WebPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    // Command URLs raised by the new-tab page's JavaScript. They describe
    // an action, never a real document; executing it and refusing the
    // navigation keeps the page exactly where it is.
    if (isMainFrame
        && url.scheme() == QLatin1String("nor")
        && url.host() == QLatin1String("newtab")
        && url.path().startsWith(QLatin1String("/action/"))) {
        // Only the new-tab page itself may issue commands: a random site
        // cannot drive pin/remove just by linking to such a URL.
        const QUrl current = this->url();
        const bool fromNewTab = current.scheme() == QLatin1String("nor")
                                && current.host() == QLatin1String("newtab");
        if (!fromNewTab)
            return false;
        const bool stateChanged = handleNewTabAction(url);
        if (stateChanged) {
            // Re-render with fresh Top-Sites state. Queued: we are inside
            // the navigation callback.
            QTimer::singleShot(0, this, [this]() {
                if (auto *view = qobject_cast<QWebEngineView *>(parent()))
                    view->reload();
            });
        }
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

bool WebPage::handleNewTabAction(const QUrl &commandUrl)
{
    const QString action = commandUrl.path().mid(QLatin1String("/action/").size());
    QUrlQuery query(commandUrl);
    const QString urlParam = query.queryItemValue(QStringLiteral("url"), QUrl::FullyDecoded);
    const QString titleParam = query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded);
    const QString newUrlParam = query.queryItemValue(QStringLiteral("newurl"), QUrl::FullyDecoded);
    const QUrl target(urlParam);

    // Navigation commands work from any new-tab page (private included).
    if (action == QLatin1String("open-tab")) {
        if (m_window && target.isValid())
            m_window->openInNewForegroundTab(target);
        return false;
    }
    if (action == QLatin1String("open-window")) {
        if (m_window && target.isValid())
            m_window->openInNewWindow(target);
        return false;
    }

    // Customization actions are preferences, not browsing data: they run
    // from private new-tab pages too (a wallpaper choice is not history).
    if (action.startsWith(QLatin1String("bg-"))
        || action == QLatin1String("newtab-pref")
        || action == QLatin1String("newtab-reset"))
        return handleNewTabCustomization(commandUrl);

    // Mutating commands are refused outright in private sessions: a
    // private window must never add, remove or edit the user's real
    // Top-Sites list (the page hides the actions; this is the hard
    // guarantee behind them).
    if (profile() && profile()->isOffTheRecord())
        return false;

    BrowserApplication *app = m_window ? m_window->app() : nullptr;
    TopSitesStore *topSites = app ? app->topSites() : nullptr;
    if (!topSites || !target.isValid() || target.host().isEmpty())
        return false;

    if (action == QLatin1String("pin"))
        return topSites->pinSite(target, titleParam);
    if (action == QLatin1String("unpin"))
        return topSites->unpinSite(target);
    if (action == QLatin1String("remove"))
        return topSites->removeSite(target, titleParam);
    if (action == QLatin1String("edit")) {
        const QUrl newTarget(newUrlParam);
        if (!newTarget.isValid() || newTarget.host().isEmpty())
            return false;
        return topSites->editSite(target, newTarget, titleParam);
    }
    return false;
}

// New Tab customization actions. These are PREFERENCES (not browsing
// data), so unlike Top-Sites mutations they are allowed from a private
// window: picking a wallpaper never leaks or alters history.
bool WebPage::handleNewTabCustomization(const QUrl &commandUrl)
{
    const QString action = commandUrl.path().mid(QLatin1String("/action/").size());
    QUrlQuery query(commandUrl);
    BrowserApplication *app = m_window ? m_window->app() : nullptr;
    AppConfig *config = app ? app->config() : nullptr;
    if (!config)
        return false;

    // Live-applied preferences: persist, then push the full state back to
    // the page (norApplyPrefs) so even raw action URLs converge visually.
    if (action == QLatin1String("newtab-pref")) {
        if (query.hasQueryItem(QLatin1String("blur")))
            config->setNewTabBlurEnabled(
                query.queryItemValue(QLatin1String("blur")) == QLatin1String("1"));
        if (query.hasQueryItem(QLatin1String("topsites")))
            config->setNewTabShowTopSites(
                query.queryItemValue(QLatin1String("topsites")) == QLatin1String("1"));
        if (query.hasQueryItem(QLatin1String("opacity"))) {
            bool ok = false;
            const int opacity = query.queryItemValue(
                QLatin1String("opacity")).toInt(&ok);
            if (ok)
                config->setNewTabSearchOpacity(opacity);
        }
        pushNewTabPrefsToPage();
        return false; // no reload: live application keeps the page stable
    }

    if (action == QLatin1String("bg-set")) {
        const QString id = query.queryItemValue(QLatin1String("id"), QUrl::FullyDecoded);
        if (id.isEmpty()) {
            config->setNewTabBackgroundEnabled(false); // solid color mode
        } else {
            BackgroundStore *backgrounds = app->backgrounds();
            if (!backgrounds || !backgrounds->contains(id))
                return false;
            config->setNewTabBackgroundId(id);
            config->setNewTabBackgroundEnabled(true);
        }
        pushNewTabPrefsToPage();
        return false; // applied live through norApplyPrefs
    }

    if (action == QLatin1String("bg-random")) {
        BackgroundStore *backgrounds = app ? app->backgrounds() : nullptr;
        if (!backgrounds)
            return false;
        const QString id = backgrounds->randomId(config->newTabBackgroundId());
        if (id.isEmpty())
            return false;
        config->setNewTabBackgroundId(id);
        config->setNewTabBackgroundEnabled(true);
        return true; // the page cannot know which one was picked: reload
    }

    if (action == QLatin1String("bg-import")) {
        BackgroundStore *backgrounds = app ? app->backgrounds() : nullptr;
        if (!backgrounds || !m_window)
            return false;
        // QFileDialog over the window; smoke runs skip it via the property
        // below and hand the path in through the query instead.
        QString source = query.queryItemValue(QLatin1String("path"), QUrl::FullyDecoded);
        if (source.isEmpty()) {
            if (QCoreApplication::instance()->property("norNoDialogs").toBool())
                return false;
            source = QFileDialog::getOpenFileName(
                m_window, tr("Choose a background image"), QString(),
                tr("Images (*.jpg *.jpeg *.png *.bmp *.webp);;All files (*)"));
        }
        if (source.isEmpty())
            return false; // user cancelled
        QString error;
        const QString id = backgrounds->importFromFile(source, &error);
        if (id.isEmpty()) {
            qWarning("newtab: background import failed: %s", qPrintable(error));
            runJavaScript(QStringLiteral("norImportFailed();"));
            return false;
        }
        config->setNewTabBackgroundId(id);
        config->setNewTabBackgroundEnabled(true);
        return true; // new image: reload renders it
    }

    if (action == QLatin1String("newtab-reset")) {
        config->resetNewTabCustomization();
        return true; // everything may have changed: reload
    }
    return false;
}

// Pushes the persisted New Tab preferences to the page's norApplyPrefs()
// hook. Used after every customization command so the page and the config
// can never disagree — including when a command arrives without going
// through the panel UI (keyboard/automation/raw action URLs).
void WebPage::pushNewTabPrefsToPage()
{
    BrowserApplication *app = m_window ? m_window->app() : nullptr;
    AppConfig *config = app ? app->config() : nullptr;
    if (!config)
        return;
    BackgroundStore *backgrounds = app->backgrounds();
    QJsonObject p;
    const bool bgOn = config->newTabBackgroundEnabled() && backgrounds;
    QString id = config->newTabBackgroundId();
    if (bgOn && backgrounds->contains(id)) {
        p.insert(QStringLiteral("bgEnabled"), true);
        p.insert(QStringLiteral("bgUrl"),
                 QStringLiteral("nor://background/") + id);
        p.insert(QStringLiteral("bgDark"), backgrounds->entry(id).dark);
    } else {
        p.insert(QStringLiteral("bgEnabled"), false);
        p.insert(QStringLiteral("bgUrl"), QString());
        p.insert(QStringLiteral("bgDark"), true);
    }
    p.insert(QStringLiteral("blur"), config->newTabBlurEnabled());
    p.insert(QStringLiteral("searchAlpha"),
             config->newTabSearchOpacity() / 100.0);
    p.insert(QStringLiteral("showTopSites"), config->newTabShowTopSites());
    runJavaScript(QStringLiteral("norApplyPrefs(%1);").arg(
        QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Compact))));
}

QWebEnginePage *WebPage::createWindow(WebWindowType type)
{
    if (!m_window)
        return nullptr;
    switch (type) {
    case QWebEnginePage::WebBrowserWindow:
        return m_window->openInNewWindow();
    case QWebEnginePage::WebBrowserBackgroundTab:
        return m_window->openInNewBackgroundTab(QUrl());
    case QWebEnginePage::WebBrowserTab:
    case QWebEnginePage::WebDialog:
    default:
        return m_window->openInNewForegroundTab(QUrl());
    }
}

// ------------------------------------------------------ cosmetic filtering --

AdBlockClient *WebPage::adBlockClient() const
{
    return m_window ? m_window->app()->adBlock() : nullptr;
}

void WebPage::onUrlChanged(const QUrl &url)
{
    // Navigation to a new document: recompute the URL-specific hiding
    // script before the document is created.
    applyCosmeticFilters(url);
}

void WebPage::removeCosmeticScript(const QString &name)
{
    const QList<QWebEngineScript> found = scripts().find(name);
    for (const QWebEngineScript &s : found)
        scripts().remove(s);
}

void WebPage::applyCosmeticFilters(const QUrl &url)
{
    m_cosmeticExceptions.clear();
    m_cosmeticHideSelectors.clear();
    m_genericHide = false;
    removeCosmeticScript(QString::fromLatin1(kGenericScriptName));

    AdBlockClient *client = adBlockClient();
    if (!client || !client->cosmeticFilteringSupported())
        return;

    const CosmeticResources res = client->cosmeticResources(url);
    if (!res.isValid)
        return;

    m_cosmeticExceptions = res.exceptions;
    m_genericHide = res.genericHide;

    // Exception selectors re-allow elements; drop them from the hide set.
    QStringList selectors = res.hideSelectors;
    if (!res.exceptions.isEmpty()) {
        for (const QString &exception : res.exceptions)
            selectors.removeAll(exception);
    }
    m_cosmeticHideSelectors = selectors;

    removeCosmeticScript(QString::fromLatin1(kCosmeticScriptName));
    if (selectors.isEmpty())
        return;

    const QByteArray json = QJsonDocument(QJsonArray::fromStringList(selectors))
                                .toJson(QJsonDocument::Compact);
    QWebEngineScript script;
    script.setName(QString::fromLatin1(kCosmeticScriptName));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::ApplicationWorld);
    script.setRunsOnSubFrames(true);
    script.setSourceCode(QString::fromLatin1(kCosmeticScriptTemplate)
                             .arg(QString::fromUtf8(json)));
    scripts().insert(script);
}

void WebPage::applyGenericCosmeticFilters()
{
    AdBlockClient *client = adBlockClient();
    if (!client || !client->cosmeticFilteringSupported())
        return;

    // The URL-specific script runs at DocumentCreation — but on the very
    // first navigation the script is registered after the document was
    // already created, so enforce the specific selectors here too (this
    // late pass also carries the generic selectors, which need the DOM).
    const QStringList selectorsToEnforce = m_cosmeticHideSelectors;

    if (m_genericHide) {
        // The page opted out of generic hiding ($generichide): still
        // enforce the URL-specific selectors from the late pass.
        if (selectorsToEnforce.isEmpty())
            return;
        const QByteArray selectorJson =
            QJsonDocument(QJsonArray::fromStringList(selectorsToEnforce))
                .toJson(QJsonDocument::Compact);
        const QString injection = QStringLiteral(
            "(function(){try{if(document.getElementById('nor-cosmetic-late'))return;"
            "var s=document.createElement('style');"
            "s.id='nor-cosmetic-late';"
            "s.textContent=%1.join(',')+'{display:none!important;}';"
            "(document.head||document.documentElement).appendChild(s);"
            "}catch(e){}})()")
            .arg(QString::fromUtf8(selectorJson));
        runJavaScript(injection, QWebEngineScript::ApplicationWorld,
                      [](const QVariant &) {});
        return;
    }

    // Collect the classes and ids present on the page, then ask the
    // engine which generic "##" selectors apply to them.
    const QString probe = QStringLiteral(
        "(function(){try{"
        "var c={},i={},e,j,t;"
        "e=document.querySelectorAll('[class]');"
        "for(j=0;j<e.length&&j<5000;j++){t=e[j].className;"
        "if(typeof t==='string')t.split(/\\s+/).forEach(function(x){if(x)c[x]=1;});}"
        "e=document.querySelectorAll('[id]');"
        "for(j=0;j<e.length&&j<5000;j++){t=e[j].id;if(t)i[t]=1;}"
        "return JSON.stringify([Object.keys(c),Object.keys(i)]);"
        "}catch(err){return '[]';}})()");
    runJavaScript(probe, QWebEngineScript::ApplicationWorld,
                  [this, client, selectorsToEnforce](const QVariant &result) {
        const QString json = result.toString();
        QStringList selectors = selectorsToEnforce;
        if (!json.isEmpty() && json != QLatin1String("[]")) {
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isArray() && doc.array().size() == 2) {
                QStringList classes;
                for (const QJsonValue &v : doc.array().at(0).toArray())
                    classes.append(v.toString());
                QStringList ids;
                for (const QJsonValue &v : doc.array().at(1).toArray())
                    ids.append(v.toString());
                selectors += client->genericHideSelectors(classes, ids,
                                                          m_cosmeticExceptions);
            }
        }
        if (selectors.isEmpty())
            return;

        const QByteArray selectorJson =
            QJsonDocument(QJsonArray::fromStringList(selectors))
                .toJson(QJsonDocument::Compact);
        const QString injection = QStringLiteral(
            "(function(){try{if(document.getElementById('nor-cosmetic-late'))return;"
            "var s=document.createElement('style');"
            "s.id='nor-cosmetic-late';"
            "s.textContent=%1.join(',')+'{display:none!important;}';"
            "(document.head||document.documentElement).appendChild(s);"
            "}catch(e){}})()")
            .arg(QString::fromUtf8(selectorJson));
        runJavaScript(injection, QWebEngineScript::ApplicationWorld,
                      [](const QVariant &) {});
    });
}
