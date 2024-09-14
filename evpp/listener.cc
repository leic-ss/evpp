#include "evpp/inner_pre.h"

#include "evpp/listener.h"
#include "evpp/event_loop.h"
#include "evpp/fd_channel.h"
#include "evpp/libevent.h"
#include "evpp/sockets.h"

namespace evpp {
Listener::Listener(EventLoop* l, const std::string& addr)
    : loop_(l), addr_(addr) {
}

Listener::~Listener() {
    chan_.reset();
    EVUTIL_CLOSESOCKET(fd_);
    fd_ = INVALID_SOCKET;
}

void Listener::Listen(int backlog) {
    fd_ = sock::CreateNonblockingSocket();
    if (fd_ < 0) {
        int serrno = errno;
        _log_err(myLog, "Create a nonblocking socket failed, err: %s", strerror(serrno).c_str());
        return;
    }

    struct sockaddr_storage addr = sock::ParseFromIPPort(addr_.data());
    // TODO Add retry when failed
    int ret = ::bind(fd_, sock::sockaddr_cast(&addr), static_cast<socklen_t>(sizeof(struct sockaddr)));
    if (ret < 0) {
        int serrno = errno;
        _log_err(myLog, "bind error: %s addr: %s", strerror(serrno).c_str(), addr_.c_str());
    }

    ret = ::listen(fd_, backlog);
    if (ret < 0) {
        int serrno = errno;
        _log_err(myLog, "Listen failed, err: %s", strerror(serrno).c_str());
    }
}

void Listener::Accept() {
    // DLOG_TRACE;
    chan_.reset(new FdChannel(loop_, fd_, true, false));
    chan_->SetReadCallback(std::bind(&Listener::HandleAccept, this));
    loop_->RunInLoop(std::bind(&FdChannel::AttachToLoop, chan_.get()));
    _log_info(myLog, "TCPServer is running at %s", addr_.c_str());
}

void Listener::HandleAccept() {
    _log_info(myLog, "A new connection is comming in");
    assert(loop_->IsInLoopThread());
    struct sockaddr_storage ss;
    socklen_t addrlen = sizeof(ss);
    int nfd = -1;
    if ((nfd = ::accept(fd_, sock::sockaddr_cast(&ss), &addrlen)) == -1) {
        int serrno = errno;
        if (serrno != EAGAIN && serrno != EINTR) {
            _log_warn(myLog, "bad accept %s", strerror(serrno).c_str());
        }
        return;
    }

    if (evutil_make_socket_nonblocking(nfd) < 0) {
        _log_err(myLog, "set fd=%d nonblocking failed.", nfd);
        EVUTIL_CLOSESOCKET(nfd);
        return;
    }

    sock::SetKeepAlive(nfd, true);

    std::string raddr = sock::ToIPPort(&ss);
    if (raddr.empty()) {
        _log_err(myLog, "sock::ToIPPort(&ss) failed.");
        EVUTIL_CLOSESOCKET(nfd);
        return;
    }

    _log_trace(myLog, "accepted a connection from %s , listen fd=%d , client fd=%d", raddr.c_str(), fd_, nfd);

    if (new_conn_fn_) {
        new_conn_fn_(nfd, raddr, sock::sockaddr_in_cast(&ss));
    }
}

void Listener::Stop() {
    assert(loop_->IsInLoopThread());
    chan_->DisableAllEvent();
    chan_->Close();
}
}
