all: server client

server: $(test/rdma-server.cpp) lib
	@mkdir -p bin
	c++ -std=c++0x -o bin/server -I include -pthread -l ibverbs -l rdmacm test/server.cc target/rdma.o
	
client: $(test/rdma-client.cpp) lib
	@mkdir -p bin
	c++ -std=c++0x -o bin/client -I include -pthread -l ibverbs -l rdmacm test/client.cc target/rdma.o
	
lib: $(include/rdma.hpp) $(src/rdma.cpp)
	@mkdir -p target
	c++ -std=c++0x -o target/rdma.o -I include -c src/rdma.cc
	
clean:
	rm -rf bin/ target/
	
.PHONY: all server client lib clean
