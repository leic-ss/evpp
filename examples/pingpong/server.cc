#include <evpp/tcp_server.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>
#include "evpp/logger.h"

void OnConnection(const evpp::TCPConnPtr& conn) {
    if (conn->IsConnected()) {
        conn->SetTCPNoDelay(true);
    }
}

void OnMessage(const evpp::TCPConnPtr& conn,
               evpp::Buffer* msg) {
    conn->Send(msg);
}

int main(int argc, char* argv[]) {
    std::string addr = "0.0.0.0:9099";
    int thread_num = 4;

    if (argc != 1 && argc != 3) {
        printf("Usage: %s <port> <thread-num>\n", argv[0]);
        printf("  e.g: %s 9099 12\n", argv[0]);
        return 0;
    }

    if (argc == 3) {
        addr = std::string("0.0.0.0:") + argv[1];
        thread_num = atoi(argv[2]);
    }

    evpp::logger* log = evpp::CCLogger::instance();
    log->setLogLevel("TRAC");
    evpp::EventLoop loop;
    loop.SetLogger(log);

    evpp::TCPServer server(&loop, addr, "TCPPingPongServer", thread_num);
    server.SetMessageCallback(&OnMessage);
    server.SetConnectionCallback(&OnConnection);
    server.SetLogger(log);
    server.Init();
    server.Start();
    loop.Run();
    return 0;
}

#ifdef WIN32
#include "../winmain-inl.h"
#endif
