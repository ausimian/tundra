.PHONY: all nif svr

UNAME=$(shell uname)
TARGET_DIR=$(MIX_APP_PATH)/priv
TARGET_NIF=$(TARGET_DIR)/tundra_nif.so
TARGET_SVR=$(TARGET_DIR)/tundra_svr

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

all: nif svr
	@:

clean:
	rm -f $(TARGET_NIF) $(TARGET_SVR)

nif: $(TARGET_NIF)

$(TARGET_NIF): c_src/nif.c
	@mkdir -p $(TARGET_DIR)
	$(CC) $(CFLAGS) -I${ERL_INTERFACE_INCLUDE_DIR} $(SYMFLAGS) -fPIC -shared -o $@ $?

svr: $(TARGET_SVR)

$(TARGET_SVR): c_src/svr.c
	@mkdir -p $(TARGET_DIR)
	$(CC) $(CFLAGS) -o $@ $?

