#include "RequestInterceptor.h"

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "FilterRule.h"

using namespace std::chrono_literals;

RequestInterceptor::RequestInterceptor(AppConfig &config, AdBlockClient &client,
                                       QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
    , m_config(config)
    , m_client(client)
{
    refreshPolicy();
}

void RequestInterceptor::refreshPolicy()
{
    // GUI thread only (see class comment for the threading contract).
    Policy p;
    p.dnt = m_config.sendDoNotTrack();
    p.gpc = m_config.sendGlobalPrivacyControl();
    p.dataSaverEnabled = m_config.dataSaverEnabled();
    p.dsBlockImages = m_config.dataSaverBlockImages();
    p.dsBlockThirdPartyImages = m_config.dataSaverBlockThirdPartyImages();
    p.dsExceptions = m_config.dataSaverExceptions();
    {
        QMutexLocker locker(&m_policyMutex);
        m_policy = p;
    }
}

quint32 RequestInterceptor::mapResourceType(QWebEngineUrlRequestInfo::ResourceType type)
{
    using RT = QWebEngineUrlRequestInfo::ResourceType;
    switch (type) {
    case RT::ResourceTypeMainFrame:
    case RT::ResourceTypeNavigationPreloadMainFrame:
        return FilterRule::TypeDocument;
    case RT::ResourceTypeSubFrame:
    case RT::ResourceTypeNavigationPreloadSubFrame:
        return FilterRule::TypeSubdocument;
    case RT::ResourceTypeStylesheet:
        return FilterRule::TypeStylesheet;
    case RT::ResourceTypeScript:
        return FilterRule::TypeScript;
    case RT::ResourceTypeImage:
    case RT::ResourceTypeFavicon:
        return FilterRule::TypeImage;
    case RT::ResourceTypeFontResource:
        return FilterRule::TypeFont;
    case RT::ResourceTypeMedia:
        return FilterRule::TypeMedia;
    case RT::ResourceTypeXhr:
        return FilterRule::TypeXmlHttpRequest;
    case RT::ResourceTypePing:
    case RT::ResourceTypeCspReport:
        return FilterRule::TypePing;
    case RT::ResourceTypeWebSocket:
        return FilterRule::TypeWebSocket;
    case RT::ResourceTypeObject:
    case RT::ResourceTypePluginResource:
        return FilterRule::TypeObject;
    default:
        return FilterRule::TypeOther;
    }
}

bool RequestInterceptor::dataSaverBlocksImage(const QUrl &requestUrl,
                                             const QUrl &firstPartyUrl,
                                             bool dataSaverEnabled, bool siteExcepted,
                                             bool blockAllImages,
                                             bool blockThirdPartyImages)
{
    if (!dataSaverEnabled || siteExcepted)
        return false;
    if (blockAllImages)
        return true;
    if (!blockThirdPartyImages)
        return false;
    return FilterRule::isThirdParty(requestUrl.host().toLower(),
                                    firstPartyUrl.host().toLower());
}

void RequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const QUrl requestUrl = info.requestUrl();
    const QUrl firstParty = info.firstPartyUrl();
    const quint32 type = mapResourceType(info.resourceType());

    // Snapshot the privacy policy once per request (thread-safe copy).
    Policy policy;
    {
        QMutexLocker locker(&m_policyMutex);
        policy = m_policy;
    }

    // --- Privacy signals (DNT / Global Privacy Control) ---------------------
    // Sent on every http/https request (subresources included) when enabled.
    // These are honest opt-out signals, not anonymity tools: they identify a
    // privacy-conscious configuration, exactly like Firefox's DNT does.
    const QString scheme = requestUrl.scheme();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (policy.dnt)
            info.setHttpHeader(QByteArrayLiteral("DNT"), QByteArrayLiteral("1"));
        if (policy.gpc)
            info.setHttpHeader(QByteArrayLiteral("Sec-GPC"), QByteArrayLiteral("1"));
    }

    // --- Security policy ---------------------------------------------------
    // Block file:// main-frame navigations initiated by web content.
    if ((type & FilterRule::TypeDocument)
        && requestUrl.scheme() == QLatin1String("file")) {
        const QString fpScheme = firstParty.scheme();
        if (fpScheme == QLatin1String("http") || fpScheme == QLatin1String("https")
            || fpScheme == QLatin1String("nor")) {
            info.block(true);
            return;
        }
    }

    // --- Privacy filtering ---------------------------------------------------
    const FilterDecision decision = m_client.decide(requestUrl, firstParty, type);
    if (decision.block) {
        info.block(true);
        // Attribute the block to the site being visited (first-party host),
        // so per-site statistics stay meaningful; the session total is
        // maintained inside the client (thread-safe).
        const QString siteHost = firstParty.host().toLower();
        if (!siteHost.isEmpty())
            m_client.noteBlockedForSite(siteHost);
        else
            m_client.noteBlocked();
        emit blockedRequest(requestUrl.host());
        return;
    }

    // --- Data Saver -----------------------------------------------------------
    // Image blocking is per request (NOT the profile-wide AutoLoadImages
    // attribute): per-site exceptions actually exempt a site now.
    if (policy.dataSaverEnabled) {
        const QString siteHost = firstParty.host().toLower();
        bool excepted = false;
        for (const QString &allowed : policy.dsExceptions) {
            if (siteHost == allowed || siteHost.endsWith(QLatin1Char('.') + allowed)) {
                excepted = true;
                break;
            }
        }
        if (!excepted) {
            // Speculative prefetches and pings are pure background traffic.
            using RT = QWebEngineUrlRequestInfo::ResourceType;
            if (info.resourceType() == RT::ResourceTypePrefetch
                || info.resourceType() == RT::ResourceTypePing) {
                info.block(true);
                return;
            }
            // Optional image blocking with working per-site exceptions.
            if ((type & FilterRule::TypeImage)
                && dataSaverBlocksImage(requestUrl, firstParty, true, false,
                                        policy.dsBlockImages,
                                        policy.dsBlockThirdPartyImages)) {
                info.block(true);
                return;
            }
        }
    }
}
