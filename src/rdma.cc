#include <exception>
#include <thread>
#include <vector>
#include <memory>

#include <rdma/rdma_verbs.h>

#include "rdma.h"

namespace rdma {

class addrinfo_exception : public std::exception {
public:
  virtual const char *what() const throw() {
    return "fail to resolve address";
  }
};

class buffer_exception : public std::exception {
public:
  virtual const char *what() const throw() {
    return "fail to register memory";
  }
};

class channel_exception : public std::exception {
  virtual const char *what() const throw() {
    return "channel fail";
  }
};

Addr::~Addr() noexcept {
  if (addrinfo_ != nullptr) {
    rdma_freeaddrinfo(addrinfo_);
  }
}

Addr::Addr(Addr &&addr) {
  this->addrinfo_ = addr.addrinfo_;
  addr.addrinfo_ = nullptr;
}

Addr& Addr::operator=(Addr &&addr) {
  this->~Addr();
  this->addrinfo_ = addr.addrinfo_;
  addr.addrinfo_ = nullptr;
  return *this;
}

Addr Addr::resolve_local(const std::string &port) {

  rdma_addrinfo hints = {};
  hints.ai_flags = RAI_PASSIVE;
  hints.ai_port_space = RDMA_PS_TCP;

  rdma_addrinfo *addrinfo;
  if (rdma_getaddrinfo(nullptr, const_cast<char*>(port.c_str()), &hints, &addrinfo)) {
    throw addrinfo_exception();
  }
  return Addr(addrinfo);
}

Addr Addr::resolve_remote(const std::string &host, const std::string &port) {

  rdma_addrinfo hints = {};
  hints.ai_port_space = RDMA_PS_TCP;

  rdma_addrinfo *addrinfo;
  if (rdma_getaddrinfo(const_cast<char*>(host.c_str()),
        const_cast<char*>(port.c_str()), &hints, &addrinfo)) {
    throw addrinfo_exception();
  }
  return Addr(addrinfo);
}

Buffer::~Buffer() noexcept {
  if (mr_ != nullptr) {
    rdma_dereg_mr(mr_);
  }
}

Buffer::Buffer(Buffer &&buffer) {
  this->mr_ = buffer.mr_;
  buffer.mr_ = nullptr;
}

Buffer& Buffer::operator=(Buffer &&buffer) {
  this->~Buffer();
  this->mr_ = buffer.mr_;
  buffer.mr_ = nullptr;
  return *this;
}

void* Buffer::addr() const {
  return mr_->addr;
}

size_t Buffer::length() const {
  return mr_->length;
}

uint32_t Buffer::local_key() const {
  return mr_->lkey;
}

uint32_t Buffer::remote_key() const {
  return mr_->rkey;
}

Selector* Selector::get() {
  static Selector selector;
  return &selector;
}

Selector::Selector() {
  done = false;
  thread_ = std::thread([this]() {
    std::vector<ibv_cq*> finished_cqs;
    while (!done) {
      std::lock_guard<std::mutex> l(this->mu_);
      for (auto &iter : this->callbacks_) {
        ibv_wc wc = {};
        std::promise<ibv_wc> p;
        switch (ibv_poll_cq(iter.first, 1, &wc)) {
        case 0:
          break;
        case 1:
          p.set_value(wc);
          iter.second(p.get_future());
          finished_cqs.push_back(iter.first);
          break;
        default:
          try {
            throw channel_exception();
          } catch (std::exception e) {
            p.set_exception(std::current_exception());
            iter.second(p.get_future());
          }
          finished_cqs.push_back(iter.first);
          break;
        }
      }
      for (auto cq : finished_cqs) {
        this->callbacks_.erase(cq);
      }
      finished_cqs.clear();
    }
  });
}

void Selector::register_callback(ibv_cq* cq, DoneCallBack done) {
  std::lock_guard<std::mutex> l(mu_);
  callbacks_[cq] = std::move(done);
}

void Selector::unregister(ibv_cq* cq) {
  std::lock_guard<std::mutex> l(mu_);
  callbacks_.erase(cq);
}

Selector::~Selector() {
  done = true;
  std::lock_guard<std::mutex> l(mu_);
  for (auto &iter : this->callbacks_) {
    try {
      throw channel_exception();
    } catch (std::exception e) {
      std::promise<ibv_wc> p;
      p.set_exception(std::current_exception());
      iter.second(p.get_future());
    }
  }
  this->callbacks_.clear();
  thread_.join();
}

Channel Channel::connect(const Addr &addr) {

  ibv_qp_init_attr init_attr = {};
  init_attr.cap.max_send_wr = 1;
  init_attr.cap.max_recv_wr = 1;
  init_attr.cap.max_recv_sge = 1;
  init_attr.cap.max_send_sge = 1;
  init_attr.sq_sig_all = 1;
  rdma_cm_id *id = nullptr;
  if (rdma_create_ep(&id, addr.addrinfo_, nullptr, &init_attr)) {
    throw channel_exception();
  }
  if (rdma_connect(id, nullptr)) {
    rdma_destroy_ep(id);
    throw channel_exception();
  };
  return Channel(id, Selector::get());
}

Channel::~Channel() noexcept {
  if (id_ != nullptr) {
    rdma_disconnect(id_);
    rdma_destroy_ep(id_);
  }
}

Channel::Channel(Channel &&channel) {
  this->selector_ = channel.selector_;
  this->id_ = channel.id_;
  channel.selector_ = nullptr;
  channel.id_ = nullptr;
}

Channel& Channel::operator=(Channel &&channel) {
  this->~Channel();
  this->selector_ = channel.selector_;
  this->id_ = channel.id_;
  channel.selector_ = nullptr;
  channel.id_ = nullptr;
  return *this;
}

Buffer Channel::register_buffer(void *address, size_t length) {
  ibv_mr *mr = rdma_reg_msgs(id_, address, length);
  if (mr == nullptr) {
    throw buffer_exception();
  }
  return Buffer(mr);
}

void Channel::recv(Buffer &buffer, ptrdiff_t offset, Selector::DoneCallBack done) {
  std::promise<ibv_wc> p;
  if (rdma_post_recv(id_, nullptr, reinterpret_cast<char*>(buffer.addr()) + offset, buffer.length(), buffer.mr_)) {
    try {
      throw channel_exception();
    } catch (std::exception e) {
      p.set_exception(std::current_exception());
    }
  } else {
    selector_->register_callback(id_->recv_cq, std::move(done));
  }
}

void Channel::send(const Buffer &buffer, size_t length, ptrdiff_t offset, Selector::DoneCallBack done) {
  std::promise<ibv_wc> p;
  if (rdma_post_send(id_, nullptr, reinterpret_cast<char*>(buffer.addr()) + offset, length, buffer.mr_, 0)) {
    try {
      throw channel_exception();
    } catch (std::exception e) {
      p.set_exception(std::current_exception());
    }
  } else {
    selector_->register_callback(id_->send_cq, std::move(done));
  }
}

std::future<ibv_wc> Channel::recv(Buffer &buffer, ptrdiff_t offset) {
  auto promise = std::make_shared<std::promise<ibv_wc>>();
  recv(buffer, offset, [promise](std::future<ibv_wc> f) {
    try {
      promise->set_value(f.get());
    } catch (std::exception e) {
      promise->set_exception(std::current_exception());
    }
  });
  return promise->get_future();
}

std::future<ibv_wc> Channel::send(const Buffer &buffer, size_t length, ptrdiff_t offset) {
  auto promise = std::make_shared<std::promise<ibv_wc>>();
  send(buffer, length, offset, [promise](std::future<ibv_wc> f) {
    try {
      promise->set_value(f.get());
    } catch (std::exception e) {
      promise->set_exception(std::current_exception());
    }
  });
  return promise->get_future();
}

ibv_wc Channel::recv_sync(Buffer &buffer, ptrdiff_t offset) {
  if (rdma_post_recv(id_, nullptr, reinterpret_cast<char*>(buffer.addr()) + offset, buffer.length(), buffer.mr_)) {
    throw channel_exception();
  } else {
    ibv_wc wc = {};
    if (rdma_get_recv_comp(id_, &wc) < 0) {
      throw channel_exception();
    }
    return wc;
  }
}

ibv_wc Channel::send_sync(const Buffer &buffer, size_t length, ptrdiff_t offset) {
  if (rdma_post_send(id_, nullptr, reinterpret_cast<char*>(buffer.addr()) + offset, length, buffer.mr_, 0)) {
    throw channel_exception();
  } else {
    ibv_wc wc = {};
    if (rdma_get_send_comp(id_, &wc) < 0) {
      throw channel_exception();
    }
    return wc;
  }
}

ServerChannel::~ServerChannel() noexcept {
  if (id_ != nullptr) {
    rdma_destroy_ep(id_);
  }
}

ServerChannel::ServerChannel(ServerChannel &&server_channel) {
  this->id_ = server_channel.id_;
  server_channel.id_ = nullptr;
}

ServerChannel& ServerChannel::operator=(ServerChannel &&server_channel) {
  this->~ServerChannel();
  this->id_ = server_channel.id_;
  server_channel.id_ = nullptr;
  return *this;
}

ServerChannel ServerChannel::listen(const Addr &addr, int backlog) {

  ibv_qp_init_attr init_attr = {};
  init_attr.cap.max_send_wr = 1;
  init_attr.cap.max_recv_wr = 1;
  init_attr.cap.max_recv_sge = 1;
  init_attr.cap.max_send_sge = 1;
  init_attr.sq_sig_all = 1;
  rdma_cm_id *id;
  if (rdma_create_ep(&id, addr.addrinfo_, nullptr, &init_attr)) {
    throw channel_exception();
  }
  if (rdma_listen(id, backlog)) {
    rdma_destroy_ep(id);
    throw channel_exception();
  }
  return ServerChannel(id);
}

Channel ServerChannel::accept() {
  rdma_cm_id *client;
  if (rdma_get_request(id_, &client)) {
    throw channel_exception();
  }
  if (rdma_accept(client, nullptr)) {
    throw channel_exception();
  }
  return Channel(client, Selector::get());
}

}  // namespace rdma
