#ifndef RDMA_H_
#define RDMA_H_

#include <string>
#include <future>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>

#include <rdma/rdma_cma.h>

namespace rdma {

class Addr {

 public:
  virtual ~Addr() noexcept;
  Addr(const Addr &) = delete;
  Addr &operator=(const Addr &) = delete;
  Addr(Addr &&addr);
  Addr &operator=(Addr &&);

  static Addr resolve_remote(const std::string &host, const std::string &port);
  static Addr resolve_local(const std::string &port);

 protected:
  explicit Addr(rdma_addrinfo *addrinfo) : addrinfo_(addrinfo) {}

 private:
  friend class Channel;
  friend class ServerChannel;
  rdma_addrinfo *addrinfo_;
};

class Buffer {

 public:
  virtual ~Buffer() noexcept;
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&);
  Buffer &operator=(Buffer &&);

  void* addr() const;
  size_t length() const;

  uint32_t local_key() const;
  uint32_t remote_key() const;

 protected:
  explicit Buffer(ibv_mr* mr) : mr_(mr) {}

 private:
  friend class Channel;
  ibv_mr *mr_;
};

class Selector {

 public:
  typedef std::function<void(std::future<ibv_wc>)> DoneCallBack;

  virtual ~Selector();
  Selector(Selector &) = delete;
  Selector(Selector &&) = delete;
  Selector& operator=(Selector &) = delete;
  Selector& operator=(Selector &&) = delete;

  void register_callback(ibv_cq* cq, DoneCallBack done);
  void unregister(ibv_cq* cq);

  static Selector* get();

 protected:
  explicit Selector();

 private:
  std::thread thread_;
  std::map<ibv_cq*, DoneCallBack> callbacks_;
  std::atomic<bool> done;

  std::mutex mu_;
};

class Channel {

 public:
  virtual ~Channel() noexcept;
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  Channel(Channel &&);
  Channel &operator=(Channel &&);

  static Channel connect(const Addr &addr);

  Buffer register_buffer(void *address, size_t length);

  void recv(Buffer &buffer, ptrdiff_t offset, Selector::DoneCallBack done);
  void send(const Buffer &buffer, size_t length, ptrdiff_t offset, Selector::DoneCallBack done);

  std::future<ibv_wc> recv(Buffer &buffer, ptrdiff_t offset);
  std::future<ibv_wc> send(const Buffer &buffer, size_t length, ptrdiff_t offset);

  ibv_wc recv_sync(Buffer &buffer, ptrdiff_t offset);
  ibv_wc send_sync(const Buffer &buffer, size_t length, ptrdiff_t offset);

  sockaddr* src_addr() const noexcept;
  sockaddr* dst_addr() const noexcept;
  uint16_t src_port() const noexcept;
  uint16_t dst_port() const noexcept;

 protected:
  explicit Channel(rdma_cm_id *id, Selector *selector) : id_(id), selector_(selector) {};

 private:
  friend class ServerChannel;
  rdma_cm_id *id_;
  Selector *selector_;
};

class ServerChannel {

 public:
  virtual ~ServerChannel() noexcept;
  ServerChannel(const ServerChannel &) = delete;
  ServerChannel &operator=(const ServerChannel &) = delete;
  ServerChannel(ServerChannel &&);
  ServerChannel &operator=(ServerChannel &&);

  static ServerChannel listen(const Addr &addr, int backlog = 0);

  Channel accept();

  sockaddr* addr() const noexcept;
  uint16_t port() const noexcept;

 protected:
  explicit ServerChannel(rdma_cm_id *id) : id_(id) {};

 private:
  friend class Selector;
  rdma_cm_id *id_;
};

}  // namespace rdma

#endif /* RDMA_H */
