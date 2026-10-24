// ============================================================================
// src/platform/posix/websocket_posix.cpp -- NOT RETAIL. The WebSocket seam on Linux
// ============================================================================
// platform_websocket.h, for ext.neighbour (docs/fork/NEIGHBOUR_TOWNS.md). Windows has WinHTTP
// (src/platform/win/websocket_win.cpp) and macOS Network.framework
// (src/platform/darwin/websocket_darwin.cpp, which replaces this file on Apple). Linux has no
// WebSocket client in the OS, so this is one: a TCP socket, OpenSSL for wss:// (TLS 1.2 or
// later, SNI, the system's CA store, a host-name check), and RFC 6455's handshake and framing
// from src/platform/common/websocket_frames.h. Pings are answered here; a close from the relay
// is answered with its own code and reads as Closed.
//
// THREADS. One thread blocks in WebSocketReceiveText while others call WebSocketSendText, and
// OpenSSL forbids two calls on one SSL object at once. So the socket is non-blocking, `io` is
// held around each single SSL_read / SSL_write / recv / send and never while waiting, and the
// waiting is poll() on the socket plus a self-pipe that WebSocketCancel writes to (never
// drained, so every later wait wakes at once). `send` keeps a whole frame together when two
// threads send (the reader answers pings). A TLS call can need the other direction (WANT_READ
// from SSL_write, WANT_WRITE from SSL_read) and the other thread's call can consume what a wait
// was waiting for, so TLS waits are sliced: the call is retried every kSliceMs.
//
// No SIGPIPE: plain sends pass MSG_NOSIGNAL, and TLS writes through the small socket BIO below,
// which does the same (OpenSSL's own socket BIO calls write()).
// ============================================================================

#include "platform/common/platform_websocket.h"

#include "platform/common/websocket_frames.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace Platform {

namespace WSF = WebSocketFrames;

struct WebSocketObject {
    int               fd = -1;
    int               wakeRead = -1;    // the self-pipe: readable once WebSocketCancel ran
    int               wakeWrite = -1;
    SSL_CTX*          ctx = nullptr;
    SSL*              ssl = nullptr;    // null for ws://
    std::mutex        ioMu;             // around each single SSL / socket call, never while waiting
    std::mutex        sendMu;           // one whole frame at a time
    std::atomic<bool> cancelled{false};
    std::atomic<bool> broken{false};      // a call failed or the line ended: no more I/O
    std::atomic<bool> closeSent{false};   // our close frame went out: no frame after it
        // The receiving thread's only.
    WSF::Reader   reader;
    bool          ended = false;
    WebSocketRecv endedAs = WebSocketRecv::Closed;

    ~WebSocketObject() {
        if (ssl != nullptr) SSL_free(ssl);   // frees the BIO; the socket is closed below
        if (ctx != nullptr) SSL_CTX_free(ctx);
        if (fd >= 0) close(fd);
        if (wakeRead >= 0) close(wakeRead);
        if (wakeWrite >= 0) close(wakeWrite);
    }
};

namespace {

using Clock = std::chrono::steady_clock;

constexpr int      kSliceMs        = 250;     // TLS: a waiting call is retried this often
constexpr uint32_t kSendTimeoutMs  = 10000;   // as the macOS twin
constexpr uint32_t kCloseTimeoutMs = 250;     // WebSocketFree's parting close frame
constexpr size_t   kMaxHead        = 16384;   // the upgrade answer's status line and headers
constexpr size_t   kReadChunk      = 16384;
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;   // SO_NOSIGPIPE is set on the socket instead (Prepare)
#endif

const char kTimedOut[] = "the relay did not answer in time";

Clock::time_point After(uint32_t ms) {
    return Clock::now() + std::chrono::milliseconds(ms);
}

// Milliseconds to `deadline` for poll(), rounded up so a wait never spins on 0; -1 for none.
int MsLeft(Clock::time_point deadline) {
    if (deadline == Clock::time_point::max()) return -1;
    const Clock::time_point now = Clock::now();
    if (now >= deadline) return 0;
    const long long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1;
    return left > INT_MAX ? INT_MAX : static_cast<int>(left);
}

std::string Unreachable(int err) {
    return std::string("cannot reach the relay: ") + std::strerror(err);
}

// ---- the TLS layer's socket BIO: OpenSSL's own, but send() with kSendFlags ----

int BioFd(BIO* bio) {
    return static_cast<int>(reinterpret_cast<intptr_t>(BIO_get_data(bio)));
}

int BioWrite(BIO* bio, const char* data, int size) {
    BIO_clear_retry_flags(bio);
    const ssize_t n = ::send(BioFd(bio), data, static_cast<size_t>(size), kSendFlags);
    if (n >= 0) return static_cast<int>(n);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) BIO_set_retry_write(bio);
    return -1;
}

int BioRead(BIO* bio, char* data, int size) {
    BIO_clear_retry_flags(bio);
    const ssize_t n = ::recv(BioFd(bio), data, static_cast<size_t>(size), 0);
    if (n >= 0) return static_cast<int>(n);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) BIO_set_retry_read(bio);
    return -1;
}

long BioCtrl(BIO* /*bio*/, int cmd, long /*num*/, void* /*ptr*/) {
    return cmd == BIO_CTRL_FLUSH ? 1 : 0;   // nothing is buffered here
}

int BioCreate(BIO* /*bio*/) {
    return 1;
}

int BioDestroy(BIO* /*bio*/) {
    return 1;   // the socket belongs to the WebSocketObject
}

// Made once, kept for the process's life.
BIO_METHOD* SocketBio() {
    static BIO_METHOD* method = [] {
        const int index = BIO_get_new_index();
        BIO_METHOD* m = index < 0 ? nullptr : BIO_meth_new(index | BIO_TYPE_SOURCE_SINK, "openba websocket socket");
        if (m != nullptr) {
            BIO_meth_set_write(m, BioWrite);
            BIO_meth_set_read(m, BioRead);
            BIO_meth_set_ctrl(m, BioCtrl);
            BIO_meth_set_create(m, BioCreate);
            BIO_meth_set_destroy(m, BioDestroy);
        }
        return m;
    }();
    return method;
}

// ---- waiting and single calls ----

enum class Wake : uint8_t { Ready, Timeout, Cancelled, Failed };

// Waits for `events` on the socket (Ready also when a TLS slice ran out: retry the call), the
// deadline, or with `watchCancel` the self-pipe. Never holds a lock.
Wake WaitFor(WebSocketObject* o, short events, Clock::time_point deadline, bool watchCancel) {
    pollfd fds[2] = {};
    fds[0].fd = o->fd;
    fds[0].events = events;
    fds[1].fd = o->wakeRead;
    fds[1].events = POLLIN;
    const nfds_t count = watchCancel ? 2 : 1;
    for (;;) {
        if (watchCancel && o->cancelled.load()) return Wake::Cancelled;
        int  wait = MsLeft(deadline);
        bool sliced = false;
        if (wait == 0) return Wake::Timeout;
        if (o->ssl != nullptr && (wait < 0 || wait > kSliceMs)) {
            wait = kSliceMs;
            sliced = true;
        }
        const int rc = poll(fds, count, wait);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return Wake::Failed;
        }
        if (watchCancel && (fds[1].revents & POLLIN) != 0) return Wake::Cancelled;
        if (rc == 0 && sliced) return Wake::Ready;
        if (fds[0].revents != 0) return Wake::Ready;   // POLLERR / POLLHUP too: the call reports it
    }
}

enum class Io : uint8_t { Done, WantRead, WantWrite, End, Failed };

Io FromSsl(int err) {
    switch (err) {
    case SSL_ERROR_WANT_READ:   return Io::WantRead;
    case SSL_ERROR_WANT_WRITE:  return Io::WantWrite;
    case SSL_ERROR_ZERO_RETURN: return Io::End;
    default:                    return Io::Failed;
    }
}

Io FromErrno(Io want) {
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? want : Io::Failed;
}

Io ReadOnce(WebSocketObject* o, char* buf, size_t size, size_t& got) {
    std::lock_guard<std::mutex> lock(o->ioMu);
    got = 0;
    if (o->ssl == nullptr) {
        const ssize_t n = ::recv(o->fd, buf, size, 0);
        if (n > 0) {
            got = static_cast<size_t>(n);
            return Io::Done;
        }
        return n == 0 ? Io::End : FromErrno(Io::WantRead);
    }
    ERR_clear_error();
    const int n = SSL_read(o->ssl, buf, static_cast<int>(size));
    if (n > 0) {
        got = static_cast<size_t>(n);
        return Io::Done;
    }
    return FromSsl(SSL_get_error(o->ssl, n));
}

Io WriteOnce(WebSocketObject* o, const char* data, size_t size, size_t& put) {
    std::lock_guard<std::mutex> lock(o->ioMu);
    put = 0;
    if (o->ssl == nullptr) {
        const ssize_t n = ::send(o->fd, data, size, kSendFlags);
        if (n >= 0) {
            put = static_cast<size_t>(n);
            return Io::Done;
        }
        return FromErrno(Io::WantWrite);
    }
    ERR_clear_error();
    const int n = SSL_write(o->ssl, data, static_cast<int>(size));   // partial writes on (StartTls)
    if (n > 0) {
        put = static_cast<size_t>(n);
        return Io::Done;
    }
    return FromSsl(SSL_get_error(o->ssl, n));
}

enum class Got : uint8_t { Done, End, Failed, Timeout, Cancelled };

Got FromWake(Wake w) {
    switch (w) {
    case Wake::Timeout:   return Got::Timeout;
    case Wake::Cancelled: return Got::Cancelled;
    default:              return Got::Failed;
    }
}

// Some bytes, waiting for them as long as it takes (to `deadline`), or why not.
Got ReadSome(WebSocketObject* o, char* buf, size_t size, size_t& got, Clock::time_point deadline) {
    for (;;) {
        if (o->cancelled.load()) return Got::Cancelled;
        const Io io = ReadOnce(o, buf, size, got);
        if (io == Io::Done) return Got::Done;
        if (io == Io::End) return Got::End;
        if (io == Io::Failed) return Got::Failed;
        const Wake w = WaitFor(o, io == Io::WantRead ? POLLIN : POLLOUT, deadline, true);
        if (w != Wake::Ready) return FromWake(w);
    }
}

// All of `data`, or why not; `started` once a write was tried (a TLS write that has to wait
// is pending inside OpenSSL even when it took no byte yet). The parting close in WebSocketFree
// goes out after the cancel, so it does not watch it.
Got WriteAll(WebSocketObject* o, const char* data, size_t size, Clock::time_point deadline, bool watchCancel,
             bool& started) {
    size_t done = 0;
    started = false;
    while (done < size) {
        if (watchCancel && o->cancelled.load()) return Got::Cancelled;
        size_t   put = 0;
        const Io io = WriteOnce(o, data + done, size - done, put);
        started = true;
        if (io == Io::Done) {
            done += put;
            continue;
        }
        if (io == Io::End || io == Io::Failed) return Got::Failed;
        const Wake w = WaitFor(o, io == Io::WantRead ? POLLIN : POLLOUT, deadline, watchCancel);
        if (w != Wake::Ready) return FromWake(w);
    }
    return Got::Done;
}

// One whole frame. A frame cut short (or a TLS write left pending) leaves the line unusable,
// so a failure after the first write marks it broken.
bool SendFrame(WebSocketObject* o, uint8_t opcode, const void* data, size_t size, uint32_t timeoutMs, bool parting) {
    uint8_t mask[4];
    if (RAND_bytes(mask, sizeof(mask)) != 1) return false;
    const std::string           frame = WSF::EncodeClientFrame(opcode, data, size, mask);
    std::lock_guard<std::mutex> lock(o->sendMu);
    if (o->broken.load() || o->closeSent.load()) return false;
    if (!parting && o->cancelled.load()) return false;
    bool started = false;
    if (WriteAll(o, frame.data(), frame.size(), After(timeoutMs), !parting, started) != Got::Done) {
        if (started) o->broken.store(true);
        return false;
    }
    if (opcode == WSF::kClose) o->closeSent.store(true);
    return true;
}

// ---- connecting ----

struct Lookup {
    std::mutex              mu;
    std::condition_variable cv;
    bool                    done = false;
    bool                    abandoned = false;   // timed out: the lookup frees its own result
    int                     rc = 0;
    addrinfo*               result = nullptr;
};

// getaddrinfo has no timeout of its own, so it runs on a thread of its own; one that outlives
// `timeoutMs` is left to finish and clean up after itself.
bool Resolve(const WSF::Url& where, uint32_t timeoutMs, addrinfo*& out, std::string& error) {
    std::shared_ptr<Lookup> lookup = std::make_shared<Lookup>();
    const std::string       host = where.host;
    const std::string       service = std::to_string(where.port);
    try {
        std::thread([lookup, host, service] {
            addrinfo hints = {};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_NUMERICSERV;
            addrinfo* result = nullptr;
            const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
            std::lock_guard<std::mutex> lock(lookup->mu);
            if (lookup->abandoned) {
                if (result != nullptr) freeaddrinfo(result);
                return;
            }
            lookup->rc = rc;
            lookup->result = rc == 0 ? result : nullptr;
            lookup->done = true;
            lookup->cv.notify_all();
        }).detach();
    } catch (const std::system_error&) {
        error = "cannot reach the relay: the name lookup could not start";
        return false;
    }
    std::unique_lock<std::mutex> lock(lookup->mu);
    if (!lookup->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&lookup] { return lookup->done; })) {
        lookup->abandoned = true;
        error = "cannot reach the relay: the relay's name did not resolve in time";
        return false;
    }
    if (lookup->rc != 0 || lookup->result == nullptr) {
        if (lookup->rc == EAI_NONAME) {
            error = "cannot reach the relay: the relay's name was not found";
        } else {
            error = std::string("cannot reach the relay: ") + gai_strerror(lookup->rc);
        }
        return false;
    }
    out = lookup->result;
    return true;
}

bool SetFlags(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return false;
    return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

bool OpenWakePipe(WebSocketObject* o, std::string& error) {
    int fds[2];
    if (pipe(fds) != 0) {
        error = Unreachable(errno);
        return false;
    }
    o->wakeRead = fds[0];
    o->wakeWrite = fds[1];
    if (!SetFlags(fds[0]) || !SetFlags(fds[1])) {
        error = Unreachable(errno);
        return false;
    }
    return true;
}

// Non-blocking, close-on-exec, no SIGPIPE, no Nagle (the relay's messages are small and the
// Link wants them sent now).
bool Prepare(int fd) {
    if (!SetFlags(fd)) return false;
    const int one = 1;
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return true;
}

// Each address in turn until one connects, all within `timeoutMs`.
bool ConnectAny(WebSocketObject* o, const addrinfo* list, uint32_t timeoutMs, std::string& error) {
    const Clock::time_point deadline = After(timeoutMs);
    int                     lastError = ECONNREFUSED;
    for (const addrinfo* a = list; a != nullptr; a = a->ai_next) {
        const int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            lastError = errno;
            continue;
        }
        o->fd = fd;
        if (!Prepare(fd)) {
            lastError = errno;
        } else if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
            return true;
        } else if (errno != EINPROGRESS) {
            lastError = errno;
        } else {
            const Wake w = WaitFor(o, POLLOUT, deadline, false);
            if (w == Wake::Ready) {
                int       soError = 0;
                socklen_t len = sizeof(soError);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) != 0) soError = errno;
                if (soError == 0) return true;
                lastError = soError;
            } else if (w == Wake::Timeout) {
                close(fd);
                o->fd = -1;
                error = kTimedOut;
                return false;
            } else {
                lastError = errno;
            }
        }
        close(fd);
        o->fd = -1;
    }
    error = Unreachable(lastError);
    return false;
}

bool IsIpLiteral(const std::string& host) {
    unsigned char addr[sizeof(in6_addr)];
    return inet_pton(AF_INET, host.c_str(), addr) == 1 || inet_pton(AF_INET6, host.c_str(), addr) == 1;
}

std::string TlsFailure(SSL* ssl) {
    std::string why = "cannot reach the relay: the secure (TLS) handshake failed";
    const long  verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK) {
        why += std::string(" (") + X509_verify_cert_error_string(verify) + ")";
        return why;
    }
    const unsigned long e = ERR_peek_last_error();
    const char*         reason = e != 0 ? ERR_reason_error_string(e) : nullptr;
    if (reason != nullptr) why += std::string(" (") + reason + ")";
    return why;
}

// TLS 1.2 or later over the connected socket: SNI, the system's CA store, the relay's name
// (or address) checked against its certificate. HTTP/1.1 by ALPN, which the upgrade needs.
bool StartTls(WebSocketObject* o, const std::string& host, Clock::time_point deadline, std::string& error) {
    error = "cannot reach the relay: OpenSSL could not start";
    o->ctx = SSL_CTX_new(TLS_client_method());
    if (o->ctx == nullptr) return false;
    SSL_CTX_set_min_proto_version(o->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(o->ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_mode(o->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
#if defined(SSL_OP_IGNORE_UNEXPECTED_EOF)
    SSL_CTX_set_options(o->ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);   // a TCP end without close_notify is an end
#endif
    if (SSL_CTX_set_default_verify_paths(o->ctx) != 1) {
        error = "cannot reach the relay: the system's certificate store could not be read";
        return false;
    }
    static const unsigned char kAlpn[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    if (SSL_CTX_set_alpn_protos(o->ctx, kAlpn, sizeof(kAlpn)) != 0) return false;   // 0 is success here

    o->ssl = SSL_new(o->ctx);
    if (o->ssl == nullptr || SocketBio() == nullptr) return false;
    BIO* bio = BIO_new(SocketBio());
    if (bio == nullptr) return false;
    BIO_set_data(bio, reinterpret_cast<void*>(static_cast<intptr_t>(o->fd)));
    BIO_set_init(bio, 1);
    SSL_set_bio(o->ssl, bio, bio);   // the SSL owns it now
    if (IsIpLiteral(host)) {
        // No SNI for an address (RFC 6066 §3); the certificate must name the address itself.
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(o->ssl), host.c_str()) != 1) return false;
    } else {
        if (SSL_set_tlsext_host_name(o->ssl, host.c_str()) != 1) return false;
        if (SSL_set1_host(o->ssl, host.c_str()) != 1) return false;
    }

    for (;;) {
        int rc;
        int err = SSL_ERROR_NONE;
        {
            std::lock_guard<std::mutex> lock(o->ioMu);
            ERR_clear_error();
            rc = SSL_connect(o->ssl);
            if (rc != 1) err = SSL_get_error(o->ssl, rc);
            if (rc != 1 && err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) error = TlsFailure(o->ssl);
        }
        if (rc == 1) break;
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            o->broken.store(true);
            return false;
        }
        const Wake w = WaitFor(o, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline, false);
        if (w == Wake::Timeout) {
            error = kTimedOut;
            return false;
        }
        if (w != Wake::Ready) {
            error = Unreachable(errno);
            return false;
        }
    }
    error.clear();
    return true;
}

// RFC 6455 §4.1: the request with a fresh random key, then the 101 answer checked.
bool Upgrade(WebSocketObject* o, const WSF::Url& where, Clock::time_point deadline, std::string& error) {
    uint8_t nonce[16];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
        error = "cannot reach the relay: no random bytes for the WebSocket key";
        return false;
    }
    const std::string key = WSF::Base64(nonce, sizeof(nonce));
    const std::string request = WSF::UpgradeRequest(where, key);
    bool              started = false;
    const Got         sent = WriteAll(o, request.data(), request.size(), deadline, false, started);
    if (sent != Got::Done) {
        error = sent == Got::Timeout ? kTimedOut : "cannot reach the relay: the line failed during the WebSocket upgrade";
        return false;
    }
    std::string head;
    size_t      end = std::string::npos;
    while ((end = WSF::HeadEnd(head)) == std::string::npos) {
        if (head.size() > kMaxHead) {
            error = "the relay's answer to the WebSocket upgrade is too long";
            return false;
        }
        char      buf[4096];
        size_t    got = 0;
        const Got r = ReadSome(o, buf, sizeof(buf), got, deadline);
        if (r != Got::Done) {
            error = r == Got::Timeout ? kTimedOut : "the relay closed the line during the WebSocket upgrade";
            return false;
        }
        head.append(buf, got);
    }
    if (!WSF::CheckUpgradeResponse(head.substr(0, end), key, error)) return false;
    // Frames the relay sent straight after its answer belong to the reader.
    if (end < head.size()) o->reader.Append(head.data() + end, head.size() - end);
    return true;
}

WebSocketRecv Receive(WebSocketObject* o, std::string& out, size_t maxBytes) {
    char buf[kReadChunk];
    for (;;) {
        if (o->cancelled.load()) return WebSocketRecv::Closed;
        std::string payload;
        switch (o->reader.Next(payload, maxBytes)) {
        case WSF::Reader::Event::Text:
            out.swap(payload);
            return WebSocketRecv::Text;
        case WSF::Reader::Event::Ping:
            SendFrame(o, WSF::kPong, payload.data(), payload.size(), kSendTimeoutMs, false);
            continue;
        case WSF::Reader::Event::Close: {
            // Answered with the relay's own code (§5.5.1); the relay then ends the line.
            const std::string code = payload.size() >= 2 ? payload.substr(0, 2) : std::string();
            SendFrame(o, WSF::kClose, code.data(), code.size(), kSendTimeoutMs, false);
            return WebSocketRecv::Closed;
        }
        case WSF::Reader::Event::Error: {
            // Too long (1009) or a protocol violation (1002): say so, then give up the line.
            const std::string code = WSF::ClosePayload(o->reader.ErrorCode());
            SendFrame(o, WSF::kClose, code.data(), code.size(), kSendTimeoutMs, false);
            return WebSocketRecv::Error;
        }
        case WSF::Reader::Event::NeedMore:
            break;
        }
        size_t    got = 0;
        const Got r = ReadSome(o, buf, sizeof(buf), got, Clock::time_point::max());
        if (r == Got::Done) {
            o->reader.Append(buf, got);
            continue;
        }
        if (r == Got::Cancelled) return WebSocketRecv::Closed;   // the line is whole: Free says goodbye
        o->broken.store(true);
        if (r == Got::Failed && !o->cancelled.load()) return WebSocketRecv::Error;
        return WebSocketRecv::Closed;   // the line ended without a close frame
    }
}

}  // namespace

WebSocketHandle WebSocketConnect(const char* url, uint32_t timeoutMs, std::string& error) {
    error.clear();
    WSF::Url where;
    if (!WSF::ParseUrl(url != nullptr ? url : "", where, error)) return nullptr;

    addrinfo* addresses = nullptr;
    if (!Resolve(where, timeoutMs, addresses, error)) return nullptr;
    std::unique_ptr<WebSocketObject> o(new WebSocketObject());
    const bool connected = OpenWakePipe(o.get(), error) && ConnectAny(o.get(), addresses, timeoutMs, error);
    freeaddrinfo(addresses);
    if (!connected) return nullptr;
    // TLS and the upgrade share the third timeout (after the name lookup's and the connect's).
    const Clock::time_point deadline = After(timeoutMs);
    if (where.secure && !StartTls(o.get(), where.host, deadline, error)) return nullptr;
    if (!Upgrade(o.get(), where, deadline, error)) return nullptr;
    return o.release();
}

bool WebSocketSendText(WebSocketHandle o, const char* data, size_t size) {
    if (o == nullptr) return false;
    return SendFrame(o, WSF::kText, data, size, kSendTimeoutMs, false);
}

WebSocketRecv WebSocketReceiveText(WebSocketHandle o, std::string& out, size_t maxBytes) {
    out.clear();
    if (o == nullptr) return WebSocketRecv::Closed;
    if (o->ended) return o->endedAs;
    const WebSocketRecv r = Receive(o, out, maxBytes);
    if (r != WebSocketRecv::Text) {
        o->ended = true;
        o->endedAs = r;
    }
    return r;
}

void WebSocketCancel(WebSocketHandle o) {
    if (o == nullptr) return;
    if (o->cancelled.exchange(true)) return;
    const char one = 1;
    while (write(o->wakeWrite, &one, 1) < 0 && errno == EINTR) {
    }
}

void WebSocketFree(WebSocketHandle o) {
    if (o == nullptr) return;
    WebSocketCancel(o);
    // Every thread is done with the handle: a parting close frame and TLS close_notify, brief
    // and best effort, when the line is still whole.
    const std::string code = WSF::ClosePayload(WSF::kCloseNormal);
    SendFrame(o, WSF::kClose, code.data(), code.size(), kCloseTimeoutMs, true);
    if (o->ssl != nullptr && !o->broken.load()) {
        std::lock_guard<std::mutex> lock(o->ioMu);
        ERR_clear_error();
        SSL_shutdown(o->ssl);
    }
    delete o;
}

}  // namespace Platform
