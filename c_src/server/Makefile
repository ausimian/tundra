CC = gcc
CFLAGS = -Wall -Wextra -Werror -Wfatal-errors -O2 -std=c11 -pedantic
TARGET = tundra_server
TEST_CLIENT = test_client
SRCDIR = src

# Package metadata
VERSION ?= 0.1.0
PACKAGE_NAME = tundra-server
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
    DEB_ARCH = amd64
endif
ifeq ($(ARCH),aarch64)
    DEB_ARCH = arm64
endif
ifeq ($(ARCH),arm64)
    DEB_ARCH = arm64
endif

# Package directories
PKG_DIR = pkg
DEB_DIR = $(PKG_DIR)/deb
DEB_ROOT = $(DEB_DIR)/$(PACKAGE_NAME)_$(VERSION)_$(DEB_ARCH)
OSX_DIR = $(PKG_DIR)/osx
OSX_ROOT = $(OSX_DIR)/root
SRCS = $(SRCDIR)/main.c $(SRCDIR)/protocol.c

# Platform-specific sources
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    SRCS += $(SRCDIR)/tun_linux.c
endif
ifeq ($(UNAME_S),Darwin)
    SRCS += $(SRCDIR)/tun_darwin.c
endif

OBJS = $(SRCS:.c=.o)

.PHONY: all clean install test deb pkg pkg-clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CLIENT): test_client.c $(SRCDIR)/protocol.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_CLIENT)
	@echo "Build the test client. Run server with: sudo ./$(TARGET)"
	@echo "Then in another terminal: ./$(TEST_CLIENT)"

clean:
	rm -f $(TARGET) $(TEST_CLIENT) $(OBJS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Clean package artifacts
pkg-clean:
	rm -rf $(PKG_DIR)

# Build Debian package
deb: $(TARGET)
	@echo "Building Debian package..."
	rm -rf $(DEB_ROOT)
	mkdir -p $(DEB_ROOT)/usr/local/bin
	mkdir -p $(DEB_ROOT)/lib/systemd/system
	mkdir -p $(DEB_ROOT)/DEBIAN
	cp $(TARGET) $(DEB_ROOT)/usr/local/bin/
	chmod 755 $(DEB_ROOT)/usr/local/bin/$(TARGET)
	@echo "Package: $(PACKAGE_NAME)" > $(DEB_ROOT)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_ROOT)/DEBIAN/control
	@echo "Section: net" >> $(DEB_ROOT)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_ROOT)/DEBIAN/control
	@echo "Architecture: $(DEB_ARCH)" >> $(DEB_ROOT)/DEBIAN/control
	@echo "Maintainer: Tundra Maintainers" >> $(DEB_ROOT)/DEBIAN/control
	@echo "Description: Tundra VPN server" >> $(DEB_ROOT)/DEBIAN/control
	@echo " Privileged daemon for TUN device creation and management." >> $(DEB_ROOT)/DEBIAN/control
	@echo "[Unit]" > $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "Description=Tundra Server" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "After=network.target" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "[Service]" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "Type=simple" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "ExecStart=/usr/local/bin/$(TARGET)" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "Restart=on-failure" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "[Install]" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@echo "WantedBy=multi-user.target" >> $(DEB_ROOT)/lib/systemd/system/tundra-server.service
	@# postinst - enable and start service after install
	@echo "#!/bin/sh" > $(DEB_ROOT)/DEBIAN/postinst
	@echo "set -e" >> $(DEB_ROOT)/DEBIAN/postinst
	@echo "systemctl daemon-reload" >> $(DEB_ROOT)/DEBIAN/postinst
	@echo "systemctl enable tundra-server.service" >> $(DEB_ROOT)/DEBIAN/postinst
	@echo "systemctl start tundra-server.service" >> $(DEB_ROOT)/DEBIAN/postinst
	@chmod 755 $(DEB_ROOT)/DEBIAN/postinst
	@# prerm - stop service before removal
	@echo "#!/bin/sh" > $(DEB_ROOT)/DEBIAN/prerm
	@echo "set -e" >> $(DEB_ROOT)/DEBIAN/prerm
	@echo "systemctl stop tundra-server.service || true" >> $(DEB_ROOT)/DEBIAN/prerm
	@echo "systemctl disable tundra-server.service || true" >> $(DEB_ROOT)/DEBIAN/prerm
	@chmod 755 $(DEB_ROOT)/DEBIAN/prerm
	@# postrm - reload systemd after removal
	@echo "#!/bin/sh" > $(DEB_ROOT)/DEBIAN/postrm
	@echo "set -e" >> $(DEB_ROOT)/DEBIAN/postrm
	@echo "systemctl daemon-reload" >> $(DEB_ROOT)/DEBIAN/postrm
	@chmod 755 $(DEB_ROOT)/DEBIAN/postrm
	dpkg-deb --build --root-owner-group $(DEB_ROOT)
	@echo "Package created: $(DEB_ROOT).deb"

# Build macOS package
LAUNCHD_LABEL = net.ausimian.tundra.server
pkg: $(TARGET)
	@echo "Building macOS package..."
	rm -rf $(OSX_ROOT)
	mkdir -p $(OSX_ROOT)/usr/local/bin
	mkdir -p $(OSX_ROOT)/Library/LaunchDaemons
	mkdir -p $(OSX_DIR)/scripts
	cp $(TARGET) $(OSX_ROOT)/usr/local/bin/
	chmod 755 $(OSX_ROOT)/usr/local/bin/$(TARGET)
	@# Create uninstall script
	@echo '#!/bin/bash' > $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'set -e' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'if [ "$$(id -u)" -ne 0 ]; then echo "Must run as root"; exit 1; fi' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'echo "Stopping $(LAUNCHD_LABEL)..."' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'launchctl bootout system/$(LAUNCHD_LABEL) 2>/dev/null || true' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'echo "Removing files..."' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'rm -f /Library/LaunchDaemons/$(LAUNCHD_LABEL).plist' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'rm -f /usr/local/bin/$(TARGET)' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'rm -f /var/log/tundra-server.log' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'rm -f /usr/local/bin/uninstall-tundra-server' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'pkgutil --forget $(LAUNCHD_LABEL) >/dev/null 2>&1 || true' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@echo 'echo "Tundra server uninstalled."' >> $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	chmod 755 $(OSX_ROOT)/usr/local/bin/uninstall-tundra-server
	@# Create LaunchDaemon plist
	@echo '<?xml version="1.0" encoding="UTF-8"?>' > $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '<plist version="1.0">' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '<dict>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <key>Label</key>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <string>$(LAUNCHD_LABEL)</string>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <key>ProgramArguments</key>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <array>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '        <string>/usr/local/bin/$(TARGET)</string>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    </array>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <key>RunAtLoad</key>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <true/>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <key>KeepAlive</key>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <true/>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <key>StandardErrorPath</key>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <string>/var/log/tundra-server.log</string>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <key>StandardOutPath</key>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '    <string>/var/log/tundra-server.log</string>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '</dict>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@echo '</plist>' >> $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	chmod 644 $(OSX_ROOT)/Library/LaunchDaemons/$(LAUNCHD_LABEL).plist
	@# preinstall - unload existing daemon before upgrade
	@echo '#!/bin/bash' > $(OSX_DIR)/scripts/preinstall
	@echo 'launchctl bootout system/$(LAUNCHD_LABEL) 2>/dev/null || true' >> $(OSX_DIR)/scripts/preinstall
	@echo 'exit 0' >> $(OSX_DIR)/scripts/preinstall
	chmod 755 $(OSX_DIR)/scripts/preinstall
	@# postinstall - load the daemon
	@echo '#!/bin/bash' > $(OSX_DIR)/scripts/postinstall
	@echo 'launchctl bootstrap system /Library/LaunchDaemons/$(LAUNCHD_LABEL).plist' >> $(OSX_DIR)/scripts/postinstall
	@echo 'exit 0' >> $(OSX_DIR)/scripts/postinstall
	chmod 755 $(OSX_DIR)/scripts/postinstall
	pkgbuild \
		--root $(OSX_ROOT) \
		--identifier $(LAUNCHD_LABEL) \
		--version $(VERSION) \
		--scripts $(OSX_DIR)/scripts \
		--install-location / \
		$(PKG_DIR)/$(PACKAGE_NAME)-$(VERSION)-$(ARCH).pkg
	@echo "Package created: $(PKG_DIR)/$(PACKAGE_NAME)-$(VERSION)-$(ARCH).pkg"
	@echo "To uninstall: sudo uninstall-tundra-server"
