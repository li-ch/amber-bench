Benchmarking using librdmacm-cpp
===

Dependency
---

Make sure an OFED distribution is installed, or install the following packages:

```bash
sudo apt-get install librdmacm-dev libibverbs-dev rdmacm-utils ibverbs-utils
```

Compile
---

```bash
make
```

Hello World
---

On server, run
```bash
./bin/rdma-server 7001
```

On client, run
```bash
./bin/rdma-client <server_ip> 7001
```
