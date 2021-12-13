#ifndef ASIO_H
#define ASIO_H

#ifdef ASIO_STANDALONE
#include <asio.hpp>
#include <asio/steady_timer.hpp>
using asio::steady_timer;
#else
#include <boost/asio.hpp>
using asio = boost::asio;
typedef boost::asio::deadline_timer steady_timer;
#endif

#ifdef WIN32
using stream = asio::windows::stream_handle;
#else
using stream = asio::posix::stream_descriptor;
#endif
//typedef stream::native_handle_type uv_os_sock_t;
typedef asio::detail::socket_type uv_os_sock_t;
// typedef asio::ip::tcp::socket::native_handle_type uv_os_sock_t;

static const int UV_READABLE = 1;
static const int UV_WRITABLE = 2;

namespace uS {

struct Loop : asio::io_service {

    static Loop *createLoop(bool defaultLoop = true) {
        return new Loop;
    }

    void destroy() {
        delete this;
    }

    void run() {
        asio::io_service::run();
    }

    void poll() {
        asio::io_service::poll();
    }
};

struct Timer {
    
    steady_timer asio_timer;
    void *data;

    Timer(Loop *loop) : asio_timer(*loop) {

    }

    void start(void (*cb)(Timer *), int first, int repeat) {
#ifdef ASIO_STANDALONE
        asio_timer.expires_from_now(std::chrono::milliseconds(first));
        asio_timer.async_wait([this, cb, repeat](const asio::error_code &ec) 
#else
        asio_timer.expires_from_now(boost::posix_time::milliseconds(first));
        asio_timer.async_wait([this, cb, repeat](const boost::system::error_code &ec)
#endif
        {
            if (ec != asio::error::operation_aborted) {
                if (repeat) {
                    start(cb, repeat, repeat);
                }
                cb(this);
            }
        });
    }

    void setData(void *data) {
        this->data = data;
    }

    void *getData() {
        return data;
    }

    // bug: cancel does not cancel expired timers!
    // it has to guarantee that the timer is not called after
    // stop is called! ffs boost!
    void stop() {
        asio_timer.cancel();
    }

    void close() {
        asio_timer.get_io_service().post([this]() {
            delete this;
        });
    }
};

struct Async {
    Loop *loop;
    void (*cb)(Async *);
    void *data;

    asio::io_service::work asio_work;

    Async(Loop *loop) : loop(loop), asio_work(*loop) {
    }

    void start(void (*cb)(Async *)) {
        this->cb = cb;
    }

    void send() {
        loop->post([this]() {
            cb(this);
        });
    }

    void close() {
        loop->post([this]() {
            delete this;
        });
    }

    void setData(void *data) {
        this->data = data;
    }

    void *getData() {
        return data;
    }
};

struct Poll {
    stream *socket;

    void (*cb)(Poll *p, int status, int events);

    Poll(Loop *loop, uv_os_sock_t fd) {
        socket = new stream(*loop, fd);
#ifndef WIN32
        socket->non_blocking(true);
#endif
    }

    bool isClosed() {
        return !socket;
    }

    uv_os_sock_t getFd() {
        return socket ? socket->native_handle() : asio::detail::invalid_socket;
    }

    void setCb(void (*cb)(Poll *p, int status, int events)) {
        this->cb = cb;
    }

    void (*getCb())(Poll *, int, int) {
        return cb;
    }

    void reInit(Loop *loop, uv_os_sock_t fd) {
        delete socket;
        socket = new stream(*loop, fd);
#ifndef WIN32
        socket->non_blocking(true);
#endif
    }

    void start(Loop *, Poll *self, int events) {
        if (events & UV_READABLE) {
            socket->async_read_some(asio::null_buffers(), [self](asio::error_code ec, std::size_t) {
                if (ec != asio::error::operation_aborted) {
                    self->start(nullptr, self, UV_READABLE);
                    self->cb(self, ec ? -1 : 0, UV_READABLE);
                }
            });
        }

        if (events & UV_WRITABLE) {
            socket->async_write_some(asio::null_buffers(), [self](asio::error_code ec, std::size_t) {
                if (ec != asio::error::operation_aborted) {
                    self->start(nullptr, self, UV_WRITABLE);
                    self->cb(self, ec ? -1 : 0, UV_WRITABLE);
                }
            });
        }
    }

    void change(Loop *, Poll *self, int events) {
        socket->cancel();
        start(nullptr, self, events);
    }

    bool fastTransfer(Loop *loop, Loop *newLoop, int events) {
        return false;
    }

    // todo: asio is thread safe, use it!
    bool threadSafeChange(Loop *loop, Poll *self, int events) {
        return false;
    }

    void stop(Loop *) {
        socket->cancel();
    }

    // this is not correct, but it works for now
    // think about transfer - should allow one to not delete
    // but in this case it doesn't matter at all
    void close(Loop *loop, void (*cb)(Poll *)) {
#ifdef WIN32
        socket->close();
#else
        socket->release();
#endif
        socket->get_io_service().post([cb, this]() {
            cb(this);
        });
        delete socket;
        socket = nullptr;
    }
};

}

#endif // ASIO_H
