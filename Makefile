# Tundra NIF Makefile
#
# This builds only the NIF (Erlang Native Interface) component.
# The standalone server is built separately in the c_src/server/ directory.
#
# See c_src/server/README.md for server build instructions.

.PHONY: all nif clean

UNAME=$(shell uname)
TARGET_DIR=$(MIX_APP_PATH)/priv
TARGET_NIF=$(TARGET_DIR)/tundra_nif.so

CFLAGS=-Werror -Wfatal-errors -Wall -Wextra -O2 -flto -std=c11 -pedantic

ERL_INTERFACE_INCLUDE_DIR ?= $(shell elixir --eval 'IO.puts(Path.join([:code.root_dir(), "usr", "include"]))')

SYMFLAGS=-fvisibility=hidden
ifeq ($(UNAME), Linux)
	CFLAGS+=-D__STDC_WANT_LIB_EXT2__=1
	SYMFLAGS+=
else ifeq ($(UNAME), Darwin)
	SYMFLAGS+=-undefined dynamic_lookup
else
	$(error "Unsupported platform")
endif

all: nif
	@:

clean:
	rm -f $(TARGET_NIF)

nif: $(TARGET_NIF)

# Platform-specific source files
TUN_SRC=
ifeq ($(UNAME), Linux)
	TUN_SRC=c_src/server/src/tun_linux.c
else ifeq ($(UNAME), Darwin)
	TUN_SRC=c_src/server/src/tun_darwin.c
endif

$(TARGET_NIF): c_src/nif.c c_src/server/src/protocol.h c_src/server/src/server.h $(TUN_SRC)
	@mkdir -p $(TARGET_DIR)
	$(CC) $(CFLAGS) -I${ERL_INTERFACE_INCLUDE_DIR} $(SYMFLAGS) -fPIC -shared -o $@ c_src/nif.c $(TUN_SRC)

