.PHONY: all nif svr

TARGET_DIR=$(MIX_APP_PATH)/priv
TARGET_NIF=$(TARGET_DIR)/tundra_nif.so
TARGET_SVR=$(TARGET_DIR)/tundra_svr

CFLAGS=-Werror -Wfatal-errors -Wall -Wextra -O2 -flto -std=c11 -pedantic

all: nif svr

clean:
	rm -f $(TARGET_NIF) $(TARGET_SVR)

nif: $(TARGET_NIF)

$(TARGET_NIF): c_src/nif.c
	@mkdir -p $(TARGET_DIR)
	$(CC) $(CFLAGS) -I${ERL_INTERFACE_INCLUDE_DIR} -undefined dynamic_lookup -fPIC -shared -o $@ $?

svr: $(TARGET_SVR)

$(TARGET_SVR): c_src/svr.c
	@mkdir -p $(TARGET_DIR)
	$(CC) $(CFLAGS) -o $@ $?

