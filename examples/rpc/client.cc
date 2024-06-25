#include "client.h"

namespace rpc
{

void AWaiter::waitUsec(size_t time_us)
{
    WStatus expected = WStatus::idle;
    if (status.compare_exchange_strong(expected, WStatus::ready)) {
        // invoke() has not been invoked yet, wait for it.
        std::unique_lock<std::mutex> lk(cvLock);
        expected = WStatus::ready;
        if (status.compare_exchange_strong(expected, WStatus::waiting)) {
            if (time_us) {
                cv.wait_for(lk, std::chrono::microseconds(time_us));
            } else {
                cv.wait(lk);
            }
            status.store(WStatus::done);
        } else {
            // invoke() has grabbed `cvLock` earlier than this.
        }
    } else {
        // invoke() already has been called earlier than this.
    }
}

void AWaiter::invoke() 
{
    WStatus expected = WStatus::idle;
    if (status.compare_exchange_strong(expected, WStatus::done)) {
        // wait() has not been invoked yet, do nothing.
        return;
    }

    std::unique_lock<std::mutex> lk(cvLock);
    expected = WStatus::ready;
    if (status.compare_exchange_strong(expected, WStatus::done)) {
        // wait() has been called earlier than invoke(),
        // but invoke() has grabbed `cvLock` earlier than wait().
        // Do nothing.
    } else {
        // wait() is waiting for ack.
        cv.notify_all();
    }
}

void ConnectionRPC::Start()
{
    client_.Connect();
}

void ConnectionRPC::Wait()
{
    _log_info(myLog, "start wait %lu", (uint64_t)client_.connecting_timeout().Microseconds());

    awaiter.waitUsec( (uint64_t)client_.connecting_timeout().Microseconds() );

    _log_info(myLog, "end wait %lu", (uint64_t)client_.connecting_timeout().Microseconds());
}

void ConnectionRPC::Stop()
{
    client_.Disconnect();
}

void ConnectionRPC::Write(const std::string& message)
{
    evpp::TCPConnPtr conn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn = connection_;
    }

    if (conn) {
        conn->Send(message);
    }

    return ;
}

void ConnectionRPC::OnConnection(const evpp::TCPConnPtr& conn)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (conn->IsConnected()) {
        connection_ = conn;
    } else {
        connection_.reset();
    }

    _log_info(myLog, "invoke %lu status: %d",
              (uint64_t)client_.connecting_timeout().Microseconds(), conn->status());
    awaiter.invoke();
}

void ConnectionRPC::OnMessage(const evpp::TCPConnPtr& conn, evpp::Buffer* buf)
{
    _log_info(myLog, "recv: %s", buf->ToString().c_str());
}

bool ClientRPC::Initial() 
{
    auto evtp = new evpp::EventLoopThreadPool(nullptr, thread_count_);
    evtp->SetLogger(myLog);
    tpool_.reset(evtp);

    bool ret = tpool_->Start(true);
    return ret;
}

ConnectionRPCPtr ClientRPC::Connect(std::string remote_addr_)
{
    ConnectionRPCPtr conn = std::make_shared<ConnectionRPC>(tpool_->GetNextLoop(), remote_addr_, "c1");
    conn->SetLogger(myLog);
    conn->Start();

    return conn;
}

void ClientRPC::Quit()
{
    tpool_->Stop();

    while (!tpool_->IsStopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    tpool_.reset();
}

void ClientRPC::HandleTimeout()
{

}

}