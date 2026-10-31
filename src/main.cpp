#include "BrowserApplication.h"
#include "LanguageManager.h"
#include "NewTabSchemeHandler.h"
#include "Paths.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QSettings>
#include <QUrl>

namespace {
// WebRTC exposes every local interface/IP to pages by default. Restricting
// the candidates to the default public interface removes the local-network
// IP leak without breaking ordinary calls. This must be decided BEFORE the
// WebEngine initializes, so it reads the setting straight from the ini file
// (same storage AppConfig uses; the key is duplicated there deliberately —
// keep both in sync).
void applyEarlyChromiumPrivacyFlags()
{
    QSettings settings;
    // Default is ON, including on first run before AppConfig has written
    // the key into the ini file.
    if (!settings.value(QStringLiteral("privacy/webrtcPublicIpOnly"), true).toBool())
        return;
    const QByteArray current = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    const QByteArray flag =
        QByteArrayLiteral("--webrtc-ip-handling-policy=default_public_interface_only");
    if (current.contains(flag))
        return;
    const QByteArray merged = current.isEmpty()
                                  ? flag
                                  : current + QByteArrayLiteral(" ") + flag;
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", merged);
}
} // namespace

int main(int argc, char *argv[])
{
    // Custom URL schemes must be registered before QApplication exists.
    NewTabSchemeHandler::registerNorScheme();

    // Chromium-level privacy flags must be set before the WebEngine spins
    // up (it initializes with QApplication).
    applyEarlyChromiumPrivacyFlags();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nor-browser"));
    QApplication::setOrganizationName(QStringLiteral("NOR"));
    QApplication::setApplicationDisplayName(QStringLiteral("NØR Browser"));
    QApplication::setApplicationVersion(QStringLiteral(NOR_VERSION));
    // Quit is managed by BrowserApplication (private windows may outlive
    // regular ones).
    QApplication::setQuitOnLastWindowClosed(false);

    // Bundled fonts keep the Arabic UI readable even on systems without
    // Arabic fonts (widgets pick them up via font fallback).
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansArabic-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoNaskhArabic-Regular.ttf"));

    // Deferred site-storage wipe requested via "Clear browsing data".
    // Runs before any profile exists (see Paths::applyPendingSiteStorageWipe).
    Paths::applyPendingSiteStorageWipe();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NØR Browser — private by design."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("url"),
                                 QStringLiteral("Address or search term to open."));
    QCommandLineOption privateOption(QStringList{ QStringLiteral("p"), QStringLiteral("private") },
                                      QStringLiteral("Start a private window."));
    QCommandLineOption smokeOption(QStringList{ QStringLiteral("smoke") },
                                   QStringLiteral("Run the navigation smoke test against a local test server base URL."),
                                   QStringLiteral("baseUrl"));
    QCommandLineOption smokePrivateOption(QStringList{ QStringLiteral("smoke-private") },
                                          QStringLiteral("Run the private-browsing smoke test."),
                                          QStringLiteral("baseUrl"));
    QCommandLineOption smokeHttpsOption(QStringList{ QStringLiteral("smoke-https") },
                                        QStringLiteral("Load one real HTTPS URL and report the result."),
                                        QStringLiteral("url"));
    QCommandLineOption smokeLanguageOption(QStringList{ QStringLiteral("smoke-language") },
                                           QStringLiteral("Test EN -> AR -> FR switching; saves screenshots into <dir>."),
                                           QStringLiteral("dir"));
    QCommandLineOption smokeDownloadOption(QStringList{ QStringLiteral("smoke-download") },
                                           QStringLiteral("Test the download flow against a local test server base URL."),
                                           QStringLiteral("baseUrl"));
    QCommandLineOption smokeCosmeticOption(QStringList{ QStringLiteral("smoke-cosmetic") },
                                          QStringLiteral("Test cosmetic ad filtering (element hiding) against a local test server base URL."),
                                          QStringLiteral("baseUrl"));
    QCommandLineOption smokeTlsBadOption(QStringList{ QStringLiteral("smoke-tls-bad") },
                                         QStringLiteral("A HTTPS URL with an invalid certificate must be rejected; a valid HTTPS URL must still load."),
                                         QStringLiteral("url"));
    QCommandLineOption smokeBlockingOption(QStringList{ QStringLiteral("smoke-blocking") },
                                           QStringLiteral("End-to-end ad/tracker blocking test: engine decisions + interceptor on a live page. Takes the local test-server base URL; pass EasyList/EasyPrivacy files via --smoke-easylist/--smoke-easyprivacy."),
                                           QStringLiteral("baseUrl"));
    QCommandLineOption smokeEasyListOption(QStringList{ QStringLiteral("smoke-easylist") },
                                           QStringLiteral("Path to a real easylist.txt for --smoke-blocking."),
                                           QStringLiteral("path"));
    QCommandLineOption smokeEasyPrivacyOption(QStringList{ QStringLiteral("smoke-easyprivacy") },
                                              QStringLiteral("Path to a real easyprivacy.txt for --smoke-blocking."),
                                              QStringLiteral("path"));
    QCommandLineOption smokePrivacyOption(QStringList{ QStringLiteral("smoke-privacy") },
                                          QStringLiteral("Privacy hardening test: DNT/GPC headers, unified User-Agent and third-party cookie blocking against a local test server base URL."),
                                          QStringLiteral("baseUrl"));
    QCommandLineOption smokeTopSitesOption(QStringList{ QStringLiteral("smoke-topsites") },
                                           QStringLiteral("Top Sites end-to-end test: empty-state, frecency ranking, pin/edit/remove, private isolation, clear-history, viewport (no status bar) and tab-switch flash. Takes the local test-server base URL; screenshots go to --screenshot."),
                                           QStringLiteral("baseUrl"));
    QCommandLineOption smokeChromeOption(QStringList{ QStringLiteral("smoke-chrome") },
                                         QStringLiteral("Chrome appearance matrix at 1366x768: new tab, many tabs, tab context menu, settings/downloads/bookmarks dialogs, fullscreen, AR/FR, resize, private window. Saves screenshots into <dir>."),
                                         QStringLiteral("dir"));
    QCommandLineOption smokeNewTabOption(QStringList{ QStringLiteral("smoke-newtab") },
                                         QStringLiteral("New Tab experience matrix at 1366x768: backgrounds (dark/light/busy/custom), blur on/off, search focus stability, customization panel, solid color, reset, AR/FR, and title-sync consistency. Saves screenshots into <dir>."),
                                         QStringLiteral("dir"));
    QCommandLineOption screenshotOption(QStringList{ QStringLiteral("screenshot") },
                                        QStringLiteral("Save a window screenshot (used with --smoke)."),
                                        QStringLiteral("path"));
    parser.addOptions({ privateOption, smokeOption, smokePrivateOption, smokeHttpsOption,
                        smokeLanguageOption, smokeDownloadOption, smokeCosmeticOption,
                        smokeTlsBadOption, smokeBlockingOption, smokeEasyListOption,
                        smokeEasyPrivacyOption, smokePrivacyOption, smokeTopSitesOption,
                        smokeChromeOption, smokeNewTabOption, screenshotOption });
    parser.process(app);

    Theme::apply();

    auto *nor = new BrowserApplication();

    // "Match my desktop theme": follow the freedesktop color-scheme
    // portal live (and re-style instantly when it flips).
    Theme::watchSystemScheme(nor);

    if (parser.isSet(smokeOption)) {
        const int rc = nor->runSmokeNavigation(parser.value(smokeOption),
                                               parser.value(screenshotOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokePrivateOption)) {
        const int rc = nor->runSmokePrivate(parser.value(smokePrivateOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeHttpsOption)) {
        const int rc = nor->runSmokeHttps(QUrl(parser.value(smokeHttpsOption)));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeDownloadOption)) {
        const int rc = nor->runSmokeDownload(parser.value(smokeDownloadOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeCosmeticOption)) {
        const int rc = nor->runSmokeCosmetic(parser.value(smokeCosmeticOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeTlsBadOption)) {
        const QUrl tlsUrl(parser.value(smokeTlsBadOption));
        const QUrl goodUrl = QUrl::fromUserInput(
            parser.value(smokeTlsBadOption).contains(QLatin1String("badssl"))
                ? QStringLiteral("https://example.com")
                : QStringLiteral("https://example.com"));
        const int rc = nor->runSmokeTlsBad(tlsUrl, goodUrl);
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeBlockingOption)) {
        const int rc = nor->runSmokeBlocking(parser.value(smokeBlockingOption),
                                             parser.value(smokeEasyListOption),
                                             parser.value(smokeEasyPrivacyOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokePrivacyOption)) {
        const int rc = nor->runSmokePrivacy(parser.value(smokePrivacyOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeLanguageOption)) {
        const int rc = nor->runSmokeLanguage(parser.value(smokeLanguageOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeTopSitesOption)) {
        const int rc = nor->runSmokeTopSites(parser.value(smokeTopSitesOption),
                                             parser.value(screenshotOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeChromeOption)) {
        const int rc = nor->runSmokeChrome(parser.value(smokeChromeOption));
        delete nor;
        return rc;
    }
    if (parser.isSet(smokeNewTabOption)) {
        const int rc = nor->runSmokeNewTab(parser.value(smokeNewTabOption));
        delete nor;
        return rc;
    }

    QUrl startUrl;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        startUrl = QUrl::fromUserInput(args.first());

    nor->initialize(startUrl, parser.isSet(privateOption));
    const int rc = app.exec();
    delete nor; // windows and profiles die in a safe order here
    return rc;
}
