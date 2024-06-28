#include "client.h"

int main(int argc, char* argv[]) {
    std::string addr = "127.0.0.1:9099";

    if (argc == 2) {
        addr = argv[1];
    }

    evpp::logger* my_logger = evpp::CCLogger::instance();
    my_logger->setLogLevel("trac");

    evpp::EventLoop loop;

    rpc::ClientRPCPtr client = std::make_shared<rpc::ClientRPC>(3, my_logger);
    if (!client->Initial()) {
    	fprintf(stderr, "init failed!\n");
    	return -1;
    }

    rpc::ConnectionRPCPtr conn = client->Connect(addr);
    conn->Wait();

    auto func = [my_logger] (rpc::ContextXPtr ctx) {
        auto buf = ctx->rspbuf;

        _log_info(my_logger, "seqno: %d status: %d %s", ctx->seqno, ctx->status, buf->ToString().c_str());
    };

    for (int i = 0; i < 100; i++) {
        conn->Request("hello, client!", func, 100.0);
    }

    loop.Run();
    return 0;
}