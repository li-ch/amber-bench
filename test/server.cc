#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <cassert>
#include <memory>

#include "rdma.h"
#include "bench_const.h"

void server(const std::string &port) {
  auto addr = rdma::Addr::resolve_local(port);
  auto server_channel = rdma::ServerChannel::listen(addr, 5);
  while (true) {
    std::cout << "accepting connection" << std::endl;
    auto channel = std::make_shared<rdma::Channel>(server_channel.accept());
    std::async(std::launch::async, [channel] () {
      size_t length = MSG_LEN;
      char buf[length];
      auto buffer = std::make_shared<rdma::Buffer>(channel->register_buffer(buf, sizeof(buf)));

      for (int i = 0; i < ITER_NUM; i++) {
        ibv_wc wc = channel->recv_sync(*buffer.get(), 0);
        assert(wc.status == IBV_WC_SUCCESS);
        std::cout << "received message with length " << wc.byte_len << std::endl;
        size_t byte_len = wc.byte_len;
        channel->send(*buffer.get(), byte_len, 0, [channel, buffer, byte_len](std::future<ibv_wc> f) {
          assert(f.get().status == IBV_WC_SUCCESS);
        std::cout << "sent message with length " << byte_len << std::endl;
        });
      }
    });
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "Usage: ./rdma-server <listen_port>" << std::endl;
    return EXIT_FAILURE;
  }
  server(argv[1]);
  return EXIT_SUCCESS;
}
