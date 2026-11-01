// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

class QGuiApplication;

// ---------------------------------------------------------------------------
// NØR design system — "Calm graphite, one violet pulse."
//
// Every color, metric and radius used by the browser chrome lives HERE as a
// design token. The QSS shipped in resources/style/chrome.qss.in is a
// TEMPLATE: Theme::applyStyleSheet() substitutes %TOKEN% placeholders with
// the values below, so Dark and Light are guaranteed to stay structurally
// identical and only ever differ in values (single source of truth).
//
// The new-tab page receives the same palette through placeholders injected
// by NewTabSchemeHandler, so page and chrome always match.
//
// Rules encoded by this system:
//   * Neutral graphite surfaces (no purple-tinted grays).
//   * ONE accent (NØR violet) reserved for: focus, selection, the brand
//     mark. It must never paint large fills.
//   * Hierarchy through elevation (window < header < elevated < surface),
//     not through borders and cards.
//   * Hover/active states are white/black overlays, never new colors.
// ---------------------------------------------------------------------------

namespace Theme
{

// ---- color tokens ---------------------------------------------------------
struct Palette
{
    // Base surfaces (elevation order).
    QColor window;      // deepest: page surroundings, crash overlay
    QColor header;      // unified tab+navigation rows
    QColor elevated;    // menus, popups, dialogs
    QColor surface;     // input fields, search box

    // Interaction overlays (white for dark, black for light).
    QColor hoverOverlay;
    QColor activeOverlay;

    // Text.
    QColor text;
    QColor textDim;     // secondary labels, tab titles (inactive)
    QColor textMuted;   // placeholders, hints, meta

    // Lines.
    QColor line;        // hairline separators
    QColor lineStrong;  // control borders on hover/focus

    // The single accent + its tints.
    QColor accent;
    QColor accentHover;
    QColor accentDim;   // translucent accent: focus rings, subtle fills
    QColor accentText;  // accent-readable text on surfaces

    // Rare semantic colors (success/danger only where they mean something).
    QColor danger;

    // Icon stroke color (IconProvider recolors every SVG with this).
    QColor icon;
    QColor iconActive;

    bool dark = true;
};

// ---- metrics (heights in px; scaled by the platform device ratio) ---------
namespace Metrics
{
constexpr int TabRowHeight = 38;        // one tab strip incl. padding
constexpr int NavRowHeight = 46;        // navigation row incl. padding
constexpr int BookmarksRowHeight = 34;
constexpr int ControlHeight = 32;       // buttons / address field content
constexpr int IconSize = 18;            // toolbar icons
constexpr int TabIconSize = 16;
constexpr int TabMinWidth = 92;
constexpr int TabMaxWidth = 220;
constexpr int RadiusControl = 6;
constexpr int RadiusField = 8;
constexpr int RadiusMenu = 10;
}

// ---- scheme ----------------------------------------------------------------
enum class Scheme { System, Dark, Light };
QString schemeKey();                    // "system" | "dark" | "light"
void setSchemeKey(const QString &key);  // persists + re-applies live
Scheme scheme();                        // resolved (System -> effective)
bool isDark();
const Palette &palette();

// Effective page background: every QWebEnginePage is painted with this so a
// tab switch never flashes white (Chromium paints the page background
// before the first composited frame). Matches the new-tab background.
QColor webBackground();

// Load fonts + apply the generated stylesheet app-wide. Safe to call again
// (theme switch): icon caches must be cleared by the caller.
void apply();

// Regenerate and set the stylesheet only.
void applyStyleSheet();

// Watch the platform color scheme (System mode) and re-apply on change.
void watchSystemScheme(QObject *context);

} // namespace Theme
