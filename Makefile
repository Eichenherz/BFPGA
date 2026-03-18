VERILOG_SRC = nand.sv display_driver.sv
TB_SRC      = nand_tb.cpp HtLib/Linux/linux_impl.cpp
TOP_MODULE  = display_driver
VERI_DIR    = display_driver
OBJ_DIR     = obj
BUILD_DIR   = build
BIN         = $(BUILD_DIR)/V$(TOP_MODULE)

CXX         = clang++
CC          = cc
CXXFLAGS    = -std=c++20 -g -O0
INCLUDES    = -isystem $(CURDIR)/HtLib -I$(CURDIR)/$(OBJ_DIR) -I$(VERI_DIR) \
              -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd
LDFLAGS     = -lwayland-client -lpthread -latomic

PROTO_DIR   = /usr/share/wayland-protocols/stable/xdg-shell
WL_HEADER   = $(OBJ_DIR)/xdg-shell-client-protocol.h
WL_CODE     = $(OBJ_DIR)/xdg-shell-protocol.c

VERI_STAMP  = $(VERI_DIR)/.stamp
VERI_RTLIB  = /usr/share/verilator/include/verilated.cpp \
              /usr/share/verilator/include/verilated_vcd_c.cpp \
              /usr/share/verilator/include/verilated_threads.cpp

.PHONY: all clean run debug wave

all: $(BIN)

# --- codegen ---
$(WL_HEADER): $(PROTO_DIR)/xdg-shell.xml
	mkdir -p $(OBJ_DIR)
	wayland-scanner client-header $< $@

$(WL_CODE): $(PROTO_DIR)/xdg-shell.xml
	mkdir -p $(OBJ_DIR)
	wayland-scanner private-code $< $@

$(VERI_STAMP): $(VERILOG_SRC)
	verilator --cc --trace --Mdir $(VERI_DIR) --top-module $(TOP_MODULE) $(VERILOG_SRC)
	touch $@

# --- build ---
$(BIN): $(VERI_STAMP) $(WL_HEADER) $(WL_CODE) $(TB_SRC)
	mkdir -p $(OBJ_DIR) $(BUILD_DIR)
# NOTE: must compile as C — wayland-scanner output has no extern "C" guards,
#       compiling as C++ mangles symbols and breaks linkage with the header
	$(CC) -O2 -c $(WL_CODE) -o $(OBJ_DIR)/xdg-shell-protocol.o
	cd $(OBJ_DIR) && $(CXX) $(CXXFLAGS) $(INCLUDES) -c $(addprefix $(CURDIR)/,$(TB_SRC)) $(CURDIR)/$(VERI_DIR)/*.cpp $(VERI_RTLIB)
	$(CXX) $(OBJ_DIR)/*.o $(LDFLAGS) -o $@

run: $(BIN)
	./$(BIN)

debug: $(BIN)
	lldb ./$(BIN)

wave: run
	gtkwave nand_tb.vcd &

clean:
	rm -rf $(OBJ_DIR) $(BUILD_DIR) $(VERI_DIR) nand_tb.vcd
