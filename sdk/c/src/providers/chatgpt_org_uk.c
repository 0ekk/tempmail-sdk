/**
 * mail.chatgpt.org.uk 渠道实现
 */
#include "tempmail_internal.h"

#define CG_BASE "https://mail.chatgpt.org.uk/api"
#define CG_HOME "https://mail.chatgpt.org.uk/"

static const char* cg_headers[] = {
    "Accept: */*",
    "Referer: https://mail.chatgpt.org.uk/",
    "Origin: https://mail.chatgpt.org.uk",
    "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",
    "DNT: 1",
    NULL
};

static char* cg_extract_gm_sid(const char *cookies) {
    if (!cookies) return NULL;
    const char *start = strstr(cookies, "gm_sid=");
    if (!start) return NULL;
    const char *end = strchr(start, ';');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char* cg_fetch_gm_sid_once(long *status_out) {
    if (status_out) *status_out = 0;
    tm_http_response_t *home = tm_http_request(TM_HTTP_GET, CG_HOME, cg_headers, NULL, 15);
    if (!home) return NULL;
    if (status_out) *status_out = home->status;
    if (home->status != 200) { tm_http_response_free(home); return NULL; }

    char *gm_sid = cg_extract_gm_sid(home->cookies);
    tm_http_response_free(home);
    return gm_sid;
}

static char* cg_fetch_gm_sid(void) {
    long status = 0;
    char *gm_sid = cg_fetch_gm_sid_once(&status);
    if (gm_sid) return gm_sid;
    if (status == 401 || status == 200) {
        return cg_fetch_gm_sid_once(&status);
    }
    return NULL;
}

static char* cg_fetch_inbox_token_once(const char *email, const char *gm_sid, long *status_out) {
    if (status_out) *status_out = 0;
    char body[512];
    snprintf(body, sizeof(body), "{\"email\":\"%s\"}", email);

    char cookie_hdr[512];
    snprintf(cookie_hdr, sizeof(cookie_hdr), "Cookie: %s", gm_sid);

    const char* headers[] = {
        "Accept: */*",
        "Referer: https://mail.chatgpt.org.uk/",
        "Origin: https://mail.chatgpt.org.uk",
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",
        "DNT: 1",
        "Content-Type: application/json",
        cookie_hdr,
        NULL
    };

    tm_http_response_t *resp = tm_http_request(TM_HTTP_POST, CG_BASE "/inbox-token", headers, body, 15);
    if (!resp) return NULL;
    if (status_out) *status_out = resp->status;
    if (resp->status != 200) { tm_http_response_free(resp); return NULL; }

    cJSON *json = cJSON_Parse(resp->body);
    tm_http_response_free(resp);
    if (!json) return NULL;

    cJSON *auth = cJSON_GetObjectItemCaseSensitive(json, "auth");
    const char *token = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(auth, "token"));
    if (!token || !token[0]) { cJSON_Delete(json); return NULL; }

    char *out = tm_strdup(token);
    cJSON_Delete(json);
    return out;
}

static char* cg_fetch_inbox_token(const char *email) {
    long status = 0;
    char *gm_sid = cg_fetch_gm_sid();
    if (!gm_sid) return NULL;

    char *token = cg_fetch_inbox_token_once(email, gm_sid, &status);
    if (!token && status == 401) {
        free(gm_sid);
        gm_sid = cg_fetch_gm_sid();
        if (!gm_sid) return NULL;
        token = cg_fetch_inbox_token_once(email, gm_sid, &status);
    }
    free(gm_sid);
    return token;
}

static tm_email_t* cg_fetch_emails_once(const char *token, const char *email, int *count, long *status_out) {
    if (count) *count = -1;
    if (status_out) *status_out = 0;
    if (!token || !token[0]) return NULL;

    char url[512];
    snprintf(url, sizeof(url), CG_BASE "/emails?email=%s", email);

    char token_hdr[1024];
    snprintf(token_hdr, sizeof(token_hdr), "x-inbox-token: %s", token);

    const char* headers[] = {
        "Accept: */*",
        "Referer: https://mail.chatgpt.org.uk/",
        "Origin: https://mail.chatgpt.org.uk",
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",
        "DNT: 1",
        token_hdr,
        NULL
    };

    tm_http_response_t *resp = tm_http_request(TM_HTTP_GET, url, headers, NULL, 15);
    if (!resp) return NULL;
    if (status_out) *status_out = resp->status;
    if (resp->status != 200) { tm_http_response_free(resp); return NULL; }

    cJSON *json = cJSON_Parse(resp->body);
    tm_http_response_free(resp);
    if (!json) return NULL;

    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "success"))) { cJSON_Delete(json); return NULL; }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(data, "emails");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (count) *count = n;
    if (n == 0) { cJSON_Delete(json); return NULL; }

    tm_email_t *emails = tm_emails_new(n);
    for (int i = 0; i < n; i++) emails[i] = tm_normalize_email(cJSON_GetArrayItem(arr, i), email);
    cJSON_Delete(json);
    return emails;
}


tm_email_info_t* tm_provider_chatgpt_org_uk_generate(void) {
    tm_http_response_t *resp = tm_http_request(TM_HTTP_GET, CG_BASE "/generate-email", cg_headers, NULL, 15);
    if (!resp || resp->status != 200) { tm_http_response_free(resp); return NULL; }

    cJSON *json = cJSON_Parse(resp->body);
    tm_http_response_free(resp);
    if (!json) return NULL;

    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "success"))) { cJSON_Delete(json); return NULL; }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    const char *email = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "email"));
    if (!email || !email[0]) { cJSON_Delete(json); return NULL; }

    char *token = cg_fetch_inbox_token(email);
    if (!token) { cJSON_Delete(json); return NULL; }

    tm_email_info_t *info = tm_email_info_new();
    info->channel = CHANNEL_CHATGPT_ORG_UK;
    info->email = tm_strdup(email);
    info->token = token;
    cJSON_Delete(json);
    return info;
}


tm_email_t* tm_provider_chatgpt_org_uk_get_emails(const char *token, const char *email, int *count) {
    *count = -1;
    if (!token || !token[0]) return NULL;

    long status = 0;
    tm_email_t *emails = cg_fetch_emails_once(token, email, count, &status);
    if (status == 401) {
        char *refreshed = cg_fetch_inbox_token(email);
        if (refreshed) {
            emails = cg_fetch_emails_once(refreshed, email, count, &status);
            free(refreshed);
        }
    }
    return emails;
}
