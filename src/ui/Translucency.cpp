#include "Translucency.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QLibrary>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include <QtGui/qguiapplication_platform.h>   // QNativeInterface::QX11Application
#include <cstdlib>                             // free()

// Minimal xcb declarations (we only dlopen libxcb and resolve a handful of
// symbols; no build-time X11 dependency).
using xcb_connection_t = struct xcb_connection_t;
using xcb_window_t = quint32;
using xcb_atom_t = quint32;

struct xcb_intern_atom_reply_t { quint8 response_type; quint8 pad0[5]; quint16 sequence; quint32 length; xcb_atom_t atom; };
struct xcb_intern_atom_cookie_t { unsigned int sequence; };
struct xcb_get_selection_owner_reply_t { quint8 response_type; quint8 pad0[5]; quint16 sequence; quint32 length; xcb_window_t owner; };
struct xcb_get_selection_owner_cookie_t { unsigned int sequence; };
struct xcb_void_cookie_t { unsigned int sequence; };

namespace {

struct XcbApi
{
    xcb_connection_t *(*connect)(const char *, int *);
    void (*disconnect)(xcb_connection_t *);
    xcb_intern_atom_cookie_t (*intern_atom)(xcb_connection_t *, quint8, quint16, const char *);
    xcb_intern_atom_reply_t *(*intern_atom_reply)(xcb_connection_t *, xcb_intern_atom_cookie_t, void *);
    xcb_get_selection_owner_cookie_t (*get_selection_owner)(xcb_connection_t *, xcb_atom_t);
    xcb_get_selection_owner_reply_t *(*get_selection_owner_reply)(xcb_connection_t *, xcb_get_selection_owner_cookie_t, void *);
    xcb_void_cookie_t (*change_property)(xcb_connection_t *, quint8, xcb_window_t, xcb_atom_t, xcb_atom_t, quint8, quint32, const void *);
    int (*flush)(xcb_connection_t *);
    int (*has_error)(const xcb_connection_t *);
};

bool resolveXcb(XcbApi &api)
{
    QLibrary lib(QStringLiteral("xcb"));
    if (!lib.load())
        return false;
    api.connect = reinterpret_cast<decltype(api.connect)>(lib.resolve("xcb_connect"));
    api.disconnect = reinterpret_cast<decltype(api.disconnect)>(lib.resolve("xcb_disconnect"));
    api.intern_atom = reinterpret_cast<decltype(api.intern_atom)>(lib.resolve("xcb_intern_atom"));
    api.intern_atom_reply = reinterpret_cast<decltype(api.intern_atom_reply)>(lib.resolve("xcb_intern_atom_reply"));
    api.get_selection_owner = reinterpret_cast<decltype(api.get_selection_owner)>(lib.resolve("xcb_get_selection_owner"));
    api.get_selection_owner_reply = reinterpret_cast<decltype(api.get_selection_owner_reply)>(lib.resolve("xcb_get_selection_owner_reply"));
    api.change_property = reinterpret_cast<decltype(api.change_property)>(lib.resolve("xcb_change_property"));
    api.flush = reinterpret_cast<decltype(api.flush)>(lib.resolve("xcb_flush"));
    api.has_error = reinterpret_cast<decltype(api.has_error)>(lib.resolve("xcb_connection_has_error"));
    return api.connect && api.disconnect && api.intern_atom && api.intern_atom_reply
           && api.get_selection_owner && api.get_selection_owner_reply && api.change_property
           && api.flush && api.has_error;
}

// The xcb connection Qt already uses (same screen, same connection) —
// obtained through the public QNativeInterface::QX11Application.
xcb_connection_t *qtXcbConnection()
{
    const auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

xcb_atom_t internAtom(XcbApi &api, xcb_connection_t *conn, const char *name)
{
    const xcb_intern_atom_cookie_t cookie =
        api.intern_atom(conn, /*only_if_exists=*/0, quint16(qstrlen(name)), name);
    xcb_intern_atom_reply_t *reply = api.intern_atom_reply(conn, cookie, nullptr);
    const xcb_atom_t atom = reply ? reply->atom : 0;
    if (reply)
        free(reply);
    return atom;
}

int s_supported = -1;   // -1 unknown, 0 no, 1 yes

} // namespace

namespace Translucency
{

bool isSupported()
{
    if (s_supported >= 0)
        return s_supported == 1;

    s_supported = 0;

    if (qEnvironmentVariableIsSet("NOR_NO_TRANSLUCENCY"))
        return false;

    const QString platform = QGuiApplication::platformName();
    if (platform != QLatin1String("xcb"))
        return false;   // Wayland/offscreen/etc: stay opaque, stay honest.

    XcbApi api{};
    if (!resolveXcb(api))
        return false;

    // Connect our own short-lived connection purely for the compositor
    // probe (Qt's connection stays untouched).
    xcb_connection_t *conn = api.connect(nullptr, 0);
    if (!conn || api.has_error(conn)) {
        if (conn)
            api.disconnect(conn);
        return false;
    }
    const xcb_atom_t cmAtom = internAtom(api, conn, "_NET_WM_CM_S0");
    bool compositor = false;
    if (cmAtom) {
        const auto cookie = api.get_selection_owner(conn, cmAtom);
        if (xcb_get_selection_owner_reply_t *reply =
                api.get_selection_owner_reply(conn, cookie, nullptr)) {
            compositor = reply->owner != 0;
            free(reply);
        }
    }
    api.disconnect(conn);

    if (compositor)
        s_supported = 1;
    return compositor;
}

void applyBlurBehind(QWidget *w, const QRegion &region)
{
    if (!isSupported() || !w)
        return;
    XcbApi api{};
    if (!resolveXcb(api))
        return;
    xcb_connection_t *conn = qtXcbConnection();
    if (!conn)
        return;
    const xcb_atom_t blurAtom = internAtom(api, conn, "_KDE_NET_WM_BLUR_BEHIND_REGION");
    const xcb_atom_t cardinalAtom = internAtom(api, conn, "CARDINAL");
    if (!blurAtom || !cardinalAtom)
        return;

    const WId winId = w->winId();
    if (!winId)
        return;

    // Region -> flat uint32 quad list {x,y,w,h}, window coordinates.
    QVector<quint32> rects;
    const auto list = region.rects();
    rects.reserve(int(list.size()) * 4);
    for (const QRect &r : list) {
        rects << quint32(r.x()) << quint32(r.y())
              << quint32(r.width()) << quint32(r.height());
    }
    constexpr quint8 kPropModeReplace = 0;   // XCB_PROP_MODE_REPLACE
    api.change_property(conn, kPropModeReplace, xcb_window_t(winId),
                        blurAtom, cardinalAtom, 32, quint32(rects.size()),
                        rects.constData());
    api.flush(conn);
}

void applyBlurBehindWhole(QWidget *w)
{
    applyBlurBehind(w, QRegion());
}

void enableTranslucency(QWidget *w)
{
    if (!isSupported() || !w)
        return;
    w->setAttribute(Qt::WA_TranslucentBackground, true);
}

} // namespace Translucency
