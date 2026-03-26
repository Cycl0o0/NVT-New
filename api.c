#include "api.h"
#include "config.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#else
#include <sys/utsname.h>
#endif

/* ── curl write callback ─────────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

static void copy_token(char *dst, size_t dst_sz, const char *src)
{
    size_t j = 0;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] && j + 1 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            dst[j++] = (char)c;
        } else if ((c == ' ' || c == '/') && j > 0 && dst[j - 1] != '-') {
            dst[j++] = '-';
        }
    }

    while (j > 0 && dst[j - 1] == '-') j--;
    dst[j] = '\0';
}

static void copy_version_token(char *dst, size_t dst_sz, const char *src)
{
    size_t j = 0;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] && j + 1 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            dst[j++] = (char)c;
        } else {
            break;
        }
    }

    dst[j] = '\0';
}

static void copy_kernel_version(char *dst, size_t dst_sz, const char *src)
{
    size_t j = 0;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] && j + 1 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];

        if ((c >= '0' && c <= '9') || c == '.') {
            dst[j++] = (char)c;
        } else {
            break;
        }
    }

    while (j > 0 && dst[j - 1] == '.') j--;
    if (j == 0) {
        copy_version_token(dst, dst_sz, src);
        return;
    }
    dst[j] = '\0';
}

static void trim_line(char *s)
{
    size_t len;

    if (!s) return;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static void unquote_value(char *s)
{
    size_t len;

    if (!s || !s[0]) return;
    len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

#if !defined(_WIN32)
static void detect_uname(char *kernel_name, size_t kernel_name_sz,
                         char *kernel_version, size_t kernel_version_sz)
{
    struct utsname uts;

    if (uname(&uts) == 0) {
        copy_token(kernel_name, kernel_name_sz, uts.sysname);
        copy_kernel_version(kernel_version, kernel_version_sz, uts.release);
    }
}
#endif

#if defined(__linux__)
static int read_os_release_value(const char *key, char *out, size_t out_sz)
{
    static const char *paths[] = { "/etc/os-release", "/usr/lib/os-release" };
    char line[256];

    if (!out_sz) return 0;
    out[0] = '\0';

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *fp = fopen(paths[i], "r");
        size_t key_len = strlen(key);

        if (!fp) continue;
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') continue;
            snprintf(out, out_sz, "%s", line + key_len + 1);
            trim_line(out);
            unquote_value(out);
            fclose(fp);
            return 1;
        }
        fclose(fp);
    }

    return 0;
}
#endif

static void detect_platform(char *distro_name, size_t distro_name_sz,
                            char *distro_version, size_t distro_version_sz,
                            char *kernel_name, size_t kernel_name_sz,
                            char *kernel_version, size_t kernel_version_sz)
{
    distro_name[0] = '\0';
    distro_version[0] = '\0';
    kernel_name[0] = '\0';
    kernel_version[0] = '\0';

#if defined(_WIN32)
    OSVERSIONINFOEXA info;
    HMODULE ntdll;
    char version[32] = "";
    typedef LONG (WINAPI *rtl_get_version_fn)(OSVERSIONINFOEXA *);

    memset(&info, 0, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        rtl_get_version_fn rtl_get_version = (rtl_get_version_fn)GetProcAddress(ntdll, "RtlGetVersion");
        if (rtl_get_version && rtl_get_version(&info) == 0) {
            snprintf(version, sizeof(version), "%lu.%lu.%lu",
                     (unsigned long)info.dwMajorVersion,
                     (unsigned long)info.dwMinorVersion,
                     (unsigned long)info.dwBuildNumber);
        }
    }

    if (!version[0]) {
        OSVERSIONINFOA legacy;

        memset(&legacy, 0, sizeof(legacy));
        legacy.dwOSVersionInfoSize = sizeof(legacy);
        if (GetVersionExA(&legacy)) {
            snprintf(version, sizeof(version), "%lu.%lu.%lu",
                     (unsigned long)legacy.dwMajorVersion,
                     (unsigned long)legacy.dwMinorVersion,
                     (unsigned long)legacy.dwBuildNumber);
        }
    }

    copy_token(distro_name, distro_name_sz, "Windows");
    copy_token(kernel_name, kernel_name_sz, "NT");
    snprintf(distro_version, distro_version_sz, "%s", version[0] ? version : "0");
    snprintf(kernel_version, kernel_version_sz, "%s", distro_version);
#elif defined(__APPLE__)
    char product_version[64] = "";
    size_t product_version_sz = sizeof(product_version);

    detect_uname(kernel_name, kernel_name_sz, kernel_version, kernel_version_sz);
    copy_token(distro_name, distro_name_sz, "macOS");
    if (sysctlbyname("kern.osproductversion", product_version, &product_version_sz, NULL, 0) == 0) {
        copy_version_token(distro_version, distro_version_sz, product_version);
    }
    if (!distro_version[0]) {
        snprintf(distro_version, distro_version_sz, "%s", kernel_version[0] ? kernel_version : "0");
    }
#else
    char value[128];

    detect_uname(kernel_name, kernel_name_sz, kernel_version, kernel_version_sz);
#if defined(__linux__)
    if (read_os_release_value("NAME", value, sizeof(value))) {
        copy_token(distro_name, distro_name_sz, value);
    }
    if (read_os_release_value("VERSION_ID", value, sizeof(value))) {
        copy_version_token(distro_version, distro_version_sz, value);
    }
#endif
    if (!distro_name[0]) {
        snprintf(distro_name, distro_name_sz, "%s", kernel_name[0] ? kernel_name : "Unknown");
    }
    if (!distro_version[0]) {
        snprintf(distro_version, distro_version_sz, "%s", kernel_version[0] ? kernel_version : "0");
    }
#endif

    if (!kernel_name[0]) {
#if defined(_WIN32)
        copy_token(kernel_name, kernel_name_sz, "NT");
#elif defined(__APPLE__)
        copy_token(kernel_name, kernel_name_sz, "Darwin");
#else
        copy_token(kernel_name, kernel_name_sz, "Linux");
#endif
    }
    if (!kernel_version[0]) {
        snprintf(kernel_version, kernel_version_sz, "%s", distro_version[0] ? distro_version : "0");
    }
}

const char *api_user_agent(void)
{
    static char user_agent[USER_AGENT_MAX];
    static int ready;

    if (!ready) {
        char distro_name[48];
        char distro_version[48];
        char kernel_name[48];
        char kernel_version[48];

        detect_platform(distro_name, sizeof(distro_name),
                        distro_version, sizeof(distro_version),
                        kernel_name, sizeof(kernel_name),
                        kernel_version, sizeof(kernel_version));
        snprintf(user_agent, sizeof(user_agent), USER_AGENT_FMT,
                 NVT_VERSION, distro_name, distro_version, kernel_name, kernel_version);
        ready = 1;
    }

    return user_agent;
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    Buffer *buf = userp;
    if (buf->len + total + 1 > buf->cap) {
        buf->cap = (buf->len + total + 1) * 2;
        buf->data = realloc(buf->data, buf->cap);
        if (!buf->data) return 0;
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

/* ── generic fetch ───────────────────────────────────────────────── */

static char *http_get_timeout(const char *url, long timeout_sec)
{
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    Buffer buf = { .data = malloc(4096), .len = 0, .cap = 4096 };
    if (!buf.data) { curl_easy_cleanup(curl); return NULL; }
    buf.data[0] = '\0';

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept: */*");
    hdrs = curl_slist_append(hdrs, "Accept-Language: fr-FR,fr;q=0.9");
    hdrs = curl_slist_append(hdrs, "Connection: keep-alive");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, api_user_agent());
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

static char *http_get(const char *url)
{
    return http_get_timeout(url, 15L);
}

static char *http_get_query(const char *base, const char *query, long timeout_sec)
{
    CURL *curl = curl_easy_init();
    char *encoded;
    char *url;
    char *body;
    size_t url_len;

    if (!curl) return NULL;
    encoded = curl_easy_escape(curl, query, 0);
    if (!encoded) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    url_len = strlen(base) + strlen("?data=") + strlen(encoded) + 1;
    url = malloc(url_len);
    if (!url) {
        curl_free(encoded);
        curl_easy_cleanup(curl);
        return NULL;
    }

    snprintf(url, url_len, "%s?data=%s", base, encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);

    body = http_get_timeout(url, timeout_sec);
    free(url);
    return body;
}

static char *http_get_extra_header_timeout(const char *url, const char *extra_header, long timeout_sec)
{
    CURL *curl = curl_easy_init();
    Buffer buf = { .data = NULL, .len = 0, .cap = 0 };
    struct curl_slist *hdrs = NULL;
    CURLcode res;

    if (!curl) return NULL;

    buf.data = malloc(4096);
    if (!buf.data) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    buf.cap = 4096;
    buf.data[0] = '\0';

    hdrs = curl_slist_append(hdrs, "Accept: application/json");
    hdrs = curl_slist_append(hdrs, "Accept-Language: fr-FR,fr;q=0.9");
    hdrs = curl_slist_append(hdrs, "Connection: keep-alive");
    if (extra_header && extra_header[0]) hdrs = curl_slist_append(hdrs, extra_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, api_user_agent());
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);

    res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

static char *http_get_basic_auth_timeout(const char *url, const char *username,
                                         const char *password, long timeout_sec)
{
    CURL *curl = curl_easy_init();
    Buffer buf = { .data = NULL, .len = 0, .cap = 0 };
    struct curl_slist *hdrs = NULL;
    CURLcode res;

    if (!curl || !username || !username[0]) {
        if (curl) curl_easy_cleanup(curl);
        return NULL;
    }

    buf.data = malloc(4096);
    if (!buf.data) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    buf.cap = 4096;
    buf.data[0] = '\0';

    hdrs = curl_slist_append(hdrs, "Accept: application/json");
    hdrs = curl_slist_append(hdrs, "Accept-Language: fr-FR,fr;q=0.9");
    hdrs = curl_slist_append(hdrs, "Connection: keep-alive");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, api_user_agent());
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password ? password : "");

    res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

static const char *idfm_api_key(void)
{
    return IDFM_API_KEY[0] ? IDFM_API_KEY : NULL;
}

static const char *sncf_api_key(void)
{
    return SNCF_API_KEY[0] ? SNCF_API_KEY : NULL;
}

static char *http_get_idfm(const char *url)
{
    char header[192];
    const char *key = idfm_api_key();

    if (!key || !key[0]) return NULL;
    /* The live PRIM Navitia gateway accepts the dedicated apikey header here. */
    snprintf(header, sizeof(header), "apikey: %s", key);
    return http_get_extra_header_timeout(url, header, 20L);
}

static char *http_get_sncf(const char *url)
{
    const char *key = sncf_api_key();

    if (!key || !key[0]) return NULL;
    return http_get_basic_auth_timeout(url, key, "", 20L);
}

static int json_array_has_string(const cJSON *arr, const char *value)
{
    cJSON *item;

    if (!cJSON_IsArray(arr) || !value || !value[0]) return 0;
    cJSON_ArrayForEach(item, arr) {
        if (cJSON_IsString(item) && strcmp(item->valuestring, value) == 0) return 1;
    }
    return 0;
}

static int utf8_is_continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

static int utf8_sequence_length(const unsigned char *src)
{
    unsigned char c0;
    unsigned char c1;
    unsigned char c2;
    unsigned char c3;

    if (!src || !src[0]) return 0;
    c0 = src[0];
    c1 = src[1];
    c2 = src[2];
    c3 = src[3];

    if (c0 <= 0x7F) return 1;
    if (c0 >= 0xC2 && c0 <= 0xDF && c1 && utf8_is_continuation(c1)) return 2;
    if (c0 == 0xE0 && c1 >= 0xA0 && c1 <= 0xBF && c2 && utf8_is_continuation(c2)) return 3;
    if (((c0 >= 0xE1 && c0 <= 0xEC) || c0 == 0xEE || c0 == 0xEF) &&
        c1 && utf8_is_continuation(c1) &&
        c2 && utf8_is_continuation(c2)) {
        return 3;
    }
    if (c0 == 0xED && c1 >= 0x80 && c1 <= 0x9F && c2 && utf8_is_continuation(c2)) return 3;
    if (c0 == 0xF0 && c1 >= 0x90 && c1 <= 0xBF &&
        c2 && utf8_is_continuation(c2) &&
        c3 && utf8_is_continuation(c3)) {
        return 4;
    }
    if (c0 >= 0xF1 && c0 <= 0xF3 &&
        c1 && utf8_is_continuation(c1) &&
        c2 && utf8_is_continuation(c2) &&
        c3 && utf8_is_continuation(c3)) {
        return 4;
    }
    if (c0 == 0xF4 && c1 >= 0x80 && c1 <= 0x8F &&
        c2 && utf8_is_continuation(c2) &&
        c3 && utf8_is_continuation(c3)) {
        return 4;
    }
    return 0;
}

static unsigned cp1252_codepoint(unsigned char byte)
{
    switch (byte) {
    case 0x80: return 0x20AC;
    case 0x82: return 0x201A;
    case 0x83: return 0x0192;
    case 0x84: return 0x201E;
    case 0x85: return 0x2026;
    case 0x86: return 0x2020;
    case 0x87: return 0x2021;
    case 0x88: return 0x02C6;
    case 0x89: return 0x2030;
    case 0x8A: return 0x0160;
    case 0x8B: return 0x2039;
    case 0x8C: return 0x0152;
    case 0x8E: return 0x017D;
    case 0x91: return 0x2018;
    case 0x92: return 0x2019;
    case 0x93: return 0x201C;
    case 0x94: return 0x201D;
    case 0x95: return 0x2022;
    case 0x96: return 0x2013;
    case 0x97: return 0x2014;
    case 0x98: return 0x02DC;
    case 0x99: return 0x2122;
    case 0x9A: return 0x0161;
    case 0x9B: return 0x203A;
    case 0x9C: return 0x0153;
    case 0x9E: return 0x017E;
    case 0x9F: return 0x0178;
    default: return byte;
    }
}

static void utf8_append_codepoint(char *dst, size_t dst_sz, size_t *len, unsigned codepoint)
{
    if (!dst || !dst_sz || !len) return;
    if (*len >= dst_sz - 1) return;

    if (codepoint <= 0x7F) {
        if (*len + 1 >= dst_sz) return;
        dst[(*len)++] = (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        if (*len + 2 >= dst_sz) return;
        dst[(*len)++] = (char)(0xC0 | (codepoint >> 6));
        dst[(*len)++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        if (*len + 3 >= dst_sz) return;
        dst[(*len)++] = (char)(0xE0 | (codepoint >> 12));
        dst[(*len)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        dst[(*len)++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        if (*len + 4 >= dst_sz) return;
        dst[(*len)++] = (char)(0xF0 | (codepoint >> 18));
        dst[(*len)++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        dst[(*len)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        dst[(*len)++] = (char)(0x80 | (codepoint & 0x3F));
    }
    dst[*len] = '\0';
}

static void copy_utf8_sanitized(char *dst, size_t dst_sz, const char *src)
{
    size_t len = 0;
    const unsigned char *p = (const unsigned char *)src;

    if (!dst || !dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    while (*p && len + 1 < dst_sz) {
        int seq_len = utf8_sequence_length(p);

        if (seq_len == 1 && *p <= 0x7F) {
            dst[len++] = (char)*p++;
            dst[len] = '\0';
            continue;
        }

        if (seq_len >= 2) {
            if (len + (size_t)seq_len >= dst_sz) break;
            memcpy(dst + len, p, (size_t)seq_len);
            len += (size_t)seq_len;
            dst[len] = '\0';
            p += seq_len;
            continue;
        }

        utf8_append_codepoint(dst, dst_sz, &len, cp1252_codepoint(*p));
        p++;
    }
}

static int decode_named_html_entity(const char *name, unsigned *codepoint)
{
    if (strcmp(name, "nbsp") == 0) *codepoint = ' ';
    else if (strcmp(name, "amp") == 0) *codepoint = '&';
    else if (strcmp(name, "lt") == 0) *codepoint = '<';
    else if (strcmp(name, "gt") == 0) *codepoint = '>';
    else if (strcmp(name, "quot") == 0) *codepoint = '"';
    else if (strcmp(name, "apos") == 0) *codepoint = '\'';
    else if (strcmp(name, "eacute") == 0) *codepoint = 0xE9;
    else if (strcmp(name, "egrave") == 0) *codepoint = 0xE8;
    else if (strcmp(name, "ecirc") == 0) *codepoint = 0xEA;
    else if (strcmp(name, "agrave") == 0) *codepoint = 0xE0;
    else if (strcmp(name, "ccedil") == 0) *codepoint = 0xE7;
    else if (strcmp(name, "uuml") == 0) *codepoint = 0xFC;
    else return 0;
    return 1;
}

static void html_to_text(char *dst, size_t dst_sz, const char *src)
{
    size_t len = 0;
    int need_space = 0;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] && len + 1 < dst_sz; ) {
        if (src[i] == '<') {
            size_t j = i + 1;
            int newline = 0;

            while (src[j] && src[j] != '>') j++;
            if (src[i + 1] == 'p' || src[i + 1] == '/' || src[i + 1] == 'b' || src[i + 1] == 'l') {
                newline = 1;
            }
            if (newline && len > 0 && dst[len - 1] != '\n') dst[len++] = '\n';
            dst[len] = '\0';
            i = src[j] == '>' ? j + 1 : j;
            need_space = 0;
            continue;
        }

        if (src[i] == '&') {
            char entity[32];
            size_t j = i + 1;
            size_t k = 0;
            unsigned codepoint = 0;

            while (src[j] && src[j] != ';' && k + 1 < sizeof(entity)) entity[k++] = src[j++];
            entity[k] = '\0';
            if (src[j] == ';') j++;

            if (entity[0] == '#') {
                codepoint = (entity[1] == 'x' || entity[1] == 'X')
                    ? (unsigned)strtoul(entity + 2, NULL, 16)
                    : (unsigned)strtoul(entity + 1, NULL, 10);
                utf8_append_codepoint(dst, dst_sz, &len, codepoint ? codepoint : '?');
                i = j;
                need_space = 0;
                continue;
            }
            if (decode_named_html_entity(entity, &codepoint)) {
                utf8_append_codepoint(dst, dst_sz, &len, codepoint);
                i = j;
                need_space = 0;
                continue;
            }
        }

        if (isspace((unsigned char)src[i])) {
            if (!need_space && len > 0 && dst[len - 1] != '\n') {
                dst[len++] = ' ';
                dst[len] = '\0';
            }
            need_space = 1;
            i++;
            continue;
        }

        dst[len++] = src[i++];
        dst[len] = '\0';
        need_space = 0;
    }

    while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\n')) dst[--len] = '\0';
}

static void parse_hex_triplet(const char *src, int *r, int *g, int *b)
{
    unsigned value = 0;

    if (src && src[0] == '#') src++;
    if (src && strlen(src) >= 6) sscanf(src, "%x", &value);
    if (r) *r = (value >> 16) & 0xFF;
    if (g) *g = (value >> 8) & 0xFF;
    if (b) *b = value & 0xFF;
}

static int parse_navitia_datetime(const char *src, struct tm *tm_out)
{
    struct tm tmv;

    if (!src || strlen(src) < 15 || !tm_out) return 0;
    memset(&tmv, 0, sizeof(tmv));
    if (sscanf(src, "%4d%2d%2dT%2d%2d%2d",
               &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday,
               &tmv.tm_hour, &tmv.tm_min, &tmv.tm_sec) != 6) {
        return 0;
    }
    tmv.tm_year -= 1900;
    tmv.tm_mon -= 1;
    tmv.tm_isdst = -1;
    *tm_out = tmv;
    return 1;
}

static int navitia_datetime_diff_seconds(const char *current_dt, const char *target_dt)
{
    struct tm current_tm;
    struct tm target_tm;
    time_t current_ts;
    time_t target_ts;

    if (!parse_navitia_datetime(current_dt, &current_tm)) return -1;
    if (!parse_navitia_datetime(target_dt, &target_tm)) return -1;

    current_ts = mktime(&current_tm);
    target_ts = mktime(&target_tm);
    if (current_ts == (time_t)-1 || target_ts == (time_t)-1) return -1;
    return (int)difftime(target_ts, current_ts);
}

static void navitia_to_iso_datetime(const char *src, char *dst, size_t dst_sz)
{
    struct tm tmv;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (!parse_navitia_datetime(src, &tmv)) return;
    strftime(dst, dst_sz, "%Y-%m-%dT%H:%M:%S", &tmv);
}

static void seconds_to_waiting_time(int seconds, char *dst, size_t dst_sz)
{
    int hours;
    int minutes;
    int secs;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (seconds < 0) return;
    hours = seconds / 3600;
    minutes = (seconds % 3600) / 60;
    secs = seconds % 60;
    snprintf(dst, dst_sz, "%02d:%02d:%02d", hours, minutes, secs);
}

/* ── helpers ─────────────────────────────────────────────────────── */

static const char *jstr(const cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static int jint(const cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}


static bool jbool(const cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsTrue(v);
}

static double jarr_double(const cJSON *arr, int idx)
{
    cJSON *v = cJSON_GetArrayItem((cJSON *)arr, idx);
    return cJSON_IsNumber(v) ? v->valuedouble : 0.0;
}

static double jcoord_double(const cJSON *pt, int idx, const char *key)
{
    if (cJSON_IsArray(pt)) return jarr_double(pt, idx);
    if (cJSON_IsObject(pt)) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)pt, key);
        return cJSON_IsNumber(v) ? v->valuedouble : 0.0;
    }
    return 0.0;
}

#define SCOPY(dst, src) copy_utf8_sanitized((dst), sizeof(dst), (src))

/* ── API init / cleanup ──────────────────────────────────────────── */

void api_init(void)  { curl_global_init(CURL_GLOBAL_DEFAULT); }
void api_cleanup(void) { curl_global_cleanup(); }

/* ── fetch_lines ─────────────────────────────────────────────────── */

int fetch_lines(Line *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "?key=" API_KEY "&typename=sv_ligne_a");

    char *raw = http_get(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    int n = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        if (n >= max) break;
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        if (!p) continue;

        Line *l = &out[n];
        l->gid    = jint(p, "gid");
        SCOPY(l->libelle, jstr(p, "libelle"));
        l->ident  = jint(p, "ident");
        SCOPY(l->vehicule, jstr(p, "vehicule"));
        l->active = jbool(p, "active");
        l->sae    = jbool(p, "sae");
        n++;
    }
    cJSON_Delete(root);
    return n;
}

/* ── fetch_stops (into hashmap) ──────────────────────────────────── */

void stopmap_init(StopMap *m)
{
    memset(m->buckets, 0, sizeof(m->buckets));
}

void stopmap_free(StopMap *m)
{
    for (int i = 0; i < STOP_BUCKETS; i++) {
        StopEntry *e = m->buckets[i];
        while (e) {
            StopEntry *next = e->next;
            free(e);
            e = next;
        }
        m->buckets[i] = NULL;
    }
}

static void stopmap_insert(StopMap *m, int gid, const char *libelle, double lon, double lat)
{
    unsigned h = (unsigned)gid % STOP_BUCKETS;
    /* check dups */
    for (StopEntry *e = m->buckets[h]; e; e = e->next)
        if (e->gid == gid) return;
    StopEntry *e = malloc(sizeof(*e));
    e->gid = gid;
    SCOPY(e->libelle, libelle);
    e->lon = lon;
    e->lat = lat;
    e->next = m->buckets[h];
    m->buckets[h] = e;
}

const char *stopmap_lookup(const StopMap *m, int gid)
{
    unsigned h = (unsigned)gid % STOP_BUCKETS;
    for (StopEntry *e = m->buckets[h]; e; e = e->next)
        if (e->gid == gid) return e->libelle;
    return "?";
}

bool stopmap_lookup_pos(const StopMap *m, int gid, double *lon, double *lat)
{
    unsigned h = (unsigned)gid % STOP_BUCKETS;
    for (StopEntry *e = m->buckets[h]; e; e = e->next) {
        if (e->gid != gid) continue;
        if (lon) *lon = e->lon;
        if (lat) *lat = e->lat;
        return true;
    }
    return false;
}

int fetch_stops(StopMap *map)
{
    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "?key=" API_KEY "&typename=sv_arret_p");

    char *raw = http_get(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    int n = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        cJSON *geom = cJSON_GetObjectItemCaseSensitive(feat, "geometry");
        cJSON *coords = geom ? cJSON_GetObjectItemCaseSensitive(geom, "coordinates") : NULL;
        double lon = jcoord_double(coords, 0, "lon");
        double lat = jcoord_double(coords, 1, "lat");
        if (!p) continue;
        stopmap_insert(map, jint(p, "gid"), jstr(p, "libelle"), lon, lat);
        n++;
    }
    cJSON_Delete(root);
    return n;
}

/* ── fetch_stop_groups ───────────────────────────────────────────── */

int fetch_stop_groups(StopGroup *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "?key=" API_KEY "&typename=sv_arret_p");

    char *raw = http_get(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    int n = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        if (!p) continue;

        const char *libelle = jstr(p, "libelle");
        int gid = jint(p, "gid");

        /* find existing group by libelle */
        int found = -1;
        for (int i = 0; i < n; i++) {
            if (strcmp(out[i].libelle, libelle) == 0) {
                found = i;
                break;
            }
        }

        if (found >= 0) {
            /* add gid to existing group */
            if (out[found].ngids < 16)
                out[found].gids[out[found].ngids++] = gid;
        } else if (n < max) {
            /* new group */
            StopGroup *sg = &out[n];
            SCOPY(sg->libelle, libelle);
            SCOPY(sg->groupe, libelle);
            sg->gids[0] = gid;
            sg->ngids = 1;
            n++;
        }
    }
    cJSON_Delete(root);
    return n;
}

static int parse_prefixed_id(const char *src)
{
    const char *p;

    if (!src || !src[0]) return 0;
    p = strrchr(src, '_');
    if (p && isdigit((unsigned char)p[1])) return atoi(p + 1);
    p = strrchr(src, ':');
    if (p && isdigit((unsigned char)p[1])) return atoi(p + 1);
    while (*src && !isdigit((unsigned char)*src)) src++;
    return atoi(src);
}

static void parse_rgb_triplet(const char *src, int *r, int *g, int *b)
{
    int rv = 0, gv = 0, bv = 0;
    if (src) sscanf(src, "(%d,%d,%d)", &rv, &gv, &bv);
    if (r) *r = rv;
    if (g) *g = gv;
    if (b) *b = bv;
}

static void append_token(char *dst, size_t dst_sz, const char *token)
{
    size_t len;

    if (!dst || !dst_sz || !token || !token[0]) return;
    len = strlen(dst);
    if (len && len + 1 < dst_sz) {
        dst[len++] = ' ';
        dst[len] = '\0';
    }
    if (len < dst_sz - 1) {
        copy_utf8_sanitized(dst + len, dst_sz - len, token);
    }
}

static int load_toulouse_lines_live(ToulouseLine *out, int max, int *total_count)
{
    char url[512];
    char *raw;
    cJSON *root, *lines_obj, *items, *item;
    int n = 0;

    if (total_count) *total_count = 0;
    snprintf(url, sizeof(url), TISSEO_API_BASE "/lines.json?network=%s&displayTerminus=1&key=%s",
             TISSEO_NETWORK_ENCODED, TISSEO_API_KEY);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    lines_obj = cJSON_GetObjectItemCaseSensitive(root, "lines");
    items = lines_obj ? cJSON_GetObjectItemCaseSensitive(lines_obj, "line") : NULL;
    if (total_count) *total_count = cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0;

    cJSON_ArrayForEach(item, items) {
        ToulouseLine *line;

        if (n >= max) break;
        line = &out[n];
        memset(line, 0, sizeof(*line));
        SCOPY(line->ref, jstr(item, "id"));
        line->id = parse_prefixed_id(jstr(item, "id"));
        SCOPY(line->code, jstr(item, "shortName"));
        SCOPY(line->libelle, jstr(item, "name"));
        SCOPY(line->couleur, jstr(item, "bgXmlColor"));
        SCOPY(line->texte_couleur, jstr(item, "fgXmlColor"));
        parse_rgb_triplet(jstr(item, "color"), &line->r, &line->g, &line->b);

        cJSON *mode = cJSON_GetObjectItemCaseSensitive(item, "transportMode");
        cJSON *terminus = cJSON_GetObjectItemCaseSensitive(item, "terminus");
        SCOPY(line->mode, mode ? jstr(mode, "name") : "");
        line->terminus_count = 0;
        cJSON *term;
        cJSON_ArrayForEach(term, terminus) {
            int dup = 0;
            if (line->terminus_count >= (int)(sizeof(line->terminus_refs) / sizeof(line->terminus_refs[0]))) break;
            for (int i = 0; i < line->terminus_count; i++) {
                if (strcmp(line->terminus_refs[i], jstr(term, "id")) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;
            SCOPY(line->terminus_refs[line->terminus_count], jstr(term, "id"));
            SCOPY(line->terminus_names[line->terminus_count], jstr(term, "name"));
            line->terminus_count++;
        }
        n++;
    }

    cJSON_Delete(root);
    return n;
}

static void stop_line_list_from_array(const cJSON *items, const char *short_key,
                                      char *dst, size_t dst_sz, char *mode, size_t mode_sz)
{
    cJSON *item;

    if (dst_sz) dst[0] = '\0';
    if (mode_sz) mode[0] = '\0';
    cJSON_ArrayForEach(item, items) {
        append_token(dst, dst_sz, jstr(item, short_key));
        if (mode && mode_sz && !mode[0]) {
            cJSON *transport_mode = cJSON_GetObjectItemCaseSensitive(item, "transportMode");
            if (transport_mode) copy_utf8_sanitized(mode, mode_sz, jstr(transport_mode, "name"));
        }
    }
}

static int load_toulouse_stop_areas_live(ToulouseStop *out, int max, int *total_count)
{
    char url[512];
    char *raw;
    cJSON *root, *stops_obj, *items, *item;
    int n = 0;

    if (total_count) *total_count = 0;
    snprintf(url, sizeof(url), TISSEO_API_BASE "/stop_areas.json?network=%s&displayLines=1&key=%s",
             TISSEO_NETWORK_ENCODED, TISSEO_API_KEY);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    stops_obj = cJSON_GetObjectItemCaseSensitive(root, "stopAreas");
    items = stops_obj ? cJSON_GetObjectItemCaseSensitive(stops_obj, "stopArea") : NULL;
    if (total_count) *total_count = cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0;

    cJSON_ArrayForEach(item, items) {
        ToulouseStop *stop;
        cJSON *lines;

        if (n >= max) break;
        stop = &out[n];
        memset(stop, 0, sizeof(*stop));
        SCOPY(stop->ref, jstr(item, "id"));
        stop->id = parse_prefixed_id(jstr(item, "id"));
        SCOPY(stop->libelle, jstr(item, "name"));
        SCOPY(stop->commune, jstr(item, "cityName"));
        lines = cJSON_GetObjectItemCaseSensitive(item, "line");
        stop_line_list_from_array(lines, "shortName", stop->lignes, sizeof(stop->lignes),
                                  stop->mode, sizeof(stop->mode));
        n++;
    }

    cJSON_Delete(root);
    return n;
}

static int load_toulouse_stop_points_live(ToulouseStop *out, int max, int *total_count)
{
    char url[512];
    char *raw;
    cJSON *root, *stops_obj, *items, *item;
    int n = 0;

    if (total_count) *total_count = 0;
    snprintf(url, sizeof(url), TISSEO_API_BASE "/stop_points.json?network=%s&key=%s",
             TISSEO_NETWORK_ENCODED, TISSEO_API_KEY);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    stops_obj = cJSON_GetObjectItemCaseSensitive(root, "physicalStops");
    items = stops_obj ? cJSON_GetObjectItemCaseSensitive(stops_obj, "physicalStop") : NULL;
    if (total_count) *total_count = cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0;

    cJSON_ArrayForEach(item, items) {
        ToulouseStop *stop;
        cJSON *lines;
        cJSON *stop_area;

        if (n >= max) break;
        stop = &out[n];
        memset(stop, 0, sizeof(*stop));
        SCOPY(stop->ref, jstr(item, "id"));
        stop->id = parse_prefixed_id(jstr(item, "id"));
        SCOPY(stop->libelle, jstr(item, "name"));
        lines = cJSON_GetObjectItemCaseSensitive(item, "lines");
        stop_line_list_from_array(lines, "short_name", stop->lignes, sizeof(stop->lignes),
                                  stop->mode, sizeof(stop->mode));
        stop_area = cJSON_GetObjectItemCaseSensitive(item, "stopArea");
        if (stop_area) {
            SCOPY(stop->commune, jstr(stop_area, "cityName"));
            SCOPY(stop->adresse, jstr(stop_area, "name"));
        }
        n++;
    }

    cJSON_Delete(root);
    return n;
}

int fetch_toulouse_snapshot(ToulouseSnapshot *snap, ToulouseLine *lines, int max_lines,
                            ToulouseStop *stops, int max_stops)
{
    int nlines;
    int nstops;

    if (!snap) return -1;
    memset(snap, 0, sizeof(*snap));
    SCOPY(snap->doc_ref, "DOCUMENTATION_DEVELOPPEUR_API_2_FR");
    SCOPY(snap->doc_version, "2");
    SCOPY(snap->doc_date, "2025-05-21");
    SCOPY(snap->lines_url, TISSEO_API_BASE "/lines.json?network=Tisseo&displayTerminus=1");
    SCOPY(snap->stops_url, TISSEO_API_BASE "/stop_areas.json?network=Tisseo&displayLines=1");
    snap->live = 1;

    nlines = load_toulouse_lines_live(lines, max_lines, &snap->total_lines);
    nstops = load_toulouse_stop_areas_live(stops, max_stops, &snap->total_stops);
    if (nstops < 0) {
        SCOPY(snap->stops_url, TISSEO_API_BASE "/stop_points.json?network=Tisseo");
        nstops = load_toulouse_stop_points_live(stops, max_stops, &snap->total_stops);
    }

    snap->sample_lines = nlines > 0 ? nlines : 0;
    snap->sample_stops = nstops > 0 ? nstops : 0;
    if (nlines < 0 && nstops < 0) return -1;
    return 0;
}

int fetch_toulouse_alerts(ToulouseAlert *out, int max)
{
    char url[512];
    char *raw;
    cJSON *root, *items, *item;
    int n = 0;

    snprintf(url, sizeof(url), TISSEO_API_BASE "/messages.json?network=%s&key=%s",
             TISSEO_NETWORK_ENCODED, TISSEO_API_KEY);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    items = cJSON_GetObjectItemCaseSensitive(root, "messages");
    cJSON_ArrayForEach(item, items) {
        ToulouseAlert *alert;
        cJSON *message;
        cJSON *lines;

        if (n >= max) break;
        message = cJSON_GetObjectItemCaseSensitive(item, "message");
        if (!message) continue;
        alert = &out[n];
        memset(alert, 0, sizeof(*alert));
        SCOPY(alert->id, jstr(message, "id"));
        SCOPY(alert->titre, jstr(message, "title"));
        SCOPY(alert->message, jstr(message, "content"));
        SCOPY(alert->importance, jstr(message, "importanceLevel"));
        SCOPY(alert->scope, jstr(message, "scope"));
        lines = cJSON_GetObjectItemCaseSensitive(item, "lines");
        stop_line_list_from_array(lines, "shortName", alert->lines, sizeof(alert->lines), NULL, 0);
        n++;
    }

    cJSON_Delete(root);
    return n;
}

int fetch_toulouse_passages(const char *stop_area_ref, ToulousePassage *out, int max)
{
    char url[768];
    char *raw;
    cJSON *root, *departures, *areas, *area, *schedules, *schedule, *journeys, *journey;
    int n = 0;

    if (!stop_area_ref || !stop_area_ref[0]) return -1;

    snprintf(url, sizeof(url),
             TISSEO_API_BASE "/stops_schedules.json?stopAreaId=%s&timetableByArea=1&number=3&displayRealTime=1&key=%s",
             stop_area_ref, TISSEO_API_KEY);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    departures = cJSON_GetObjectItemCaseSensitive(root, "departures");
    areas = departures ? cJSON_GetObjectItemCaseSensitive(departures, "stopAreas") : NULL;
    cJSON_ArrayForEach(area, areas) {
        schedules = cJSON_GetObjectItemCaseSensitive(area, "schedules");
        cJSON_ArrayForEach(schedule, schedules) {
            cJSON *line = cJSON_GetObjectItemCaseSensitive(schedule, "line");
            cJSON *destination = cJSON_GetObjectItemCaseSensitive(schedule, "destination");
            cJSON *stop = cJSON_GetObjectItemCaseSensitive(schedule, "stop");

            journeys = cJSON_GetObjectItemCaseSensitive(schedule, "journeys");
            cJSON_ArrayForEach(journey, journeys) {
                ToulousePassage *passage;
                if (n >= max) break;
                passage = &out[n];
                memset(passage, 0, sizeof(*passage));
                SCOPY(passage->line_code, line ? jstr(line, "shortName") : "");
                SCOPY(passage->line_name, line ? jstr(line, "name") : "");
                SCOPY(passage->destination, destination ? jstr(destination, "name") : "");
                SCOPY(passage->stop_name, stop ? jstr(stop, "name") : jstr(area, "name"));
                SCOPY(passage->datetime, jstr(journey, "dateTime"));
                SCOPY(passage->waiting_time, jstr(journey, "waiting_time"));
                passage->realtime = atoi(jstr(journey, "realTime")) > 0;
                passage->delayed = 0;
                n++;
            }
            if (n >= max) break;
        }
        if (n >= max) break;
    }

    cJSON_Delete(root);
    return n;
}

static int waiting_minutes(const char *waiting_time)
{
    int hh = 0, mm = 0, ss = 0;

    if (!waiting_time || !waiting_time[0]) return -1;
    if (sscanf(waiting_time, "%d:%d:%d", &hh, &mm, &ss) != 3) return -1;
    return hh * 60 + mm + (ss > 0 ? 1 : 0);
}

static const char *schedule_way_to_dir(const cJSON *destination)
{
    const char *way = destination ? jstr(destination, "way") : "";

    if (strcmp(way, "backward") == 0) return "RETOUR";
    return "ALLER";
}

int fetch_toulouse_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max)
{
    char stops_list[384];
    char url[1024];
    char *raw;
    cJSON *root, *departures, *areas, *area, *schedules, *schedule, *journeys, *journey;
    int n = 0;

    if (!line || !out || max <= 0 || line->terminus_count <= 0 || !line->ref[0]) return -1;

    stops_list[0] = '\0';
    for (int i = 0; i < line->terminus_count; i++) {
        append_token(stops_list, sizeof(stops_list), line->terminus_refs[i]);
    }
    if (!stops_list[0]) return -1;

    for (char *p = stops_list; *p; p++) {
        if (*p == ' ') *p = ',';
    }

    snprintf(url, sizeof(url),
             TISSEO_API_BASE "/stops_schedules.json?stopsList=%s&lineId=%s&timetableByArea=1&number=3&displayRealTime=1&key=%s",
             stops_list, line->ref, TISSEO_API_KEY);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    departures = cJSON_GetObjectItemCaseSensitive(root, "departures");
    areas = departures ? cJSON_GetObjectItemCaseSensitive(departures, "stopAreas") : NULL;
    cJSON_ArrayForEach(area, areas) {
        schedules = cJSON_GetObjectItemCaseSensitive(area, "schedules");
        cJSON_ArrayForEach(schedule, schedules) {
            cJSON *line_obj = cJSON_GetObjectItemCaseSensitive(schedule, "line");
            cJSON *destination = cJSON_GetObjectItemCaseSensitive(schedule, "destination");
            cJSON *stop = cJSON_GetObjectItemCaseSensitive(schedule, "stop");

            journeys = cJSON_GetObjectItemCaseSensitive(schedule, "journeys");
            cJSON_ArrayForEach(journey, journeys) {
                ToulouseVehicle *vehicle;
                int eta;

                if (n >= max) break;
                vehicle = &out[n];
                memset(vehicle, 0, sizeof(*vehicle));
                SCOPY(vehicle->line_code, line_obj ? jstr(line_obj, "shortName") : line->code);
                SCOPY(vehicle->line_name, line_obj ? jstr(line_obj, "name") : line->libelle);
                SCOPY(vehicle->current_stop, stop ? jstr(stop, "name") : jstr(area, "name"));
                SCOPY(vehicle->next_stop, destination ? jstr(destination, "name") : "");
                SCOPY(vehicle->terminus, destination ? jstr(destination, "name") : "");
                SCOPY(vehicle->sens, schedule_way_to_dir(destination));
                SCOPY(vehicle->datetime, jstr(journey, "dateTime"));
                SCOPY(vehicle->waiting_time, jstr(journey, "waiting_time"));
                vehicle->realtime = atoi(jstr(journey, "realTime")) > 0;
                vehicle->delayed = vehicle->realtime ? 0 : 1;
                vehicle->vitesse = 0;
                eta = waiting_minutes(vehicle->waiting_time);
                vehicle->arret = eta >= 0 && eta <= 1;
                n++;
            }
            if (n >= max) break;
        }
        if (n >= max) break;
    }

    cJSON_Delete(root);
    return n;
}

static const char *idfm_line_mode_name(const cJSON *item)
{
    cJSON *commercial_mode = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "commercial_mode");
    cJSON *physical_modes = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "physical_modes");
    cJSON *physical_mode = cJSON_GetArrayItem(physical_modes, 0);

    if (commercial_mode && jstr(commercial_mode, "name")[0]) return jstr(commercial_mode, "name");
    if (physical_mode && jstr(physical_mode, "name")[0]) return jstr(physical_mode, "name");
    return "";
}

static void idfm_fill_line(ToulouseLine *line, const cJSON *item)
{
    cJSON *routes = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "routes");
    cJSON *route;

    memset(line, 0, sizeof(*line));
    SCOPY(line->ref, jstr(item, "id"));
    line->id = parse_prefixed_id(jstr(item, "id"));
    SCOPY(line->code, jstr(item, "code"));
    if (!line->code[0]) SCOPY(line->code, jstr(item, "name"));
    SCOPY(line->libelle, jstr(item, "name"));
    SCOPY(line->mode, idfm_line_mode_name(item));
    SCOPY(line->couleur, jstr(item, "color"));
    SCOPY(line->texte_couleur, jstr(item, "text_color"));
    parse_hex_triplet(line->couleur, &line->r, &line->g, &line->b);

    line->terminus_count = 0;
    cJSON_ArrayForEach(route, routes) {
        cJSON *direction = cJSON_GetObjectItemCaseSensitive(route, "direction");
        const char *direction_id = direction ? jstr(direction, "id") : "";
        const char *direction_name = direction ? jstr(direction, "name") : jstr(route, "name");
        int dup = 0;

        if (line->terminus_count >= (int)(sizeof(line->terminus_refs) / sizeof(line->terminus_refs[0]))) break;
        for (int i = 0; i < line->terminus_count; i++) {
            if (strcmp(line->terminus_refs[i], direction_id) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;
        SCOPY(line->terminus_refs[line->terminus_count], direction_id);
        SCOPY(line->terminus_names[line->terminus_count], direction_name);
        line->terminus_count++;
    }
}

int fetch_idfm_snapshot(IdfmSnapshot *snap, ToulouseLine *lines, int max_lines)
{
    int n = 0;
    int total_lines = 0;

    if (!snap || !lines || max_lines <= 0) return -1;
    memset(snap, 0, sizeof(*snap));
    SCOPY(snap->doc_ref, "https://prim.iledefrance-mobilites.fr/fr/aide-et-contact/documentation/generalites/ressources");
    SCOPY(snap->doc_version, "v2");
    SCOPY(snap->doc_date, "2026-03-14");
    SCOPY(snap->lines_url, IDFM_API_BASE "/lines");
    SCOPY(snap->stops_url, IDFM_API_BASE "/lines/{line_id}/stop_areas");
    SCOPY(snap->alerts_url, IDFM_LINE_REPORTS_BASE "/line_reports");
    snap->live = 1;

    for (int page = 0; n < max_lines; page++) {
        char url[512];
        char *raw;
        cJSON *root;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        int items_on_page;

        snprintf(url, sizeof(url), IDFM_API_BASE "/lines?count=1000&start_page=%d", page);
        raw = http_get_idfm(url);
        if (!raw) return n > 0 ? 0 : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? 0 : -1;

        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        total_lines = jint(pagination, "total_result");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "lines");

        cJSON_ArrayForEach(item, items) {
            if (n >= max_lines) break;
            idfm_fill_line(&lines[n], item);
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0 || n >= total_lines) break;
    }

    snap->total_lines = total_lines > 0 ? total_lines : n;
    snap->sample_lines = n;
    return n > 0 ? 0 : -1;
}

int fetch_idfm_line_stops(const ToulouseLine *line, ToulouseStop *out, int max)
{
    int n = 0;

    if (!line || !line->ref[0] || !out || max <= 0) return -1;

    for (int page = 0; n < max; page++) {
        char url[768];
        char *raw;
        cJSON *root;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        int items_on_page;

        snprintf(url, sizeof(url), IDFM_API_BASE "/lines/%s/stop_areas?count=100&start_page=%d",
                 line->ref, page);
        raw = http_get_idfm(url);
        if (!raw) return n > 0 ? n : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? n : -1;

        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "stop_areas");

        cJSON_ArrayForEach(item, items) {
            ToulouseStop *stop;
            cJSON *coord;
            cJSON *regions;
            cJSON *region;

            if (n >= max) break;
            stop = &out[n];
            memset(stop, 0, sizeof(*stop));
            SCOPY(stop->ref, jstr(item, "id"));
            stop->id = parse_prefixed_id(jstr(item, "id"));
            SCOPY(stop->libelle, jstr(item, "name"));
            SCOPY(stop->adresse, jstr(item, "label"));
            SCOPY(stop->lignes, line->code);
            SCOPY(stop->mode, line->mode);

            coord = cJSON_GetObjectItemCaseSensitive(item, "coord");
            if (coord) {
                stop->lon = atof(jstr(coord, "lon"));
                stop->lat = atof(jstr(coord, "lat"));
            }

            regions = cJSON_GetObjectItemCaseSensitive(item, "administrative_regions");
            region = cJSON_GetArrayItem(regions, 0);
            if (region) SCOPY(stop->commune, jstr(region, "name"));
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0) break;
    }

    return n;
}

static const char *idfm_best_message_text(const cJSON *messages, char *title, size_t title_sz)
{
    static char text[2048];
    cJSON *message;
    const char *fallback = "";

    if (title_sz) title[0] = '\0';
    text[0] = '\0';

    cJSON_ArrayForEach(message, messages) {
        cJSON *channel = cJSON_GetObjectItemCaseSensitive(message, "channel");
        cJSON *types = channel ? cJSON_GetObjectItemCaseSensitive(channel, "types") : NULL;
        const char *msg = jstr(message, "text");

        if (!msg[0]) continue;
        if (json_array_has_string(types, "title")) {
            copy_utf8_sanitized(title, title_sz, msg);
            continue;
        }
        if (json_array_has_string(types, "web")) {
            html_to_text(text, sizeof(text), msg);
            return text;
        }
        if (!fallback[0]) fallback = msg;
    }

    if (fallback[0]) html_to_text(text, sizeof(text), fallback);
    return text;
}

static void idfm_impacted_line_codes(const cJSON *impacted_objects, char *dst, size_t dst_sz)
{
    cJSON *entry;

    if (!dst_sz) return;
    dst[0] = '\0';
    cJSON_ArrayForEach(entry, impacted_objects) {
        cJSON *pt_object = cJSON_GetObjectItemCaseSensitive(entry, "pt_object");
        cJSON *line = pt_object ? cJSON_GetObjectItemCaseSensitive(pt_object, "line") : NULL;
        const char *code = line ? jstr(line, "code") : "";

        if (code[0]) append_token(dst, dst_sz, code);
    }
}

int fetch_idfm_alerts(ToulouseAlert *out, int max)
{
    int n = 0;

    if (!out || max <= 0) return -1;

    for (int page = 0; n < max; page++) {
        char url[512];
        char *raw;
        cJSON *root;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        int items_on_page;

        snprintf(url, sizeof(url), IDFM_LINE_REPORTS_BASE "/line_reports?count=100&start_page=%d", page);
        raw = http_get_idfm(url);
        if (!raw) return n > 0 ? n : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? n : -1;

        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "disruptions");

        cJSON_ArrayForEach(item, items) {
            ToulouseAlert *alert;
            cJSON *severity;
            const char *message;

            if (n >= max) break;
            alert = &out[n];
            memset(alert, 0, sizeof(*alert));
            severity = cJSON_GetObjectItemCaseSensitive(item, "severity");
            SCOPY(alert->id, jstr(item, "id"));
            SCOPY(alert->scope, jstr(item, "category"));
            SCOPY(alert->importance, severity ? jstr(severity, "name") : "");
            message = idfm_best_message_text(
                cJSON_GetObjectItemCaseSensitive(item, "messages"),
                alert->titre,
                sizeof(alert->titre)
            );
            SCOPY(alert->message, message);
            if (!alert->titre[0]) SCOPY(alert->titre, alert->message);
            idfm_impacted_line_codes(cJSON_GetObjectItemCaseSensitive(item, "impacted_objects"),
                                     alert->lines, sizeof(alert->lines));
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0) break;
    }

    return n;
}

static int idfm_departure_matches_line(const cJSON *departure, const ToulouseLine *line)
{
    cJSON *route = cJSON_GetObjectItemCaseSensitive((cJSON *)departure, "route");
    cJSON *line_obj = route ? cJSON_GetObjectItemCaseSensitive(route, "line") : NULL;
    cJSON *display = cJSON_GetObjectItemCaseSensitive((cJSON *)departure, "display_informations");
    const char *line_id = line_obj ? jstr(line_obj, "id") : "";
    const char *code = display ? jstr(display, "code") : "";

    if (line && line->ref[0] && strcmp(line_id, line->ref) == 0) return 1;
    if (line && line->code[0] && strcmp(code, line->code) == 0) return 1;
    return 0;
}

int fetch_idfm_passages(const ToulouseLine *line, const ToulouseStop *stop, ToulousePassage *out, int max)
{
    char url[768];
    char *raw;
    cJSON *root;
    cJSON *context;
    cJSON *items;
    cJSON *item;
    const char *current_dt;
    int n = 0;

    if (!line || !stop || !stop->ref[0] || !out || max <= 0) return -1;

    snprintf(url, sizeof(url), IDFM_API_BASE "/stop_areas/%s/departures?count=1000", stop->ref);
    raw = http_get_idfm(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    context = cJSON_GetObjectItemCaseSensitive(root, "context");
    current_dt = context ? jstr(context, "current_datetime") : "";
    items = cJSON_GetObjectItemCaseSensitive(root, "departures");

    cJSON_ArrayForEach(item, items) {
        ToulousePassage *passage;
        cJSON *display;
        cJSON *route;
        cJSON *direction;
        cJSON *stop_date_time;
        const char *departure_dt;
        const char *base_departure_dt;
        int wait_seconds;

        if (n >= max) break;
        if (!idfm_departure_matches_line(item, line)) continue;

        display = cJSON_GetObjectItemCaseSensitive(item, "display_informations");
        route = cJSON_GetObjectItemCaseSensitive(item, "route");
        direction = route ? cJSON_GetObjectItemCaseSensitive(route, "direction") : NULL;
        stop_date_time = cJSON_GetObjectItemCaseSensitive(item, "stop_date_time");
        departure_dt = stop_date_time ? jstr(stop_date_time, "departure_date_time") : "";
        base_departure_dt = stop_date_time ? jstr(stop_date_time, "base_departure_date_time") : "";
        wait_seconds = navitia_datetime_diff_seconds(current_dt, departure_dt);

        passage = &out[n];
        memset(passage, 0, sizeof(*passage));
        SCOPY(passage->line_code, display ? jstr(display, "code") : line->code);
        SCOPY(passage->line_name, display ? jstr(display, "name") : line->libelle);
        SCOPY(passage->destination, display ? jstr(display, "direction") : (direction ? jstr(direction, "name") : ""));
        SCOPY(passage->stop_name, stop->libelle);
        navitia_to_iso_datetime(departure_dt, passage->datetime, sizeof(passage->datetime));
        seconds_to_waiting_time(wait_seconds < 0 ? 0 : wait_seconds, passage->waiting_time, sizeof(passage->waiting_time));
        passage->realtime = stop_date_time && strcmp(jstr(stop_date_time, "data_freshness"), "base_schedule") != 0;
        passage->delayed = departure_dt[0] && base_departure_dt[0] && strcmp(departure_dt, base_departure_dt) != 0;
        n++;
    }

    cJSON_Delete(root);
    return n;
}

int fetch_idfm_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max)
{
    char url[768];
    char *raw;
    cJSON *root;
    cJSON *items;
    cJSON *entry;
    int n = 0;

    if (!line || !line->ref[0] || !out || max <= 0) return -1;

    snprintf(url, sizeof(url), IDFM_API_BASE "/lines/%s/vehicle_positions?count=1", line->ref);
    raw = http_get_idfm(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    items = cJSON_GetObjectItemCaseSensitive(root, "vehicle_positions");
    entry = cJSON_GetArrayItem(items, 0);
    if (entry) {
        cJSON *journeys = cJSON_GetObjectItemCaseSensitive(entry, "vehicle_journey_positions");
        cJSON *journey;

        cJSON_ArrayForEach(journey, journeys) {
            ToulouseVehicle *vehicle;
            cJSON *vehicle_journey;

            if (n >= max) break;
            vehicle_journey = cJSON_GetObjectItemCaseSensitive(journey, "vehicle_journey");
            if (!vehicle_journey) continue;

            vehicle = &out[n];
            memset(vehicle, 0, sizeof(*vehicle));
            SCOPY(vehicle->line_code, line->code);
            SCOPY(vehicle->line_name, line->libelle);
            SCOPY(vehicle->current_stop, jstr(vehicle_journey, "name"));
            SCOPY(vehicle->terminus, jstr(vehicle_journey, "headsign"));
            SCOPY(vehicle->sens, jstr(vehicle_journey, "headsign"));
            vehicle->realtime = 1;
            n++;
        }
    }

    cJSON_Delete(root);
    return n;
}

static const char *sncf_line_mode_name(const cJSON *item)
{
    cJSON *physical_modes = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "physical_modes");
    cJSON *physical_mode = cJSON_GetArrayItem(physical_modes, 0);
    cJSON *commercial_mode = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "commercial_mode");

    if (physical_mode && jstr(physical_mode, "name")[0]) return jstr(physical_mode, "name");
    if (commercial_mode && jstr(commercial_mode, "name")[0]) return jstr(commercial_mode, "name");
    return "";
}

static void sncf_fill_line(ToulouseLine *line, const cJSON *item)
{
    cJSON *routes = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "routes");
    cJSON *route;

    memset(line, 0, sizeof(*line));
    SCOPY(line->ref, jstr(item, "id"));
    line->id = parse_prefixed_id(jstr(item, "id"));
    SCOPY(line->code, jstr(item, "code"));
    if (!line->code[0]) SCOPY(line->code, jstr(item, "name"));
    SCOPY(line->libelle, jstr(item, "name"));
    if (!line->libelle[0]) SCOPY(line->libelle, line->code);
    SCOPY(line->mode, sncf_line_mode_name(item));
    SCOPY(line->couleur, jstr(item, "color"));
    SCOPY(line->texte_couleur, jstr(item, "text_color"));
    parse_hex_triplet(line->couleur, &line->r, &line->g, &line->b);

    cJSON_ArrayForEach(route, routes) {
        cJSON *direction = cJSON_GetObjectItemCaseSensitive(route, "direction");
        const char *direction_id = direction ? jstr(direction, "id") : "";
        const char *direction_name = direction ? jstr(direction, "name") : jstr(route, "name");
        int dup = 0;

        if (line->terminus_count >= (int)(sizeof(line->terminus_refs) / sizeof(line->terminus_refs[0]))) break;
        for (int i = 0; i < line->terminus_count; i++) {
            if (strcmp(line->terminus_refs[i], direction_id) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;
        SCOPY(line->terminus_refs[line->terminus_count], direction_id);
        SCOPY(line->terminus_names[line->terminus_count], direction_name);
        line->terminus_count++;
    }
}

int fetch_sncf_snapshot(IdfmSnapshot *snap, ToulouseLine *lines, int max_lines)
{
    int n = 0;
    int total_lines = 0;

    if (!snap || !lines || max_lines <= 0) return -1;
    memset(snap, 0, sizeof(*snap));
    SCOPY(snap->doc_ref, "https://doc.navitia.io/");
    SCOPY(snap->doc_version, "v1");
    SCOPY(snap->doc_date, "2026-03-14");
    SCOPY(snap->lines_url, SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/lines");
    SCOPY(snap->stops_url, SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/lines/{line_id}/stop_areas");
    SCOPY(snap->alerts_url, SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/traffic_reports");
    snap->live = 1;

    for (int page = 0; n < max_lines; page++) {
        char url[512];
        char *raw;
        cJSON *root;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        int items_on_page;

        snprintf(url, sizeof(url), SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/lines?count=1000&start_page=%d", page);
        raw = http_get_sncf(url);
        if (!raw) return n > 0 ? 0 : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? 0 : -1;

        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        total_lines = jint(pagination, "total_result");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "lines");

        cJSON_ArrayForEach(item, items) {
            if (n >= max_lines) break;
            sncf_fill_line(&lines[n], item);
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0 || n >= total_lines) break;
    }

    snap->total_lines = total_lines > 0 ? total_lines : n;
    snap->sample_lines = n;
    return n > 0 ? 0 : -1;
}

int fetch_sncf_line_stops(const ToulouseLine *line, ToulouseStop *out, int max)
{
    int n = 0;

    if (!line || !line->ref[0] || !out || max <= 0) return -1;

    for (int page = 0; n < max; page++) {
        char url[768];
        char *raw;
        cJSON *root;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        int items_on_page;

        snprintf(url, sizeof(url), SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/lines/%s/stop_areas?count=100&start_page=%d",
                 line->ref, page);
        raw = http_get_sncf(url);
        if (!raw) return n > 0 ? n : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? n : -1;

        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "stop_areas");

        cJSON_ArrayForEach(item, items) {
            ToulouseStop *stop;
            cJSON *coord;
            cJSON *regions;
            cJSON *region;

            if (n >= max) break;
            stop = &out[n];
            memset(stop, 0, sizeof(*stop));
            SCOPY(stop->ref, jstr(item, "id"));
            stop->id = parse_prefixed_id(jstr(item, "id"));
            SCOPY(stop->libelle, jstr(item, "name"));
            SCOPY(stop->adresse, jstr(item, "label"));
            SCOPY(stop->lignes, line->code);
            SCOPY(stop->mode, line->mode);

            coord = cJSON_GetObjectItemCaseSensitive(item, "coord");
            if (coord) {
                stop->lon = atof(jstr(coord, "lon"));
                stop->lat = atof(jstr(coord, "lat"));
            }

            regions = cJSON_GetObjectItemCaseSensitive(item, "administrative_regions");
            region = cJSON_GetArrayItem(regions, 0);
            if (region) SCOPY(stop->commune, jstr(region, "name"));
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0) break;
    }

    return n;
}

int fetch_sncf_alerts(ToulouseAlert *out, int max)
{
    int n = 0;

    if (!out || max <= 0) return -1;

    for (int page = 0; n < max; page++) {
        char url[512];
        char *raw;
        cJSON *root;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        int items_on_page;

        snprintf(url, sizeof(url), SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/traffic_reports?count=100&start_page=%d", page);
        raw = http_get_sncf(url);
        if (!raw) return n > 0 ? n : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? n : -1;

        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "disruptions");

        cJSON_ArrayForEach(item, items) {
            ToulouseAlert *alert;
            cJSON *severity;
            const char *message;
            const char *status = jstr(item, "status");

            if (n >= max) break;
            if (strcmp(status, "past") == 0) continue;

            alert = &out[n];
            memset(alert, 0, sizeof(*alert));
            severity = cJSON_GetObjectItemCaseSensitive(item, "severity");
            SCOPY(alert->id, jstr(item, "id"));
            SCOPY(alert->scope, status[0] ? status : "network");
            SCOPY(alert->importance, severity ? jstr(severity, "name") : "");
            message = idfm_best_message_text(
                cJSON_GetObjectItemCaseSensitive(item, "messages"),
                alert->titre,
                sizeof(alert->titre)
            );
            SCOPY(alert->message, message);
            if (!alert->titre[0]) SCOPY(alert->titre, alert->importance[0] ? alert->importance : "Alerte SNCF");
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0) break;
    }

    return n;
}

int fetch_sncf_passages(const ToulouseLine *line, const ToulouseStop *stop, ToulousePassage *out, int max)
{
    char url[768];
    char *raw;
    cJSON *root;
    cJSON *context;
    cJSON *items;
    cJSON *item;
    const char *current_dt;
    int n = 0;

    if (!line || !stop || !stop->ref[0] || !out || max <= 0) return -1;

    snprintf(url, sizeof(url), SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/stop_areas/%s/departures?count=1000", stop->ref);
    raw = http_get_sncf(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    context = cJSON_GetObjectItemCaseSensitive(root, "context");
    current_dt = context ? jstr(context, "current_datetime") : "";
    items = cJSON_GetObjectItemCaseSensitive(root, "departures");

    cJSON_ArrayForEach(item, items) {
        ToulousePassage *passage;
        cJSON *display;
        cJSON *route;
        cJSON *direction;
        cJSON *stop_date_time;
        const char *departure_dt;
        const char *base_departure_dt;
        int wait_seconds;

        if (n >= max) break;
        if (!idfm_departure_matches_line(item, line)) continue;

        display = cJSON_GetObjectItemCaseSensitive(item, "display_informations");
        route = cJSON_GetObjectItemCaseSensitive(item, "route");
        direction = route ? cJSON_GetObjectItemCaseSensitive(route, "direction") : NULL;
        stop_date_time = cJSON_GetObjectItemCaseSensitive(item, "stop_date_time");
        departure_dt = stop_date_time ? jstr(stop_date_time, "departure_date_time") : "";
        base_departure_dt = stop_date_time ? jstr(stop_date_time, "base_departure_date_time") : "";
        wait_seconds = navitia_datetime_diff_seconds(current_dt, departure_dt);
        if (wait_seconds < 0) continue;

        passage = &out[n];
        memset(passage, 0, sizeof(*passage));
        SCOPY(passage->line_code, display ? jstr(display, "code") : line->code);
        SCOPY(passage->line_name, display ? jstr(display, "name") : line->libelle);
        SCOPY(passage->destination, display ? jstr(display, "direction") : (direction ? jstr(direction, "name") : ""));
        SCOPY(passage->stop_name, stop->libelle);
        navitia_to_iso_datetime(departure_dt, passage->datetime, sizeof(passage->datetime));
        seconds_to_waiting_time(wait_seconds, passage->waiting_time, sizeof(passage->waiting_time));
        passage->realtime = stop_date_time && strcmp(jstr(stop_date_time, "data_freshness"), "base_schedule") != 0;
        passage->delayed = departure_dt[0] && base_departure_dt[0] && strcmp(departure_dt, base_departure_dt) != 0;
        n++;
    }

    cJSON_Delete(root);
    return n;
}

static const char *navitia_link_id(const cJSON *links, const char *type)
{
    cJSON *link;

    cJSON_ArrayForEach(link, links) {
        if (strcmp(jstr(link, "type"), type) == 0) return jstr(link, "id");
    }
    return "";
}

int fetch_sncf_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max)
{
    int n = 0;

    if (!line || !line->ref[0] || !out || max <= 0) return -1;

    for (int page = 0; n < max; page++) {
        char url[768];
        char *raw;
        cJSON *root;
        cJSON *context;
        cJSON *pagination;
        cJSON *items;
        cJSON *item;
        const char *current_dt;
        int items_on_page;

        snprintf(url, sizeof(url), SNCF_API_BASE "/coverage/" SNCF_COVERAGE "/lines/%s/departures?count=100&start_page=%d",
                 line->ref, page);
        raw = http_get_sncf(url);
        if (!raw) return n > 0 ? n : -1;

        root = cJSON_Parse(raw);
        free(raw);
        if (!root) return n > 0 ? n : -1;

        context = cJSON_GetObjectItemCaseSensitive(root, "context");
        current_dt = context ? jstr(context, "current_datetime") : "";
        pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
        items_on_page = jint(pagination, "items_on_page");
        items = cJSON_GetObjectItemCaseSensitive(root, "departures");

        cJSON_ArrayForEach(item, items) {
            ToulouseVehicle *vehicle;
            cJSON *display;
            cJSON *stop_point;
            cJSON *stop_date_time;
            const char *vehicle_ref;
            const char *departure_dt;
            const char *base_departure_dt;
            int wait_seconds;
            int dup = 0;

            if (n >= max) break;
            if (!idfm_departure_matches_line(item, line)) continue;

            stop_date_time = cJSON_GetObjectItemCaseSensitive(item, "stop_date_time");
            departure_dt = stop_date_time ? jstr(stop_date_time, "departure_date_time") : "";
            base_departure_dt = stop_date_time ? jstr(stop_date_time, "base_departure_date_time") : "";
            wait_seconds = navitia_datetime_diff_seconds(current_dt, departure_dt);
            if (wait_seconds < 0) continue;

            vehicle_ref = navitia_link_id(cJSON_GetObjectItemCaseSensitive(item, "links"), "vehicle_journey");
            for (int i = 0; i < n; i++) {
                if (vehicle_ref[0] && strcmp(out[i].ref, vehicle_ref) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;

            display = cJSON_GetObjectItemCaseSensitive(item, "display_informations");
            stop_point = cJSON_GetObjectItemCaseSensitive(item, "stop_point");
            vehicle = &out[n];
            memset(vehicle, 0, sizeof(*vehicle));
            SCOPY(vehicle->ref, vehicle_ref);
            SCOPY(vehicle->line_code, display ? jstr(display, "code") : line->code);
            SCOPY(vehicle->line_name, display ? jstr(display, "name") : line->libelle);
            SCOPY(vehicle->current_stop, stop_point ? jstr(stop_point, "name") : "");
            SCOPY(vehicle->next_stop, stop_point ? jstr(stop_point, "name") : "");
            SCOPY(vehicle->terminus, display ? jstr(display, "direction") : "");
            SCOPY(vehicle->sens, display ? jstr(display, "headsign") : "");
            navitia_to_iso_datetime(departure_dt, vehicle->datetime, sizeof(vehicle->datetime));
            seconds_to_waiting_time(wait_seconds, vehicle->waiting_time, sizeof(vehicle->waiting_time));
            vehicle->realtime = stop_date_time && strcmp(jstr(stop_date_time, "data_freshness"), "base_schedule") != 0;
            vehicle->delayed = departure_dt[0] && base_departure_dt[0] && strcmp(departure_dt, base_departure_dt) != 0;
            n++;
        }
        cJSON_Delete(root);

        if (items_on_page <= 0 || n >= max) break;
    }

    return n;
}

/* ── external basemap (French commune contours) ──────────────────── */

static void metro_map_init(MetroMap *m)
{
    memset(m, 0, sizeof(*m));
    m->minlon = MAP_BDX_WEST;
    m->maxlon = MAP_BDX_EAST;
    m->minlat = MAP_BDX_SOUTH;
    m->maxlat = MAP_BDX_NORTH;
}

static int map_store_add_path_arrays(MapPoint *points, int *npoints, int max_points,
                                     MapPath *paths, int *npaths, int max_paths,
                                     const cJSON *geom, unsigned char kind);
static int metro_map_add_path_arrays(MetroMap *m, const cJSON *geom, unsigned char kind);

static void line_route_map_init(LineRouteMap *m)
{
    memset(m, 0, sizeof(*m));
}

static void atlas_map_init(AtlasMap *m)
{
    memset(m, 0, sizeof(*m));
    m->minlon = MAP_BDX_WEST;
    m->maxlon = MAP_BDX_EAST;
    m->minlat = MAP_BDX_SOUTH;
    m->maxlat = MAP_BDX_NORTH;
}

static void atlas_routes_init(AtlasRoutes *m)
{
    memset(m, 0, sizeof(*m));
}

static void metro_map_add_label_pos(MetroMap *m, double lon, double lat, const char *name, unsigned char rank)
{
    if (!name[0] || !rank || m->nlabels >= MAX_MAP_LABELS) return;
    for (int i = 0; i < m->nlabels; i++) {
        if (strcmp(m->labels[i].name, name) == 0) return;
    }
    m->labels[m->nlabels].lon = lon;
    m->labels[m->nlabels].lat = lat;
    SCOPY(m->labels[m->nlabels].name, name);
    m->labels[m->nlabels].rank = rank;
    m->nlabels++;
}

static int map_store_add_path_arrays(MapPoint *points, int *npoints, int max_points,
                                     MapPath *paths, int *npaths, int max_paths,
                                     const cJSON *geom, unsigned char kind)
{
    int n;
    int start, count = 0, step = 1, last_added = -1;

    if (!geom) return 0;
    n = cJSON_GetArraySize((cJSON *)geom);
    if (n < 2 || *npaths >= max_paths || *npoints >= max_points - 2)
        return 0;

    while (((n + step - 1) / step) + 1 > (max_points - *npoints))
        step++;

    start = *npoints;
    for (int i = 0; i < n; i += step) {
        cJSON *pt = cJSON_GetArrayItem((cJSON *)geom, i);
        if (!pt) continue;
        points[*npoints].lon = jcoord_double(pt, 0, "lon");
        points[*npoints].lat = jcoord_double(pt, 1, "lat");
        (*npoints)++;
        count++;
        last_added = i;
        if (*npoints >= max_points) break;
    }

    if (last_added != n - 1 && *npoints < max_points) {
        cJSON *pt = cJSON_GetArrayItem((cJSON *)geom, n - 1);
        if (pt) {
            points[*npoints].lon = jcoord_double(pt, 0, "lon");
            points[*npoints].lat = jcoord_double(pt, 1, "lat");
            (*npoints)++;
            count++;
        }
    }

    if (count < 2) {
        *npoints = start;
        return 0;
    }

    paths[*npaths].start = start;
    paths[*npaths].count = count;
    paths[*npaths].kind = kind;
    (*npaths)++;
    return 1;
}

static int metro_map_add_path_arrays(MetroMap *m, const cJSON *geom, unsigned char kind)
{
    return map_store_add_path_arrays(m->points, &m->npoints, MAX_MAP_POINTS,
                                     m->paths, &m->npaths, MAX_MAP_PATHS,
                                     geom, kind);
}

static void metro_map_add_polygon_feature(MetroMap *m, const cJSON *coords, const char *name)
{
    cJSON *ring = cJSON_GetArrayItem((cJSON *)coords, 0);
    double minlon = 1e9, maxlon = -1e9, minlat = 1e9, maxlat = -1e9;
    int have_geom = 0;
    if (!ring) return;
    if (metro_map_add_path_arrays(m, ring, MAP_KIND_BOUNDARY)) {
        cJSON *pt;
        cJSON_ArrayForEach(pt, ring) {
            double lon = jcoord_double(pt, 0, "lon");
            double lat = jcoord_double(pt, 1, "lat");
            if (lon < minlon) minlon = lon;
            if (lon > maxlon) maxlon = lon;
            if (lat < minlat) minlat = lat;
            if (lat > maxlat) maxlat = lat;
            have_geom = 1;
        }
    }
    if (have_geom && name && name[0])
        metro_map_add_label_pos(m, (minlon + maxlon) / 2.0, (minlat + maxlat) / 2.0, name, 3);
}

static void metro_map_finalize_bounds(MetroMap *m)
{
    if (m->npoints <= 0) return;
    m->minlon = m->maxlon = m->points[0].lon;
    m->minlat = m->maxlat = m->points[0].lat;
    for (int i = 1; i < m->npoints; i++) {
        if (m->points[i].lon < m->minlon) m->minlon = m->points[i].lon;
        if (m->points[i].lon > m->maxlon) m->maxlon = m->points[i].lon;
        if (m->points[i].lat < m->minlat) m->minlat = m->points[i].lat;
        if (m->points[i].lat > m->maxlat) m->maxlat = m->points[i].lat;
    }
}

static int fetch_epci_metro_map(const char *epci_code, MetroMap *out)
{
    char url[512];
    char *raw;
    cJSON *root, *features, *feat;

    snprintf(url, sizeof(url),
             MAP_API_BASE "/epcis/%s/communes?format=geojson&geometry=contour&fields=nom", epci_code);

    metro_map_init(out);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    features = cJSON_GetObjectItemCaseSensitive(root, "features");
    cJSON_ArrayForEach(feat, features) {
        cJSON *geometry = cJSON_GetObjectItemCaseSensitive(feat, "geometry");
        cJSON *properties = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        cJSON *coords;
        const char *type;
        const char *name;

        if (!geometry || !properties) continue;
        coords = cJSON_GetObjectItemCaseSensitive(geometry, "coordinates");
        type = jstr(geometry, "type");
        name = jstr(properties, "nom");
        if (!coords) continue;

        if (strcmp(type, "Polygon") == 0) {
            metro_map_add_polygon_feature(out, coords, name);
        } else if (strcmp(type, "MultiPolygon") == 0) {
            cJSON *poly;
            cJSON_ArrayForEach(poly, coords) {
                metro_map_add_polygon_feature(out, poly, name);
            }
        }
    }

    metro_map_finalize_bounds(out);
    cJSON_Delete(root);
    return out->npaths;
}

int fetch_metro_map(MetroMap *out)
{
    return fetch_epci_metro_map(MAP_BDX_EPCI, out);
}

int fetch_toulouse_metro_map(MetroMap *out)
{
    return fetch_epci_metro_map(MAP_TLS_EPCI, out);
}

static int line_route_add_linestring(LineRouteMap *out, const cJSON *coords, unsigned char kind)
{
    if (!map_store_add_path_arrays(out->points, &out->npoints, MAX_ROUTE_POINTS,
                                   out->paths, &out->npaths, MAX_ROUTE_PATHS,
                                   coords, kind)) {
        return 0;
    }
    if (kind == MAP_KIND_ROUTE_ALLER) out->aller_paths++;
    else if (kind == MAP_KIND_ROUTE_RETOUR) out->retour_paths++;
    return 1;
}

int fetch_line_route(int line_gid, LineRouteMap *out)
{
    char url[512];
    char *raw;
    cJSON *root, *features, *feat;

    snprintf(url, sizeof(url),
             API_BASE "/features/sv_chem_l?key=" API_KEY
             "&filter=%%7B%%22rs_sv_ligne_a%%22:%d%%7D", line_gid);

    line_route_map_init(out);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!features) features = root;

    cJSON_ArrayForEach(feat, features) {
        cJSON *geometry = cJSON_GetObjectItemCaseSensitive(feat, "geometry");
        cJSON *properties = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        cJSON *coords;
        const char *type;
        unsigned char kind;

        if (!geometry || !properties) continue;
        coords = cJSON_GetObjectItemCaseSensitive(geometry, "coordinates");
        type = jstr(geometry, "type");
        if (!coords) continue;

        kind = strcmp(jstr(properties, "sens"), "RETOUR") == 0
             ? MAP_KIND_ROUTE_RETOUR
             : MAP_KIND_ROUTE_ALLER;

        if (strcmp(type, "LineString") == 0) {
            line_route_add_linestring(out, coords, kind);
        } else if (strcmp(type, "MultiLineString") == 0) {
            cJSON *part;
            cJSON_ArrayForEach(part, coords) {
                line_route_add_linestring(out, part, kind);
            }
        }
    }

    cJSON_Delete(root);
    return out->npaths;
}

static int toulouse_route_add_wkt_linestring(LineRouteMap *out, const char *wkt)
{
    const char *p = strchr(wkt, '(');
    int start;
    int count = 0;

    if (!p) return 0;
    p++;
    start = out->npoints;
    while (*p && *p != ')') {
        char *endptr;
        double lon, lat;

        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p || *p == ')') break;
        lon = strtod(p, &endptr);
        if (endptr == p) break;
        p = endptr;
        while (*p && isspace((unsigned char)*p)) p++;
        lat = strtod(p, &endptr);
        if (endptr == p) break;
        p = endptr;

        if (out->npoints >= MAX_ROUTE_POINTS) break;
        out->points[out->npoints].lon = lon;
        out->points[out->npoints].lat = lat;
        out->npoints++;
        count++;
    }

    if (count >= 2 && out->npaths < MAX_ROUTE_PATHS) {
        out->paths[out->npaths].start = start;
        out->paths[out->npaths].count = count;
        out->paths[out->npaths].kind = MAP_KIND_ROUTE_ALLER;
        out->npaths++;
        out->aller_paths++;
        return 1;
    }

    out->npoints = start;
    return 0;
}

int fetch_toulouse_line_route(const ToulouseLine *line, LineRouteMap *out)
{
    char url[768];
    char *raw;
    cJSON *root, *lines_obj, *items, *item;

    if (!line || !out || !line->ref[0]) return -1;

    snprintf(url, sizeof(url),
             TISSEO_API_BASE "/lines.json?lineId=%s&displayGeometry=1&key=%s",
             line->ref, TISSEO_API_KEY);

    line_route_map_init(out);
    raw = http_get(url);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    lines_obj = cJSON_GetObjectItemCaseSensitive(root, "lines");
    items = lines_obj ? cJSON_GetObjectItemCaseSensitive(lines_obj, "line") : NULL;
    cJSON_ArrayForEach(item, items) {
        cJSON *geometry = cJSON_GetObjectItemCaseSensitive(item, "geometry");
        cJSON *entry;

        cJSON_ArrayForEach(entry, geometry) {
            const char *wkt = jstr(entry, "wkt");
            const char *p = wkt;

            while ((p = strstr(p, "LINESTRING")) != NULL) {
                if (!toulouse_route_add_wkt_linestring(out, p)) break;
                p += 10;
            }
        }
    }

    cJSON_Delete(root);
    return out->npaths;
}

static int atlas_map_add_linestring(AtlasMap *out, const cJSON *coords, unsigned char kind)
{
    if (!map_store_add_path_arrays(out->points, &out->npoints, MAX_ATLAS_POINTS,
                                   out->paths, &out->npaths, MAX_ATLAS_PATHS,
                                   coords, kind)) {
        return 0;
    }

    if (kind == MAP_KIND_ROAD) out->road_paths++;
    else if (kind == MAP_KIND_RAIL) out->rail_paths++;
    else if (kind == MAP_KIND_WATER) out->water_paths++;
    return 1;
}

static int atlas_routes_add_linestring(AtlasRoutes *out, const cJSON *coords,
                                       unsigned char kind, int line_gid)
{
    int before = out->npaths;

    if (!map_store_add_path_arrays(out->points, &out->npoints, MAX_ATLAS_ROUTE_POINTS,
                                   out->paths, &out->npaths, MAX_ATLAS_ROUTE_PATHS,
                                   coords, kind)) {
        return 0;
    }

    if (before < out->npaths) out->line_gids[before] = line_gid;
    return 1;
}

static const char *atlas_road_filter(int zoom)
{
    if (zoom >= 3) return "motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|service";
    if (zoom >= 2) return "motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street";
    return "motorway|trunk|primary|secondary|tertiary";
}

static const char *atlas_water_filter(int zoom)
{
    if (zoom >= 3) return "river|canal|stream|ditch|drain";
    if (zoom >= 2) return "river|canal|stream";
    return "river|canal";
}

static int fetch_atlas_map_bbox(double minlon, double maxlon, double minlat, double maxlat, int zoom, AtlasMap *out)
{
    char query[2048];
    char *raw;
    cJSON *root, *elements, *elem;

    atlas_map_init(out);
    out->minlon = minlon;
    out->maxlon = maxlon;
    out->minlat = minlat;
    out->maxlat = maxlat;

    snprintf(query, sizeof(query),
             "[out:json][timeout:25];("
             "way[\"highway\"~\"%s\"](%.6f,%.6f,%.6f,%.6f);"
             "way[\"railway\"~\"rail|tram|light_rail\"](%.6f,%.6f,%.6f,%.6f);"
             "way[\"waterway\"~\"%s\"](%.6f,%.6f,%.6f,%.6f);"
             ");out geom;",
             atlas_road_filter(zoom),
             minlat, minlon, maxlat, maxlon,
             minlat, minlon, maxlat, maxlon,
             atlas_water_filter(zoom),
             minlat, minlon, maxlat, maxlon);

    raw = http_get_query(OVERPASS_API_BASE, query, 30L);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    elements = cJSON_GetObjectItemCaseSensitive(root, "elements");
    cJSON_ArrayForEach(elem, elements) {
        cJSON *tags = cJSON_GetObjectItemCaseSensitive(elem, "tags");
        cJSON *geom = cJSON_GetObjectItemCaseSensitive(elem, "geometry");
        unsigned char kind = 0;

        if (!tags || !geom) continue;
        if (jstr(tags, "highway")[0]) kind = MAP_KIND_ROAD;
        else if (jstr(tags, "railway")[0]) kind = MAP_KIND_RAIL;
        else if (jstr(tags, "waterway")[0]) kind = MAP_KIND_WATER;
        if (!kind) continue;

        atlas_map_add_linestring(out, geom, kind);
    }

    cJSON_Delete(root);
    return out->npaths;
}

int fetch_atlas_map(AtlasMap *out)
{
    return fetch_atlas_map_bbox(MAP_BDX_WEST, MAP_BDX_EAST, MAP_BDX_SOUTH, MAP_BDX_NORTH, 0, out);
}

int fetch_detail_map(double minlon, double maxlon, double minlat, double maxlat, int zoom, AtlasMap *out)
{
    return fetch_atlas_map_bbox(minlon, maxlon, minlat, maxlat, zoom, out);
}

int fetch_atlas_routes(AtlasRoutes *out)
{
    char url[512];
    char *raw;
    cJSON *root, *features, *feat;

    snprintf(url, sizeof(url),
             API_BASE "?key=" API_KEY "&typename=sv_chem_l");

    atlas_routes_init(out);
    raw = http_get_timeout(url, 30L);
    if (!raw) return -1;

    root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    features = cJSON_GetObjectItemCaseSensitive(root, "features");
    cJSON_ArrayForEach(feat, features) {
        cJSON *geometry = cJSON_GetObjectItemCaseSensitive(feat, "geometry");
        cJSON *properties = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        cJSON *coords;
        const char *type;
        unsigned char kind;
        int line_gid;

        if (!geometry || !properties) continue;
        coords = cJSON_GetObjectItemCaseSensitive(geometry, "coordinates");
        type = jstr(geometry, "type");
        line_gid = jint(properties, "rs_sv_ligne_a");
        if (!coords || !line_gid) continue;

        kind = strcmp(jstr(properties, "sens"), "RETOUR") == 0
             ? MAP_KIND_ROUTE_RETOUR
             : MAP_KIND_ROUTE_ALLER;

        if (strcmp(type, "LineString") == 0) {
            atlas_routes_add_linestring(out, coords, kind, line_gid);
        } else if (strcmp(type, "MultiLineString") == 0) {
            cJSON *part;
            cJSON_ArrayForEach(part, coords) {
                atlas_routes_add_linestring(out, part, kind, line_gid);
            }
        }
    }

    cJSON_Delete(root);
    return out->npaths;
}

/* ── CourseCache ─────────────────────────────────────────────────── */

void course_cache_init(CourseCache *cc)
{
    memset(cc->buckets, 0, sizeof(cc->buckets));
}

void course_cache_free(CourseCache *cc)
{
    for (int i = 0; i < COURSE_BUCKETS; i++) {
        CourseEntry *e = cc->buckets[i];
        while (e) {
            CourseEntry *next = e->next;
            free(e);
            e = next;
        }
        cc->buckets[i] = NULL;
    }
}

static CourseEntry *course_cache_lookup(CourseCache *cc, int gid)
{
    unsigned h = (unsigned)gid % COURSE_BUCKETS;
    for (CourseEntry *e = cc->buckets[h]; e; e = e->next)
        if (e->gid == gid) return e;
    return NULL;
}

static void course_cache_insert(CourseCache *cc, int gid, int ligne_id, int terminus_gid)
{
    if (course_cache_lookup(cc, gid)) return;
    CourseEntry *e = malloc(sizeof(*e));
    e->gid = gid;
    e->ligne_id = ligne_id;
    e->terminus_gid = terminus_gid;
    unsigned h = (unsigned)gid % COURSE_BUCKETS;
    e->next = cc->buckets[h];
    cc->buckets[h] = e;
}

static int course_cache_loaded;

static void course_cache_load_all(CourseCache *cc)
{
    if (course_cache_loaded) return;
    course_cache_loaded = 1;

    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "?key=" API_KEY "&typename=sv_cours_a");

    char *raw = http_get(url);
    if (!raw) return;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return;

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        if (!p) continue;
        course_cache_insert(cc, jint(p, "gid"),
                            jint(p, "rs_sv_ligne_a"),
                            jint(p, "rg_sv_arret_p_na"));
    }
    cJSON_Delete(root);
}

/* ── fetch_passages ──────────────────────────────────────────────── */

static void parse_hhmm(const char *datetime, char *out, size_t sz)
{
    /* datetime format: "2026-03-10 14:03:00" or similar */
    /* extract HH:MM */
    out[0] = '\0';
    if (!datetime || !datetime[0]) return;
    const char *space = strchr(datetime, 'T');
    if (!space) space = strchr(datetime, ' ');
    if (!space) return;
    space++;
    /* space now points to "HH:MM:SS" */
    if (strlen(space) >= 5) {
        snprintf(out, sz, "%.5s", space);
    }
}

int fetch_passages(int stop_gid, Passage *out, int max, CourseCache *cc)
{
    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "/features/sv_horai_a?key=" API_KEY
             "&filter=%%7B%%22etat%%22:%%22NON_REALISE%%22,%%22rs_sv_arret_p%%22:%d%%7D",
             stop_gid);

    char *raw = http_get(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!features) features = root;

    int n = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        if (n >= max) break;
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        if (!p) continue;

        Passage *pa = &out[n];
        parse_hhmm(jstr(p, "hor_estime"), pa->hor_estime, sizeof(pa->hor_estime));
        parse_hhmm(jstr(p, "hor_theo"), pa->hor_theo, sizeof(pa->hor_theo));
        pa->cours_id = jint(p, "rs_sv_cours_a");

        /* bulk-load all courses on first call */
        course_cache_load_all(cc);
        CourseEntry *ce = course_cache_lookup(cc, pa->cours_id);
        if (ce) {
            pa->ligne_id = ce->ligne_id;
            pa->terminus_gid = ce->terminus_gid;
        } else {
            pa->ligne_id = 0;
            pa->terminus_gid = 0;
        }
        n++;
    }
    cJSON_Delete(root);
    return n;
}

/* ── fetch_vehicles ──────────────────────────────────────────────── */

int fetch_vehicles(int line_gid, Vehicle *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "/features/sv_vehic_p?key=" API_KEY
             "&filter=%%7B%%22rs_sv_ligne_a%%22:%d%%7D", line_gid);

    char *raw = http_get(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    /* The filtered endpoint may return a FeatureCollection or bare array */
    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!features) features = root;   /* bare array fallback */

    int n = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        if (n >= max) break;
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        if (!p) continue;
        cJSON *geom = cJSON_GetObjectItemCaseSensitive(feat, "geometry");
        cJSON *coords = geom ? cJSON_GetObjectItemCaseSensitive(geom, "coordinates") : NULL;

        Vehicle *v = &out[n];
        v->gid       = jint(p, "gid");
        v->lon       = jcoord_double(coords, 0, "lon");
        v->lat       = jcoord_double(coords, 1, "lat");
        SCOPY(v->etat, jstr(p, "etat"));
        v->retard    = jint(p, "retard");
        v->vitesse   = jint(p, "vitesse");
        SCOPY(v->vehicule, jstr(p, "vehicule"));
        SCOPY(v->statut, jstr(p, "statut"));
        SCOPY(v->sens, jstr(p, "sens"));
        SCOPY(v->terminus, jstr(p, "terminus"));
        v->arret     = jbool(p, "arret");
        v->arret_actu = jint(p, "rs_sv_arret_p_actu");
        v->arret_suiv = jint(p, "rs_sv_arret_p_suiv");
        v->ligne_id  = jint(p, "rs_sv_ligne_a");
        n++;
    }
    cJSON_Delete(root);
    return n;
}

/* ── fetch_alerts ────────────────────────────────────────────────── */

int fetch_alerts(Alert *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url),
             API_BASE "?key=" API_KEY "&typename=sv_messa_a");

    char *raw = http_get(url);
    if (!raw) return -1;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) return -1;

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    int n = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        if (n >= max) break;
        cJSON *p = cJSON_GetObjectItemCaseSensitive(feat, "properties");
        if (!p) continue;

        Alert *a = &out[n];
        a->gid      = jint(p, "gid");
        SCOPY(a->titre, jstr(p, "titre"));
        SCOPY(a->message, jstr(p, "message"));
        SCOPY(a->severite, jstr(p, "severite"));
        a->ligne_id = jint(p, "rs_sv_ligne_a");
        n++;
    }
    cJSON_Delete(root);
    return n;
}
