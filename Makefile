VERILOG_SRC = nand.sv
TB_SRC      = nand_tb.cpp
TOP_MODULE  = nand_gate
OBJ_DIR     = obj
BIN         = $(OBJ_DIR)/V$(TOP_MODULE)

VERILATOR_FLAGS = --cc --exe --trace --build -CFLAGS "-std=c++20 -g -O0" --compiler clang

PROTO_DIR   = /usr/share/wayland-protocols/stable/xdg-shell
WL_BUILD    = build
WL_HEADER   = $(WL_BUILD)/xdg-shell-client-protocol.h
WL_CODE     = $(WL_BUILD)/xdg-shell-protocol.c

.PHONY: all clean run debug wave

all: $(BIN)

$(WL_HEADER): $(PROTO_DIR)/xdg-shell.xml
	mkdir -p $(WL_BUILD)
	wayland-scanner client-header $< $@

$(WL_CODE): $(PROTO_DIR)/xdg-shell.xml
	mkdir -p $(WL_BUILD)
	wayland-scanner private-code $< $@

$(BIN): $(VERILOG_SRC) $(TB_SRC) $(WL_HEADER) $(WL_CODE)
	verilator $(VERILATOR_FLAGS) --top-module $(TOP_MODULE) $(VERILOG_SRC) $(TB_SRC)

run: $(BIN)
	./$(BIN)

debug: $(BIN)
	lldb ./$(BIN)

wave: run
	gtkwave nand_tb.vcd &

clean:
	rm -rf $(OBJ_DIR) $(WL_BUILD) nand_tb.vcd
