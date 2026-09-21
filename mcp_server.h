#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#ifndef MCP_MAX_TOOLS
#define MCP_MAX_TOOLS 16
#endif
#ifndef MCP_MAX_RESOURCES
#define MCP_MAX_RESOURCES 16
#endif
#ifndef MCP_MAX_PARAMS
#define MCP_MAX_PARAMS 8
#endif
#ifndef MCP_RX_BUFFER_SIZE
#define MCP_RX_BUFFER_SIZE 4096
#endif
#ifndef MCP_TX_BUFFER_SIZE
#define MCP_TX_BUFFER_SIZE 4096
#endif

class MCPArgs {
public:
    explicit MCPArgs(const char *json) : json_(json ? json : "") {}

    bool getBool(const char *name, bool fallback = false) const;
    int getInt(const char *name, int fallback = 0) const;
    float getFloat(const char *name, float fallback = 0.0f) const;
    bool getString(const char *name, char *out, size_t out_size) const;

private:
    const char *json_;
    const char *findValue(const char *name) const;
};

class MCPServer {
public:
    using ToolCallback = bool (*)(MCPArgs &args, char *out, size_t out_size, void *user_data);
    using ResourceCallback = bool (*)(char *out, size_t out_size, void *user_data);

    class MCPTool {
    public:
        MCPTool() : server_(nullptr), index_(-1) {}

        template<typename T>
        MCPTool &param(const char *name, const char *description = nullptr, bool required = true) {
            if (server_ && index_ >= 0)
                server_->addParam(index_, name, description, typeName<T>(), required);
            return *this;
        }

    private:
        friend class MCPServer;
        MCPTool(MCPServer *server, int index) : server_(server), index_(index) {}

        template<typename T> static const char *typeName();
        MCPServer *server_;
        int index_;
    };

    explicit MCPServer(const char *name, const char *version = "0.1");

    MCPTool tool(const char *name,
                     const char *description,
                     ToolCallback callback,
                     void *user_data = nullptr);

    bool resource(const char *uri,
                  const char *name,
                  const char *description,
                  ResourceCallback callback,
                  const char *mime_type = "text/plain",
                  void *user_data = nullptr);

    bool begin(uint16_t port = 8000);

    size_t toolCount() const { return tool_count_; }
    size_t resourceCount() const { return resource_count_; }

private:
    struct Param {
        const char *name;
        const char *description;
        const char *type;
        bool required;
    };

    struct Tool {
        const char *name;
        const char *description;
        ToolCallback callback;
        void *user_data;
        Param params[MCP_MAX_PARAMS];
        size_t param_count;
    };

    struct Resource {
        const char *uri;
        const char *name;
        const char *description;
        const char *mime_type;
        ResourceCallback callback;
        void *user_data;
    };

    const char *name_;
    const char *version_;
    uint16_t port_;
    Tool tools_[MCP_MAX_TOOLS];
    Resource resources_[MCP_MAX_RESOURCES];
    size_t tool_count_;
    size_t resource_count_;

    bool addParam(int tool_index, const char *name, const char *description,
                  const char *type, bool required);

    struct Client;
    static err_t acceptThunk(void *arg, struct tcp_pcb *newpcb, err_t err);
    static err_t recvThunk(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
    static void errThunk(void *arg, err_t err);

    err_t accept(struct tcp_pcb *newpcb, err_t err);
    err_t receive(Client *client, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
    void closeClient(Client *client);
    void handleHttp(Client *client);
    void handleMcp(Client *client, const char *body);

    void sendHttp(Client *client, int status, const char *content_type, const char *body);
    void sendJson(Client *client, const char *json);

    Tool *findTool(const char *name);
    Resource *findResource(const char *uri);

    static int jsonId(const char *body);
    static bool hasMethod(const char *body, const char *method);
    static bool jsonString(const char *body, const char *key, char *out, size_t out_size);
    static const char *argumentsObject(const char *body);
    static void jsonEscapeAppend(char *out, size_t out_size, size_t &used, const char *text);
    static bool append(char *out, size_t out_size, size_t &used, const char *fmt, ...);
    static void logPrefix();
    static void logf(const char *fmt, ...);
};

template<> inline const char *MCPServer::MCPTool::typeName<bool>() { return "boolean"; }
template<> inline const char *MCPServer::MCPTool::typeName<int>() { return "integer"; }
template<> inline const char *MCPServer::MCPTool::typeName<float>() { return "number"; }
template<> inline const char *MCPServer::MCPTool::typeName<double>() { return "number"; }
template<> inline const char *MCPServer::MCPTool::typeName<const char *>() { return "string"; }
