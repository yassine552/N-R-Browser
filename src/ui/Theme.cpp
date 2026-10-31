#include "Theme.h"

#include "Translucency.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QSettings>
#include <QStyleHints>
#include <QGuiApplication>

// Settings key duplicated from AppConfig (which owns the typed accessors).
// Keep both in sync — see applyEarlyChromiumPrivacyFlags() for the same
// deliberate pattern: Theme must resolve the scheme BEFORE any
// BrowserApplication/AppConfig exists.
static const char *kSchemeKey = "ui/theme";

namespace {
constexpr qreal kHeaderAlpha = 0.96;   // translucency when a compositor runs

Theme::Palette makeDark()
{
    Theme::Palette p;
    p.dark = true;
    // Neutral graphite ramp — no purple tint anywhere.
    p.window      = QColor(0x17, 0x18, 0x1b);
    p.header      = QColor(0x1f, 0x20, 0x23);
    p.elevated    = QColor(0x27, 0x29, 0x2d);
    p.surface     = QColor(0x2b, 0x2d, 0x31);
    p.hoverOverlay   = QColor(255, 255, 255, 14);   // ~5.5%
    p.activeOverlay  = QColor(255, 255, 255, 23);   // ~9%
    p.text        = QColor(0xe9, 0xea, 0xee);
    p.textDim     = QColor(0xa3, 0xa7, 0xaf);
    p.textMuted   = QColor(0x6d, 0x71, 0x78);
    p.line        = QColor(255, 255, 255, 20);      // ~8%
    p.lineStrong  = QColor(255, 255, 255, 36);      // ~14%
    // The one accent: NØR violet.
    p.accent      = QColor(0x8b, 0x5c, 0xf6);
    p.accentHover = QColor(0x9b, 0x76, 0xf8);
    p.accentDim   = QColor(139, 92, 246, 82);       // ~32%
    p.accentText  = QColor(0xc3, 0xb1, 0xff);
    p.danger      = QColor(0xe0, 0x6c, 0x75);
    p.icon        = QColor(0xc5, 0xc9, 0xd1);
    p.iconActive  = QColor(0xf1, 0xf2, 0xf6);
    return p;
}

Theme::Palette makeLight()
{
    Theme::Palette p;
    p.dark = false;
    p.window      = QColor(0xf4, 0xf4, 0xf6);
    p.header      = QColor(0xff, 0xff, 0xff);
    p.elevated    = QColor(0xff, 0xff, 0xff);
    p.surface     = QColor(0xf0, 0xf0, 0xf3);
    p.hoverOverlay   = QColor(0, 0, 0, 12);         // ~4.7%
    p.activeOverlay  = QColor(0, 0, 0, 21);         // ~8%
    p.text        = QColor(0x1d, 0x1e, 0x22);
    p.textDim     = QColor(0x4c, 0x4f, 0x56);
    p.textMuted   = QColor(0x84, 0x86, 0x8c);
    p.line        = QColor(0, 0, 0, 26);            // ~10%
    p.lineStrong  = QColor(0, 0, 0, 41);            // ~16%
    // Deeper violet keeps AA contrast on white surfaces.
    p.accent      = QColor(0x74, 0x42, 0xe8);
    p.accentHover = QColor(0x63, 0x36, 0xd2);
    p.accentDim   = QColor(116, 66, 232, 64);
    p.accentText  = QColor(0x63, 0x36, 0xd2);
    p.danger      = QColor(0xc6, 0x38, 0x3f);
    p.icon        = QColor(0x45, 0x48, 0x4e);
    p.iconActive  = QColor(0x1d, 0x1e, 0x22);
    return p;
}

QString rgba(const QColor &c, qreal alpha = -1.0)
{
    // Negative alpha = "use the color's own alpha" — palettes carry
    // translucency IN the QColor (hover overlays, hairlines). The old
    // default (1.0) silently stomped it, rendering every line and hover
    // overlay OPAQUE: the "bright white borders" this fixes.
    QColor t = c;
    if (alpha >= 0.0)
        t.setAlphaF(alpha);
    return t.name(QColor::HexArgb);     // #AARRGGBB, valid in QSS
}

QString argb(const QColor &c, qreal alpha = -1.0)
{
    QColor t = c;
    if (alpha >= 0.0)
        t.setAlphaF(alpha);
    // QSS rgba() syntax keeps templates readable for translucency values.
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(t.red()).arg(t.green()).arg(t.blue())
        .arg(t.alphaF(), 0, 'f', 3);
}
} // namespace

namespace Theme
{

namespace {
Palette s_dark = makeDark();
Palette s_light = makeLight();
Scheme s_forced = Scheme::System;
bool s_resolved = false;
} // namespace

QString schemeKey()
{
    QSettings settings;
    const QString key = settings.value(QLatin1String(kSchemeKey),
                                       QStringLiteral("system")).toString();
    return key;
}

static Scheme resolveForced()
{
    const QString key = schemeKey().toLower();
    if (key == QLatin1String("dark"))
        return Scheme::Dark;
    if (key == QLatin1String("light"))
        return Scheme::Light;
    return Scheme::System;
}

Scheme scheme()
{
    if (!s_resolved) {
        s_forced = resolveForced();
        s_resolved = true;
    }
    if (s_forced != Scheme::System)
        return s_forced;
    // Follow the platform (freedesktop color-scheme portal via Qt).
    const Qt::ColorScheme platform = QGuiApplication::styleHints()->colorScheme();
    return platform == Qt::ColorScheme::Light ? Scheme::Light : Scheme::Dark;
}

bool isDark() { return scheme() == Scheme::Dark; }

const Palette &palette() { return isDark() ? s_dark : s_light; }

QColor webBackground()
{
    // The page canvas sits one step deeper than the header so the content
    // area reads as "inside" the chrome.
    return palette().window;
}

void setSchemeKey(const QString &key)
{
    QSettings settings;
    settings.setValue(QLatin1String(kSchemeKey), key);
    settings.sync();
    s_forced = resolveForced();
    s_resolved = true;
    apply();            // live re-style of every window
}

void applyStyleSheet()
{
    const Palette &p = palette();

    // Translucency: only when a real compositor runs (X11/KWin etc.) and
    // the user has not opted out. The header then blends ~4% into the
    // desktop and KWin blurs behind it (see Translucency::applyBlurBehind).
    const bool glass = Translucency::isSupported();
    const QString headerBg = glass ? argb(p.header, kHeaderAlpha)
                                   : rgba(p.header);
    const QString windowBg = glass ? argb(p.window, kHeaderAlpha)
                                   : rgba(p.window);
    const QString elevatedBg = glass ? argb(p.elevated, 0.97) : rgba(p.elevated);

    QFile tpl(QStringLiteral(":/style/chrome.qss.in"));
    if (!tpl.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Theme: chrome.qss.in missing from resources");
        return;
    }
    QString qss = QString::fromUtf8(tpl.readAll());

    // --- surfaces ---------------------------------------------------------
    qss.replace("%WINDOW%", windowBg);
    qss.replace("%HEADER%", headerBg);
    qss.replace("%ELEVATED%", elevatedBg);
    qss.replace("%SURFACE%", rgba(p.surface));
    qss.replace("%SURFACE_LIGHTER%", rgba(p.surface.lighter(106)));
    qss.replace("%HOVER%", argb(p.hoverOverlay));
    qss.replace("%ACTIVE%", argb(p.activeOverlay));
    // --- text ---------------------------------------------------------------
    qss.replace("%TEXT%", rgba(p.text));
    qss.replace("%TEXT_DIM%", rgba(p.textDim));
    qss.replace("%TEXT_MUTED%", rgba(p.textMuted));
    // --- lines --------------------------------------------------------------
    qss.replace("%LINE%", argb(p.line));
    qss.replace("%LINE_STRONG%", argb(p.lineStrong));
    qss.replace("%BORDER_INPUT%", argb(p.lineStrong));
    // Resting border for input FIELDS: visibly softer than LINE so the
    // address bar never reads as a bright frame; focus switches it to the
    // accent (see QLineEdit#addressField in chrome.qss.in).
    {
        QColor fieldLine(p.line.red(), p.line.green(), p.line.blue(),
                         qMax(8, int(p.line.alpha() * 0.65)));
        qss.replace("%FIELD_LINE%", argb(fieldLine));
    }
    // --- accent ---------------------------------------------------------------
    qss.replace("%ACCENT%", rgba(p.accent));
    qss.replace("%ACCENT_HOVER%", rgba(p.accentHover));
    qss.replace("%ACCENT_DIM%", argb(p.accentDim));
    qss.replace("%ACCENT_TEXT%", rgba(p.accentText));
    qss.replace("%DANGER%", rgba(p.danger));
    // --- selection --------------------------------------------------------------
    qss.replace("%SEL_BG%", argb(p.accentDim));
    qss.replace("%SEL_FG%", rgba(p.text));
    // --- metrics -----------------------------------------------------------------
    qss.replace("%TAB_ROW%", QString::number(Metrics::TabRowHeight));
    qss.replace("%NAV_ROW%", QString::number(Metrics::NavRowHeight));
    qss.replace("%CTRL_H%", QString::number(Metrics::ControlHeight));
    qss.replace("%ICON%", QString::number(Metrics::IconSize));
    qss.replace("%R_CTRL%", QString::number(Metrics::RadiusControl));
    qss.replace("%R_FIELD%", QString::number(Metrics::RadiusField));
    qss.replace("%R_MENU%", QString::number(Metrics::RadiusMenu));
    qss.replace("%TAB_MIN_W%", QString::number(Metrics::TabMinWidth));
    qss.replace("%TAB_MAX_W%", QString::number(Metrics::TabMaxWidth));
    // --- private mode tint (subtle, Firefox-like) ----------------------------------
    const QColor privTint = isDark() ? QColor(0x23, 0x1f, 0x2e) : QColor(0xf3, 0xee, 0xfc);
    qss.replace("%PRIV_HEADER%", glass ? argb(privTint, kHeaderAlpha) : rgba(privTint));

    qApp->setStyleSheet(qss);
}

void apply()
{
    applyStyleSheet();

    // One family, clear in EN/FR/AR (Noto Sans + Arabic fallback bundled).
    QFont font = qApp->font();
    font.setFamilies({ QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Arabic"),
                       QStringLiteral("Noto Sans SC"), QStringLiteral("DejaVu Sans"),
                       QStringLiteral("Segoe UI") });
    font.setPointSizeF(9.5);
    qApp->setFont(font);
}

void watchSystemScheme(QObject *context)
{
    if (!context)
        return;
    QObject::connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged,
                     context, []() { apply(); });
}

} // namespace Theme
