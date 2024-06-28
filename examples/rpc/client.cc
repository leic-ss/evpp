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
    auto self = shared_from_this();

    auto func = [self, message] () {
        auto conn = self->client_.conn();
        conn->Send(message);
    };

    client_.loop()->RunInLoop(func);
    return ;
}

void ConnectionRPC::OnMessage(const evpp::TCPConnPtr& conn, evpp::Buffer* buf)
{
    while (true) {
        uint32_t total = buf->length();
        if (total < sizeof(uint32_t)) {
            return ;
        }
        uint32_t expect_len = buf->PeekInt32();
        if (total < expect_len) {
            return ;
        }
        buf->Skip(sizeof(uint32_t));
        uint32_t seqno = buf->ReadInt32();

        evpp::Slice msg = buf->Next(expect_len - 2*sizeof(uint32_t));

        // _log_info(myLog, "recv: %d %d seq: %d %.*s", total, expect_len, seqno, msg.size(), msg.data());

        auto iter = ctxs.find(seqno);
        if (iter != ctxs.end()) {
            ContextXPtr ctx = iter->second;
            ctx->rspbuf = std::make_shared<evpp::Buffer>();
            ctx->rspbuf->Append(msg);

            ctx->timer->Cancel();
            ctxs.erase(iter);

            if(ctx->cb) ctx->cb(ctx);
        } else {
            // warn already timeout
        }
    }

    return ;
}

void ConnectionRPC::Request(const evpp::Slice message, ContextX::Callback cb, double timeout_ms)
{
    auto self = shared_from_this();

    uint32_t seqno = sequence.fetch_add(1);

    auto reqbuf = std::make_shared<evpp::Buffer>();
    reqbuf->Append(message);
    uint32_t total_len = reqbuf->total();

    reqbuf->PrependInt32(seqno);
    reqbuf->PrependInt32(total_len);
    reqbuf->ResetData();

    ContextXPtr ctx = std::make_shared<ContextX>();
    ctx->reqbuf = reqbuf;
    ctx->seqno = seqno;
    ctx->cb = cb;

    auto timeout_handler = [self, this, seqno] () {

        auto iter = ctxs.find(seqno);
        if (iter != ctxs.end()) {
            ContextXPtr ctx = iter->second;
            ctx->status = ContextX::Status::TIMEOUT;

            ctxs.erase(iter);

            if (ctx->cb) ctx->cb(ctx);

            _log_info(myLog, "timeout_handler! seqno: %d", seqno);
        } else {
            // error
        }
    };

    auto func = [self, this, ctx] () {

        ctxs.emplace(ctx->seqno, ctx);

        auto conn = client_.conn();
        if (conn) {
            conn->Send(ctx->reqbuf->begin(), ctx->reqbuf->total());
        } else {
            _log_info(myLog, "conn is null");
        }
    };

    client_.loop()->RunInLoop(func);

    ctx->timer = client_.loop()->RunAfter(timeout_ms, timeout_handler);

    return ;
}

void ConnectionRPC::OnConnection(const evpp::TCPConnPtr& conn)
{
    _log_info(myLog, "invoke %lu status: %d",
              (uint64_t)client_.connecting_timeout().Microseconds(), conn->status());
    awaiter.invoke();
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