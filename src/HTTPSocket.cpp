#include "HTTPSocket.h"
#include "Group.h"
#include "Extensions.h"
#include <cstdio>

#define MAX_HEADERS 100
#define MAX_HEADER_BUFFER_SIZE 4096
#define FORCE_SLOW_PATH false

namespace uWS {

// UNSAFETY NOTE: assumes *end == '\r' (might unref end pointer)
char *getHeaders(char *buffer, char *end, Header *headers, size_t maxHeaders) {
    for (unsigned int i = 0; i < maxHeaders; i++) {
        for (headers->key = buffer; (*buffer != ':') & (*buffer > 32); *(buffer++) |= 32);
        if (*buffer == '\r') {
            // 找到http头部结尾?
            if ((buffer != end) & (buffer[1] == '\n') & (i > 0)) {
                // 最后一个元素置为false
                headers->key = nullptr;
                return buffer + 2;
            } else {
                return nullptr;
            }
        } else {
            headers->keyLength = (unsigned int) (buffer - headers->key);
            for (buffer++; (*buffer == ':' || *buffer < 33) && *buffer != '\r'; buffer++);
            headers->value = buffer;
            buffer = (char *) memchr(buffer, '\r', end - buffer); //for (; *buffer != '\r'; buffer++);
            if (buffer /*!= end*/ && buffer[1] == '\n') {
                headers->valueLength = (unsigned int) (buffer - headers->value);
                buffer += 2;
                headers++;
            } else {
                return nullptr;
            }
        }
    }
    return nullptr;
}

// UNSAFETY NOTE: assumes 24 byte input length
static void base64(unsigned char *src, char *dst) {
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 18; i += 3) {
        *dst++ = b64[(src[i] >> 2) & 63];
        *dst++ = b64[((src[i] & 3) << 4) | ((src[i + 1] & 240) >> 4)];
        *dst++ = b64[((src[i + 1] & 15) << 2) | ((src[i + 2] & 192) >> 6)];
        *dst++ = b64[src[i + 2] & 63];
    }
    *dst++ = b64[(src[18] >> 2) & 63];
    *dst++ = b64[((src[18] & 3) << 4) | ((src[19] & 240) >> 4)];
    *dst++ = b64[((src[19] & 15) << 2)];
    *dst++ = '=';
}

template <bool isServer>
uS::Socket *HttpSocket<isServer>::onData(uS::Socket *s, char *data, size_t length) {
    HttpSocket<isServer> *httpSocket = (HttpSocket<isServer> *) s;

    httpSocket->cork(true);

    if (httpSocket->contentLength) {
        httpSocket->missedDeadline = false;
        if (httpSocket->contentLength >= length) {
            Group<isServer>::from(httpSocket)->httpDataHandler(httpSocket->outstandingResponsesTail, data, length, httpSocket->contentLength -= length);
            return httpSocket;
        } else {
            Group<isServer>::from(httpSocket)->httpDataHandler(httpSocket->outstandingResponsesTail, data, httpSocket->contentLength, 0);
            data += httpSocket->contentLength;
            length -= httpSocket->contentLength;
            // receive all contentLenght
            httpSocket->contentLength = 0;
        }
    }

    if (FORCE_SLOW_PATH || httpSocket->httpBuffer.length()) {
        if (httpSocket->httpBuffer.length() + length > MAX_HEADER_BUFFER_SIZE) {
            httpSocket->onEnd(httpSocket);
            return httpSocket;
        }

        httpSocket->httpBuffer.reserve(httpSocket->httpBuffer.length() + length + WebSocketProtocol<uWS::CLIENT, WebSocket<uWS::CLIENT>>::CONSUME_POST_PADDING);
        httpSocket->httpBuffer.append(data, length);
        data = (char *) httpSocket->httpBuffer.data();
        length = httpSocket->httpBuffer.length();
    }

    char *end = data + length;
    char *cursor = data;
    *end = '\r';
    Header headers[MAX_HEADERS];
    do {
        char *lastCursor = cursor;
        if ((cursor = getHeaders(cursor, end, headers, MAX_HEADERS))) {
            HttpRequest req(headers);

            if (isServer) {
                auto servGroup = Group<SERVER>::from(httpSocket);
                auto servSocket = (HttpSocket<SERVER> *) httpSocket;
                headers->valueLength = std::max<int>(0, headers->valueLength - 9);
                httpSocket->missedDeadline = false;
                if (req.getHeader("upgrade", 7)) {
                    if (servGroup->httpUpgradeHandler) {
                        servGroup->httpUpgradeHandler(servSocket, req);
                    } else {
                        Header secKey = req.getHeader("sec-websocket-key", 17);
                        Header extensions = req.getHeader("sec-websocket-extensions", 24);
                        Header subprotocol = req.getHeader("sec-websocket-protocol", 22);
                        if (secKey.valueLength == 24) {
                            bool perMessageDeflate;
                            httpSocket->upgrade(secKey.value, extensions.value, extensions.valueLength,
                                               subprotocol.value, subprotocol.valueLength, &perMessageDeflate);
                            servGroup->removeHttpSocket(servSocket);

                            // Warning: changes socket, needs to inform the stack of Poll address change!
                            WebSocket<isServer> *webSocket = new WebSocket<isServer>(perMessageDeflate, httpSocket);
                            webSocket->template setState<WebSocket<isServer>>();
                            webSocket->change(webSocket->nodeData->loop, webSocket, webSocket->setPoll(UV_READABLE));
                            Group<isServer>::from(webSocket)->addWebSocket(webSocket);

                            webSocket->cork(true);
                            Group<isServer>::from(webSocket)->connectionHandler(webSocket, req);
                            // todo: should not uncork if closed!
                            webSocket->cork(false);
                            delete httpSocket;

                            return webSocket;
                        } else {
                            // secKey error
                            httpSocket->onEnd(httpSocket);
                        }
                    }
                    return httpSocket;
                } else {
                    if (servGroup->httpRequestHandler) {
                        // 创建一个响应增加到尾部, 由于http-keepalive会复用http连接，发送多个http请求响应
                        HttpResponse *res = HttpResponse::allocateResponse(servSocket);
                        if (httpSocket->outstandingResponsesTail) {
                            httpSocket->outstandingResponsesTail->next = res;
                        } else {
                            httpSocket->outstandingResponsesHead = res;
                        }
                        httpSocket->outstandingResponsesTail = res;

                        Header contentLength;
                        if (req.getMethod() != HttpMethod::METHOD_GET && (contentLength = req.getHeader("content-length", 14))) {
                            // 得出contentLength
                            httpSocket->contentLength = atoi(contentLength.value);
                            size_t bytesToRead = std::min<size_t>(httpSocket->contentLength, end - cursor);
                            if (bytesToRead > 0) {
                                servGroup->httpRequestHandler(res, req, cursor, bytesToRead, httpSocket->contentLength -= bytesToRead);
                                cursor += bytesToRead;
                            }
                        } else {
                            // get or without content-length
                            servGroup->httpRequestHandler(res, req, nullptr, 0, 0);
                        }

                        if (httpSocket->isClosed() || httpSocket->isShuttingDown()) {
                            return httpSocket;
                        }
                    } else {
                        httpSocket->onEnd(httpSocket);
                        return httpSocket;
                    }
                }
            } else {
                if (req.getHeader("upgrade", 7)) {

                    // Warning: changes socket, needs to inform the stack of Poll address change!
                    WebSocket<isServer> *webSocket = new WebSocket<isServer>(false, httpSocket);
                    httpSocket->cancelTimeout();
                    webSocket->setUserData(httpSocket->httpUser);
                    webSocket->template setState<WebSocket<isServer>>();
                    webSocket->change(webSocket->nodeData->loop, webSocket, webSocket->setPoll(UV_READABLE));
                    Group<isServer>::from(webSocket)->addWebSocket(webSocket);

                    webSocket->cork(true);
                    Group<isServer>::from(webSocket)->connectionHandler(webSocket, req);
                    if (!(webSocket->isClosed() || webSocket->isShuttingDown())) {
                        WebSocketProtocol<isServer, WebSocket<isServer>>::consume(cursor, (unsigned int) (end - cursor), webSocket);
                    }
                    webSocket->cork(false);
                    delete httpSocket;

                    return webSocket;
                } else {
                    // @todo处理正常的http客户端响应
                    httpSocket->onEnd(httpSocket);
                }
                return httpSocket;
            }
        } else {
            if (!httpSocket->httpBuffer.length()) {
                if (length > MAX_HEADER_BUFFER_SIZE) {
                    httpSocket->onEnd(httpSocket);
                } else {
                    httpSocket->httpBuffer.append(lastCursor, end - lastCursor);
                }
            }
            return httpSocket;
        }
    } while(cursor != end);

    httpSocket->cork(false);
    httpSocket->httpBuffer.clear();

    return httpSocket;
}

// todo: make this into a transformer and make use of sendTransformed
template <bool isServer>
void HttpSocket<isServer>::upgrade(const char *secKey, const char *extensions, size_t extensionsLength,
                                   const char *subprotocol, size_t subprotocolLength, bool *perMessageDeflate) {

    Queue::Message *messagePtr = nullptr;

    if (isServer) {
        *perMessageDeflate = false;
        std::string extensionsResponse;
        if (extensionsLength) {
            Group<isServer> *group = Group<isServer>::from(this);
            ExtensionsNegotiator<uWS::SERVER> extensionsNegotiator(group->extensionOptions);
            extensionsNegotiator.readOffer(std::string(extensions, extensionsLength));
            extensionsResponse = extensionsNegotiator.generateOffer();
            if (extensionsNegotiator.getNegotiatedOptions() & PERMESSAGE_DEFLATE) {
                *perMessageDeflate = true;
            }
        }

        unsigned char shaInput[] = "XXXXXXXXXXXXXXXXXXXXXXXX258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        // 用secKey的当前24字节
        memcpy(shaInput, secKey, 24);
        unsigned char shaDigest[SHA_DIGEST_LENGTH];
        SHA1(shaInput, sizeof(shaInput) - 1, shaDigest);

        char upgradeBuffer[1024];
        memcpy(upgradeBuffer, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ", 97);
        base64(shaDigest, upgradeBuffer + 97);
        memcpy(upgradeBuffer + 125, "\r\n", 2);
        size_t upgradeResponseLength = 127;
        if (extensionsResponse.length() && extensionsResponse.length() < 200) {
            memcpy(upgradeBuffer + upgradeResponseLength, "Sec-WebSocket-Extensions: ", 26); upgradeResponseLength += 26;
            memcpy(upgradeBuffer + upgradeResponseLength, extensionsResponse.data(), extensionsResponse.length()); upgradeResponseLength += extensionsResponse.length();
            memcpy(upgradeBuffer + upgradeResponseLength, "\r\n", 2); upgradeResponseLength += 2;
        }
        // select first protocol
        for (unsigned int i = 0; i < subprotocolLength; i++) {
            if (subprotocol[i] == ',') {
                subprotocolLength = i;
                break;
            }
        }
        if (subprotocolLength && subprotocolLength < 200) {
            memcpy(upgradeBuffer + upgradeResponseLength, "Sec-WebSocket-Protocol: ", 24); upgradeResponseLength += 24;
            memcpy(upgradeBuffer + upgradeResponseLength, subprotocol, subprotocolLength); upgradeResponseLength += subprotocolLength;
            memcpy(upgradeBuffer + upgradeResponseLength, "\r\n", 2); upgradeResponseLength += 2;
        }
        static char stamp[] = "Sec-WebSocket-Version: 13\r\nWebSocket-Server: uWebSockets\r\n\r\n";
        memcpy(upgradeBuffer + upgradeResponseLength, stamp, sizeof(stamp) - 1);
        upgradeResponseLength += sizeof(stamp) - 1;

        messagePtr = allocMessage(upgradeResponseLength, upgradeBuffer);
    } else {
        messagePtr = allocMessage(httpBuffer.length(), httpBuffer.data());
        httpBuffer.clear();
    }

    bool wasTransferred;
    if (write(messagePtr, wasTransferred)) {
        if (!wasTransferred) {
            freeMessage(messagePtr);
        } else {
            messagePtr->callback = nullptr;
        }
    } else {
        freeMessage(messagePtr);
    }
}

template <bool isServer>
void HttpSocket<isServer>::onEnd(uS::Socket *s) {
    HttpSocket<isServer> *httpSocket = (HttpSocket<isServer> *) s;
    auto httpGroup = Group<isServer>::from(httpSocket);
    if (!httpSocket->isShuttingDown()) {
        if (isServer) {
            httpGroup->removeHttpSocket(httpSocket);
            httpGroup->httpDisconnectionHandler(httpSocket);
        }
    } else {
        httpSocket->cancelTimeout();
    }

    httpSocket->template closeSocket<HttpSocket<isServer>>();

    while (!httpSocket->messageQueue.empty()) {
        Queue::Message *message = httpSocket->messageQueue.front();
        if (message->callback) {
            message->callback(nullptr, message->callbackData, true, nullptr);
        }
        httpSocket->messageQueue.pop();
    }

    HttpResponse* resp = httpSocket->outstandingResponsesHead;
    httpSocket->outstandingResponsesHead = nullptr;
    httpSocket->outstandingResponsesTail = nullptr;
    while (resp) {
        HttpResponse *next = resp->next;
        httpGroup->httpCancelledRequestHandler(resp);
        delete resp;
        resp = next;
    }

    if (httpSocket->preAllocatedResponse) {
        delete httpSocket->preAllocatedResponse;
    }

    httpSocket->nodeData->clearPendingPollChanges(httpSocket);

    if (!isServer) {
        httpSocket->cancelTimeout();
        Group<CLIENT>::from(httpSocket)->errorHandler(httpSocket->httpUser);
    }
}

template struct HttpSocket<SERVER>;
template struct HttpSocket<CLIENT>;

void HttpResponse::write(const char *message, size_t length /*= 0*/, 
    void(*callback)(void *httpSocket, void *data, bool cancelled, void *reserved) /*= nullptr*/, 
    void *callbackData /*= nullptr*/)
{
    struct NoopTransformer {
        static size_t estimate(const char *data, size_t length) {
            return length;
        }

        static size_t transform(const char *src, char *dst, size_t length, int transformData) {
            memcpy(dst, src, length);
            return length;
        }
    };

    httpSocket->sendTransformed<NoopTransformer>(message, length, callback, callbackData, 0);
    hasHead = true;
}

void HttpResponse::end(const char *message /*= nullptr*/, size_t length /*= 0*/, 
    void(*callback)(void *httpResponse, void *data, bool cancelled, void *reserved) /*= nullptr*/, 
    void *callbackData /*= nullptr*/)
{
    struct TransformData {
        bool hasHead;
    } transformData = { hasHead };

    struct HttpTransformer {

        // todo: this should get TransformData!
        static size_t estimate(const char *data, size_t length) {
            return length + 128;
        }

        static size_t transform(const char *src, char *dst, size_t length, TransformData transformData) {
            // todo: sprintf is extremely slow
            int offset = 0;
            if (!transformData.hasHead)
                offset = std::sprintf(dst, "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", (unsigned int)length);
            memcpy(dst + offset, src, length);
            return length + offset;
        }
    };

    if (httpSocket->outstandingResponsesHead != this) 
    { // 非第一个响应，则缓存内容，容后再传，并标记这个请求已完成(hasEnded=true)
        auto *messagePtr = httpSocket->allocMessage(HttpTransformer::estimate(message, length));
        messagePtr->length = HttpTransformer::transform(message, (char *)messagePtr->data, length, transformData);
        messagePtr->callback = callback;
        messagePtr->callbackData = callbackData;
        messagePtr->nextMessage = messageQueue;
        // add message to queue
        this->messageQueue = messagePtr;
        hasEnded = true;
    }
    else {
        // 发送完第一个/本响应后，再次发送其他响应
        httpSocket->sendTransformed<HttpTransformer>(message, length, callback, callbackData, transformData);
        // move head as far as possible
        HttpResponse *head = next;
        while (head) {
            // empty message queue
            auto *messagePtr = head->messageQueue;
            while (messagePtr) {
                auto *nextMessage = messagePtr->nextMessage;

                bool wasTransferred;
                if (httpSocket->write(messagePtr, wasTransferred)) {
                    if (!wasTransferred) {
                        httpSocket->freeMessage(messagePtr);
                        if (callback) {
                            callback(this, callbackData, false, nullptr);
                        }
                    }
                    else {
                        messagePtr->callback = callback;
                        messagePtr->callbackData = callbackData;
                    }
                }
                else {
                    httpSocket->freeMessage(messagePtr);
                    if (callback) {
                        callback(this, callbackData, true, nullptr);
                    }
                    goto updateHead;
                }
                messagePtr = nextMessage;
            }
            if (!head->hasEnded) {
                // cannot go beyond unfinished responses
                break;
            }
            else {
                HttpResponse *next = head->next;
                head->freeResponse(httpSocket);
                head = next;
            }
        }
    updateHead:
        httpSocket->outstandingResponsesHead = head;
        if (!head) {
            httpSocket->outstandingResponsesTail = nullptr;
        }

        freeResponse(httpSocket);
    }
}

}
