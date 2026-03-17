VERILOG_SRC = nand.sv
TB_SRC      = nand_tb.cpp HtLib/Linux/linux_impl.cpp
TOP_MODULE  = nand_gate
VERI_DIR    = verilator
OBJ_DIR     = obj
BIN         = $(OBJ_DIR)/V$(TOP_MODULE)

CXX         = clang++
CC          = cc
CXXFLAGS    = -std=c++20 -g -O0
INCLUDES    = -isystem $(CURDIR)/HtLib -I$(CURDIR)/$(WL_BUILD) -I$(VERI_DIR) \
              -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd
LDFLAGS     = -lwayland-client -lpthread -latomic

PROTO_DIR   = /usr/share/wayland-protocols/stable/xdg-shell
WL_BUILD    = build
WL_HEADER   = $(WL_BUILD)/xdg-shell-client-protocol.h
WL_CODE     = $(WL_BUILD)/xdg-shell-protocol.c

VERI_STAMP  = $(VERI_DIR)/.stamp
VERI_RTLIB  = /usr/share/verilator/include/verilated.cpp \
              /usr/share/verilator/include/verilated_vcd_c.cpp \
              /usr/share/verilator/include/verilated_threads.cpp

.PHONY: all clean run debug wave

all: $(BIN)

# --- codegen ---
$(WL_HEADER): $(PROTO_DIR)/xdg-shell.xml
	mkdir -p $(WL_BUILD)
	wayland-scanner client-header $< $@

$(WL_CODE): $(PROTO_DIR)/xdg-shell.xml
	mkdir -p $(WL_BUILD)
	wayland-scanner private-code $< $@

$(VERI_STAMP): $(VERILOG_SRC)
	verilator --cc --trace --Mdir $(VERI_DIR) --top-module $(TOP_MODULE) $(VERILOG_SRC)
	touch $@

# --- build ---
$(BIN): $(VERI_STAMP) $(WL_HEADER) $(WL_CODE) $(TB_SRC)
	mkdir -p $(OBJ_DIR)
# NOTE: must compile as C — wayland-scanner output has no extern "C" guards,
#       compiling as C++ mangles symbols and breaks linkage with the header
	$(CC) -O2 -c $(WL_CODE) -o $(OBJ_DIR)/xdg-shell-protocol.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $(TB_SRC) $(VERI_DIR)/*.cpp $(VERI_RTLIB)
	mv *.o $(OBJ_DIR)/
	$(CXX) $(OBJ_DIR)/*.o $(LDFLAGS) -o $@

run: $(BIN)
	./$(BIN)

debug: $(BIN)
	lldb ./$(BIN)

wave: run
	gtkwave nand_tb.vcd &

clean:
	rm -rf $(OBJ_DIR) $(VERI_DIR) $(WL_BUILD) nand_tb.vcd
