CXX ?= g++
CC ?= gcc
CPPFLAGS := $(shell fltk-config --cxxflags)
CXXFLAGS := -Wall -Wextra -std=c++17 -pthread -g
CFLAGS := -Wall -Wextra -std=gnu11 -g
LDLIBS := $(shell fltk-config --use-images --ldflags) -pthread
SANITIZE ?=
CXXFLAGS += $(SANITIZE)
CFLAGS += $(SANITIZE)
LDFLAGS += $(SANITIZE)
SRCS := $(wildcard ui/*.cpp)
OBJS := $(SRCS:ui/%.cpp=obj/%.o)

all: server throw_in_durak
server: server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
throw_in_durak: $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)
obj/%.o: ui/%.cpp ui/client.hpp ui/game_table.hpp ui/windows.hpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
obj:
	mkdir -p obj
client_c: client_c.c
	$(CC) $(CFLAGS) $< -pthread -o $@ $(LDFLAGS)
tests/server_test: tests/server_test.c server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
test: server tests/server_test
	./tests/server_test
	python3 tests/integration_test.py
clean:
	rm -rf obj throw_in_durak server client_c tests/server_test tests/client_test
rebuild:
	$(MAKE) clean
	$(MAKE) all
.PHONY: all clean rebuild test

tests/client_test: tests/client_test.cpp $(SRCS) ui/client.hpp ui/game_table.hpp ui/windows.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DDURAK_NO_MAIN tests/client_test.cpp $(SRCS) -o $@ $(LDFLAGS) $(LDLIBS)
test-ui: tests/client_test
	./tests/client_test
.PHONY: test-ui
