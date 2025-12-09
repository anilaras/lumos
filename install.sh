#!/bin/bash

# --- Configuration & Colors ---
BINARY_NAME="lumos"
INSTALL_PATH="/usr/local/bin/$BINARY_NAME"
SERVICE_PATH="/etc/systemd/system/${BINARY_NAME}.service"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}### Lumos Installation Wizard ###${NC}"
echo "Starting system-wide (Root) installation..."
echo ""

# 1. PREREQUISITE CHECKS
echo -e "${YELLOW}[1/5] Checking prerequisites...${NC}"

# Check for Backlight support
if [ -z "$(ls -A /sys/class/backlight/ 2>/dev/null)" ]; then
    echo -e "${RED}Error: No backlight driver found in /sys/class/backlight/.${NC}"
    echo "Lumos requires a controllable backlight interface (e.g., laptop screen)."
    exit 1
fi

# Check for Make and GCC
if ! command -v make &> /dev/null || ! command -v gcc &> /dev/null; then
    echo -e "${RED}Error: 'make' or 'gcc' not found.${NC}"
    echo "Please install build tools first (e.g., sudo dnf install make gcc)."
    exit 1
fi

echo "System is ready."

# 2. COMPILATION
echo -e "${YELLOW}[2/5] Compiling source code...${NC}"
make clean 2>/dev/null
make

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Compilation failed.${NC}"
    exit 1
fi
echo "Compilation successful."

# 3. BINARY INSTALLATION
echo -e "${YELLOW}[3/5] Installing binary...${NC}"

# Stop existing service if running
if systemctl is-active --quiet $BINARY_NAME; then
    echo "Stopping existing service..."
    sudo systemctl stop $BINARY_NAME
fi

sudo cp "$BINARY_NAME" "$INSTALL_PATH"
sudo chmod +x "$INSTALL_PATH"

echo "Installed to: $INSTALL_PATH"

# 4. SERVICE CONFIGURATION
echo -e "${YELLOW}[4/5] Creating Systemd service...${NC}"

# Dynamically create the service file
sudo bash -c "cat > $SERVICE_PATH" <<EOF
[Unit]
Description=Lumos Intelligent Auto-Brightness
After=graphical.target systemd-user-sessions.service

[Service]
Type=simple
# Run the C binary (default interval: 60s)
ExecStart=$INSTALL_PATH -i 60
Restart=on-failure
RestartSec=5

# Run as Root to bypass permission issues
User=root
Group=root

[Install]
WantedBy=multi-user.target
EOF

echo "Service file created: $SERVICE_PATH"

# 5. ACTIVATION
echo -e "${YELLOW}[5/5] Enabling service...${NC}"

sudo systemctl daemon-reload
sudo systemctl enable $BINARY_NAME
sudo systemctl restart $BINARY_NAME

# FINAL STATUS
if systemctl is-active --quiet $BINARY_NAME; then
    echo ""
    echo -e "${GREEN}INSTALLATION SUCCESSFUL!${NC}"
    echo "------------------------------------------------"
    echo "Lumos is now running in the background."
    echo "Check status: sudo systemctl status $BINARY_NAME"
    echo "View logs:    sudo journalctl -u $BINARY_NAME -f"
else
    echo -e "${RED}Warning: Service installed but failed to start.${NC}"
    echo "Please check errors with: sudo systemctl status $BINARY_NAME"
fi