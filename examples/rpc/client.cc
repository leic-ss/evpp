#include "rpc/client.h"

int main(int argc, char* argv[]) {
    std::string addr = "127.0.0.1:9099";

    if (argc == 2) {
        addr = argv[1];
    }

    rpc2::ClientPtr client = std::make_shared<rpc2::Client>(addr);
    if (!client->Init()) {
    	fprintf(stderr, "init failed!\n");
    	return -1;
    }

    sleep(1);

    rpc2::ContextXPtr ctx = std::make_shared<rpc2::ClientCtx>();
    ctx->convert<rpc2::ClientCtx>()->request = "test message!";

    client->Request(ctx, [] (rpc2::ContextXPtr) {

    }, 200);

    sleep(1);

    client.reset();
    return 0;
}