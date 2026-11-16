CXX      ?= g++
CXXFLAGS ?= -O2 -march=native -std=c++17 -Wall -Wextra
BIN_DIR   = bin

PROGS = $(BIN_DIR)/pcap2bin $(BIN_DIR)/cauchy_intervals $(BIN_DIR)/exact_intervals \
        $(BIN_DIR)/verify $(BIN_DIR)/perfil

.PHONY: all clean
all: $(PROGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/pcap2bin: tools/pcap2bin.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN_DIR)/%: src/%.cpp src/*.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BIN_DIR)
