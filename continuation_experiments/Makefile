EXAMPLES = \
	direct_bulk \
	bulk_driver \
	bulk_driver_in_promise \
	cleaner_bulk_model

TESTS = $(addsuffix .test, $(EXAMPLES))

CC = /usr/local/opt/llvm/bin/clang
CXX = /usr/local/opt/llvm/bin/clang++

LDFLAGS += -L/usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib
CXXFLAGS += -I/usr/local/opt/llvm/include -I/usr/local/opt/llvm/include/c++/v1/

CXXFLAGS = -std=c++17 -pthread -Wall -Wextra

.PHONY: all clean

all: $(EXAMPLES)

test: $(TESTS)

clean:
	rm -f $(EXAMPLES)

$(EXAMPLES): %: %.cpp
	$(CXX) $(CXXFLAGS) -o$@ $<

%.test: %
	./$(basename $<)
