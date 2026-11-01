// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QRegion>

class QWidget;

// Real desktop translucency for the browser chrome, the honest way.
//
// What this does:
//  1. Detects an X11 compositor (owner of the _NET_WM_CM_S0 selection).
//     No compositor -> every helper below is a no-op and the theme paints
//     fully opaque surfaces (the "consistent semi-opaque fallback").
//  2. When supported, asks KWin (and any WM honouring the freedesktop
//     extension) to BLUR BEHIND specific regions of a window via the
//     _KDE_NET_WM_BLUR_BEHIND_REGION property — the same mechanism KDE
//     applications use. Only the small header strip and open menus ever
//     become translucent, never the page area and never the whole window.
//  3. Wayland / offscreen / no-compositor sessions are detected and skip
//     translucency entirely (no guessing, no artifacts).
//
// Escape hatch for troubleshooting: NOR_NO_TRANSLUCENCY=1.
//
// libxcb is resolved at RUNTIME (dlopen) so a pure-Wayland box without
// libxcb still runs; nothing is linked against X11 at build time.
namespace Translucency
{

// True once, cached: platform is X11 AND a compositor owns the CM selection
// AND the env escape hatch is unset.
bool isSupported();

// Ask the WM to blur behind `region` of widget `w` (window coordinates).
// An empty region means "whole window". No-op when unsupported.
// Call again whenever the geometry changes (resize, bookmarks-bar toggle).
void applyBlurBehind(QWidget *w, const QRegion &region);

// Whole-widget variant used for popups (menus).
void applyBlurBehindWhole(QWidget *w);

// Enable the argb visual on `w` (prerequisite for translucent painting).
// No-op when unsupported.
void enableTranslucency(QWidget *w);

} // namespace Translucency
