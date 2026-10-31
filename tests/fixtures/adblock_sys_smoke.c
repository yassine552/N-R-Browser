/* Standalone smoke test for the nor_adblock_sys C ABI.
 * Builds an engine from the real EasyList + EasyPrivacy, checks known
 * trackers, cosmetic resources and serialization; prints RSS measurements.
 */
#include "nor_adblock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long vm_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int g_fail = 0;
static void check(const char *name, int ok)
{
    printf("%-42s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) g_fail++;
}

static int blocked(NorAdblockEngine *e, const char *url, const char *fp,
                   const char *rt)
{
    return nor_adblock_engine_should_block(e, url, fp, rt, "get", NULL) == 1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512];
    long rss0 = vm_rss_kb();

    NorAdblockFilterSet *fs = nor_adblock_filter_set_new(0 /* no debug */);
    check("filter set created", fs != NULL);

    snprintf(path, sizeof path, "%s/easylist.txt", dir);
    char *easylist = read_file(path);
    check("easylist read", easylist != NULL);
    long n1 = easylist ? nor_adblock_filter_set_add_list(fs, easylist) : -1;
    printf("  easylist offered %ld lines\n", n1);

    snprintf(path, sizeof path, "%s/easyprivacy.txt", dir);
    char *easyprivacy = read_file(path);
    long n2 = easyprivacy ? nor_adblock_filter_set_add_list(fs, easyprivacy) : -1;
    printf("  easyprivacy offered %ld lines\n", n2);

    long rssAfterParse = vm_rss_kb();
    NorAdblockEngine *engine = nor_adblock_engine_new(fs);
    check("engine built", engine != NULL);
    long rss1 = vm_rss_kb();
    printf("  RSS: before=%ldkB after-parse=%ldkB after-engine=%ldkB (+%ldkB engine)\n",
           rss0, rssAfterParse, rss1, rss1 - rss0);

    /* Known tracker requests that EasyList/EasyPrivacy block. */
    check("doubleclick.net script blocked",
          blocked(engine, "https://securepubads.g.doubleclick.net/tag/js/gpt.js",
                  "https://www.example.com/", "script"));
    check("googlesyndication image blocked",
          blocked(engine, "https://pagead2.googlesyndication.com/pagead/adshow",
                  "https://news.example.org/", "image"));
    check("google-analytics script blocked",
          blocked(engine, "https://www.google-analytics.com/analytics.js",
                  "https://shop.example.net/", "script"));
    check("example.com document allowed",
          !blocked(engine, "https://www.example.com/", "https://example.com/", "document"));
    check("first-party script allowed",
          !blocked(engine, "https://example.com/js/app.js",
                   "https://example.com/", "script"));
    check("bad input -> negative",
          nor_adblock_engine_should_block(engine, NULL, "x", "script", "get", NULL) < 0);

    /* $domain= support (built-in engine cannot do this). */
    NorAdblockFilterSet *fs2 = nor_adblock_filter_set_new(0);
    nor_adblock_filter_set_add_list(fs2, "banner$domain=foo.com|~bar.foo.com");
    NorAdblockEngine *e2 = nor_adblock_engine_new(fs2);
    check("$domain applies on listed domain",
          blocked(e2, "https://cdn.example.org/banner", "https://foo.com/", "image"));
    check("$domain exception (~) honored",
          !blocked(e2, "https://cdn.example.org/banner", "https://bar.foo.com/", "image"));
    check("$domain skipped elsewhere",
          !blocked(e2, "https://cdn.example.org/banner", "https://other.com/", "image"));
    nor_adblock_engine_free(e2);

    /* Public suffix correctness: subdomain vs registrable domain. */
    NorAdblockFilterSet *fs3 = nor_adblock_filter_set_new(0);
    nor_adblock_filter_set_add_list(fs3, "||ads.example.co.uk^");
    NorAdblockEngine *e3 = nor_adblock_engine_new(fs3);
    check("||anchor matches subdomain",
          blocked(e3, "https://tracker.ads.example.co.uk/x.js", "https://site.com/", "script"));
    check("||anchor does not over-match",
          !blocked(e3, "https://ads.example.co.uk.evil.com/x.js", "https://site.com/", "script"));
    nor_adblock_engine_free(e3);

    /* match-case option (only valid with full-regex filters, per ABP). */
    NorAdblockFilterSet *fs4 = nor_adblock_filter_set_new(0);
    nor_adblock_filter_set_add_list(fs4, "/BannerAd/$match-case");
    NorAdblockEngine *e4 = nor_adblock_engine_new(fs4);
    check("$match-case blocks exact case",
          blocked(e4, "https://x.com/img/BannerAd.png", "https://x.com/", "image"));
    check("$match-case skips other case",
          !blocked(e4, "https://x.com/img/bannerad.png", "https://x.com/", "image"));
    nor_adblock_engine_free(e4);

    /* Cosmetic filtering resources. */
    NorAdblockFilterSet *fs5 = nor_adblock_filter_set_new(0);
    nor_adblock_filter_set_add_list(fs5,
        "example.com##.ad-banner\n"
        "##.generic-hidden\n"
        "example.com#@#.whitelisted-box\n");
    NorAdblockEngine *e5 = nor_adblock_engine_new(fs5);
    char *res = nor_adblock_engine_url_cosmetic_resources(e5, "https://example.com/page");
    check("cosmetic resources returned", res != NULL);
    if (res) {
        printf("  cosmetic json: %.160s\n", res);
        check("specific selector present", strstr(res, "ad-banner") != NULL);
        check("exception selector present", strstr(res, "whitelisted-box") != NULL);
        nor_adblock_string_free(res);
    }
    char *generic = nor_adblock_engine_generic_hide_selectors(e5, "ad-banner,generic-hidden", "", "whitelisted-box");
    check("generic hide selectors returned", generic != NULL);
    if (generic) {
        printf("  generic json: %.120s\n", generic);
        check("generic selector matched", strstr(generic, "generic-hidden") != NULL);
        nor_adblock_string_free(generic);
    }
    nor_adblock_engine_free(e5);

    /* Serialization roundtrip on the real-lists engine. */
    NorAdblockBytes bytes = nor_adblock_engine_serialize(engine);
    check("engine serialized", bytes.data != NULL && bytes.len > 0);
    printf("  serialized size: %zu bytes\n", bytes.len);
    NorAdblockEngine *restored = nor_adblock_engine_deserialize(bytes.data, bytes.len);
    check("engine deserialized", restored != NULL);
    if (restored) {
        check("restored engine still blocks",
              blocked(restored, "https://securepubads.g.doubleclick.net/tag/js/gpt.js",
                      "https://www.example.com/", "script"));
        check("restored engine still allows",
              !blocked(restored, "https://example.com/js/app.js", "https://example.com/", "script"));
        nor_adblock_engine_free(restored);
    }
    check("corrupt deserialize -> NULL",
          nor_adblock_engine_deserialize((const uint8_t *)"garbage-not-an-engine", 22) == NULL);
    check("empty deserialize -> NULL",
          nor_adblock_engine_deserialize(NULL, 0) == NULL);

    /* Performance: 20k decisions on the real engine. */
    {
        const char *url = "https://pagead2.googlesyndication.com/pagead/adshow?x=1";
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        int hits = 0;
        for (int i = 0; i < 20000; i++)
            hits += blocked(engine, url, "https://news.example.org/", "image");
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        printf("  20000 decisions in %.1f ms (%.2f us/req), hits=%d\n",
               ms, ms * 1000.0 / 20000, hits);
        check("perf decisions all correct", hits == 20000);
    }

    nor_adblock_engine_free(engine);
    free(easylist);
    free(easyprivacy);
    long rssEnd = vm_rss_kb();
    printf("  final RSS: %ldkB (leak delta after frees: %ldkB)\n", rssEnd, rssEnd - rss0);

    printf(g_fail ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
