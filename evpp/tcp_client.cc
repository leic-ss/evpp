#include <atomic>

#include "evpp/inner_pre.h"

#include "evpp/tcp_client.h"
#include "evpp/libevent.h"
#include "evpp/tcp_conn.h"
#include "evpp/fd_channel.h"
#include "evpp/connector.h"

#include <condition_variable>
#include <mutex>

namespace evpp {
std::atomic<uint64_t> id;
TCPClient::TCPClient(EventLoop* l, const std::string& raddr, const std::string& n)
    : loop_(l)
    , remote_addr_(raddr)
    , name_(n)
    , conn_fn_(&internal::DefaultConnectionCallback)
    , msg_fn_(&internal::DefaultMessageCallback) {
    _log_trace(myLog, "remote addr = %s", raddr.c_str());
}

TCPClient::~TCPClient() {
    // DLOG_TRACE;
    assert(!connector_.get());
    auto_reconnect_.store(false);
    TCPConnPtr c = conn();
    if (c) {
        // Most of the cases, the conn_ is at disconnected status at this time.
        // But some times, the user application layer will call TCPClient::Close()
        // and delete TCPClient object immediately, that will make conn_ to be at disconnecting status.
        assert(c->IsDisconnected() || c->IsDisconnecting());
        if (c->IsDisconnecting()) {
            // the reference count includes :
            //  - this
            //  - c
            //  - A disconnecting callback which hold a shared_ptr of TCPConn
            assert(c.use_count() >= 3);
            c->SetCloseCallback(CloseCallback());
        }
    }
    conn_.reset();
}

void TCPClient::Bind(const std::string& addr/*host:port*/) {
    local_addr_ = addr;
}

void TCPClient::Connect() {
    _log_info(myLog, "remote addr = %s", remote_addr().c_str());
    auto f = [this]() {
        assert(loop_->IsInLoopThread());
        connector_.reset(new Connector(loop_, this));
        connector_->SetNewConnectionCallback(std::bind(&TCPClient::OnConnection, this, std::placeholders::_1, std::placeholders::_2));
        connector_->SetLogger(myLog);
        connector_->Start();
    };
    loop_->RunInLoop(f);
}

void TCPClient::Disconnect(bool wait) {
    // DLOG_TRACE;
    if (!wait) {
        loop_->RunInLoop(std::bind(&TCPClient::DisconnectInLoop, this));
    } else {
        std::mutex mtx;
        std::condition_variable cond;
        // lock
        std::unique_lock<std::mutex> lock(mtx);

        auto func = [&cond, &mtx, this] () {

            DisconnectInLoop();
            // lock
            do {
                // make sure cond reach wait
                std::lock_guard<std::mutex> guard(mtx);
            } while (0);
            // notify
            cond.notify_one();
            // done
        };

        loop_->RunInLoop(func);

        cond.wait(lock);        
    }
}

void TCPClient::DisconnectInLoop() {
    _log_info(myLog, "TCPClient::DisconnectInLoop remote addr = %s", remote_addr_.c_str());
    assert(loop_->IsInLoopThread());
    auto_reconnect_.store(false);

    if (conn_) {
        _log_info(myLog, "Close the TCPConn status=%s", conn_->StatusToString().c_str());
        assert(!conn_->IsDisconnected() && !conn_->IsDisconnecting());
        conn_->Close();
    } else {
        // When connector_ is connecting to the remote server ...
        assert(connector_ && !connector_->IsConnected());
    }

    if (connector_->IsConnected() || connector_->IsDisconnected()) {
        _log_trace(myLog, "Nothing to do with connector_, Connector::status=%d", connector_->status());
    } else {
        // When connector_ is trying to connect to the remote server we should cancel it to release the resources.
        connector_->Cancel();
    }

    connector_.reset(); // Free connector_ in loop thread immediately
}

void TCPClient::Reconnect() {
    _log_trace(myLog, "Try to reconnect to %s in %lf s again", remote_addr_.c_str(), reconnect_interval_.Seconds());
    Connect();
}

void TCPClient::SetConnectionCallback(const ConnectionCallback& cb) {
    conn_fn_ = cb;
    auto  c = conn();
    if (c) {
        c->SetConnectionCallback(cb);
    }
}

void TCPClient::OnConnection(evpp_socket_t sockfd, const std::string& laddr) {
    if (sockfd < 0) {
        _log_trace(myLog, "Failed to connect to %s . errno=%d err=%s", remote_addr_.c_str(), errno, strerror(errno).c_str());
        // We need to notify this failure event to the user layer
        // Note: When we could not connect to a server,
        //       the user layer will receive this notification constantly
        //       because the connector_ will retry to do reconnection all the time.
        conn_fn_(TCPConnPtr(new TCPConn(loop_, "", sockfd, laddr, remote_addr_, 0)));
        return;
    }

    _log_warn(myLog, "Successfully connected to %s", remote_addr_.c_str());
    assert(loop_->IsInLoopThread());
    TCPConnPtr c = TCPConnPtr(new TCPConn(loop_, name_, sockfd, laddr, remote_addr_, id++));
    c->set_type(TCPConn::kOutgoing);
    c->SetLogger(myLog);
    c->SetMessageCallback(msg_fn_);
    c->SetConnectionCallback(conn_fn_);
    c->SetCloseCallback(std::bind(&TCPClient::OnRemoveConnection, this, std::placeholders::_1));

    {
        std::lock_guard<std::mutex> guard(mutex_);
        conn_ = c;
    }

    c->OnAttachedToLoop();
}

void TCPClient::OnRemoveConnection(const TCPConnPtr& c) {
    assert(c.get() == conn_.get());
    assert(loop_->IsInLoopThread());
    conn_.reset();
    _log_warn(myLog, "OnRemoveConnection %s, auto_reconnect_: %s",
              remote_addr_.c_str(), auto_reconnect_.load() ? "true" : "false");
    if (auto_reconnect_.load()) {
        Reconnect();
    }
}

TCPConnPtr TCPClient::conn() const {
    if (loop_->IsInLoopThread()) {
        return conn_;
    } else {
        // If it is not in the loop thread, we should add a lock here
        std::lock_guard<std::mutex> guard(mutex_);
        TCPConnPtr c = conn_;
        return c;
    }
}
}
