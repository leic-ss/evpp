#include "client.h"

namespace rpc2
{

Client::Async::Async(ClientPtr c_, ContextXPtr ctx_, ContextX::cb cb_)
: c      (c_)
, ctx    (ctx_)
, cb     (cb_)
{
}

Client::Async::~Async(void) {

}

void Client::Async::invoke(void)
{
	if(timer) timer->Cancel();

    evpp::EventLoop* loop = c->tcp_client_ptr_->loop();
    auto self = shared_from_this();
    loop->RunInLoop([self] () {
    	self->cb(self->ctx);
    });
}

Client::Async::ptr Client::erase(uint32_t seq)
{
    Client::Async::ptr ctx = nullptr;
    // find
    auto it = ctxs.find(seq);
    // find out
    if (it != ctxs.end()) {
        // assign
        ctx = it->second;
        // erase
        ctxs.erase(it);
    }
    // return
    return ctx;
}

Client::~Client()
{
	if (event_loop_thread_) {
        event_loop_thread_->Stop();

        delete event_loop_thread_;
        event_loop_thread_ = nullptr;
    }
}

bool Client::Init()
{
	if (event_loop_thread_) return false;

    event_loop_thread_ = new evpp::EventLoopThread();
    if (!event_loop_thread_->Start()) return false;

	tcp_client_ptr_ = std::make_shared<evpp::TCPClient>(event_loop_thread_->loop(), addr_, "rpc_c");
	if (!tcp_client_ptr_) {
		return false;
	}

	tcp_client_ptr_->SetMessageCallback(std::bind(&Client::MessageCallBack, this, std::placeholders::_1, std::placeholders::_2));
	tcp_client_ptr_->SetConnectionCallback(std::bind(&Client::ConnectCallBack, this, std::placeholders::_1));
	tcp_client_ptr_->Connect();
	return true;
}

void Client::ConnectCallBack(const evpp::TCPConnPtr& conn)
{
	if (conn->IsConnected()) {
		LOG_INFO << "Connected to " << conn->remote_addr();
	} else {
		LOG_INFO << "Connect Failed to " << conn->remote_addr();
	}

	return ;
}

void Client::MessageCallBack(const evpp::TCPConnPtr& conn, evpp::Buffer* msg)
{
	LOG_TRACE << "Receive a message [" << msg->ToString() << "]";

	// find async and invoke

	msg->Reset();
}

void Client::Request(ContextXPtr ctx, ContextX::cb cb, uint32_t timeout_ms)
{
	if (!tcp_client_ptr_) return ;

    auto self(convert<Client>());

    // if (ctx->reqMsec == 0) {
    //     ctx->reqMsec = TimeHelper::currentMs();
    // }

    auto async(std::make_shared<Async>(self, ctx, cb));

    tcp_client_ptr_->loop()->RunInLoop([this, self, ctx, async, timeout_ms] () {
    	ctxs.emplace(ctx->seq, async);

    	// timer

    	auto conn = tcp_client_ptr_->conn();
    	if (conn) conn->Send(ctx->convert<ClientCtx>()->request);
    });
}

}