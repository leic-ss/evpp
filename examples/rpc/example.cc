#include "client.h"

int main(int argc, char* argv[]) {
    std::string addr = "127.0.0.1:9099";

    if (argc == 2) {
        addr = argv[1];
    }

    evpp::logger* my_logger = evpp::CCLogger::instance();
    my_logger->setLogLevel("trac");

    evpp::EventLoop loop;

    rpc::ClientRPCPtr client = std::make_shared<rpc::ClientRPC>(2, my_logger);
    if (!client->Initial()) {
    	fprintf(stderr, "init failed!\n");
    	return -1;
    }

    rpc::ConnectionRPCPtr conn = client->Connect(addr);
    conn->Wait();

    for (int i = 0; i < 100; i++) {
        conn->Write("hello, client!");
    }

    loop.Run();
    return 0;
}