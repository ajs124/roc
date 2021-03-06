/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_netio/udp_sender.h"
#include "roc_core/helpers.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_packet/address_to_str.h"

namespace roc {
namespace netio {

UDPSender::UDPSender(uv_loop_t& event_loop, core::IAllocator& allocator)
    : allocator_(allocator)
    , loop_(event_loop)
    , write_sem_initialized_(false)
    , handle_initialized_(false)
    , pending_(0)
    , stopped_(true)
    , container_(NULL)
    , packet_counter_(0) {
}

UDPSender::~UDPSender() {
    if (handle_initialized_ || write_sem_initialized_) {
        roc_panic("udp sender: sender was not fully closed before calling destructor");
    }
}

void UDPSender::destroy() {
    allocator_.destroy(*this);
}

bool UDPSender::start(packet::Address& bind_address) {
    if (int err = uv_async_init(&loop_, &write_sem_, write_sem_cb_)) {
        roc_log(LogError, "udp sender: uv_async_init(): [%s] %s", uv_err_name(err),
                uv_strerror(err));
        return false;
    }

    write_sem_.data = this;
    write_sem_initialized_ = true;

    if (int err = uv_udp_init(&loop_, &handle_)) {
        roc_log(LogError, "udp sender: uv_udp_init(): [%s] %s", uv_err_name(err),
                uv_strerror(err));
        return false;
    }

    handle_.data = this;
    handle_initialized_ = true;

    unsigned flags = 0;
    if (bind_address.port() > 0) {
        flags |= UV_UDP_REUSEADDR;
    }

    if (int err = uv_udp_bind(&handle_, bind_address.saddr(), flags)) {
        roc_log(LogError, "udp sender: uv_udp_bind(): [%s] %s", uv_err_name(err),
                uv_strerror(err));
        return false;
    }

    int addrlen = (int)bind_address.slen();
    if (int err = uv_udp_getsockname(&handle_, bind_address.saddr(), &addrlen)) {
        roc_log(LogError, "udp sender: uv_udp_getsockname(): [%s] %s", uv_err_name(err),
                uv_strerror(err));
        return false;
    }

    if (addrlen != (int)bind_address.slen()) {
        roc_log(LogError,
                "udp sender: uv_udp_getsockname(): unexpected len: got=%lu expected=%lu",
                (unsigned long)addrlen, (unsigned long)bind_address.slen());
        return false;
    }

    roc_log(LogInfo, "udp sender: opened port %s",
            packet::address_to_str(bind_address).c_str());

    stopped_ = false;
    address_ = bind_address;
    return true;
}

void UDPSender::stop() {
    core::Mutex::Lock lock(mutex_);

    stopped_ = true;

    if (pending_ == 0) {
        close_();
    }
}

void UDPSender::remove(core::List<UDPSender>& container) {
    roc_panic_if(container_);

    if (handle_initialized_ || write_sem_initialized_) {
        stop();
        container_ = &container;
        address_ = packet::Address();
    } else {
        container.remove(*this);
    }
}

const packet::Address& UDPSender::address() const {
    return address_;
}

void UDPSender::write(const packet::PacketPtr& pp) {
    if (!pp) {
        roc_panic("udp sender: unexpected null packet");
    }

    if (!pp->udp()) {
        roc_panic("udp sender: unexpected non-udp packet");
    }

    if (!pp->data()) {
        roc_panic("udp sender: unexpected packet w/o data");
    }

    {
        core::Mutex::Lock lock(mutex_);

        if (stopped_) {
            return;
        }

        list_.push_back(*pp);
        ++pending_;
    }

    if (int err = uv_async_send(&write_sem_)) {
        roc_panic("udp sender: uv_async_send(): [%s] %s", uv_err_name(err),
                  uv_strerror(err));
    }
}

void UDPSender::close_cb_(uv_handle_t* handle) {
    roc_panic_if_not(handle);

    UDPSender& self = *(UDPSender*)handle->data;

    if (handle == (uv_handle_t*)&self.handle_) {
        self.handle_initialized_ = false;
    } else {
        self.write_sem_initialized_ = false;
    }

    if (self.handle_initialized_ || self.write_sem_initialized_) {
        return;
    }

    if (self.container_) {
        self.container_->remove(self);
    }
}

void UDPSender::write_sem_cb_(uv_async_t* handle) {
    roc_panic_if_not(handle);

    UDPSender& self = *(UDPSender*)handle->data;

    while (packet::PacketPtr pp = self.read_()) {
        packet::UDP& udp = *pp->udp();

        self.packet_counter_++;

        roc_log(LogTrace, "udp sender: sending packet: num=%u src=%s dst=%s sz=%ld",
                self.packet_counter_, packet::address_to_str(self.address_).c_str(),
                packet::address_to_str(udp.dst_addr).c_str(), (long)pp->data().size());

        uv_buf_t buf;
        buf.base = (char*)pp->data().data();
        buf.len = pp->data().size();

        udp.request.data = &self;

        if (int err = uv_udp_send(&udp.request, &self.handle_, &buf, 1,
                                  udp.dst_addr.saddr(), send_cb_)) {
            roc_log(LogError, "udp sender: uv_udp_send(): [%s] %s", uv_err_name(err),
                    uv_strerror(err));
            continue;
        }

        // will be decremented in send_cb_()
        pp->incref();
    }
}

void UDPSender::send_cb_(uv_udp_send_t* req, int status) {
    roc_panic_if_not(req);

    UDPSender& self = *(UDPSender*)req->data;

    packet::PacketPtr pp =
        packet::Packet::container_of(ROC_CONTAINER_OF(req, packet::UDP, request));

    // one reference for incref() called from write_sem_cb_()
    // one reference for the shared pointer above
    roc_panic_if(pp->getref() < 2);

    // decrement reference counter incremented in async_cb_()
    pp->decref();

    if (status < 0) {
        roc_log(LogError,
                "udp sender:"
                " can't send packet: src=%s dst=%s sz=%ld: [%s] %s",
                packet::address_to_str(self.address_).c_str(),
                packet::address_to_str(pp->udp()->dst_addr).c_str(),
                (long)pp->data().size(), uv_err_name(status), uv_strerror(status));
    }

    core::Mutex::Lock lock(self.mutex_);

    --self.pending_;

    if (self.stopped_ && self.pending_ == 0) {
        self.close_();
    }
}

packet::PacketPtr UDPSender::read_() {
    core::Mutex::Lock lock(mutex_);

    packet::PacketPtr pp = list_.front();
    if (pp) {
        list_.remove(*pp);
    }

    return pp;
}

void UDPSender::close_() {
    if (handle_initialized_ && !uv_is_closing((uv_handle_t*)&handle_)) {
        roc_log(LogInfo, "udp sender: closing port %s",
                packet::address_to_str(address_).c_str());

        uv_close((uv_handle_t*)&handle_, close_cb_);
    }

    if (write_sem_initialized_ && !uv_is_closing((uv_handle_t*)&write_sem_)) {
        uv_close((uv_handle_t*)&write_sem_, close_cb_);
    }
}

} // namespace netio
} // namespace roc
