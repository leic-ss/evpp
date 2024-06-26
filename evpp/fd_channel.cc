#include "evpp/inner_pre.h"

#include <string.h>

#include "evpp/fd_channel.h"
#include "evpp/libevent.h"
#include "evpp/event_loop.h"

namespace evpp {
static_assert(FdChannel::kReadable == EV_READ, "");
static_assert(FdChannel::kWritable == EV_WRITE, "");

FdChannel::FdChannel(EventLoop* l, evpp_socket_t f, bool r, bool w)
    : loop_(l), attached_(false), event_(nullptr), fd_(f) {
    _log_trace(myLog, "fd=%d", fd_);
    assert(fd_ > 0);
    events_ = (r ? kReadable : 0) | (w ? kWritable : 0);
    event_ = new event;
    memset(event_, 0, sizeof(struct event));
}

FdChannel::~FdChannel() {
    _log_trace(myLog, "fd=%d", fd_);
    // fprintf(stderr, "destruction fd=%d\n", fd_);
    assert(event_ == nullptr);
}

void FdChannel::Close() {
    _log_trace(myLog, "fd=%d", fd_);
    // fprintf(stderr, "close fd=%d\n", fd_);
    assert(event_);
    if (event_) {
        assert(!attached_);
        if (attached_) {
            EventDel(event_);
        }

        delete (event_);
        event_ = nullptr;
    }
    read_fn_ = ReadEventCallback();
    write_fn_ = EventCallback();
}

void FdChannel::AttachToLoop() {
    assert(!IsNoneEvent());
    assert(loop_->IsInLoopThread());

    if (attached_) {
        // FdChannel::Update may be called many times
        // So doing this can avoid event_add will be called more than once.
        DetachFromLoop();
    }

    assert(!attached_);
    ::event_set(event_, fd_, events_ | EV_PERSIST,
                &FdChannel::HandleEvent, this);
    ::event_base_set(loop_->event_base(), event_);

    if (EventAdd(event_, nullptr) == 0) {
        _log_trace(myLog, "fd=%d watching event=%s", fd_, EventsToString().c_str());
        attached_ = true;
    } else {
        _log_err(myLog, "fd=%d with event %s attach to event loop failed", fd_, EventsToString().c_str());
    }
}

void FdChannel::EnableReadEvent() {
    int events = events_;
    events_ |= kReadable;

    if (events_ != events) {
        Update();
    }
}

void FdChannel::EnableWriteEvent() {
    int events = events_;
    events_ |= kWritable;

    if (events_ != events) {
        Update();
    }
}

void FdChannel::DisableReadEvent() {
    int events = events_;
    events_ &= (~kReadable);

    if (events_ != events) {
        Update();
    }
}

void FdChannel::DisableWriteEvent() {
    int events = events_;
    events_ &= (~kWritable);
    if (events_ != events) {
        Update();
    }
}

void FdChannel::DisableAllEvent() {
    if (events_ == kNone) {
        return;
    }

    events_ = kNone;
    Update();
}

void FdChannel::DetachFromLoop() {
    assert(loop_->IsInLoopThread());
    assert(attached_);

    if (EventDel(event_) == 0) {
        attached_ = false;
        _log_trace(myLog, "fd=%d detach from event loop", fd_);
    } else {
        _log_err(myLog, "DetachFromLoop fd=%d with event %s detach from event loop failed", fd_, EventsToString().c_str());
    }
}

void FdChannel::Update() {
    assert(loop_->IsInLoopThread());

    if (IsNoneEvent()) {
        DetachFromLoop();
    } else {
        AttachToLoop();
    }
}

std::string FdChannel::EventsToString() const {
    std::string s;

    if (events_ & kReadable) {
        s = "kReadable";
    }

    if (events_ & kWritable) {
        if (!s.empty()) {
            s += "|";
        }

        s += "kWritable";
    }

    return s;
}

void FdChannel::HandleEvent(evpp_socket_t sockfd, short which, void* v) {
    FdChannel* c = (FdChannel*)v;
    c->HandleEvent(sockfd, which);
}

void FdChannel::HandleEvent(evpp_socket_t sockfd, short which) {
    // fprintf(stderr, "sockfd=%d fd=%d %d err=%s\n", sockfd, fd_, this, EventsToString().c_str());
    _log_trace(myLog, "sockfd=%d fd=%d err=%s", sockfd, fd_, EventsToString().c_str());
    assert(sockfd == fd_);
    // _log_trace(myLog, "fd=%d err=%s", sockfd, EventsToString().c_str());

    if ((which & kReadable) && read_fn_) {
        read_fn_();
    }

    if ((which & kWritable) && write_fn_) {
        write_fn_();
    }
}
}
