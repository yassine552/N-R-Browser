# Filter Rule Syntax (NØR Browser)

NØR Browser ships a local privacy-filtering engine implemented as a
Qt WebEngine **URL request interceptor**. It understands a practical
subset of the Adblock Plus rule syntax. **Full EasyList compatibility is
NOT claimed** — unsupported rules are skipped and counted, and the count
is visible in Settings → Privacy.

## What is enforced

| Construct | Meaning | Example |
|---|---|---|
| `! text` / `# text` | comment (ignored) | `! my list` |
| `@@rule` | exception — overrides blocking rules | `@@||cdn.example^` |
| `\|\|domain.tld^` | block any request whose host is `domain.tld` or a subdomain, ending at a separator | `\|\|doubleclick.net^` |
| `\|\|domain.tld` | same, without trailing separator (may also match `domain.tld.evil.io`-style prefixes, as several engines do) | `\|\|ads.example` |
| `\|https://…` | URL must start with the pattern | `\|https://ads.` |
| `…\|` | URL must end with the pattern | `/tracker.js\|` |
| plain text | substring match anywhere in the URL | `banner468` |
| `*` | wildcard inside the pattern | `\|\|cdn.*/ads/*` |
| `^` | separator: any character except letter / digit / `_` / `.` / `%` / `-`, or end of URL | `\|\|example.com^` |
| `$image,script` | restrict to resource types | `\|\|media.net^$script,image` |
| `$third-party` | only requests whose registrable domain differs from the page's | `\|\|analytics.com^$third-party` |
| `$~third-party` | only first-party requests | `\|\|widget.com^$~third-party` |
| `$object, $media, $font, $websocket, $xmlhttprequest, $ping, $document, $subdocument, $stylesheet, $other` | further resource-type filters | |
| `$important` | accepted, precedence not implemented (documented no-op) | |
| `$collapse` | accepted, cosmetic no-op | |

Supported resource types map directly onto WebEngine request types
(main frame, sub-frame, stylesheet, script, image, font, object, XHR,
ping, WebSocket, media, other).

Matching is **case-insensitive** by design.

## What is NOT enforced (skipped with reason)

| Construct | Reason |
|---|---|
| `##selector` / `#@#selector` | cosmetic element hiding — a network interceptor cannot implement it; skipped as `cosmetic-rule` |
| `$domain=a\|b` | per-domain activation is not supported; skipped as `unsupported-option` |
| `$match-case` | the engine matches case-insensitively; skipped |
| `$csp=…`, `$rewrite=…`, `$removeparam=…`, `$webrtc`, `$popup` | not implementable at the request-interception level |
| `[Adblock Plus 2.0]` metadata lines | ignored |

Skipped-line counts are reported in Settings → Privacy
(“N unsupported lines skipped”) so you can judge a list's fit before
relying on it.

## Site allowlist (different from `@@` rules)

Settings → Privacy → “Sites exempt from blocking” maintains a host
allowlist: when the request's host is `allowed.com` or a subdomain of
it, **no blocking rule applies at all** (including for the site's
sub-resources on other pages). This is the per-site kill-switch for
troubleshooting a broken site.

## Built-in starter list

`resources/filters/starter-blocklist.txt` is a curated, conservative
list (~100 rules) targeting major ad networks, analytics/fingerprinting
hosts, pop/redirect ad hosts, and third-party social widgets, plus
explicit `@@` exceptions for common CDNs and login endpoints so
everyday sites keep working. You can disable blocking entirely with the
shield icon in the status bar or via Settings → Privacy.

## Custom lists

Settings → Privacy → “Additional filter lists (local files)” lets you
add any local text file in the syntax above. Lists are read from disk
when added/removed and at startup — nothing is ever uploaded, and no
browsing data is shared with any list source. If you obtain lists from
third parties, download them yourself and add the local file.

## Engine limitations (honest list)

- Rules match on the fully-encoded, lower-cased URL string; percent-
  encoded paths can defeat naive substring rules (inherent to the
  format).
- The registrable-domain heuristic uses the last two labels; multi-part
  public suffixes (e.g. `co.uk`) are treated as first-party with their
  second-level host. This makes `$third-party` slightly more permissive
  on such TLDs.
- Cosmetic filtering, element hiding, and scriptlet injection are out of
  scope by design.
- Blocking statistics are a session counter, not a per-site report.
