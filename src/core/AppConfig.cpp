#include "AppConfig.h"

#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {
// QSettings treats '/' as a group separator, and origins look like
// "https://host". Encode them so each origin is exactly one group level.
QString encodeOrigin(const QString &origin)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(origin, QByteArrayLiteral("._-")));
}
QString decodeOrigin(const QString &encoded)
{
    return QUrl::fromPercentEncoding(encoded.toUtf8());
}
} // namespace

using namespace std::chrono_literals;

namespace {
constexpr const char *kKeyLanguage = "ui/language";
constexpr const char *kKeyHomePage = "startup/homepage";
constexpr const char *kKeyDownloadDir = "downloads/directory";
constexpr const char *kKeyDownloadAsk = "downloads/ask";
constexpr const char *kKeyAdBlockEnabled = "blocking/enabled";
constexpr const char *kKeyCustomLists = "blocking/customLists";
constexpr const char *kKeyAllowlist = "blocking/allowlist";
constexpr const char *kKeyListUrls = "blocking/listUrls";
constexpr const char *kKeyListAutoUpdate = "blocking/listAutoUpdate";
constexpr const char *kKeyListState = "blocking/listState";
constexpr const char *kKeySuggestions = "ui/addressBarSuggestions";
constexpr const char *kKeyDataSaver = "datasaver/enabled";
constexpr const char *kKeyDsBlockImages = "datasaver/blockImages";
constexpr const char *kKeyDsThirdPartyImages = "datasaver/blockThirdPartyImages";
constexpr const char *kKeyDsExceptions = "datasaver/exceptions";
constexpr const char *kKeyBlock3pCookies = "privacy/blockThirdPartyCookies";
constexpr const char *kKeySessionCookies = "privacy/sessionOnlyCookies";
constexpr const char *kKeyDnt = "privacy/sendDoNotTrack";
constexpr const char *kKeyGpc = "privacy/sendGlobalPrivacyControl";
constexpr const char *kKeySimplifiedUa = "privacy/simplifiedUserAgent";
constexpr const char *kKeyWebrtcPublicIp = "privacy/webrtcPublicIpOnly";
constexpr const char *kKeyEnginesJson = "search/engines";
constexpr const char *kKeyDefaultEngine = "search/default";
constexpr const char *kKeyBookmarksBar = "ui/bookmarksBar";
constexpr const char *kKeyWindowGeometry = "window/geometry";
// ---- New Tab customization (Brave-inspired backgrounds + glass) ----
constexpr const char *kKeyNtBgEnabled = "newtab/bgEnabled";
constexpr const char *kKeyNtBgId = "newtab/bgId";
constexpr const char *kKeyNtBlur = "newtab/blur";
constexpr const char *kKeyNtSearchOpacity = "newtab/searchOpacity";
constexpr const char *kKeyNtShowTopSites = "newtab/showTopSites";
} // namespace

AppConfig::AppConfig(QObject *parent)
    : QObject(parent)
{
    m_settings = new QSettings(this);
    initDefaults();
}

AppConfig::AppConfig(const QString &iniPath, QObject *parent)
    : QObject(parent)
{
    m_settings = new QSettings(iniPath, QSettings::IniFormat, this);
    initDefaults();
}

AppConfig::~AppConfig() = default;

void AppConfig::initDefaults()
{
    if (!m_settings->contains(kKeyLanguage))
        m_settings->setValue(kKeyLanguage, QStringLiteral("en"));
    if (!m_settings->contains(kKeyHomePage))
        m_settings->setValue(kKeyHomePage, QStringLiteral("nor://newtab"));
    if (!m_settings->contains(kKeyDownloadAsk))
        m_settings->setValue(kKeyDownloadAsk, true);
    if (!m_settings->contains(kKeyAdBlockEnabled))
        m_settings->setValue(kKeyAdBlockEnabled, true);
    if (!m_settings->contains(kKeyDataSaver))
        m_settings->setValue(kKeyDataSaver, false);
    if (!m_settings->contains(kKeyDsThirdPartyImages))
        m_settings->setValue(kKeyDsThirdPartyImages, true);
    if (!m_settings->contains(kKeyDsBlockImages))
        m_settings->setValue(kKeyDsBlockImages, true);
    if (!m_settings->contains(kKeyBookmarksBar))
        m_settings->setValue(kKeyBookmarksBar, true);
    if (!m_settings->contains(kKeyListUrls))
        m_settings->setValue(kKeyListUrls, QStringList{
            QStringLiteral("https://easylist.to/easylist/easylist.txt"),
            QStringLiteral("https://easylist.to/easylist/easyprivacy.txt")});
    if (!m_settings->contains(kKeyListAutoUpdate))
        m_settings->setValue(kKeyListAutoUpdate, true);
    if (!m_settings->contains(kKeySuggestions))
        m_settings->setValue(kKeySuggestions, true);
    // Privacy hardening defaults: third-party cookie blocking, DNT + GPC
    // signals, unified Chromium User-Agent and WebRTC IP minimization are
    // all ON by default; each is individually switchable in Settings.
    if (!m_settings->contains(kKeyBlock3pCookies))
        m_settings->setValue(kKeyBlock3pCookies, true);
    if (!m_settings->contains(kKeySessionCookies))
        m_settings->setValue(kKeySessionCookies, false);
    if (!m_settings->contains(kKeyDnt))
        m_settings->setValue(kKeyDnt, true);
    if (!m_settings->contains(kKeyGpc))
        m_settings->setValue(kKeyGpc, true);
    if (!m_settings->contains(kKeySimplifiedUa))
        m_settings->setValue(kKeySimplifiedUa, true);
    if (!m_settings->contains(kKeyWebrtcPublicIp))
        m_settings->setValue(kKeyWebrtcPublicIp, true);
    // New Tab customization defaults: calm graphite background image on,
    // blur on, medium glass, Top Sites visible.
    if (!m_settings->contains(kKeyNtBgEnabled))
        m_settings->setValue(kKeyNtBgEnabled, true);
    if (!m_settings->contains(kKeyNtBgId))
        m_settings->setValue(kKeyNtBgId, QStringLiteral("nor-graphite"));
    if (!m_settings->contains(kKeyNtBlur))
        m_settings->setValue(kKeyNtBlur, true);
    if (!m_settings->contains(kKeyNtSearchOpacity))
        m_settings->setValue(kKeyNtSearchOpacity, 55);
    if (!m_settings->contains(kKeyNtShowTopSites))
        m_settings->setValue(kKeyNtShowTopSites, true);
    m_settings->sync();
}

QString AppConfig::language() const { return m_settings->value(kKeyLanguage).toString(); }
void AppConfig::setLanguage(const QString &code) { m_settings->setValue(kKeyLanguage, code); emit changed(); }

QString AppConfig::homePage() const { return m_settings->value(kKeyHomePage).toString(); }
void AppConfig::setHomePage(const QString &url) { m_settings->setValue(kKeyHomePage, url); emit changed(); }

QStringList AppConfig::adBlockListUrls() const
{
    return m_settings->value(kKeyListUrls).toStringList();
}

void AppConfig::setAdBlockListUrls(const QStringList &urls)
{
    if (urls == adBlockListUrls())
        return;
    m_settings->setValue(kKeyListUrls, urls);
    emit changed();
}

bool AppConfig::adBlockAutoUpdate() const
{
    return m_settings->value(kKeyListAutoUpdate, true).toBool();
}

void AppConfig::setAdBlockAutoUpdate(bool enabled)
{
    if (enabled == adBlockAutoUpdate())
        return;
    m_settings->setValue(kKeyListAutoUpdate, enabled);
}

// JSON map: { url: { "etag": ..., "lastModified": ..., "lastCheck": epoch,
//                    "ok": bool, "error": ... } }
QString AppConfig::filterListState() const
{
    return m_settings->value(kKeyListState, QStringLiteral("{}")).toString();
}

void AppConfig::setFilterListState(const QString &json)
{
    m_settings->setValue(kKeyListState, json);
}

bool AppConfig::addressBarSuggestions() const
{
    return m_settings->value(kKeySuggestions, true).toBool();
}

void AppConfig::setAddressBarSuggestions(bool enabled)
{
    if (enabled == addressBarSuggestions())
        return;
    m_settings->setValue(kKeySuggestions, enabled);
}

QString AppConfig::downloadDirectory() const
{
    QString dir = m_settings->value(kKeyDownloadDir).toString();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return dir;
}
void AppConfig::setDownloadDirectory(const QString &dir) { m_settings->setValue(kKeyDownloadDir, dir); emit changed(); }

bool AppConfig::askWhereToSaveDownloads() const { return m_settings->value(kKeyDownloadAsk).toBool(); }
void AppConfig::setAskWhereToSaveDownloads(bool ask) { m_settings->setValue(kKeyDownloadAsk, ask); emit changed(); }

bool AppConfig::adBlockEnabled() const { return m_settings->value(kKeyAdBlockEnabled).toBool(); }
void AppConfig::setAdBlockEnabled(bool enabled) { m_settings->setValue(kKeyAdBlockEnabled, enabled); emit changed(); }

QStringList AppConfig::customFilterLists() const { return m_settings->value(kKeyCustomLists).toStringList(); }
void AppConfig::setCustomFilterLists(const QStringList &paths) { m_settings->setValue(kKeyCustomLists, paths); emit changed(); }

QStringList AppConfig::adBlockAllowlist() const { return m_settings->value(kKeyAllowlist).toStringList(); }
void AppConfig::setAdBlockAllowlist(const QStringList &hosts) { m_settings->setValue(kKeyAllowlist, hosts); emit changed(); }

bool AppConfig::dataSaverEnabled() const { return m_settings->value(kKeyDataSaver).toBool(); }
void AppConfig::setDataSaverEnabled(bool enabled) { m_settings->setValue(kKeyDataSaver, enabled); emit changed(); }

// Data Saver image blocking happens per network request (see
// RequestInterceptor), so exceptions apply per site.
bool AppConfig::dataSaverBlockImages() const
{
    return m_settings->value(kKeyDsBlockImages, true).toBool();
}
void AppConfig::setDataSaverBlockImages(bool block)
{
    m_settings->setValue(kKeyDsBlockImages, block);
    emit changed();
}

bool AppConfig::dataSaverBlockThirdPartyImages() const
{
    return m_settings->value(kKeyDsThirdPartyImages).toBool();
}
void AppConfig::setDataSaverBlockThirdPartyImages(bool block)
{
    m_settings->setValue(kKeyDsThirdPartyImages, block);
    emit changed();
}

QStringList AppConfig::dataSaverExceptions() const { return m_settings->value(kKeyDsExceptions).toStringList(); }
void AppConfig::setDataSaverExceptions(const QStringList &hosts) { m_settings->setValue(kKeyDsExceptions, hosts); emit changed(); }

bool AppConfig::blockThirdPartyCookies() const { return m_settings->value(kKeyBlock3pCookies, true).toBool(); }
void AppConfig::setBlockThirdPartyCookies(bool block)
{
    if (block == blockThirdPartyCookies())
        return;
    m_settings->setValue(kKeyBlock3pCookies, block);
    emit changed();
}

bool AppConfig::sessionOnlyCookies() const { return m_settings->value(kKeySessionCookies, false).toBool(); }
void AppConfig::setSessionOnlyCookies(bool sessionOnly)
{
    if (sessionOnly == sessionOnlyCookies())
        return;
    m_settings->setValue(kKeySessionCookies, sessionOnly);
    emit changed();
}

bool AppConfig::sendDoNotTrack() const { return m_settings->value(kKeyDnt, true).toBool(); }
void AppConfig::setSendDoNotTrack(bool send)
{
    if (send == sendDoNotTrack())
        return;
    m_settings->setValue(kKeyDnt, send);
    emit changed();
}

bool AppConfig::sendGlobalPrivacyControl() const { return m_settings->value(kKeyGpc, true).toBool(); }
void AppConfig::setSendGlobalPrivacyControl(bool send)
{
    if (send == sendGlobalPrivacyControl())
        return;
    m_settings->setValue(kKeyGpc, send);
    emit changed();
}

bool AppConfig::simplifiedUserAgent() const { return m_settings->value(kKeySimplifiedUa, true).toBool(); }
void AppConfig::setSimplifiedUserAgent(bool simplify)
{
    if (simplify == simplifiedUserAgent())
        return;
    m_settings->setValue(kKeySimplifiedUa, simplify);
    emit changed();
}

bool AppConfig::webrtcPublicIpOnly() const { return m_settings->value(kKeyWebrtcPublicIp, true).toBool(); }
void AppConfig::setWebrtcPublicIpOnly(bool restrict)
{
    if (restrict == webrtcPublicIpOnly())
        return;
    m_settings->setValue(kKeyWebrtcPublicIp, restrict);
    emit changed();
}

QString AppConfig::searchEnginesJson() const { return m_settings->value(kKeyEnginesJson).toString(); }
void AppConfig::setSearchEnginesJson(const QString &json) { m_settings->setValue(kKeyEnginesJson, json); emit changed(); }

QString AppConfig::defaultSearchEngine() const { return m_settings->value(kKeyDefaultEngine).toString(); }
void AppConfig::setDefaultSearchEngine(const QString &name) { m_settings->setValue(kKeyDefaultEngine, name); emit changed(); }

QString AppConfig::permission(const QString &origin, const QString &feature) const
{
    return m_settings->value(QStringLiteral("Permissions/%1/%2")
                                  .arg(encodeOrigin(origin), feature))
        .toString();
}
void AppConfig::setPermission(const QString &origin, const QString &feature, const QString &value)
{
    m_settings->setValue(QStringLiteral("Permissions/%1/%2")
                             .arg(encodeOrigin(origin), feature),
                         value);
    emit changed();
}
void AppConfig::removePermission(const QString &origin)
{
    m_settings->beginGroup(QStringLiteral("Permissions"));
    m_settings->remove(encodeOrigin(origin));
    m_settings->endGroup();
    emit changed();
}
QStringList AppConfig::permissionOrigins() const
{
    m_settings->beginGroup(QStringLiteral("Permissions"));
    const QStringList raw = m_settings->childGroups();
    m_settings->endGroup();
    QStringList origins;
    origins.reserve(raw.size());
    for (const QString &r : raw)
        origins.append(decodeOrigin(r));
    return origins;
}

bool AppConfig::bookmarksBarVisible() const { return m_settings->value(kKeyBookmarksBar).toBool(); }
void AppConfig::setBookmarksBarVisible(bool visible) { m_settings->setValue(kKeyBookmarksBar, visible); emit changed(); }

QString AppConfig::uiTheme() const
{
    const QString v = m_settings->value(QStringLiteral("ui/theme")).toString();
    return (v == QLatin1String("dark") || v == QLatin1String("light")) ? v
                                                                       : QStringLiteral("system");
}
void AppConfig::setUiTheme(const QString &theme)
{
    const QString v = (theme == QLatin1String("dark") || theme == QLatin1String("light"))
                          ? theme : QStringLiteral("system");
    m_settings->setValue(QStringLiteral("ui/theme"), v);
    emit changed();
}

QByteArray AppConfig::windowGeometry() const { return m_settings->value(kKeyWindowGeometry).toByteArray(); }
void AppConfig::setWindowGeometry(const QByteArray &geometry) { m_settings->setValue(kKeyWindowGeometry, geometry); emit changed(); }

// --------------------------------------------------- New Tab customization --

bool AppConfig::newTabBackgroundEnabled() const { return m_settings->value(kKeyNtBgEnabled, true).toBool(); }
void AppConfig::setNewTabBackgroundEnabled(bool enabled) { m_settings->setValue(kKeyNtBgEnabled, enabled); emit newTabCustomizationChanged(); }

QString AppConfig::newTabBackgroundId() const
{
    return m_settings->value(kKeyNtBgId, QStringLiteral("nor-graphite")).toString();
}
void AppConfig::setNewTabBackgroundId(const QString &id) { m_settings->setValue(kKeyNtBgId, id); emit newTabCustomizationChanged(); }

bool AppConfig::newTabBlurEnabled() const { return m_settings->value(kKeyNtBlur, true).toBool(); }
void AppConfig::setNewTabBlurEnabled(bool enabled) { m_settings->setValue(kKeyNtBlur, enabled); emit newTabCustomizationChanged(); }

int AppConfig::newTabSearchOpacity() const
{
    // Clamped so a corrupt value can never make the field unreadable.
    return qBound(30, m_settings->value(kKeyNtSearchOpacity, 55).toInt(), 90);
}
void AppConfig::setNewTabSearchOpacity(int percent)
{
    m_settings->setValue(kKeyNtSearchOpacity, qBound(30, percent, 90));
    emit newTabCustomizationChanged();
}

bool AppConfig::newTabShowTopSites() const { return m_settings->value(kKeyNtShowTopSites, true).toBool(); }
void AppConfig::setNewTabShowTopSites(bool show) { m_settings->setValue(kKeyNtShowTopSites, show); emit newTabCustomizationChanged(); }

void AppConfig::resetNewTabCustomization()
{
    m_settings->setValue(kKeyNtBgEnabled, true);
    m_settings->setValue(kKeyNtBgId, QStringLiteral("nor-graphite"));
    m_settings->setValue(kKeyNtBlur, true);
    m_settings->setValue(kKeyNtSearchOpacity, 55);
    m_settings->setValue(kKeyNtShowTopSites, true);
    emit newTabCustomizationChanged();
}

void AppConfig::sync() { m_settings->sync(); }
QString AppConfig::filePath() const { return m_settings->fileName(); }
