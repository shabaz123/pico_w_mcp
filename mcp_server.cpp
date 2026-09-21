#include "mcp_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

struct MCPServer::Client {
    MCPServer *server;
    struct tcp_pcb *pcb;
    char rx[MCP_RX_BUFFER_SIZE];
    size_t rx_len;
};

MCPServer::MCPServer(const char *name, const char *version)
    : name_(name), version_(version), port_(8000), tool_count_(0), resource_count_(0) {
    memset(tools_, 0, sizeof(tools_));
    memset(resources_, 0, sizeof(resources_));
}

MCPServer::MCPTool MCPServer::tool(const char *name, const char *description,
                                      ToolCallback callback, void *user_data) {
    if (tool_count_ >= MCP_MAX_TOOLS || !name || !callback) {
        logf("MCP register tool FAILED: %s\n", name ? name : "(null)");
        return MCPTool();
    }
    Tool &t = tools_[tool_count_];
    t.name = name;
    t.description = description ? description : "";
    t.callback = callback;
    t.user_data = user_data;
    t.param_count = 0;
    int index = (int)tool_count_++;
    logf("MCP registered tool: %s\n", name);
    return MCPTool(this, index);
}

bool MCPServer::resource(const char *uri, const char *name, const char *description,
                         ResourceCallback callback, const char *mime_type, void *user_data) {
    if (resource_count_ >= MCP_MAX_RESOURCES || !uri || !name || !callback) {
        logf("MCP register resource FAILED: %s\n", uri ? uri : "(null)");
        return false;
    }
    Resource &r = resources_[resource_count_++];
    r.uri = uri;
    r.name = name;
    r.description = description ? description : "";
    r.mime_type = mime_type ? mime_type : "text/plain";
    r.callback = callback;
    r.user_data = user_data;
    logf("MCP registered resource: %s\n", uri);
    return true;
}

bool MCPServer::addParam(int tool_index, const char *name, const char *description,
                         const char *type, bool required) {
    if (tool_index < 0 || (size_t)tool_index >= tool_count_) return false;
    Tool &t = tools_[tool_index];
    if (t.param_count >= MCP_MAX_PARAMS || !name || !type) return false;
    Param &p = t.params[t.param_count++];
    p.name = name;
    p.description = description ? description : "";
    p.type = type;
    p.required = required;
    return true;
}

void MCPServer::logPrefix() {
    uint64_t ms = to_ms_since_boot(get_absolute_time());
    printf("[%6llu.%03llu] ",
           (unsigned long long)(ms / 1000),
           (unsigned long long)(ms % 1000));
}

void MCPServer::logf(const char *fmt, ...) {
    logPrefix();
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

bool MCPServer::append(char *out, size_t out_size, size_t &used, const char *fmt, ...) {
    if (used >= out_size) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + used, out_size - used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= out_size - used) return false;
    used += (size_t)n;
    return true;
}

void MCPServer::jsonEscapeAppend(char *out, size_t out_size, size_t &used, const char *text) {
    if (!text) return;
    for (const unsigned char *p = (const unsigned char *)text; *p && used + 2 < out_size; ++p) {
        switch (*p) {
            case '"':  append(out, out_size, used, "\\\""); break;
            case '\\': append(out, out_size, used, "\\\\"); break;
            case '\n': append(out, out_size, used, "\\n"); break;
            case '\r': append(out, out_size, used, "\\r"); break;
            case '\t': append(out, out_size, used, "\\t"); break;
            default:
                if (*p >= 0x20) {
                    out[used++] = (char)*p;
                    out[used] = '\0';
                }
        }
    }
}

int MCPServer::jsonId(const char *body) {
    const char *p = strstr(body, "\"id\"");
    if (!p) return 0;
    p = strchr(p, ':');
    return p ? atoi(p + 1) : 0;
}

bool MCPServer::hasMethod(const char *body, const char *method) {
    char needle[96];
    snprintf(needle, sizeof(needle), "\"method\":\"%s\"", method);
    if (strstr(body, needle)) return true;
    snprintf(needle, sizeof(needle), "\"method\": \"%s\"", method);
    return strstr(body, needle) != nullptr;
}

// Return the value of a direct child member of a JSON object.
// This deliberately skips nested objects/arrays, so params.name cannot
// accidentally match params._meta.clientInfo.name.
static const char *jsonDirectValue(const char *object, const char *key) {
    if (!object || *object != '{' || !key) return nullptr;

    const char *p = object + 1;
    int depth = 1;
    bool in_string = false;
    bool escape = false;

    while (*p && depth > 0) {
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (*p == '"') {
                in_string = false;
            }
            ++p;
            continue;
        }

        if (*p == '"') {
            // Only a string beginning at depth 1 can be a direct-child key.
            if (depth == 1) {
                const char *key_start = p + 1;
                const char *q = key_start;
                bool key_escape = false;

                while (*q) {
                    if (key_escape) {
                        key_escape = false;
                    } else if (*q == '\\') {
                        key_escape = true;
                    } else if (*q == '"') {
                        break;
                    }
                    ++q;
                }

                if (*q != '"') return nullptr;

                const char *after = q + 1;
                while (*after == ' ' || *after == '\t' ||
                       *after == '\r' || *after == '\n') ++after;

                if (*after == ':') {
                    size_t key_len = (size_t)(q - key_start);
                    if (strlen(key) == key_len &&
                        !strncmp(key_start, key, key_len)) {
                        ++after;
                        while (*after == ' ' || *after == '\t' ||
                               *after == '\r' || *after == '\n') ++after;
                        return after;
                    }
                }

                p = q + 1;
                continue;
            }

            in_string = true;
            ++p;
            continue;
        }

        if (*p == '{' || *p == '[') ++depth;
        else if (*p == '}' || *p == ']') --depth;
        ++p;
    }

    return nullptr;
}

bool MCPServer::jsonString(const char *object, const char *key,
                           char *out, size_t out_size) {
    if (!object || !key || !out || out_size == 0) return false;

    const char *p = jsonDirectValue(object, key);
    if (!p || *p != '"') return false;
    ++p;

    size_t n = 0;
    bool escape = false;
    while (*p && n + 1 < out_size) {
        if (escape) {
            // Sufficient for MCP names/URIs and simple string arguments.
            switch (*p) {
                case 'n': out[n++] = '\n'; break;
                case 'r': out[n++] = '\r'; break;
                case 't': out[n++] = '\t'; break;
                default:  out[n++] = *p; break;
            }
            escape = false;
        } else if (*p == '\\') {
            escape = true;
        } else if (*p == '"') {
            out[n] = '\0';
            return true;
        } else {
            out[n++] = *p;
        }
        ++p;
    }

    out[n] = '\0';
    return false;
}

const char *MCPServer::argumentsObject(const char *params) {
    const char *p = jsonDirectValue(params, "arguments");
    return (p && *p == '{') ? p : "{}";
}

const char *MCPArgs::findValue(const char *name) const {
    static char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", name);
    const char *p = strstr(json_, needle);
    if (!p) return nullptr;
    p = strchr(p + strlen(needle), ':');
    if (!p) return nullptr;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

bool MCPArgs::getBool(const char *name, bool fallback) const {
    const char *p = findValue(name);
    if (!p) return fallback;
    if (!strncmp(p, "true", 4)) return true;
    if (!strncmp(p, "false", 5)) return false;
    return fallback;
}

int MCPArgs::getInt(const char *name, int fallback) const {
    const char *p = findValue(name);
    return p ? atoi(p) : fallback;
}

float MCPArgs::getFloat(const char *name, float fallback) const {
    const char *p = findValue(name);
    return p ? strtof(p, nullptr) : fallback;
}

bool MCPArgs::getString(const char *name, char *out, size_t out_size) const {
    const char *p = findValue(name);
    if (!p || *p != '"' || !out || out_size == 0) return false;
    ++p;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < out_size) {
        if (*p == '\\' && p[1]) ++p;
        out[n++] = *p++;
    }
    out[n] = '\0';
    return *p == '"';
}

MCPServer::Tool *MCPServer::findTool(const char *name) {
    for (size_t i = 0; i < tool_count_; ++i)
        if (!strcmp(tools_[i].name, name)) return &tools_[i];
    return nullptr;
}

MCPServer::Resource *MCPServer::findResource(const char *uri) {
    for (size_t i = 0; i < resource_count_; ++i)
        if (!strcmp(resources_[i].uri, uri)) return &resources_[i];
    return nullptr;
}

void MCPServer::sendHttp(Client *c, int status, const char *ctype, const char *body) {
    char out[MCP_TX_BUFFER_SIZE];
    size_t body_len = body ? strlen(body) : 0;
    const char *reason = status == 200 ? "OK" :
                         status == 202 ? "Accepted" :
                         status == 400 ? "Bad Request" :
                         status == 404 ? "Not Found" : "Error";
    int n = snprintf(out, sizeof(out),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Connection: close\r\n\r\n%s",
        status, reason, ctype ? ctype : "text/plain",
        (unsigned)body_len, body ? body : "");
    if (n < 0 || (size_t)n >= sizeof(out)) {
        logf("HTTP response too large\n");
        closeClient(c);
        return;
    }
    tcp_write(c->pcb, out, (u16_t)n, TCP_WRITE_FLAG_COPY);
    tcp_output(c->pcb);
    tcp_shutdown(c->pcb, 0, 1);
}

void MCPServer::sendJson(Client *c, const char *json) {
    sendHttp(c, 200, "application/json", json);
}

void MCPServer::handleMcp(Client *c, const char *body) {
    char json[MCP_TX_BUFFER_SIZE];
    int id = jsonId(body);

    if (hasMethod(body, "server/discover")) {
        logf("MCP <- server/discover [id=%d] -> %u tools, %u resources\n",
             id, (unsigned)tool_count_, (unsigned)resource_count_);
        snprintf(json, sizeof(json),
            "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{"
            "\"cacheScope\":\"private\",\"capabilities\":{"
            "\"prompts\":{\"listChanged\":true},"
            "\"resources\":{\"listChanged\":true,\"subscribe\":true},"
            "\"tools\":{\"listChanged\":true}},"
            "\"supportedVersions\":[\"2026-07-28\"],\"ttlMs\":0,"
            "\"resultType\":\"complete\",\"_meta\":{\"io.modelcontextprotocol/serverInfo\":"
            "{\"name\":\"%s\",\"version\":\"%s\"}}}}", id, name_, version_);
        sendJson(c, json);
        return;
    }

    if (hasMethod(body, "subscriptions/listen")) {
        logf("MCP <- subscriptions/listen [id=%d] -> listening\n", id);
        snprintf(json, sizeof(json),
            "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"resultType\":\"complete\","
            "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{\"name\":\"%s\","
            "\"version\":\"%s\"}}}}", id, name_, version_);
        sendJson(c, json);
        return;
    }

    if (hasMethod(body, "tools/list")) {
        size_t used = 0;
        append(json, sizeof(json), used,
            "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"cacheScope\":\"private\",\"tools\":[", id);
        for (size_t i = 0; i < tool_count_; ++i) {
            Tool &t = tools_[i];
            if (i) append(json, sizeof(json), used, ",");
            append(json, sizeof(json), used, "{\"name\":\"");
            jsonEscapeAppend(json, sizeof(json), used, t.name);
            append(json, sizeof(json), used, "\",\"description\":\"");
            jsonEscapeAppend(json, sizeof(json), used, t.description);
            append(json, sizeof(json), used, "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{");
            for (size_t j = 0; j < t.param_count; ++j) {
                Param &p = t.params[j];
                if (j) append(json, sizeof(json), used, ",");
                append(json, sizeof(json), used, "\"%s\":{\"type\":\"%s\"", p.name, p.type);
                if (p.description && *p.description) {
                    append(json, sizeof(json), used, ",\"description\":\"");
                    jsonEscapeAppend(json, sizeof(json), used, p.description);
                    append(json, sizeof(json), used, "\"");
                }
                append(json, sizeof(json), used, "}");
            }
            append(json, sizeof(json), used, "}");
            bool first = true;
            for (size_t j = 0; j < t.param_count; ++j) if (t.params[j].required) {
                if (first) {
                    append(json, sizeof(json), used, ",\"required\":[");
                    first = false;
                } else append(json, sizeof(json), used, ",");
                append(json, sizeof(json), used, "\"%s\"", t.params[j].name);
            }
            if (!first) append(json, sizeof(json), used, "]");
            append(json, sizeof(json), used, "}}");
        }
        append(json, sizeof(json), used,
            "],\"ttlMs\":0,\"resultType\":\"complete\",\"_meta\":{"
            "\"io.modelcontextprotocol/serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"}}}}",
            name_, version_);
        logf("MCP <- tools/list [id=%d] -> %u tool(s)\n", id, (unsigned)tool_count_);
        sendJson(c, json);
        return;
    }

    if (hasMethod(body, "tools/call")) {
        const char *params = jsonDirectValue(body, "params");
        char name[96];
        if (!params || *params != '{' ||
            !jsonString(params, "name", name, sizeof(name))) {
            snprintf(json, sizeof(json), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":"
                     "{\"code\":-32602,\"message\":\"Missing tool name\"}}", id);
            sendJson(c, json);
            return;
        }
        Tool *t = findTool(name);
        if (!t) {
            logf("MCP <- tools/call [id=%d] %s -> unknown tool\n", id, name);
            snprintf(json, sizeof(json), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":"
                     "{\"code\":-32602,\"message\":\"Unknown tool\"}}", id);
            sendJson(c, json);
            return;
        }
        char result[768] = {0};
        MCPArgs args(argumentsObject(params));
        bool ok = t->callback(args, result, sizeof(result), t->user_data);
        size_t used = 0;
        append(json, sizeof(json), used, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{"
               "\"content\":[{\"type\":\"text\",\"text\":\"", id);
        jsonEscapeAppend(json, sizeof(json), used, result);
        append(json, sizeof(json), used, "\"}],\"isError\":%s,\"resultType\":\"complete\","
               "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{\"name\":\"%s\","
               "\"version\":\"%s\"}}}}", ok ? "false" : "true", name_, version_);
        logf("MCP <- tools/call [id=%d] %s -> %s\n", id, name, ok ? "OK" : "ERROR");
        sendJson(c, json);
        return;
    }

    if (hasMethod(body, "resources/list")) {
        size_t used = 0;
        append(json, sizeof(json), used,
               "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"cacheScope\":\"private\",\"resources\":[", id);
        for (size_t i = 0; i < resource_count_; ++i) {
            Resource &r = resources_[i];
            if (i) append(json, sizeof(json), used, ",");
            append(json, sizeof(json), used, "{\"name\":\"");
            jsonEscapeAppend(json, sizeof(json), used, r.name);
            append(json, sizeof(json), used, "\",\"description\":\"");
            jsonEscapeAppend(json, sizeof(json), used, r.description);
            append(json, sizeof(json), used, "\",\"mimeType\":\"%s\",\"uri\":\"", r.mime_type);
            jsonEscapeAppend(json, sizeof(json), used, r.uri);
            append(json, sizeof(json), used, "\"}");
        }
        append(json, sizeof(json), used,
               "],\"ttlMs\":0,\"resultType\":\"complete\",\"_meta\":{"
               "\"io.modelcontextprotocol/serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"}}}}",
               name_, version_);
        logf("MCP <- resources/list [id=%d] -> %u resource(s)\n", id, (unsigned)resource_count_);
        sendJson(c, json);
        return;
    }

    if (hasMethod(body, "resources/read")) {
        const char *params = jsonDirectValue(body, "params");
        char uri[160];
        if (!params || *params != '{' ||
            !jsonString(params, "uri", uri, sizeof(uri))) {
            snprintf(json, sizeof(json), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":"
                     "{\"code\":-32602,\"message\":\"Missing resource URI\"}}", id);
            sendJson(c, json);
            return;
        }
        Resource *r = findResource(uri);
        if (!r) {
            logf("MCP <- resources/read [id=%d] %s -> unknown resource\n", id, uri);
            snprintf(json, sizeof(json), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":"
                     "{\"code\":-32602,\"message\":\"Unknown resource\"}}", id);
            sendJson(c, json);
            return;
        }
        char value[768] = {0};
        bool ok = r->callback(value, sizeof(value), r->user_data);
        if (!ok) {
            snprintf(json, sizeof(json), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":"
                     "{\"code\":-32603,\"message\":\"Resource read failed\"}}", id);
            sendJson(c, json);
            return;
        }
        size_t used = 0;
        append(json, sizeof(json), used,
               "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"cacheScope\":\"private\","
               "\"contents\":[{\"uri\":\"", id);
        jsonEscapeAppend(json, sizeof(json), used, r->uri);
        append(json, sizeof(json), used, "\",\"mimeType\":\"%s\",\"text\":\"", r->mime_type);
        jsonEscapeAppend(json, sizeof(json), used, value);
        append(json, sizeof(json), used,
               "\"}],\"ttlMs\":0,\"resultType\":\"complete\",\"_meta\":{"
               "\"io.modelcontextprotocol/serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"}}}}",
               name_, version_);
        logf("MCP <- resources/read [id=%d] %s -> OK\n", id, uri);
        sendJson(c, json);
        return;
    }

    if (hasMethod(body, "notifications/roots/list_changed")) {
        logf("MCP <- notifications/roots/list_changed -> 202\n");
        sendHttp(c, 202, "text/plain", "");
        return;
    }

    char method[80] = {0};
    jsonString(body, "method", method, sizeof(method));
    logf("MCP <- %s [id=%d] -> ERROR method not found\n",
         method[0] ? method : "(unknown)", id);
    snprintf(json, sizeof(json), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":"
             "{\"code\":-32601,\"message\":\"Method not found\"}}", id);
    sendJson(c, json);
}

void MCPServer::handleHttp(Client *c) {
    c->rx[c->rx_len] = '\0';
    char *headers_end = strstr(c->rx, "\r\n\r\n");
    if (!headers_end) return;

    if (!strncmp(c->rx, "GET ", 4)) {
        char path[80] = {0};
        const char *start = c->rx + 4;
        const char *end = strchr(start, ' ');
        if (end) {
            size_t n = (size_t)(end - start);
            if (n >= sizeof(path)) n = sizeof(path) - 1;
            memcpy(path, start, n);
        }
        logf("HTTP <- GET %s -> 404\n", path[0] ? path : "(unknown)");
        sendHttp(c, 404, "text/plain", "Not Found");
        return;
    }

    if (strncmp(c->rx, "POST /mcp ", 10)) {
        sendHttp(c, 404, "text/plain", "Not Found");
        return;
    }

    size_t header_len = (size_t)(headers_end + 4 - c->rx);
    size_t content_len = 0;
    const char *cl = strstr(c->rx, "Content-Length:");
    if (!cl) cl = strstr(c->rx, "content-length:");
    if (cl) content_len = (size_t)atoi(cl + 15);
    if (c->rx_len < header_len + content_len) return;

    handleMcp(c, c->rx + header_len);
}

err_t MCPServer::recvThunk(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    Client *c = static_cast<Client *>(arg);
    return c->server->receive(c, pcb, p, err);
}

void MCPServer::errThunk(void *arg, err_t err) {
    (void)err;
    Client *c = static_cast<Client *>(arg);
    if (c) {
        c->pcb = nullptr;
        delete c;
    }
}

err_t MCPServer::acceptThunk(void *arg, struct tcp_pcb *newpcb, err_t err) {
    return static_cast<MCPServer *>(arg)->accept(newpcb, err);
}

err_t MCPServer::accept(struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    Client *c = new Client{};
    if (!c) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    c->server = this;
    c->pcb = newpcb;
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, recvThunk);
    tcp_err(newpcb, errThunk);
    return ERR_OK;
}

err_t MCPServer::receive(Client *c, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (!p) {
        closeClient(c);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    if (c->rx_len + p->tot_len >= MCP_RX_BUFFER_SIZE) {
        pbuf_free(p);
        sendHttp(c, 400, "text/plain", "Request too large");
        return ERR_OK;
    }
    pbuf_copy_partial(p, c->rx + c->rx_len, p->tot_len, 0);
    c->rx_len += p->tot_len;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    handleHttp(c);
    return ERR_OK;
}

void MCPServer::closeClient(Client *c) {
    if (!c) return;
    if (c->pcb) {
        tcp_arg(c->pcb, nullptr);
        tcp_recv(c->pcb, nullptr);
        tcp_err(c->pcb, nullptr);
        tcp_close(c->pcb);
    }
    delete c;
}

bool MCPServer::begin(uint16_t port) {
    port_ = port;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, port_) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }
    pcb = tcp_listen_with_backlog(pcb, 4);
    if (!pcb) return false;
    tcp_arg(pcb, this);
    tcp_accept(pcb, acceptThunk);
    logf("MCP server '%s' listening on port %u (%u tools, %u resources)\n",
         name_, port_, (unsigned)tool_count_, (unsigned)resource_count_);
    return true;
}
