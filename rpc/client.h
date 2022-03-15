#pragma once

#include <evpp/tcp_client.h>
#include <evpp/event_loop_thread.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>

#include <unordered_map>

namespace rpc2
{

class Client;
class ClientCtx;
using ClientPtr = std::shared_ptr<Client>;
using ClientCtxPtr = std::shared_ptr<ClientCtx>;

class ContextX;
using ContextXPtr = std::shared_ptr<ContextX>;

class ContextX : public std::enable_shared_from_this<ContextX>
{
public:
    using cb = std::function<void (ContextXPtr)>;

public:
    ContextX() : status(Status::OK) {}
    virtual ~ContextX(void) {}

public:
    template <class T> inline
    std::shared_ptr<T> convert(void) {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

public:
    enum class Status : uint8_t {
        OK = 0,
        TIMEOUT,
        FAILED,
        CLOSED
    };

public:
    uint64_t reqMsec{0};
    uint64_t resMsec{0};
    uint32_t seq;
    Status status{Status::OK};
    std::string err;
};

class ClientCtx : public ContextX
{
public:
    uint32_t pcode;
    std::string request;
    std::string response;
};

class Client : public std::enable_shared_from_this<Client>
{
public:
	class Async : public std::enable_shared_from_this<Async>
    {
	    public:
	        using ptr = std::shared_ptr<Async>;
	    public:
	        std::string err;

	    private:
	        // client
	        ClientPtr c;
	        // clisnt context
	        ContextXPtr ctx;
	        // client context callback
	        ContextX::cb cb;
	        // timer
	        evpp::InvokeTimerPtr timer;

	    private:
	        friend Client;

	    public:
	        Async(ClientPtr, ContextXPtr, ContextX::cb);
	        ~Async(void);

	    public:
	        void invoke(void);

	    public:
	        template <class T> inline
	        std::shared_ptr<T> convert(void) {
	            return std::dynamic_pointer_cast<T>(ctx);
	        }
	};

public:
	Client(const std::string& addr) : addr_(addr) { }
	~Client();

	bool Init();
	void Request(ContextXPtr ctx, ContextX::cb cb, uint32_t timeout_ms);

protected:
    // connection status
    enum Status {
        CLOSED,
        CONNECTED,
        CONNECTING,
    };

private:
	template <class T> inline
    std::shared_ptr<T> convert(void) {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

private:
	void ConnectCallBack(const evpp::TCPConnPtr& conn);
	void MessageCallBack(const evpp::TCPConnPtr& conn, evpp::Buffer* msg);

	Client::Async::ptr erase(uint32_t seq);

private:
	std::string addr_;

	std::unordered_map<uint32_t, Async::ptr> ctxs;

	evpp::EventLoopThread* event_loop_thread_{nullptr};
	std::shared_ptr<evpp::TCPClient> tcp_client_ptr_{nullptr};
};

}