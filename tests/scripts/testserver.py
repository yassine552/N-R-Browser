#!/usr/bin/env python3
"""Deterministic local test server for NØR Browser smoke tests.

Serves simple pages with predictable titles and a redirect. The browser's
--smoke* modes point at this server (default http://127.0.0.1:8377).

Usage: python3 tests/scripts/testserver.py [port]
"""
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

STYLE = """
<style>
 body { font-family: sans-serif; background: #14121a; color: #e8e4f0;
        padding: 40px; }
 a { color: #8b5cf6; }
</style>
"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # Deterministic ETags for the conditional-request (304) tests.
    # Padding keeps the body above the 512-byte plausibility threshold.
    FILTER_LIST = (
        "! Title: NOR test filter list\n"
        "! Version: 1\n"
        "||tracker.example^\n"
        "||ads.example.com^$third-party\n"
        "@@||good.example^\n"
        "test.example##.ad-banner\n"
        "##.generic-ad\n"
    ) + "".join(
        f"||padding{i}.example.net^$script\n" for i in range(120)
    )
    FILTER_LIST_ETAG = '"nor-test-list-v1"'

    def log_message(self, fmt, *args):  # quiet
        pass

    def _send(self, body: str, code: int = 200, location: str = None):
        payload = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        if location:
            self.send_header("Location", location)
        self.end_headers()
        self.wfile.write(payload)

    def _send_filter_list(self, body: str, etag: str):
        payload = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("ETag", etag)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/hello":
            self._send(
                f"<html><head><title>NOR Smoke Hello</title>{STYLE}</head>"
                "<body><h1>It works!</h1>"
                "<p>This page was served by the local NØR test server.</p>"
                "</body></html>")
        elif path == "/links":
            self._send(
                f"<html><head><title>NOR Smoke Links</title>{STYLE}</head>"
                "<body><h1>Links</h1>"
                '<p><a id="target" href="/target">Go to target page</a></p>'
                '<p><a href="/hello">Back to hello</a></p>'
                '<p><a id="dl" href="/download" download>Download a file</a></p>'
                "</body></html>")
        elif path == "/target":
            self._send(
                f"<html><head><title>NOR Smoke Target</title>{STYLE}</head>"
                "<body><h1>Target reached</h1></body></html>")
        elif path == "/download":
            payload = b"NOR smoke download payload v1\n"
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Disposition",
                             'attachment; filename="nor-smoke-file.txt"')
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/redirect":
            self._send("redirecting", code=302, location="/target")
        elif path == "/blocked-tracker":
            # Pretends to be a tracker host for interceptor tests.
            self._send("<html><body>tracker</body></html>")
        elif path == "/filterlist/good":
            # Conditional GET support: answer 304 when the ETag matches.
            if self.headers.get("If-None-Match") == self.FILTER_LIST_ETAG:
                self.send_response(304)
                self.send_header("ETag", self.FILTER_LIST_ETAG)
                self.end_headers()
                return
            self._send_filter_list(self.FILTER_LIST, self.FILTER_LIST_ETAG)
        elif path == "/filterlist/corrupt":
            # Truncated binary garbage: must never replace a cached list.
            payload = bytes(range(256)) * 8
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/filterlist/html-error":
            # An HTML error page dressed up as a list.
            self._send_filter_list(
                "<html><head><title>503 Service Unavailable</title></head>"
                "<body><h1>503</h1></body></html>", '"error-page"')
        elif path == "/cosmetic":
            # Page with ad-like elements for cosmetic-filter tests.
            self._send(
                f"<html><head><title>NOR Smoke Cosmetic</title>{STYLE}</head>"
                "<body>"
                '<div class="ad-banner">AD BANNER VISIBLE</div>'
                '<div class="generic-ad">GENERIC AD VISIBLE</div>'
                '<div class="content">REAL CONTENT</div>'
                "</body></html>")
        elif path == "/blocking":
            # End-to-end network-blocking page. Local resources exercise the
            # interceptor + engine wiring; real tracker URLs exercise the
            # actual EasyList/EasyPrivacy rules against the live internet.
            # Every result is recorded in window.__nor by onload/onerror.
            self._send(
                "<html><head><title>NOR Smoke Blocking</title>"
                "<script>"
                "window.__nor={};"
                "function t(k){window.__nor[k]='pending';"
                "return function(e){window.__nor[k]=(e&&e.type==='load')?'load':'error';};}"
                "</script></head><body>"
                # --- controls: must ALWAYS load ---
                '<img src="/pixel.png" onload="t(\'img-local\')(event)" '
                'onerror="t(\'img-local\')(event)">'
                '<script src="/content.js" onload="t(\'script-local\')(event)" '
                'onerror="t(\'script-local\')(event)"></script>'
                # --- local rules: blocked-ad.js must be blocked ---
                '<script src="/blocked-ad.js" onload="t(\'local-blocked\')(event)" '
                'onerror="t(\'local-blocked\')(event)"></script>'
                # --- exception rule must win over the blocking rule ---
                '<script src="/allowed-exception.js" onload="t(\'local-exception\')(event)" '
                'onerror="t(\'local-exception\')(event)"></script>'
                # --- real trackers with the real EasyList/EasyPrivacy ---
                '<script src="https://securepubads.g.doubleclick.net/tag/js/gpt.js" '
                'onload="t(\'doubleclick\')(event)" onerror="t(\'doubleclick\')(event)"></script>'
                '<script src="https://www.google-analytics.com/analytics.js" '
                'onload="t(\'ganalytics\')(event)" onerror="t(\'ganalytics\')(event)"></script>'
                '<script src="https://www.googletagmanager.com/gtag/js?id=GTM-NORTEST" '
                'onload="t(\'gtm\')(event)" onerror="t(\'gtm\')(event)"></script>'
                '<script src="https://sb.scorecardresearch.com/beacon.js" '
                'onload="t(\'scorecard\')(event)" onerror="t(\'scorecard\')(event)"></script>'
                "</body></html>")
        elif path == "/pixel.png":
            # 1x1 transparent PNG.
            payload = bytes.fromhex(
                "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15"
                "c4890000000d4944415478da63fcc0f0cf000301010018dd8db00000000049"
                "454e44ae426082")
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/content.js":
            payload = b"/* innocent content script */\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/blocked-ad.js":
            payload = b"/* would-be ad */\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/allowed-exception.js":
            payload = b"/* whitelisted by exception rule */\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/headers":
            # Dumps the request headers the browser actually sent (as JSON)
            # so the privacy smoke test can verify DNT / Sec-GPC / UA.
            import json
            hdrs = {k.lower(): v for k, v in self.headers.items()}
            body = ("<html><head><title>NOR Smoke Headers</title></head>"
                    "<body><pre id='h'>" + json.dumps(hdrs).replace("<", "&lt;")
                    + "</pre></body></html>")
            self._send(body)
        elif path == "/fp-cookie":
            # First-party cookie round-trip: set + read document.cookie in
            # the MAIN frame. Must keep working when 3p-cookie blocking is on.
            self._send(
                "<html><head><title>NOR Smoke FirstPartyCookie</title></head>"
                "<body><script>"
                "document.cookie='norfp=1;path=/;max-age=60';"
                "window.__fp=document.cookie.indexOf('norfp=1')>=0?'ok':'missing';"
                "</script></body></html>")
        elif path == "/tp-cookie-set":
            # Embedded cross-site (http://127.0.0.1 vs first-party localhost):
            # sets a cookie with a permissive policy and reports whether it
            # survived, via postMessage to the embedding parent.
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            # SameSite=None (cross-site cookie) without Secure: Chromium
            # drops it unless the test run disables that restriction.
            self.send_header("Set-Cookie",
                             "nor3p=1; Path=/; SameSite=None; Max-Age=60")
            payload = ("<html><head><title>NOR 3P</title></head><body>"
                       "<script>"
                       "var ok = document.cookie.indexOf('nor3p=1')>=0;"
                       "try{parent.postMessage({nor3p: ok ? 'stored' : 'blocked', jar: document.cookie || '<empty>'}, '*');}"
                       "catch(e){}"
                       "</script></body></html>").encode("utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif path == "/privacy":
            # Parent page for the third-party-cookie check. The parent is
            # served from http://localhost:<port> while the iframe loads
            # https://127.0.0.1:<tls_port> — a different site (scheme and
            # host differ), so its cookie is third-party. The TLS origin is
            # required because modern Chromium only accepts cross-site
            # cookies (SameSite=None) from secure contexts.
            self._send(
                "<html><head><title>NOR Smoke Privacy</title>"
                "<script>window.__nor={};"
                "window.addEventListener('message',function(e){"
                "if(e.data&&('nor3p' in e.data))window.__nor.tp=e.data.nor3p;});"
                "</script></head><body>"
                '<iframe id="tp" src="https://127.0.0.1:%d/tp-cookie-set" '
                'width="10" height="10"></iframe>'
                "</body></html>" % (self.server.server_address[1] + 1))
        elif path == "/shorts":
            # YouTube-Shorts-like viewport probe: a full-height dark stage
            # with a description panel pinned to the very bottom of the
            # viewport (fixed, bottom: 0). If ANY browser chrome (e.g. the
            # old permanent status bar) still took layout space, the panel
            # would be pushed out of the visible area and the marker row
            # (the #ff00aa stripe at the panel's bottom edge) would not be
            # at the window's bottom pixels. The smoke test asserts exactly
            # that through a screenshot.
            self._send(
                "<html><head><title>NOR Smoke Shorts</title>"
                "<style>"
                "html,body{margin:0;padding:0;height:100%;background:#0b0a10;}"
                "#stage{display:flex;align-items:center;justify-content:center;"
                "height:100%;color:#ddd;font:16px sans-serif;}"
                "#desc{position:fixed;left:0;right:0;bottom:0;"
                "background:#17141f;color:#e8e4f0;padding:14px 18px;"
                "font:13px sans-serif;}"
                "#marker{position:fixed;left:0;right:0;bottom:0;height:4px;"
                "background:#ff00aa;}"
                "</style></head><body>"
                '<div id="stage">Shorts viewport probe</div>'
                '<div id="desc">Description panel: title, channel, likes and '
                "comments stay fully visible and clickable at the bottom of "
                'the viewport. <a href="/hello">a link</a></div>'
                '<div id="marker"></div>'
                "</body></html>")
        else:
            self._send("<html><head><title>NOR Smoke 404</title></head>"
                       "<body><h1>Not found</h1></body></html>", code=404)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8377
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"NOR test server listening on http://127.0.0.1:{port}", flush=True)

    # Optional TLS listener (port + 1) with a freshly self-signed
    # certificate: used by the TLS-rejection smoke test. A self-signed
    # certificate must always be rejected by the browser.
    tls_port = port + 1
    tls_server = None
    try:
        import tempfile
        import subprocess
        import ssl
        with tempfile.TemporaryDirectory() as tmp:
            key = os.path.join(tmp, "key.pem")
            cert = os.path.join(tmp, "cert.pem")
            subprocess.run(
                ["openssl", "req", "-x509", "-newkey", "rsa:2048",
                 "-keyout", key, "-out", cert, "-days", "2", "-nodes",
                 "-subj", "/CN=127.0.0.1",
                 "-addext", "subjectAltName=IP:127.0.0.1"],
                check=True, capture_output=True)
            tls_server = ThreadingHTTPServer(("127.0.0.1", tls_port), Handler)
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(cert, key)
            tls_server.socket = ctx.wrap_socket(tls_server.socket, server_side=True)
            print(f"NOR test TLS server listening on https://127.0.0.1:{tls_port}", flush=True)
    except Exception as exc:  # TLS optional
        print(f"TLS listener unavailable: {exc}", flush=True)
        tls_server = None

    try:
        if tls_server:
            import threading
            threading.Thread(target=tls_server.serve_forever, daemon=True).start()
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
