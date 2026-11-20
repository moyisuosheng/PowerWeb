/*
 * powerweb.c —— PowerWeb 局域网电源控制服务（Windows 原生实现，无第三方依赖）
 *
 * 手机浏览器访问 http://<电脑IP>:8000 ，即可关机 / 重启 / 睡眠 / 锁屏 / 预约关机。
 *
 * 编译（x64）：
 *   x86_64-w64-mingw32-gcc -O2 -static -s -o PowerWeb.exe powerweb.c -lws2_32
 * 编译（MSVC）：
 *   cl /O2 powerweb.c ws2_32.lib
 *
 * 用法：
 *   PowerWeb.exe                    默认 0.0.0.0:8000
 *   PowerWeb.exe --port 8123
 *   PowerWeb.exe --key 1234         开启访问密钥
 *   PowerWeb.exe --dry-run          演练模式，只打印命令不真执行
 *   PowerWeb.exe --html other.html  指定页面文件
 */

#define __USE_MINGW_ANSI_STDIO 1
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#  pragma comment(lib, "ws2_32.lib")   /* MSVC 用；MinGW 走 -lws2_32 */
#endif

/* 版本号由 Makefile 生成到 build/version.h；单独手写 gcc 命令编译时用下面的兜底值。 */
#if defined(__has_include)
#  if __has_include("version.h")
#    include "version.h"
#  endif
#endif
#ifndef PW_VERSION
#  define PW_VERSION "0.0.0-dev"
#endif

#define REQ_MAX   (16 * 1024)
#define HTML_MAX  (2 * 1024 * 1024)
#define MAX_DELAY 315360000LL        /* Windows shutdown /t 上限：10 年 */

/* ------------------------------------------------------------------ 全局状态 */

typedef struct {
    char      action[16];            /* "shutdown" / "restart" / "" */
    long long deadlineTs;            /* 到点执行的 Unix 时间（秒），0 表示无预约 */
    long long createdTs;
} AppState;

static AppState         g_state;
static CRITICAL_SECTION g_lock;
static char             g_key[128];
static int              g_dryRun = 0;
static int              g_hidden = 0;   /* --hidden：启动后立即隐藏控制台窗口 */
static int              g_port   = 8000;
static char             g_exeDir[MAX_PATH];
static char             g_htmlPath[MAX_PATH + 32];   /* 目录 + index.html */
static char             g_statePath[MAX_PATH + 32];

/* ------------------------------------------------------------------ 小工具 */

static long long now_ts(void) { return (long long)time(NULL); }

static void logmsg(const char *fmt, ...) {
    char          tbuf[32];
    time_t        t = time(NULL);
    struct tm    *tmv = localtime(&t);
    va_list       ap;

    if (tmv) strftime(tbuf, sizeof tbuf, "%H:%M:%S", tmv);
    else     strcpy(tbuf, "--:--:--");

    printf("[%s] ", tbuf);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void fmt_ts(long long ts, const char *fmt, char *out, size_t n) {
    time_t     t = (time_t)ts;
    struct tm *tmv = localtime(&t);
    if (tmv) strftime(out, n, fmt, tmv);
    else     out[0] = '\0';
}

/* 把 ASCII 命令行转成宽字符后启动，返回退出码；--dry-run 时只打印 */
static int run_cmd(const char *cmd_ascii) {
    wchar_t           wcmd[512];
    STARTUPINFOW      si;
    PROCESS_INFORMATION pi;
    DWORD             code = 0;
    DWORD             waitMs = 20000;

    if (g_dryRun) {
        logmsg("DRY-RUN: %s", cmd_ascii);
        return 0;
    }

    MultiByteToWideChar(CP_UTF8, 0, cmd_ascii, -1, wcmd, 512);

    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb          = sizeof si;
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        logmsg("CreateProcess 失败(%lu)：%s", (unsigned long)GetLastError(), cmd_ascii);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, waitMs);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

/* ------------------------------------------------------------------ 电源动作 */

static int action_shutdown(const char *action, long long seconds) {
    char cmd[256];

    /* 先撤掉已有计划，避免 Windows 报“已存在关机计划” */
    run_cmd("shutdown.exe /a");

    snprintf(cmd, sizeof cmd, "shutdown.exe %s /t %lld",
             (strcmp(action, "restart") == 0) ? "/r" : "/s", seconds);
    return run_cmd(cmd);
}

static int action_cancel(void) { return run_cmd("shutdown.exe /a"); }

static void action_lock(void) { LockWorkStation(); }

static void action_sleep(void) {
    typedef BOOLEAN(WINAPI * PFN_SetSuspendState)(BOOLEAN, BOOLEAN, BOOLEAN);
    HMODULE              h  = LoadLibraryA("powrprof.dll");
    PFN_SetSuspendState  fn = NULL;

    if (!h) { logmsg("加载 powrprof.dll 失败"); return; }
    fn = (PFN_SetSuspendState)(void *)GetProcAddress(h, "SetSuspendState");
    if (fn) fn(FALSE, FALSE, FALSE);
    else    logmsg("GetProcAddress(SetSuspendState) 失败");
    FreeLibrary(h);
}

/* ------------------------------------------------------------------ 状态持久化 */

static void state_save(void) {
    FILE *f = fopen(g_statePath, "wb");
    if (!f) return;
    fprintf(f, "{\"action\":\"%s\",\"deadlineTs\":%lld,\"createdTs\":%lld}\n",
            g_state.action, g_state.deadlineTs, g_state.createdTs);
    fclose(f);
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* 极简 JSON 取值：本项目自己生成的 JSON 都是扁平结构，够用 */
static int json_str(const char *json, const char *key, char *out, size_t n) {
    char        pat[64];
    const char *p;
    size_t      i = 0;

    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p = skip_ws(p + strlen(pat));
    if (*p != ':') return 0;
    p = skip_ws(p + 1);
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

static int json_ll(const char *json, const char *key, long long *out) {
    char        pat[64];
    const char *p;

    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p = skip_ws(p + strlen(pat));
    if (*p != ':') return 0;
    p = skip_ws(p + 1);
    *out = strtoll(p, NULL, 10);
    return 1;
}

static void state_load(void) {
    FILE  *f;
    char   buf[512];
    size_t n;

    memset(&g_state, 0, sizeof g_state);
    f = fopen(g_statePath, "rb");
    if (!f) return;
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);

    json_str(buf, "action", g_state.action, sizeof g_state.action);
    json_ll(buf, "deadlineTs", &g_state.deadlineTs);
    json_ll(buf, "createdTs", &g_state.createdTs);
}

/* ------------------------------------------------------------------ 时间解析 */

/* 支持 "2026-11-19T01:15"、"2026-11-19 01:15:00"、以及只有 "23:30"（今天/明天） */
static long long parse_at(const char *s) {
    int       y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    struct tm tmv;
    time_t    t;
    int       got;

    if (!s) return -1;

    got = sscanf(s, "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (got >= 5) {
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year  = y - 1900;
        tmv.tm_mon   = mo - 1;
        tmv.tm_mday  = d;
        tmv.tm_hour  = h;
        tmv.tm_min   = mi;
        tmv.tm_sec   = se;
        tmv.tm_isdst = -1;
        t = mktime(&tmv);
        return (t == (time_t)-1) ? -1 : (long long)t;
    }

    got = sscanf(s, "%d:%d:%d", &h, &mi, &se);
    if (got >= 2) {
        time_t    nowt = time(NULL);
        struct tm base = *localtime(&nowt);

        base.tm_hour  = h;
        base.tm_min   = mi;
        base.tm_sec   = se;
        base.tm_isdst = -1;
        t = mktime(&base);
        if (t <= nowt) {
            base.tm_mday += 1;
            base.tm_isdst = -1;
            t = mktime(&base);
        }
        return (long long)t;
    }
    return -1;
}

/* ------------------------------------------------------------------ HTTP 基础 */

static const char *reason_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

static void send_raw(SOCKET s, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = (int)send(s, data + sent, (int)(len - sent), 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void send_body(SOCKET s, int code, const char *ctype,
                      const char *body, size_t len) {
    char head[512];

    snprintf(head, sizeof head,
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %llu\r\n"
             "Cache-Control: no-store, no-cache, must-revalidate\r\n"
             "Pragma: no-cache\r\n"
             "Connection: close\r\n"
             "\r\n",
             code, reason_text(code), ctype, (unsigned long long)len);
    send_raw(s, head, strlen(head));
    if (len > 0) send_raw(s, body, len);
}

static void send_json(SOCKET s, const char *json, int code) {
    send_body(s, code, "application/json; charset=utf-8", json, strlen(json));
}

static int ci_eq(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

/* 在请求头里取某个头的值（返回指向 req 内部的指针，读到 \r 为止） */
static const char *header_value(const char *req, size_t hdrEnd, const char *name) {
    size_t i = 0;
    size_t nameLen = strlen(name);

    while (i < hdrEnd && req[i] != '\n') i++;      /* 跳过请求行 */
    i++;

    while (i < hdrEnd) {
        size_t ls = i, le, j, v;

        while (i < hdrEnd && req[i] != '\n') i++;
        le = i;
        if (le > ls && req[le - 1] == '\r') le--;

        j = ls;
        while (j < le && req[j] != ':') j++;
        if (j < le && (j - ls) == nameLen && ci_eq(req + ls, name, nameLen)) {
            v = j + 1;
            while (v < le && (req[v] == ' ' || req[v] == '\t')) v++;
            return req + v;
        }
        i++;
    }
    return NULL;
}

/* 读一路请求，返回 1 表示读到完整的请求头（body 通过指针返回） */
static int read_request(SOCKET s, char *buf, int cap,
                        size_t *hdrLen, size_t *bodyLen) {
    size_t total    = 0;
    size_t hdrEnd   = 0;
    size_t needBody = 0;
    int    gotHdr   = 0;

    buf[0] = '\0';

    while (total + 1 < (size_t)cap) {
        int n = (int)recv(s, buf + total, (int)((size_t)cap - 1 - total), 0);
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = '\0';

        if (!gotHdr) {
            char *p = strstr(buf, "\r\n\r\n");
            if (!p) continue;
            hdrEnd   = (size_t)(p - buf) + 4;
            gotHdr   = 1;
            needBody = 0;
            {
                const char *cl = header_value(buf, hdrEnd, "content-length");
                if (cl) needBody = (size_t)atoll(cl);
            }
            if (needBody > 8192) needBody = 8192;
            if (total - hdrEnd >= needBody) break;
        } else {
            if (total - hdrEnd >= needBody) break;
        }
    }

    if (!gotHdr) return 0;

    *hdrLen  = hdrEnd;
    *bodyLen = (total > hdrEnd) ? (total - hdrEnd) : 0;
    if (*bodyLen > needBody) *bodyLen = needBody;
    return 1;
}

/* ------------------------------------------------------------------ 状态快照 */

/* 生成 /api/status 的 JSON；调用方需自行保证串足够大 */
static void build_status(char *out, size_t n) {
    char      host[64];
    DWORD     hostLen = 63;
    char      nowStr[32];
    long long now = now_ts();
    char      sched[300];
    char      at[32], date[16];
    long long dl = 0, created = 0;
    int       hasSched = 0;

    host[0] = '\0';
    GetComputerNameA(host, &hostLen);
    fmt_ts(now, "%Y-%m-%d %H:%M:%S", nowStr, sizeof nowStr);

    EnterCriticalSection(&g_lock);
    if (g_state.deadlineTs > 0) {
        long long rem = g_state.deadlineTs - now;
        if (rem > -3) {
            hasSched  = 1;
            dl        = g_state.deadlineTs;
            created   = g_state.createdTs;
        } else {
            memset(&g_state, 0, sizeof g_state);   /* 已到期，清掉本地记录 */
            state_save();
        }
    }
    if (hasSched) {
        long long rem  = dl - now;
        long long span = (created > 0 && dl > created) ? (dl - created) : 0;
        fmt_ts(dl, "%H:%M:%S", at, sizeof at);
        fmt_ts(dl, "%m-%d", date, sizeof date);
        if (rem < 0) rem = 0;
        snprintf(sched, sizeof sched,
                 "{\"action\":\"%s\",\"atTs\":%lld,\"at\":\"%s\",\"date\":\"%s\","
                 "\"remaining\":%lld,\"span\":%lld}",
                 g_state.action, dl, at, date, rem, span);
    } else {
        snprintf(sched, sizeof sched, "null");
    }
    LeaveCriticalSection(&g_lock);

    snprintf(out, n,
             "{\"ok\":true,\"app\":\"PowerWeb\",\"hostname\":\"%s\",\"nowTs\":%lld,"
             "\"now\":\"%s\",\"scheduled\":%s,\"keyRequired\":%s,\"dryRun\":%s}",
             host, now, nowStr, sched,
             (g_key[0] != '\0') ? "true" : "false",
             g_dryRun ? "true" : "false");
}

/* ------------------------------------------------------------------ 页面文件 */

static int load_file(const char *path, char **out, size_t *len) {
    FILE *f;
    long  sz;

    f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > HTML_MAX) { fclose(f); return 0; }

    *out = (char *)malloc((size_t)sz);
    if (!*out) { fclose(f); return 0; }

    if (fread(*out, 1, (size_t)sz, f) != (size_t)sz) {
        free(*out);
        *out = NULL;
        fclose(f);
        return 0;
    }
    fclose(f);
    *len = (size_t)sz;
    return 1;
}

/* ------------------------------------------------------------------ 业务处理 */

static void api_schedule(SOCKET s, const char *body) {
    char      action[16];
    char      label[16];
    char      atBuf[64];
    char      msg[160];
    char      json[320];
    long long seconds   = -1;
    long long deadline  = 0;
    int       hasDl     = 0;
    int       code;
    long long now;

    (void)s;

    if (!json_str(body, "action", action, sizeof action) ||
        (strcmp(action, "shutdown") != 0 && strcmp(action, "restart") != 0)) {
        send_json(s, "{\"ok\":false,\"error\":\"只支持 shutdown 或 restart\"}", 400);
        return;
    }
    strcpy(label, (strcmp(action, "restart") == 0) ? "重启" : "关机");

    if (json_ll(body, "seconds", &seconds)) {
        if (seconds < 0) {
            send_json(s, "{\"ok\":false,\"error\":\"seconds 不能为负\"}", 400);
            return;
        }
    } else if (json_str(body, "at", atBuf, sizeof atBuf)) {
        long long target = parse_at(atBuf);
        if (target < 0) {
            send_json(s, "{\"ok\":false,\"error\":\"时间格式不正确\"}", 400);
            return;
        }
        now = now_ts();
        seconds  = target - now;
        deadline = target;
        hasDl    = 1;
        if (seconds < 0) {
            send_json(s, "{\"ok\":false,\"error\":\"预约时间已经过去\"}", 400);
            return;
        }
    } else {
        send_json(s, "{\"ok\":false,\"error\":\"缺少 seconds 或 at 参数\"}", 400);
        return;
    }

    if (seconds > MAX_DELAY) {
        send_json(s, "{\"ok\":false,\"error\":\"预约时间过长（上限 10 年）\"}", 400);
        return;
    }

    /* 立即执行：先把响应发出去，再动手，否则电脑一关手机收不到回执 */
    if (seconds == 0) {
        snprintf(msg, sizeof msg, "已发送立即%s指令", label);
        snprintf(json, sizeof json,
                 "{\"ok\":true,\"message\":\"%s\",\"action\":\"%s\",\"seconds\":0}",
                 msg, action);
        send_json(s, json, 200);

        EnterCriticalSection(&g_lock);
        memset(&g_state, 0, sizeof g_state);
        state_save();
        LeaveCriticalSection(&g_lock);

        Sleep(400);
        action_shutdown(action, 0);
        return;
    }

    code = action_shutdown(action, seconds);
    if (code != 0) {
        snprintf(json, sizeof json,
                 "{\"ok\":false,\"error\":\"系统拒绝执行，退出码 %d\"}", code);
        send_json(s, json, 500);
        return;
    }

    now = now_ts();
    EnterCriticalSection(&g_lock);
    snprintf(g_state.action, sizeof g_state.action, "%s", action);
    g_state.createdTs  = now;
    g_state.deadlineTs = hasDl ? deadline : (now + seconds);
    state_save();
    LeaveCriticalSection(&g_lock);

    fmt_ts(g_state.deadlineTs, "%m-%d %H:%M", atBuf, sizeof atBuf);
    snprintf(msg, sizeof msg, "已预约 %s %s", atBuf, label);
    snprintf(json, sizeof json,
             "{\"ok\":true,\"message\":\"%s\",\"action\":\"%s\",\"seconds\":%lld}",
             msg, action, seconds);
    send_json(s, json, 200);
}

static void api_cancel(SOCKET s) {
    int  code = action_cancel();
    int  had;
    char json[256];

    EnterCriticalSection(&g_lock);
    had = (g_state.deadlineTs != 0);
    memset(&g_state, 0, sizeof g_state);
    state_save();
    LeaveCriticalSection(&g_lock);

    if (code == 0) {
        send_json(s, "{\"ok\":true,\"message\":\"已取消预约\"}", 200);
    } else if (code == 1116 || !had) {
        send_json(s, "{\"ok\":true,\"message\":\"当前没有进行中的关机任务\"}", 200);
    } else {
        snprintf(json, sizeof json,
                 "{\"ok\":false,\"error\":\"取消失败，退出码 %d\"}", code);
        send_json(s, json, 500);
    }
}

static void api_power(SOCKET s, const char *body) {
    char action[16];

    if (!json_str(body, "action", action, sizeof action)) {
        send_json(s, "{\"ok\":false,\"error\":\"请求内容不是合法 JSON\"}", 400);
        return;
    }

    if (strcmp(action, "lock") == 0) {
        send_json(s, "{\"ok\":true,\"message\":\"已锁定屏幕\"}", 200);
        Sleep(400);
        action_lock();
        return;
    }
    if (strcmp(action, "sleep") == 0) {
        send_json(s, "{\"ok\":true,\"message\":\"已发送睡眠指令\"}", 200);
        Sleep(400);
        action_sleep();
        return;
    }
    send_json(s, "{\"ok\":false,\"error\":\"只支持 sleep 或 lock\"}", 400);
}

/* ------------------------------------------------------------------ 单连接处理 */

static void handle_conn(SOCKET s) {
    char  *buf = (char *)malloc(REQ_MAX);
    size_t hdrLen = 0, bodyLen = 0;
    char   method[16] = {0};
    char   target[512] = {0};
    char  *path;
    char  *body = NULL;
    char   json[1024];
    char   keyBuf[160];

    if (!buf) return;
    buf[0] = '\0';

    if (!read_request(s, buf, REQ_MAX, &hdrLen, &bodyLen)) goto done;

    if (sscanf(buf, "%15s %511s", method, target) != 2) goto done;

    path = strchr(target, '?');
    if (path) *path = '\0';
    path = target;
    if (path[0] == '\0') path = "/";

    body = buf + hdrLen;
    buf[hdrLen + bodyLen] = '\0';

    /* ---- 静态页面（不需要密钥） ---- */
    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        char  *html = NULL;
        size_t len  = 0;

        if (load_file(g_htmlPath, &html, &len)) {
            send_body(s, 200, "text/html; charset=utf-8", html, len);
            free(html);
        } else {
            const char *msg = "index.html 不存在，请确认它和 PowerWeb.exe 在同一目录。";
            send_body(s, 404, "text/plain; charset=utf-8", msg, strlen(msg));
        }
        goto done;
    }

    if (strcmp(path, "/favicon.ico") == 0) {
        send_body(s, 204, "text/plain", "", 0);
        goto done;
    }

    if (strncmp(path, "/api/", 5) != 0) {
        const char *msg = "页面不存在";
        send_body(s, 404, "text/plain; charset=utf-8", msg, strlen(msg));
        goto done;
    }

    /* ---- 密钥校验 ---- */
    if (g_key[0] != '\0') {
        const char *hv = header_value(buf, hdrLen, "x-auth-key");
        size_t      i  = 0;

        keyBuf[0] = '\0';
        if (hv) {
            while (hv[i] && hv[i] != '\r' && hv[i] != '\n' && i < sizeof keyBuf - 1) {
                keyBuf[i] = hv[i];
                i++;
            }
            keyBuf[i] = '\0';
        }
        if (strcmp(keyBuf, g_key) != 0) {
            send_json(s, "{\"ok\":false,\"error\":\"访问密钥不正确\"}", 401);
            goto done;
        }
    }

    /* ---- 路由 ---- */
    if (strcmp(path, "/api/status") == 0) {
        if (strcmp(method, "GET") != 0) {
            send_json(s, "{\"ok\":false,\"error\":\"该接口只支持 GET\"}", 405);
            goto done;
        }
        build_status(json, sizeof json);
        send_json(s, json, 200);
        goto done;
    }

    if (strcmp(method, "POST") != 0) {
        send_json(s, "{\"ok\":false,\"error\":\"该接口只支持 POST\"}", 405);
        goto done;
    }

    if (strcmp(path, "/api/schedule") == 0) {
        api_schedule(s, body);
        goto done;
    }
    if (strcmp(path, "/api/cancel") == 0) {
        api_cancel(s);
        goto done;
    }
    if (strcmp(path, "/api/power") == 0) {
        api_power(s, body);
        goto done;
    }

    send_json(s, "{\"ok\":false,\"error\":\"接口不存在\"}", 404);

done:
    free(buf);
}

static DWORD WINAPI conn_thread(LPVOID param) {
    SOCKET s = (SOCKET)(uintptr_t)param;
    handle_conn(s);
    closesocket(s);
    return 0;
}

/* ------------------------------------------------------------------ 启动横幅 */

static void print_banner(void) {
    struct addrinfo hints, *res = NULL, *p;
    char            host[64];
    DWORD           hostLen = 63;
    int             shown   = 0;

    host[0] = '\0';
    GetComputerNameA(host, &hostLen);

    printf("\n");
    printf(" ==============================================================\n");
    printf("   PowerWeb · 局域网电源控制服务已启动\n");
    printf(" --------------------------------------------------------------\n");
    printf("   电脑名称 : %s\n", host);
    printf("   程序版本 : %s\n", PW_VERSION);
    printf("   监听端口 : %d\n", g_port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) == 0 && res) {
        for (p = res; p; p = p->ai_next) {
            struct sockaddr_in *sa = (struct sockaddr_in *)p->ai_addr;
            const char         *ip = inet_ntoa(sa->sin_addr);
            if (strncmp(ip, "127.", 4) == 0) continue;
            printf("   手机打开 : http://%s:%d\n", ip, g_port);
            shown = 1;
        }
        freeaddrinfo(res);
    }
    if (!shown) printf("   未检测到局域网 IPv4 地址，请在 cmd 里执行 ipconfig 查看。\n");

    printf("   访问密钥 : %s\n", (g_key[0] != '\0') ? "已开启" : "未设置（同一局域网内任何人都能操作）");
    printf("   运行模式 : %s\n", g_dryRun ? "演练模式（不会真的关机）" : "正常执行");
    printf("   页面文件 : %s\n", g_htmlPath);
    printf(" --------------------------------------------------------------\n");
    printf("   手机和电脑必须连同一个局域网；首次启动若弹出防火墙提示，\n");
    printf("   请勾选“专用网络”并允许访问，或运行 open-firewall.bat。\n");
    printf("   按 Ctrl+C 停止服务。\n");
    printf(" ==============================================================\n\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ main */

static void die(const char *msg) {
    printf("%s\n", msg);
    fflush(stdout);
    exit(1);
}

static void print_usage(void) {
    printf("\n");
    printf(" PowerWeb %s —— 局域网电源控制服务\n", PW_VERSION);
    printf(" 手机连同一个 Wi-Fi，浏览器打开 http://<电脑IP>:<端口>\n");
    printf(" 即可关机 / 重启 / 睡眠 / 锁屏 / 预约关机。\n");
    printf("\n");
    printf(" 用法：PowerWeb.exe [选项]\n");
    printf("\n");
    printf("   --port N       监听端口，默认 8000\n");
    printf("   --key K        访问密钥；设置后 /api 请求必须带 X-Auth-Key\n");
    printf("   --dry-run      演练模式，只打印要执行的命令，不真的关机\n");
    printf("   --html PATH    页面文件，默认取 exe 同目录的 index.html\n");
    printf("   --hidden       启动后隐藏控制台窗口（开机自启用）\n");
    printf("   --version      显示版本号\n");
    printf("   -h, --help     显示本帮助\n");
    printf("\n");
    printf(" 示例：\n");
    printf("   PowerWeb.exe --port 8123 --key 1234\n");
    printf("   PowerWeb.exe --dry-run\n");
    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    WSADATA      wsa;
    SOCKET       listener;
    struct sockaddr_in addr;
    int          i;
    char         modPath[MAX_PATH];
    char        *slash;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    /* ---- 命令行参数 ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_port = atoi(argv[++i]);
            if (g_port <= 0 || g_port > 65535) die("端口不合法（1-65535）");
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            strncpy(g_key, argv[++i], sizeof g_key - 1);
        } else if (strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dryrun") == 0) {
            g_dryRun = 1;
        } else if (strcmp(argv[i], "--hidden") == 0) {
            g_hidden = 1;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("PowerWeb %s\n", PW_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            printf("未知参数：%s\n", argv[i]);
            return 1;
        }
    }

    /* ---- 隐藏控制台：给开机自启用的，之后日志就看不见了 ---- */
    if (g_hidden) {
        HWND con = GetConsoleWindow();
        if (con) ShowWindow(con, SW_HIDE);
    }

    /* ---- 路径基准：exe 所在目录 ---- */
    if (!GetModuleFileNameA(NULL, modPath, MAX_PATH)) die("GetModuleFileName 失败");
    slash = strrchr(modPath, '\\');
    if (slash) { *slash = '\0'; }
    snprintf(g_exeDir, sizeof g_exeDir, "%s", modPath);

    /* --html 可在参数里覆盖，默认取 exe 同目录 index.html */
    snprintf(g_htmlPath, sizeof g_htmlPath, "%s\\index.html", g_exeDir);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) {
            snprintf(g_htmlPath, sizeof g_htmlPath, "%s", argv[i + 1]);
        }
    }
    snprintf(g_statePath, sizeof g_statePath, "%s\\state.json", g_exeDir);

    InitializeCriticalSection(&g_lock);
    state_load();

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) die("WSAStartup 失败");

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) die("创建 socket 失败");

    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)g_port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "端口 %d 绑定失败（可能被占用），换个端口：PowerWeb.exe --port 8123", g_port);
        die(msg);
    }
    if (listen(listener, SOMAXCONN) == SOCKET_ERROR) die("listen 失败");

    print_banner();
    logmsg("服务已就绪，监听 0.0.0.0:%d", g_port);

    while (1) {
        SOCKET client = accept(listener, NULL, NULL);
        if (client == INVALID_SOCKET) {
            Sleep(100);
            continue;
        }
        if (CreateThread(NULL, 0, conn_thread, (LPVOID)(uintptr_t)client,
                         0, NULL) == NULL) {
            closesocket(client);
        }
    }

    closesocket(listener);
    WSACleanup();
    DeleteCriticalSection(&g_lock);
    return 0;
}
