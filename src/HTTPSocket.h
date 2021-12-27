#ifndef HTTPSOCKET_UWS_H
#define HTTPSOCKET_UWS_H

#include "Socket.h"
#include <string>
// #include <experimental/string_view>

namespace uWS {

struct Header {
    char *key, *value;
    unsigned int keyLength, valueLength;

    operator bool() {
        // key为null来表示bool
        return key;
    }

    // slow without string_view!
    std::string toString() {
        if (!value || !valueLength) return std::string();
        return std::string(value, valueLength);
    }
};

enum HttpMethod {
    METHOD_GET,
    METHOD_POST,
    METHOD_PUT,
    METHOD_DELETE,
    METHOD_PATCH,
    METHOD_OPTIONS,
    METHOD_HEAD,
    METHOD_TRACE,
    METHOD_CONNECT,
    METHOD_INVALID
};

struct HttpRequest {
    // 以null结尾，且第一个键为 method : url
    Header *headers;

    HttpRequest(Header *headers = nullptr) : headers(headers) {}

    Header getHeader(const char *key) {
        return getHeader(key, strlen(key));
    }
    Header getHeader(const char *key, size_t length) {
        if (headers) {
            for (Header *h = headers; *++h; ) {
                if (h->keyLength == length && !strncmp(h->key, key, length)) {
                    return *h;
                }
            }
        }
        return {nullptr, nullptr, 0, 0};
    }

    Header getUrl() {
        if (headers && headers->key) {
            return *headers;
        }
        return {nullptr, nullptr, 0, 0};
    }

    HttpMethod getMethod() {
        if (!headers ||!headers->key) {
            return METHOD_INVALID;
        }
        switch (headers->keyLength) {
        case 3:
            if (!strncmp(headers->key, "get", 3)) {
                return METHOD_GET;
            } else if (!strncmp(headers->key, "put", 3)) {
                return METHOD_PUT;
            }
            break;
        case 4:
            if (!strncmp(headers->key, "post", 4)) {
                return METHOD_POST;
            } else if (!strncmp(headers->key, "head", 4)) {
                return METHOD_HEAD;
            }
            break;
        case 5:
            if (!strncmp(headers->key, "patch", 5)) {
                return METHOD_PATCH;
            } else if (!strncmp(headers->key, "trace", 5)) {
                return METHOD_TRACE;
            }
            break;
        case 6:
            if (!strncmp(headers->key, "delete", 6)) {
                return METHOD_DELETE;
            }
            break;
        case 7:
            if (!strncmp(headers->key, "options", 7)) {
                return METHOD_OPTIONS;
            } else if (!strncmp(headers->key, "connect", 7)) {
                return METHOD_CONNECT;
            }
            break;
        }
        return METHOD_INVALID;
    }
};

struct HttpResponse;

template <const bool isServer>
struct WIN32_EXPORT HttpSocket : uS::Socket {
    void *httpUser; // remove this later, setTimeout occupies user for now
    // 多个http响应列表，按请求顺序，从头到尾排序
    HttpResponse *outstandingResponsesHead = nullptr;
    HttpResponse *outstandingResponsesTail = nullptr;
    // 响应内存池(只保存一项)
    HttpResponse *preAllocatedResponse = nullptr;

    // http头部缓冲区
    std::string httpBuffer;
    size_t contentLength = 0;
    bool missedDeadline = false;

    HttpSocket(uS::Socket *socket) : uS::Socket(std::move(*socket)) {}

    void terminate() {
        onEnd(this);
    }

    void upgrade(const char *secKey, const char *extensions,
                 size_t extensionsLength, const char *subprotocol,
                 size_t subprotocolLength, bool *perMessageDeflate);

protected:
    friend struct uS::Socket;
    friend struct HttpResponse;
    friend struct Hub;
    static uS::Socket *onData(uS::Socket *s, char *data, size_t length);
    static void onEnd(uS::Socket *s);
};

struct HttpResponse {
    HttpSocket<true> *httpSocket;
    HttpResponse *next = nullptr;
    void *userData = nullptr;
    void *extraUserData = nullptr;
    uS::Socket::Queue::Message *messageQueue = nullptr;
    bool hasEnded = false;
    bool hasHead = false;

    HttpResponse(HttpSocket<true> *httpSocket) : httpSocket(httpSocket) {

    }

    //template <bool isServer>
    static HttpResponse *allocateResponse(HttpSocket<true> *httpSocket) {
        if (httpSocket->preAllocatedResponse) {
            HttpResponse *ret = httpSocket->preAllocatedResponse;
            httpSocket->preAllocatedResponse = nullptr;
            return ret;
        } else {
            return new HttpResponse(httpSocket);
        }
    }

    //template <bool isServer>
    void freeResponse(HttpSocket<true> *httpData) {
        if (httpData->preAllocatedResponse) {
            delete this;
        } else {
            httpData->preAllocatedResponse = this;
        }
    }
    // 必须自己构造http头部...
    void write(const char *message, size_t length = 0,
               void(*callback)(void *httpSocket, void *data, bool cancelled, void *reserved) = nullptr,
               void *callbackData = nullptr);

    // todo: maybe this function should have a fast path for 0 length?
    void end(const char *message = nullptr, size_t length = 0,
             void(*callback)(void *httpResponse, void *data, bool cancelled, void *reserved) = nullptr,
             void *callbackData = nullptr);

    void setUserData(void *userData) {
        this->userData = userData;
    }

    void *getUserData() {
        return userData;
    }

    HttpSocket<true> *getHttpSocket() {
        return httpSocket;
    }
};

}

#endif // HTTPSOCKET_UWS_H
