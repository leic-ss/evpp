#pragma once

#include <evpp/tcp_client.h>
#include <evpp/event_loop_thread.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>

#include <unordered_map>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <evpp/event_loop_thread_pool.h>
#include "evpp/logger.h"

namespace rpc
{

class ConnectionRPC;
class ClientRPC;
class ContextX;

using ConnectionRPCPtr = std::shared_ptr<ConnectionRPC>;
using ClientRPCPtr = std::shared_ptr<ClientRPC>;
using ContextXPtr = std::shared_ptr<ContextX>;

class AWaiter {
private:
    enum class WStatus {
        idle    = 0x0,
        ready   = 0x1,
        waiting = 0x2,
        done    = 0x3
    };

public:
    AWaiter() : status(WStatus::idle) {}

    void reset() { status.store(WStatus::idle); }

    void wait() { waitUsec(0); }

    void waitMsec(size_t time_ms) { waitUsec(time_ms * 1000); }

    void waitUsec(size_t time_us);

    void invoke();

private:
    std::atomic<WStatus> status;
    std::mutex cvLock;
    std::condition_variable cv;
};

class ContextX : public std::enable_shared_from_this<ContextX>
{
public:
    using Callback = std::function<void(ContextXPtr)>;

public:
    ContextX() : status(Status::OK) {}
    virtual ~ContextX(void) {}

public:
    enum class Status : uint8_t {
        OK = 0,
        TIMEOUT,
        FAILED,
        CLOSED
    };

public:
    uint32_t                seqno{0};
    evpp::BufferPtr         reqbuf{nullptr};
    evpp::BufferPtr         rspbuf{nullptr};

    Callback                cb{nullptr};
    Status                  status{Status::OK};
    evpp::InvokeTimerPtr    timer;
};

class ConnectionRPC : public std::enable_shared_from_this<ConnectionRPC>
{
public:
    ConnectionRPC(evpp::EventLoop* loop, const std::string& serverAddr,
                  const std::string& name)
                 : client_(loop, serverAddr, name)
    {
        client_.SetConnectionCallback(
                std::bind(&ConnectionRPC::OnConnection, this, std::placeholders::_1));
        client_.SetMessageCallback(
                std::bind(&ConnectionRPC::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    void SetLogger(evpp::logger* log_) { 
        myLog = log_;
        client_.SetLogger(log_);
    }

    void Start();

    void Wait();

    void Stop();

    void Write(const std::string& message);

    void Request(const evpp::Slice message, ContextX::Callback, double timeout_ms);

protected:
    void OnConnection(const evpp::TCPConnPtr& conn);

    void OnMessage(const evpp::TCPConnPtr& conn, evpp::Buffer* buf);

private:
    evpp::TCPClient client_;

    std::atomic<uint32_t> sequence{0};
    std::unordered_map<uint32_t, ContextXPtr> ctxs;

    evpp::logger* myLog{nullptr};
    AWaiter awaiter;
};

class ClientRPC
{
public:
    ClientRPC(int threadCount_, evpp::logger* log_)
             : thread_count_(threadCount_)
             , myLog(log_)
    { }

    ~ClientRPC() {
    }

    bool Initial();

    ConnectionRPCPtr Connect(std::string remote_addr_);

private:
    void Quit();

    void HandleTimeout();

private:
    int thread_count_;
    std::shared_ptr<evpp::EventLoopThreadPool> tpool_;
    evpp::logger* myLog{nullptr};
};

}