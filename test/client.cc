#include <string>
#include <thread>
#include <iostream>
#include <vector>
#include <cassert>

#include "rdma.h"
#include "bench_const.h"

void client(const std::string &host, const std::string &port, char *message, size_t length) {
  auto addr = rdma::Addr::resolve_remote(host, port);
  auto channel = rdma::Channel::connect(addr);
  auto buffer = channel.register_buffer(&message[0], length);

  for (int i = 0; i < ITER_NUM; i++) {
    std::cout << "Iter" << i << " starts" << std::endl;
    ibv_wc wc = channel.send(buffer, length, 0).get();
    assert(wc.status == IBV_WC_SUCCESS);
    std::cout << "Iter" << i << ": sent message with length " << length << std::endl;
    wc = channel.recv(buffer, 0).get();
    assert(wc.status == IBV_WC_SUCCESS);
    std::cout << "Iter" << i << ": received message with length " << wc.byte_len << std::endl;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
      std::cout << "Usage: ./client <host> <port>" << std::endl;
    return EXIT_FAILURE;
  }

  const size_t length = MSG_LEN;
  char message[length];

  std::vector<std::future<void>> futures;
  for (int i = 0; i < CLT_NUM; i++) {
    auto f = std::async(std::launch::async, [argv, &message]() {
      client(argv[1], argv[2], message, length);
    });
    futures.push_back(std::move(f));
  }

  for (auto &f : futures) {
    f.get();
  }
  return EXIT_SUCCESS;
}
