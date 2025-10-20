# Makefile for PZEM-004T Monitor Service

# Compiler and flags
#СС = arm-linux-gnueabihf-gcc
CC = gcc
CFLAGS = -Wall -Wextra -O2
#CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lmodbus -lm
DEBUG_CFLAGS = -g -DDEBUG

# Directories
SRCDIR = src
BUILDDIR = build
BINDIR = bin
CONFIGDIR = config
SYSTEMDDIR = systemd

# Installation paths
PREFIX = /usr/local
BIN_INSTALL_DIR = $(PREFIX)/bin
CONFIG_INSTALL_DIR = /etc/pzem
SERVICE_INSTALL_DIR = /etc/systemd/system
LOG_DIR = /var/log/pzem

# Source files
SOURCES = $(SRCDIR)/pzem_monitor.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TARGET = $(BINDIR)/pzem_monitor

# Default target - build and create templates
all: allclean templates $(TARGET)

# Create directories
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@$(CC) $(CFLAGS) -c $< -o $@

# Link target
$(TARGET): $(OBJECTS) | $(BINDIR)
	@$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build completed: $(TARGET)"

# Debug build
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: clean $(TARGET)

# Create configuration and service templates
templates: | $(CONFIGDIR) $(SYSTEMDDIR)
	@echo "Creating template files..."
    
# Create default configuration
	@echo "# PZEM-004T Default Configuration" > $(CONFIGDIR)/pzem_default.conf
	@echo "" >> $(CONFIGDIR)/pzem_default.conf
	@echo "# Serial port settings" >> $(CONFIGDIR)/pzem_default.conf
	@echo "tty_port = /dev/ttyS1" >> $(CONFIGDIR)/pzem_default.conf
	@echo "baudrate = 9600" >> $(CONFIGDIR)/pzem_default.conf
	@echo "slave_addr = 1" >> $(CONFIGDIR)/pzem_default.conf
	@echo "poll_interval_ms = 500 # Диапазон периода 50 - 10000мс" >> $(CONFIGDIR)/pzem_default.conf
	@echo "" >> $(CONFIGDIR)/pzem_default.conf
	@echo "# Logging settings" >> $(CONFIGDIR)/pzem_default.conf
	@echo "log_dir = /var/log/pzem" >> $(CONFIGDIR)/pzem_default.conf
	@echo "log_buffer_size = 10  # Размер буфера логов в строках (1-25)" >> $(CONFIGDIR)/pzem_default.conf
	@echo "" >> $(CONFIGDIR)/pzem_default.conf
	@echo "# Sensitivity settings" >> $(CONFIGDIR)/pzem_default.conf
	@echo "voltage_sensitivity = 0.1" >> $(CONFIGDIR)/pzem_default.conf
	@echo "current_sensitivity = 0.001" >> $(CONFIGDIR)/pzem_default.conf
	@echo "frequency_sensitivity = 0.1" >> $(CONFIGDIR)/pzem_default.conf
	@echo "power_sensitivity = 1.0" >> $(CONFIGDIR)/pzem_default.conf
	@echo "" >> $(CONFIGDIR)/pzem_default.conf
	@echo "# Voltage thresholds (0 = disabled)" >> $(CONFIGDIR)/pzem_default.conf
	@echo "voltage_high_alarm = 245" >> $(CONFIGDIR)/pzem_default.conf
	@echo "voltage_high_warning = 240" >> $(CONFIGDIR)/pzem_default.conf
	@echo "voltage_low_warning = 210" >> $(CONFIGDIR)/pzem_default.conf
	@echo "voltage_low_alarm = 200" >> $(CONFIGDIR)/pzem_default.conf
	@echo "" >> $(CONFIGDIR)/pzem_default.conf
	@echo "# Current thresholds (0 = disabled)" >> $(CONFIGDIR)/pzem_default.conf
	@echo "current_high_alarm = 0" >> $(CONFIGDIR)/pzem_default.conf
	@echo "current_high_warning = 0" >> $(CONFIGDIR)/pzem_default.conf
	@echo "current_low_warning = 0" >> $(CONFIGDIR)/pzem_default.conf
	@echo "current_low_alarm = 0" >> $(CONFIGDIR)/pzem_default.conf
	@echo "" >> $(CONFIGDIR)/pzem_default.conf
	@echo "# Frequency thresholds (0 = disabled)" >> $(CONFIGDIR)/pzem_default.conf
	@echo "frequency_high_alarm = 52" >> $(CONFIGDIR)/pzem_default.conf
	@echo "frequency_high_warning = 51" >> $(CONFIGDIR)/pzem_default.conf
	@echo "frequency_low_warning = 49" >> $(CONFIGDIR)/pzem_default.conf
	@echo "frequency_low_alarm = 48" >> $(CONFIGDIR)/pzem_default.conf
    
# Create systemd service file
	@echo "[Unit]" > $(SYSTEMDDIR)/pzem@.service
	@echo "Description=PZEM-004T Monitor %i" >> $(SYSTEMDDIR)/pzem@.service
	@echo "After=network.target" >> $(SYSTEMDDIR)/pzem@.service
	@echo "" >> $(SYSTEMDDIR)/pzem@.service
	@echo "[Service]" >> $(SYSTEMDDIR)/pzem@.service
	@echo "Type=simple" >> $(SYSTEMDDIR)/pzem@.service
	@echo "User=root" >> $(SYSTEMDDIR)/pzem@.service
	@echo "ExecStart=$(BIN_INSTALL_DIR)/pzem_monitor /etc/pzem/%i.conf" >> $(SYSTEMDDIR)/pzem@.service
	@echo "Restart=always" >> $(SYSTEMDDIR)/pzem@.service
	@echo "RestartSec=5" >> $(SYSTEMDDIR)/pzem@.service
	@echo "StandardOutput=journal" >> $(SYSTEMDDIR)/pzem@.service
	@echo "StandardError=journal" >> $(SYSTEMDDIR)/pzem@.service
	@echo "SyslogIdentifier=pzem-%i" >> $(SYSTEMDDIR)/pzem@.service
	@echo "" >> $(SYSTEMDDIR)/pzem@.service
	@echo "[Install]" >> $(SYSTEMDDIR)/pzem@.service
	@echo "WantedBy=multi-user.target" >> $(SYSTEMDDIR)/pzem@.service
	@echo "Template files created in $(CONFIGDIR)/ and $(SYSTEMDDIR)/"

$(CONFIGDIR):
	@mkdir -p $(CONFIGDIR)

$(SYSTEMDDIR):
	@mkdir -p $(SYSTEMDDIR)

# Install the application and service
install: $(TARGET)
	@echo "Installing PZEM Monitor..."
    
# Create directories
	@mkdir -p $(DESTDIR)$(BIN_INSTALL_DIR)
	@mkdir -p $(DESTDIR)$(CONFIG_INSTALL_DIR)
	@mkdir -p $(DESTDIR)$(LOG_DIR)
    
# Install binary
	@install -m 755 $(TARGET) $(DESTDIR)$(BIN_INSTALL_DIR)/pzem_monitor
	@echo "Installed binary to $(DESTDIR)$(BIN_INSTALL_DIR)/pzem_monitor"
    
# Install default configuration if it doesn't exist
	@if [ ! -f $(DESTDIR)$(CONFIG_INSTALL_DIR)/default.conf ]; then \
	    install -m 644 $(CONFIGDIR)/pzem_default.conf $(DESTDIR)$(CONFIG_INSTALL_DIR)/default.conf; \
	    echo "Installed default configuration to $(DESTDIR)$(CONFIG_INSTALL_DIR)/default.conf"; \
	else \
	    echo "Configuration file already exists, skipping..."; \
	fi
    
# Install systemd service
	@install -m 644 $(SYSTEMDDIR)/pzem@.service $(DESTDIR)$(SERVICE_INSTALL_DIR)/pzem@.service
	@echo "Installed systemd service to $(DESTDIR)$(SERVICE_INSTALL_DIR)/pzem@.service"
    
# Reload systemd
	@systemctl daemon-reload
	@echo "Systemd daemon reloaded"
    
	@echo "Installation completed successfully!"
	@echo ""
	@echo "Usage examples:"
	@echo "  sudo systemctl start pzem@default"
	@echo "  sudo systemctl enable pzem@default"
	@echo "  sudo systemctl status pzem@default"

# Uninstall completely
uninstall:
	@echo "Uninstalling PZEM Monitor..."
    
# Stop and disable all pzem services
	@-for service in $$(systemctl list-units 'pzem@*' --all --no-legend | awk '{print $$1}'); do \
	    echo "Stopping and disabling $$service..."; \
	    systemctl stop $$service 2>/dev/null || true; \
	    systemctl disable $$service 2>/dev/null || true; \
	done
    
# Remove binary
	@-rm -f $(DESTDIR)$(BIN_INSTALL_DIR)/pzem_monitor
	@echo "Removed binary from $(DESTDIR)$(BIN_INSTALL_DIR)/pzem_monitor"
    
# Remove systemd service
	@-rm -f $(DESTDIR)$(SERVICE_INSTALL_DIR)/pzem@.service
	@echo "Removed systemd service from $(DESTDIR)$(SERVICE_INSTALL_DIR)/pzem@.service"
    
# Reload systemd
	@systemctl daemon-reload
	@echo "Systemd daemon reloaded"
    
# Note: Config files and log directory are not removed automatically
	@echo ""
	@echo "Uninstallation completed!"
	@echo "Note: Configuration files in $(CONFIG_INSTALL_DIR) and log files in $(LOG_DIR) were not removed."
	@echo "To remove them manually:"
	@echo "  sudo rm -rf $(CONFIG_INSTALL_DIR)"
	@echo "  sudo rm -rf $(LOG_DIR)"

# Clean build files
clean:
	@echo "Cleaning build files..."
	@-rm -rf $(BUILDDIR)
	@-rm -rf $(BINDIR)
	@echo "Clean completed"

# All clean - remove all generated files including templates
allclean: clean
	@echo "Removing all generated files including templates..."
	@-rm -rf $(CONFIGDIR)
	@-rm -rf $(SYSTEMDDIR)
	@echo "Allclean completed"

# Show help
help:
	@echo "PZEM-004T Monitor Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the application and create templates (default)"
	@echo "  debug     - Build with debug symbols"
	@echo "  templates - Create configuration and service templates"
	@echo "  install   - Install application and service to system"
	@echo "  uninstall - Remove application and service from system"
	@echo "  clean     - Remove build files"
	@echo "  allclean  - Remove all generated files including templates"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Installation paths:"
	@echo "  Binary:    $(BIN_INSTALL_DIR)/pzem_monitor"
	@echo "  Config:    $(CONFIG_INSTALL_DIR)/"
	@echo "  Service:   $(SERVICE_INSTALL_DIR)/pzem@.service"
	@echo "  Logs:      $(LOG_DIR)/"

# Show version info
version:
	@echo "PZEM-004T Monitor v1.0"
	@echo "Build system for ARM Linux"

# Default target
.DEFAULT_GOAL := all

# Phony targets
.PHONY: all debug install uninstall clean allclean help templates version