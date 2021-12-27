#ifndef GROUP_UWS_H
#define GROUP_UWS_H

#include "WebSocket.h"
#include "HTTPSocket.h"
#include "Extensions.h"
#include <functional>
#include <stack>

namespace uWS {

enum ListenOptions {
    TRANSFERS
};

struct Hub;

template <bool isServer>
struct WIN32_EXPORT Group : protected uS::NodeData {
public:
    using ConnectionHandler = std::function<void(WebSocket<isServer> *, HttpRequest)>;
    void onConnection(ConnectionHandler handler) { connectionHandler = handler; }
    using TransferHandler = std::function<void(WebSocket<isServer> *)>;
    void onTransfer(TransferHandler handler) { transferHandler = handler; }
    using MessageHandler = std::function<void(WebSocket<isServer> *, char *message, size_t length, OpCode opCode)>;
    void onMessage(MessageHandler handler) { messageHandler = handler; }
    using DisconnectionHandler = std::function<void(WebSocket<isServer> *, int code, char *message, size_t length)>;
    void onDisconnection(DisconnectionHandler handler) { disconnectionHandler = handler; }
    using PingPongHandler = std::function<void(WebSocket<isServer> *, char *, size_t)>;
    void onPing(PingPongHandler handler) { pingHandler = handler; }
    void onPong(PingPongHandler handler) { pongHandler = handler; }

    using HttpConnectionHandler = std::function<void(HttpSocket<isServer> *)>;
    void onHttpConnection(HttpConnectionHandler handler) { httpConnectionHandler = handler; }
    using HttpRequestHandler = std::function<void(HttpResponse *, HttpRequest, char *data, size_t length, size_t remainingBytes)>;
    void onHttpRequest(HttpRequestHandler handler) { httpRequestHandler = handler; }
    using HttpDataHandler = std::function<void(HttpResponse *, char *data, size_t length, size_t remainingBytes)>;
    void onHttpData(HttpDataHandler handler) { httpDataHandler = handler; }
    // 响应被取消回调，httpSocket::end主动断开时回调，之后response对象失效
    using HttpCancelledRequestHandler = std::function<void(HttpResponse *)>;
    void onCancelledHttpRequest(HttpCancelledRequestHandler handler) { httpCancelledRequestHandler = handler; }
    using HttpDisconnectionHandler = std::function<void(HttpSocket<isServer> *)>;
    void onHttpDisconnection(HttpDisconnectionHandler handler) { httpDisconnectionHandler = handler; }
    using HttpUpgradeHandler = std::function<void(HttpSocket<isServer> *, HttpRequest)>;
    void onHttpUpgrade(HttpUpgradeHandler handler) { httpUpgradeHandler = handler; }

    using errorType = typename std::conditional<isServer, int, void *>::type;
    using ErrorHandler = std::function<void(errorType)>; 
    void onError(ErrorHandler handler) { errorHandler = handler; }

    // Thread safe
    void broadcast(const char *message, size_t length, OpCode opCode);
    void setUserData(void *user) { this->userData = user; }
    void *getUserData() { return this->userData; }

    // Not thread safe
    void terminate();
    void close(int code = 1000, char *message = nullptr, size_t length = 0);
    void startAutoPing(int intervalMs, std::string userMessage = "");

    // same as listen(TRANSFERS), backwards compatible API for now
    void addAsync() {
        if (!async) {
            NodeData::addAsync();
        }
    }

    void listen(ListenOptions listenOptions) {
        if (listenOptions == TRANSFERS && !async) {
            addAsync();
        }
    }

    template <class T>
    void forEach(T head, std::function<void (T)> cb) {
      uS::Socket *iterator = head;
      iterators.push(iterator);
      while (iterator) {
        auto lastIterator = iterator;
        cb((T) iterator);
        iterator = iterators.top();
        if (lastIterator == iterator) {
          iterator = iterator->next;
          iterators.top() = iterator;
        }
      }
      iterators.pop();
    }

    void forEachWebSocket(std::function<void(WebSocket<isServer>*)> cb) {
      forEach(webSocketHead, cb);
    }
    void forEachHttpSocket(std::function<void(HttpSocket<isServer>*)> cb) {
      forEach(httpSocketHead, cb);
    }

    static Group<isServer> *from(uS::Socket *s) {
        return static_cast<Group<isServer> *>(s->getNodeData());
    }
protected:
    friend struct Hub;
    friend struct WebSocket<isServer>;
    friend struct HttpSocket<false>;
    friend struct HttpSocket<true>;

    ErrorHandler errorHandler;
    ConnectionHandler connectionHandler;
    TransferHandler transferHandler;
    MessageHandler messageHandler;
    DisconnectionHandler disconnectionHandler;
    PingPongHandler pingHandler, pongHandler;
    HttpConnectionHandler httpConnectionHandler;
    HttpRequestHandler httpRequestHandler;
    HttpDataHandler httpDataHandler;
    HttpCancelledRequestHandler httpCancelledRequestHandler;
    HttpDisconnectionHandler httpDisconnectionHandler;
    HttpUpgradeHandler httpUpgradeHandler;

    unsigned int maxPayload;
    Hub *hub;
    int extensionOptions;
    uS::Timer *timer = nullptr, *httpTimer = nullptr;
    std::string userPingMessage;
    std::stack<uS::Socket *> iterators;

    // todo: cannot be named user, collides with parent!
    void *userData = nullptr;
    static void timerCallback(uS::Timer *timer);

    WebSocket<isServer> *webSocketHead = nullptr;
    HttpSocket<isServer> *httpSocketHead = nullptr;

    void addWebSocket(WebSocket<isServer> *webSocket);
    void removeWebSocket(WebSocket<isServer> *webSocket);

    // todo: remove these, template
    void addHttpSocket(HttpSocket<isServer> *httpSocket);
    void removeHttpSocket(HttpSocket<isServer> *httpSocket);

    Group(int extensionOptions, unsigned int maxPayload, Hub *hub, uS::NodeData *nodeData);
    void stopListening();
};

}

#endif // GROUP_UWS_H
