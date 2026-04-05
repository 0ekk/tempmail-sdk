/**
 * tempmailg.com：GET /public/{locale} + POST /public/get_messages
 */
#include "tempmail_internal.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _MSC_VER
#define tm_strtok_r strtok_s
#else
#define tm_strtok_r strtok_r
#endif

#ifdef _WIN32
#define tm_strcasecmp _stricmp
#else
#include <strings.h>
#define tm_strcasecmp strcasecmp
#endif

#define TMG_ORIGIN "https://tempmailg.com"
#define TMG_TOK_PREFIX "tmg1:"

#define TMG_MAX_COOKIES 48
#define TMG_MAX_COOKIE_VAL 4096

typedef struct {
    char key[96];
    char val[512];
} tm_gcookie_t;

static void tm_g_locale(const char *domain, char *out, size_t cap) {
    if (!domain || !domain[0]) {
        snprintf(out, cap, "zh");
        return;
    }
    for (const char *p = domain; *p; p++) {
        if (*p == '/' || *p == '?' || *p == '#' || *p == '\\') {
            snprintf(out, cap, "zh");
            return;
        }
    }
    snprintf(out, cap, "%s", domain);
}

static int tm_g_parse_cookie_hdr(const char *hdr, tm_gcookie_t *tab, int *n) {
    *n = 0;
    if (!hdr || !hdr[0]) return 0;
    char buf[TMG_MAX_COOKIE_VAL];
    size_t hl = strlen(hdr);
    if (hl >= sizeof(buf)) return -1;
    memcpy(buf, hdr, hl + 1);
    char *save = NULL;
    for (char *tok = tm_strtok_r(buf, ";", &save); tok; tok = tm_strtok_r(NULL, ";", &save)) {
        while (*tok == ' ' || *tok == '\t') tok++;
        char *eq = strchr(tok, '=');
        if (!eq || eq == tok) continue;
        *eq = '\0';
        const char *k = tok;
        const char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        if (*n >= TMG_MAX_COOKIES) return -1;
        snprintf(tab[*n].key, sizeof(tab[*n].key), "%s", k);
        snprintf(tab[*n].val, sizeof(tab[*n].val), "%s", v);
        (*n)++;
    }
    return 0;
}

static int tm_g_cookie_index(tm_gcookie_t *tab, int n, const char *key) {
    for (int i = 0; i < n; i++) {
        if (tm_strcasecmp(tab[i].key, key) == 0) return i;
    }
    return -1;
}

static void tm_g_merge_cookie_responses(const char *prev, const char *newhdr, char *out, size_t cap) {
    tm_gcookie_t tab[TMG_MAX_COOKIES];
    int n = 0;
    tm_g_parse_cookie_hdr(prev, tab, &n);
    tm_gcookie_t tab2[TMG_MAX_COOKIES];
    int n2 = 0;
    if (newhdr && newhdr[0]) tm_g_parse_cookie_hdr(newhdr, tab2, &n2);
    for (int i = 0; i < n2; i++) {
        int j = tm_g_cookie_index(tab, n, tab2[i].key);
        if (j >= 0) {
            snprintf(tab[j].val, sizeof(tab[j].val), "%s", tab2[i].val);
        } else if (n < TMG_MAX_COOKIES) {
            snprintf(tab[n].key, sizeof(tab[n].key), "%s", tab2[i].key);
            snprintf(tab[n].val, sizeof(tab[n].val), "%s", tab2[i].val);
            n++;
        }
    }
    /* sort by key */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (tm_strcasecmp(tab[i].key, tab[j].key) > 0) {
                tm_gcookie_t t = tab[i];
                tab[i] = tab[j];
                tab[j] = t;
            }
        }
    }
    out[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        int add = snprintf(out + pos, cap > pos ? cap - pos : 0, "%s%s=%s",
            (i > 0 && pos > 0) ? "; " : "", tab[i].key, tab[i].val);
        if (add < 0 || (size_t)add >= cap - pos) break;
        pos += (size_t)add;
    }
}

static int tm_g_xsrf_from_hdr(const char *hdr, char *out, size_t cap) {
    tm_gcookie_t tab[TMG_MAX_COOKIES];
    int n = 0;
    tm_g_parse_cookie_hdr(hdr, tab, &n);
    for (int i = 0; i < n; i++) {
        if (tm_strcasecmp(tab[i].key, "XSRF-TOKEN") == 0 || tm_strcasecmp(tab[i].key, "xsrf-token") == 0) {
            snprintf(out, cap, "%s", tab[i].val);
            return 0;
        }
    }
    return -1;
}

static int tm_g_parse_csrf(const char *html, char *out, size_t cap) {
    const char *p = strstr(html, "csrf-token");
    if (!p) return -1;
    p = strstr(p, "content=");
    if (!p) return -1;
    p += 8;
    if (*p == '"' || *p == '\'') p++;
    const char *end = p;
    while (*end && *end != '"' && *end != '\'') end++;
    size_t len = (size_t)(end - p);
    if (len == 0 || len >= cap) return -1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *tm_g_b64_encode(const unsigned char *in, size_t len) {
    size_t olen = 4 * ((len + 2) / 3) + 1;
    char *out = (char *)malloc(olen);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned n = (unsigned)in[i] << 16;
        if (i + 1 < len) n |= (unsigned)in[i + 1] << 8;
        if (i + 2 < len) n |= (unsigned)in[i + 2];
        out[j++] = b64tab[(n >> 18) & 63];
        out[j++] = b64tab[(n >> 12) & 63];
        out[j++] = (i + 1 < len) ? b64tab[(n >> 6) & 63] : '=';
        out[j++] = (i + 2 < len) ? b64tab[n & 63] : '=';
    }
    out[j] = '\0';
    return out;
}

static int tm_g_b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static unsigned char *tm_g_b64_decode(const char *s, size_t *outlen) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t sl = strlen(s);
    while (sl > 0 && s[sl - 1] == '=') sl--;
    *outlen = (sl * 3) / 4;
    unsigned char *buf = (unsigned char *)malloc(*outlen + 4);
    if (!buf) return NULL;
    size_t j = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < sl; i++) {
        int v = tm_g_b64_val((unsigned char)s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (j < *outlen + 4) buf[j++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    *outlen = j;
    buf[j] = '\0';
    return buf;
}

static int tm_g_build_page_url(const char *domain, char *url, size_t urlcap) {
    char loc[96];
    tm_g_locale(domain, loc, sizeof(loc));
    CURL *c = curl_easy_init();
    if (!c) return -1;
    char *esc = curl_easy_escape(c, loc, 0);
    curl_easy_cleanup(c);
    if (!esc) return -1;
    int n = snprintf(url, urlcap, TMG_ORIGIN "/public/%s", esc);
    curl_free(esc);
    return (n > 0 && (size_t)n < urlcap) ? 0 : -1;
}

tm_email_info_t *tm_provider_tempmailg_generate(const char *domain) {
    char page_url[512];
    if (tm_g_build_page_url(domain, page_url, sizeof(page_url)) != 0) return NULL;

    char loc[96];
    tm_g_locale(domain, loc, sizeof(loc));

    char refh[640];
    snprintf(refh, sizeof(refh), "Referer: %s", page_url);

    const char *headers_get[] = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0",
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8",
        "Cache-Control: no-cache",
        "DNT: 1",
        "Pragma: no-cache",
        refh,
        "Upgrade-Insecure-Requests: 1",
        NULL,
    };

    tm_http_response_t *r1 = tm_http_request(TM_HTTP_GET, page_url, headers_get, NULL, 15);
    if (!r1 || r1->status != 200 || !r1->body) {
        tm_http_response_free(r1);
        return NULL;
    }

    char csrf[512];
    if (tm_g_parse_csrf(r1->body, csrf, sizeof(csrf)) != 0) {
        tm_http_response_free(r1);
        return NULL;
    }

    char cookie_hdr[TMG_MAX_COOKIE_VAL];
    cookie_hdr[0] = '\0';
    if (r1->cookies && r1->cookies[0]) {
        snprintf(cookie_hdr, sizeof(cookie_hdr), "%s", r1->cookies);
    }
    tm_http_response_free(r1);

    char xsrf[512];
    if (tm_g_xsrf_from_hdr(cookie_hdr, xsrf, sizeof(xsrf)) != 0) {
        return NULL;
    }

    cJSON *body1 = cJSON_CreateObject();
    if (!body1) return NULL;
    cJSON_AddStringToObject(body1, "_token", csrf);
    char *post_body = cJSON_PrintUnformatted(body1);
    cJSON_Delete(body1);
    if (!post_body) return NULL;

    char post_url[] = TMG_ORIGIN "/public/get_messages";
    char ckh[4600];
    snprintf(ckh, sizeof(ckh), "Cookie: %s", cookie_hdr);
    char xsh[1200];
    snprintf(xsh, sizeof(xsh), "X-XSRF-TOKEN: %s", xsrf);

    const char *headers_post[] = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0",
        "Accept: application/json, text/plain, */*",
        "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8",
        "Content-Type: application/json",
        "Origin: " TMG_ORIGIN,
        refh,
        "Cache-Control: no-cache",
        "Pragma: no-cache",
        "DNT: 1",
        ckh,
        xsh,
        NULL,
    };

    tm_http_response_t *r2 = tm_http_request(TM_HTTP_POST, post_url, headers_post, post_body, 15);
    free(post_body);
    if (!r2 || r2->status != 200 || !r2->body) {
        tm_http_response_free(r2);
        return NULL;
    }

    cJSON *wrap = cJSON_Parse(r2->body);
    if (!wrap) {
        tm_http_response_free(r2);
        return NULL;
    }
    cJSON *st = cJSON_GetObjectItemCaseSensitive(wrap, "status");
    cJSON *mb = cJSON_GetObjectItemCaseSensitive(wrap, "mailbox");
    if (!cJSON_IsTrue(st) || !cJSON_IsString(mb) || !mb->valuestring || !mb->valuestring[0]) {
        cJSON_Delete(wrap);
        tm_http_response_free(r2);
        return NULL;
    }

    char *mailbox = tm_strdup(mb->valuestring);
    if (!mailbox) {
        cJSON_Delete(wrap);
        tm_http_response_free(r2);
        return NULL;
    }

    char merged[TMG_MAX_COOKIE_VAL];
    tm_g_merge_cookie_responses(cookie_hdr, r2->cookies ? r2->cookies : "", merged, sizeof(merged));
    cJSON_Delete(wrap);
    tm_http_response_free(r2);

    cJSON *tokj = cJSON_CreateObject();
    if (!tokj) {
        free(mailbox);
        return NULL;
    }
    cJSON_AddStringToObject(tokj, "l", loc);
    cJSON_AddStringToObject(tokj, "c", merged);
    cJSON_AddStringToObject(tokj, "s", csrf);
    char *jraw = cJSON_PrintUnformatted(tokj);
    cJSON_Delete(tokj);
    if (!jraw) {
        free(mailbox);
        return NULL;
    }

    unsigned char *jb = (unsigned char *)jraw;
    size_t jl = strlen(jraw);
    char *b64 = tm_g_b64_encode(jb, jl);
    free(jraw);
    if (!b64) {
        free(mailbox);
        return NULL;
    }

    size_t tlen = strlen(TMG_TOK_PREFIX) + strlen(b64) + 1;
    char *token = (char *)malloc(tlen);
    if (!token) {
        free(b64);
        free(mailbox);
        return NULL;
    }
    snprintf(token, tlen, "%s%s", TMG_TOK_PREFIX, b64);
    free(b64);

    tm_email_info_t *info = tm_email_info_new();
    if (!info) {
        free(token);
        free(mailbox);
        return NULL;
    }
    info->channel = CHANNEL_TEMPMAILG;
    info->email = mailbox;
    info->token = token;
    return info;
}

tm_email_t *tm_provider_tempmailg_get_emails(const char *token, const char *email, int *count) {
    *count = -1;
    if (!token || strncmp(token, TMG_TOK_PREFIX, strlen(TMG_TOK_PREFIX)) != 0) return NULL;
    const char *b64s = token + strlen(TMG_TOK_PREFIX);
    size_t rawlen = 0;
    unsigned char *raw = tm_g_b64_decode(b64s, &rawlen);
    if (!raw) return NULL;
    cJSON *sess = cJSON_Parse((char *)raw);
    free(raw);
    if (!sess) return NULL;

    const char *loc = "zh";
    cJSON *lj = cJSON_GetObjectItemCaseSensitive(sess, "l");
    if (cJSON_IsString(lj) && lj->valuestring && lj->valuestring[0]) loc = lj->valuestring;
    cJSON *cj = cJSON_GetObjectItemCaseSensitive(sess, "c");
    cJSON *sj = cJSON_GetObjectItemCaseSensitive(sess, "s");
    if (!cJSON_IsString(cj) || !cj->valuestring || !cJSON_IsString(sj) || !sj->valuestring) {
        cJSON_Delete(sess);
        return NULL;
    }
    char *cookie_copy = tm_strdup(cj->valuestring);
    char *csrf_copy = tm_strdup(sj->valuestring);
    cJSON_Delete(sess);
    if (!cookie_copy || !csrf_copy) {
        free(cookie_copy);
        free(csrf_copy);
        return NULL;
    }

    char page_url[512];
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(cookie_copy);
        free(csrf_copy);
        return NULL;
    }
    char *esc = curl_easy_escape(curl, loc, 0);
    curl_easy_cleanup(curl);
    if (!esc) {
        free(cookie_copy);
        free(csrf_copy);
        return NULL;
    }
    snprintf(page_url, sizeof(page_url), TMG_ORIGIN "/public/%s", esc);
    curl_free(esc);

    cJSON *bodyo = cJSON_CreateObject();
    cJSON_AddStringToObject(bodyo, "_token", csrf_copy);
    char *post_body = cJSON_PrintUnformatted(bodyo);
    cJSON_Delete(bodyo);
    if (!post_body) {
        free(cookie_copy);
        free(csrf_copy);
        return NULL;
    }

    char xsrf[512];
    if (tm_g_xsrf_from_hdr(cookie_copy, xsrf, sizeof(xsrf)) != 0) {
        free(post_body);
        free(cookie_copy);
        free(csrf_copy);
        return NULL;
    }

    char refh2[640];
    snprintf(refh2, sizeof(refh2), "Referer: %s", page_url);
    char ckh2[4600];
    snprintf(ckh2, sizeof(ckh2), "Cookie: %s", cookie_copy);
    char xsh2[1200];
    snprintf(xsh2, sizeof(xsh2), "X-XSRF-TOKEN: %s", xsrf);

    char post_url[] = TMG_ORIGIN "/public/get_messages";
    const char *headers_post[] = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0",
        "Accept: application/json, text/plain, */*",
        "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8",
        "Content-Type: application/json",
        "Origin: " TMG_ORIGIN,
        refh2,
        "Cache-Control: no-cache",
        "Pragma: no-cache",
        "DNT: 1",
        ckh2,
        xsh2,
        NULL,
    };

    tm_http_response_t *r = tm_http_request(TM_HTTP_POST, post_url, headers_post, post_body, 15);
    free(post_body);
    free(cookie_copy);
    free(csrf_copy);
    if (!r || r->status != 200 || !r->body) {
        tm_http_response_free(r);
        return NULL;
    }

    cJSON *wrap = cJSON_Parse(r->body);
    tm_http_response_free(r);
    if (!wrap) return NULL;
    cJSON *st = cJSON_GetObjectItemCaseSensitive(wrap, "status");
    if (!cJSON_IsTrue(st)) {
        cJSON_Delete(wrap);
        return NULL;
    }
    cJSON *mbx = cJSON_GetObjectItemCaseSensitive(wrap, "mailbox");
    if (cJSON_IsString(mbx) && mbx->valuestring && mbx->valuestring[0]) {
        /* trim compare */
        const char *a = mbx->valuestring;
        const char *b = email;
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        if (tm_strcasecmp(a, b) != 0) {
            cJSON_Delete(wrap);
            return NULL;
        }
    }

    cJSON *msgs = cJSON_GetObjectItemCaseSensitive(wrap, "messages");
    if (!cJSON_IsArray(msgs)) {
        cJSON_Delete(wrap);
        *count = 0;
        return NULL;
    }
    int n = cJSON_GetArraySize(msgs);
    if (n <= 0) {
        cJSON_Delete(wrap);
        *count = 0;
        return NULL;
    }
    tm_email_t *arr = tm_emails_new(n);
    if (!arr) {
        cJSON_Delete(wrap);
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(msgs, i);
        if (cJSON_IsObject(it)) {
            tm_email_t ne = tm_normalize_email(it, email);
            arr[i] = ne;
        }
    }
    cJSON_Delete(wrap);
    *count = n;
    return arr;
}
