/**
 * temporary-email.org 渠道
 * 首次 GET /zh/messages 下发 Cookie；收信需 Cookie + x-requested-with
 */
#include "tempmail_internal.h"
#include <string.h>

#define TEO_MESSAGES "https://www.temporary-email.org/zh/messages"
#define TEO_REFERER "https://www.temporary-email.org/zh"

static const char* teo_headers_base[] = {
    "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0",
    "Accept: text/plain, */*; q=0.01",
    "accept-language: zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6",
    "cache-control: no-cache",
    "dnt: 1",
    "pragma: no-cache",
    "priority: u=1, i",
    "referer: " TEO_REFERER,
    "sec-ch-ua: \"Chromium\";v=\"146\", \"Not-A.Brand\";v=\"24\", \"Microsoft Edge\";v=\"146\"",
    "sec-ch-ua-mobile: ?0",
    "sec-ch-ua-platform: \"Windows\"",
    "sec-fetch-dest: empty",
    "sec-fetch-mode: cors",
    "sec-fetch-site: same-origin",
    NULL
};

tm_email_info_t* tm_provider_temporary_email_org_generate(void) {
    tm_http_response_t *resp = tm_http_request(TM_HTTP_GET, TEO_MESSAGES, teo_headers_base, NULL, 15);
    if (!resp || resp->status != 200) { tm_http_response_free(resp); return NULL; }

    if (!resp->cookies || !strstr(resp->cookies, "temporaryemail_session=") || !strstr(resp->cookies, "email=")) {
        tm_http_response_free(resp);
        return NULL;
    }

    char *cookie_copy = tm_strdup(resp->cookies);
    cJSON *json = cJSON_Parse(resp->body);
    tm_http_response_free(resp);
    if (!json) { free(cookie_copy); return NULL; }

    const char *mailbox = TM_JSON_STR(cJSON_GetObjectItemCaseSensitive(json, "mailbox"), "");
    if (!mailbox[0] || !strchr(mailbox, '@')) {
        cJSON_Delete(json);
        free(cookie_copy);
        return NULL;
    }

    tm_email_info_t *info = tm_email_info_new();
    info->channel = CHANNEL_TEMPORARY_EMAIL_ORG;
    info->email = tm_strdup(mailbox);
    info->token = cookie_copy;
    cJSON_Delete(json);
    return info;
}

tm_email_t* tm_provider_temporary_email_org_get_emails(const char *token, const char *email, int *count) {
    *count = -1;
    if (!token || !email) return NULL;

    char cookie_hdr[6144];
    if (snprintf(cookie_hdr, sizeof(cookie_hdr), "Cookie: %s", token) >= (int)sizeof(cookie_hdr)) {
        return NULL;
    }

    const char* headers[] = {
        cookie_hdr,
        "x-requested-with: XMLHttpRequest",
        teo_headers_base[0],
        teo_headers_base[1],
        teo_headers_base[2],
        teo_headers_base[3],
        teo_headers_base[4],
        teo_headers_base[5],
        teo_headers_base[6],
        teo_headers_base[7],
        teo_headers_base[8],
        teo_headers_base[9],
        teo_headers_base[10],
        teo_headers_base[11],
        teo_headers_base[12],
        teo_headers_base[13],
        NULL
    };

    tm_http_response_t *resp = tm_http_request(TM_HTTP_GET, TEO_MESSAGES, headers, NULL, 15);
    if (!resp || resp->status != 200) { tm_http_response_free(resp); return NULL; }

    cJSON *json = cJSON_Parse(resp->body);
    tm_http_response_free(resp);
    if (!json) return NULL;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(json, "messages");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    *count = n;
    if (n == 0) { cJSON_Delete(json); return NULL; }

    tm_email_t *emails = tm_emails_new(n);
    for (int i = 0; i < n; i++) emails[i] = tm_normalize_email(cJSON_GetArrayItem(arr, i), email);
    cJSON_Delete(json);
    return emails;
}
