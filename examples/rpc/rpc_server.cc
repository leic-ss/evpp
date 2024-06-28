#include <evpp/tcp_server.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>
#include "evpp/logger.h"

void OnConnection(const evpp::TCPConnPtr& conn)
{
    if (conn->IsConnected()) {
        conn->SetTCPNoDelay(true);
    }
}

evpp::logger* my_logger = nullptr;

void OnMessage(const evpp::TCPConnPtr& conn, evpp::Buffer* reqbuf)
{
    while (true) {
        uint32_t total = reqbuf->length();
        if (total < sizeof(uint32_t)) {
            return ;
        }
        uint32_t expect_len = reqbuf->PeekInt32();
        if (total < expect_len) {
            return ;
        }
        reqbuf->Skip(sizeof(uint32_t));
        uint32_t seqno = reqbuf->ReadInt32();

        evpp::Slice msg = reqbuf->Next(expect_len - 2*sizeof(uint32_t));

        _log_info(my_logger, "recv: %d %d seq: %d %.*s", total, expect_len, seqno, msg.size(), msg.data());
        // _log_info(my_logger, "recv: %d %d seq: %d", total, expect_len, seqno);

        evpp::BufferPtr rspbuf = std::make_shared<evpp::Buffer>();

        rspbuf->Append(msg);
        uint32_t total_size = rspbuf->total();

        rspbuf->PrependInt32(seqno);
        rspbuf->PrependInt32(total_size);

        conn->SendTotal(rspbuf);
    }
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

    my_logger = evpp::CCLogger::instance();
    my_logger->setLogLevel("TRAC");
    evpp::EventLoop loop;
    loop.SetLogger(my_logger);

    evpp::TCPServer server(&loop, addr, "RPCServer", thread_num);
    server.SetMessageCallback(&OnMessage);
    server.SetConnectionCallback(&OnConnection);
    server.SetLogger(my_logger);
    server.Init();
    server.Start();
    loop.Run();
    return 0;
}

#ifdef WIN32
#include "../winmain-inl.h"
#endif
