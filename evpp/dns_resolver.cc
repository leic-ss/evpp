
#include "evpp/dns_resolver.h"
#include "evpp/event_loop.h"
#include "evpp/event_watcher.h"
#include "evpp/libevent.h"

namespace evpp {
DNSResolver::DNSResolver(EventLoop* evloop, const std::string& h, Duration timeout, const Functor& f)
    : loop_(evloop), dnsbase_(nullptr), dns_req_(nullptr), host_(h), timeout_(timeout), functor_(f) {
    _log_trace(myLog, "tid = %ld", std::this_thread::get_id());
}

DNSResolver::~DNSResolver() {
    _log_trace(myLog, "tid = %ld", std::this_thread::get_id());
    assert(dnsbase_ == nullptr);

#if LIBEVENT_VERSION_NUMBER >= 0x02001500
    assert(!timer_);
#endif
}

void DNSResolver::Start() {
    auto f = [this]() {
        _log_trace(myLog, "tid = %ld", std::this_thread::get_id());
        assert(loop_->IsInLoopThread());

#if LIBEVENT_VERSION_NUMBER >= 0x02001500
        AsyncDNSResolve();
#else
        SyncDNSResolve();
#endif
    };
    loop_->RunInLoop(f);
}

void DNSResolver::SyncDNSResolve() {
    /* Build the hints to tell getaddrinfo how to act. */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* v4 or v6 is fine. */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */

    /* Look up the hostname. */
    struct addrinfo* answer = nullptr;
    int err = getaddrinfo(host_.c_str(), nullptr, &hints, &answer);
    if (err != 0) {
        _log_err(myLog, "getaddrinfo failed. rc[%d] err[%s]", err, gai_strerror(err));
    } else {
        for (struct addrinfo* rp = answer; rp != nullptr; rp = rp->ai_next) {
            struct sockaddr_in* a = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);

            if (a->sin_addr.s_addr == 0) {
                continue;
            }

            addrs_.push_back(a->sin_addr);
            _log_trace(myLog, "host = %s resolved a ip[%s]", host_.c_str(), inet_ntoa(a->sin_addr));
        }
    }
    evutil_freeaddrinfo(answer);
    OnResolved();
}

void DNSResolver::Cancel() {
    assert(loop_->IsInLoopThread());
    if (timer_) {
        timer_->Cancel();
        timer_.reset();
    }
    functor_ = Functor(); // Release the callback
}

void DNSResolver::AsyncWait() {
    _log_trace(myLog, "tid = %ld", std::this_thread::get_id());
    timer_.reset(new TimerEventWatcher(loop_, std::bind(&DNSResolver::OnTimeout, this), timeout_));
    timer_->SetCancelCallback(std::bind(&DNSResolver::OnCanceled, this));
    timer_->Init();
    timer_->AsyncWait();
}

void DNSResolver::OnTimeout() {
    _log_trace(myLog, "tid = %ld", std::this_thread::get_id());
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
    evdns_getaddrinfo_cancel(dns_req_);
    dns_req_ = nullptr;
#endif
    ClearTimer();
    OnResolved();
}

void DNSResolver::OnCanceled() {
    _log_trace(myLog, "tid = %ld", std::this_thread::get_id());
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
    evdns_getaddrinfo_cancel(dns_req_);
    dns_req_ = nullptr;
#endif
}


#if LIBEVENT_VERSION_NUMBER >= 0x02001500
void DNSResolver::AsyncDNSResolve() {
    // Set a timer to watch the DNS resolving
    AsyncWait();

    /* Build the hints to tell getaddrinfo how to act. */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* v4 or v6 is fine. */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */

    _log_trace(myLog, "call shared_from_this");
    std::shared_ptr<DNSResolver> p = shared_from_this();
    std::shared_ptr<DNSResolver> *pp = new std::shared_ptr<DNSResolver>(p);
    dnsbase_ = evdns_base_new(loop_->event_base(), 1);
    assert(dnsbase_);
    dns_req_ = evdns_getaddrinfo(dnsbase_
                                 , host_.c_str()
                                 , nullptr /* no service name given */
                                 , &hints
                                 , &DNSResolver::OnResolved
                                 , pp);
}

void DNSResolver::OnResolved(int errcode, struct addrinfo* addr) {
    if (errcode != 0) {
        if (errcode != EVUTIL_EAI_CANCEL) {
            ClearTimer();
            _log_err(myLog, "DNS resolve failed, error code: %d error msg: %s", errcode, evutil_gai_strerror(errcode));
        } else {
            _log_warn(myLog, "DNS resolve cancel, may be timeout");
        }

        _log_warn(myLog, "delete DNS base. errcode=%d err=%s", errcode, strerror(errcode).c_str());
        evdns_base_free(dnsbase_, 0);
        dnsbase_ = nullptr;
        OnResolved();
        return;
    }


    if (addr == nullptr) {
        _log_err(myLog, "dns resolve error, addr can not be nullptr");
        _log_trace(myLog, "delete dns base");
        evdns_base_free(dnsbase_, 0);
        dnsbase_ = nullptr;
        ClearTimer();
        OnResolved();
        return;
    }


    if (addr->ai_canonname) {
        _log_trace(myLog, "resolve canon name: %s", addr->ai_canonname);
    }

    for (struct addrinfo* rp = addr; rp != nullptr; rp = rp->ai_next) {
        struct sockaddr_in* a = sock::sockaddr_in_cast(rp->ai_addr);

        if (a->sin_addr.s_addr == 0) {
            continue;
        }

        addrs_.push_back(a->sin_addr);
        _log_trace(myLog, "host=%s resolved a ip=%s", host_.c_str(), inet_ntoa(a->sin_addr));
    }
    evutil_freeaddrinfo(addr);
    ClearTimer();

    _log_trace(myLog, "delete DNS base");
    evdns_base_free(dnsbase_, 0); //TODO Do we need to free dns_req_?
    dnsbase_ = nullptr;
    OnResolved();
}

void DNSResolver::OnResolved(int errcode, struct addrinfo* addr, void* arg) {
    std::shared_ptr<DNSResolver>* pp = reinterpret_cast<std::shared_ptr<DNSResolver>*>(arg);
    (*pp)->OnResolved(errcode, addr);
    delete pp;
}
#endif

void DNSResolver::OnResolved() {
    if (functor_) {
        functor_(addrs_);

        // Release the callback immediately.
        // Sometimes, when it is timeout, this callback will be invoked in OnTimeout()
        // and `evdns_getaddrinfo_cancel(dns_req_)` will also invoke
        // OnResolved in next loop time. So we need to release this callback.
        functor_ = Functor();
    }
}

void DNSResolver::ClearTimer() {
    timer_->SetCancelCallback(TimerEventWatcher::Handler());
    timer_->Cancel();
    timer_.reset();
}

}
