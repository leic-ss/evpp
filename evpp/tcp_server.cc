#include "evpp/inner_pre.h"

#include "evpp/tcp_server.h"
#include "evpp/listener.h"
#include "evpp/tcp_conn.h"
#include "evpp/libevent.h"

#include <condition_variable>
#include <mutex>

namespace evpp {
TCPServer::TCPServer(EventLoop* loop,
                     const std::string& laddr,
                     const std::string& name,
                     uint32_t thread_num)
    : loop_(loop)
    , listen_addr_(laddr)
    , name_(name)
    , conn_fn_(&internal::DefaultConnectionCallback)
    , msg_fn_(&internal::DefaultMessageCallback)
    , next_conn_id_(0) {
    _log_trace(myLog, "name=%s listening addr %s thread_num %d", name.c_str(), laddr.c_str(), thread_num);
    tpool_.reset(new EventLoopThreadPool(loop_, thread_num));
}

TCPServer::~TCPServer() {
    // DLOG_TRACE;
    assert(connections_.empty());
    assert(!listener_);
    if (tpool_) {
        assert(tpool_->IsStopped());
        tpool_.reset();
    }
}

bool TCPServer::Init() {
    // DLOG_TRACE;
    assert(status_ == kNull);
    listener_.reset(new Listener(loop_, listen_addr_));
    listener_->Listen();
    status_.store(kInitialized);
    return true;
}

void TCPServer::AfterFork() {
    tpool_->AfterFork();
}

bool TCPServer::Start() {
    // DLOG_TRACE;
    assert(status_ == kInitialized);
    status_.store(kStarting);
    assert(listener_.get());
    bool rc = tpool_->Start(true);
    if (rc) {
        assert(tpool_->IsRunning());
        listener_->SetNewConnectionCallback(
            std::bind(&TCPServer::HandleNewConn,
                      this,
                      std::placeholders::_1,
                      std::placeholders::_2,
                      std::placeholders::_3));

        // We must set status_ to kRunning firstly and then we can accept new
        // connections. If we use the following code :
        //     listener_->Accept();
        //     status_.store(kRunning);
        // there is a chance : we have accepted a connection but status_ is not
        // kRunning that will cause the assert(status_ == kRuning) failed in
        // TCPServer::HandleNewConn.
        status_.store(kRunning);
        listener_->Accept();
    }
    return rc;
}

// void TCPServer::Stop(DoneCallback on_stopped_cb) {
//     _log_trace(myLog, "Entering ...");
//     assert(status_ == kRunning);
//     status_.store(kStopping);
//     substatus_.store(kStoppingListener);
//     loop_->RunInLoop(std::bind(&TCPServer::StopInLoop, this, on_stopped_cb));
//     _log_trace(myLog, "Leaving ...");
// }

void TCPServer::Stop(bool wait_thread_exited, DoneCallback on_stopped_cb)
{
    assert(status_ == kRunning);
    status_.store(kStopping);
    substatus_.store(kStoppingListener);

    if (!wait_thread_exited) {
        loop_->RunInLoop(std::bind(&TCPServer::StopInLoop, this, on_stopped_cb));
    } else {
        std::mutex mtx;
        std::condition_variable cond;
        // lock
        std::unique_lock<std::mutex> lock(mtx);

        auto func = [&cond, &mtx, this, on_stopped_cb] () {

            StopInLoop(on_stopped_cb);
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

    return ;
}

void TCPServer::StopInLoop(DoneCallback on_stopped_cb) {
    _log_trace(myLog, "Entering ...");
    assert(loop_->IsInLoopThread());
    listener_->Stop();
    listener_.reset();

    if (connections_.empty()) {
        // Stop all the working threads now.
        _log_trace(myLog, "no connections");
        StopThreadPool();
        if (on_stopped_cb) {
            on_stopped_cb();
            on_stopped_cb = DoneCallback();
        }
        status_.store(kStopped);
    } else {
        _log_trace(myLog, "close connections");
        for (auto& c : connections_) {
            if (c.second->IsConnected()) {
                _log_trace(myLog, "close connection id=%d fd=%d", c.second->id(), c.second->fd());
                c.second->Close();
            } else {
                _log_trace(myLog, "Do not need to call Close for this TCPConn it may be doing disconnecting."
                           " fd=%d status=%s", c.second->fd(), StatusToString().c_str());
            }
        }

        stopped_cb_ = on_stopped_cb;

        // The working threads will be stopped after all the connections closed.
    }

    _log_trace(myLog, "exited, status=%s", StatusToString().c_str());
}

void TCPServer::StopThreadPool() {
    assert(loop_->IsInLoopThread());
    assert(IsStopping());
    substatus_.store(kStoppingThreadPool);
    tpool_->Stop(true);
    assert(tpool_->IsStopped());

    // Make sure all the working threads totally stopped.
    tpool_->Join();
    tpool_.reset();

    substatus_.store(kSubStatusNull);
}

void TCPServer::HandleNewConn(evpp_socket_t sockfd,
                              const std::string& remote_addr/*ip:port*/,
                              const struct sockaddr_in* raddr) {
    _log_trace(myLog, "fd=%d", sockfd);
    assert(loop_->IsInLoopThread());
    if (IsStopping()) {
        _log_warn(myLog, "The server is at stopping status. Discard this socket fd=%d remote_addr=%s",
                  sockfd, remote_addr.c_str());
        EVUTIL_CLOSESOCKET(sockfd);
        return;
    }

    assert(IsRunning());
    EventLoop* io_loop = GetNextLoop(raddr);
#ifdef H_DEBUG_MODE
    std::string n = name_ + "-" + remote_addr + "#" + std::to_string(next_conn_id_);
#else
    std::string n = remote_addr;
#endif
    ++next_conn_id_;
    TCPConnPtr conn(new TCPConn(io_loop, n, sockfd, listen_addr_, remote_addr, next_conn_id_));
    assert(conn->type() == TCPConn::kIncoming);
    conn->SetMessageCallback(msg_fn_);
    conn->SetConnectionCallback(conn_fn_);
    conn->SetCloseCallback(std::bind(&TCPServer::RemoveConnection, this, std::placeholders::_1));
    io_loop->RunInLoop(std::bind(&TCPConn::OnAttachedToLoop, conn));
    connections_[conn->id()] = conn;
}

EventLoop* TCPServer::GetNextLoop(const struct sockaddr_in* raddr) {
    if (IsRoundRobin()) {
        return tpool_->GetNextLoop();
    } else {
        return tpool_->GetNextLoopWithHash(raddr->sin_addr.s_addr);
    }
}

void TCPServer::RemoveConnection(const TCPConnPtr& conn) {
    _log_trace(myLog, "fd=%d connections_.size()=%d", conn->fd(), connections_.size());
    auto f = [this, conn]() {
        // Remove the connection in the listening EventLoop
        _log_trace(myLog, "fd=%d connections_.size()=%d", conn->fd(), connections_.size());
        assert(this->loop_->IsInLoopThread());
        this->connections_.erase(conn->id());
        if (IsStopping() && this->connections_.empty()) {
            // At last, we stop all the working threads
            _log_trace(myLog, "Stop thread pool");
            assert(substatus_.load() == kStoppingListener);
            StopThreadPool();
            if (stopped_cb_) {
                stopped_cb_();
                stopped_cb_ = DoneCallback();
            }
            status_.store(kStopped);
        }
    };
    loop_->RunInLoop(f);
}

}
