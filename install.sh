#!/bin/bash

# --- Configuration & Colors ---
BINARY_NAME="lumos"
GUI_NAME="lumos-gui.py"
TUI_NAME="lumos-tui"
INSTALL_PATH="/usr/local/bin/$BINARY_NAME"
GUI_INSTALL_PATH="/usr/local/bin/$GUI_NAME"
TUI_INSTALL_PATH="/usr/local/bin/$TUI_NAME"
SERVICE_PATH="/etc/systemd/system/${BINARY_NAME}.service"
DESKTOP_ENTRY_PATH="/usr/share/applications/lumos-gui.desktop"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}### Lumos Installation Wizard ###${NC}"
echo "Starting system-wide (Root) installation..."
echo ""

ask_yes_no() {
    while true; do
        read -p "$1 [y/N]: " yn
        case $yn in
            [Yy]* ) return 0;;
            [Nn]* | "" ) return 1;;
            * ) echo "Please answer yes or no.";;
        esac
    done
}

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

# Check for NCurses (for TUI)
INSTALL_TUI=false
if ask_yes_no "Do you want to install the Terminal UI (lumos-tui)?"; then
    INSTALL_TUI=true
    echo -e "${YELLOW}[1.5/5] Checking build dependencies (NCurses)...${NC}"
    # Simple check for ncurses header
    if ! echo "#include <ncurses.h>" | gcc -E -xc - >/dev/null 2>&1; then
        echo -e "${RED}Error: NCurses headers not found.${NC}"
        echo "Please install ncurses development libraries:"
        echo -e "  ${YELLOW}Fedora:${NC} sudo dnf install ncurses-devel"
        echo -e "  ${YELLOW}Debian/Ubuntu:${NC} sudo apt install libncurses-dev"
        exit 1
    fi
else
    echo "Skipping TUI installation."
fi

# Check for PyQt6
INSTALL_GUI=false
if ask_yes_no "Do you want to install the Desktop GUI (Lumos Control)?"; then
    INSTALL_GUI=true
    echo -e "${YELLOW}[1.6/5] Checking Python dependencies...${NC}"
    if ! python3 -c "from PyQt6 import QtWidgets" &> /dev/null; then
        echo -e "${RED}Error: PyQt6 module (QtWidgets) not found.${NC}"
        echo "Lumos GUI requires PyQt6. Please install it using your package manager or pip:"
        echo -e "  ${YELLOW}Fedora:${NC} sudo dnf install python3-pyqt6"
        echo -e "  ${YELLOW}Debian/Ubuntu:${NC} sudo apt install python3-pyqt6"
        echo -e "  ${YELLOW}Pip:${NC} pip install PyQt6"
        exit 1
    fi
    echo "PyQt6 is installed."
else
    echo "Skipping GUI installation."
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

# Install Config
if [ ! -f "/etc/lumos.conf" ]; then
    echo "Installing default config to /etc/lumos.conf..."
    sudo cp "lumos.conf" "/etc/lumos.conf"
    sudo chmod 644 "/etc/lumos.conf"
else
    echo "Config file already exists at /etc/lumos.conf. Skipping overwrite."
fi

echo "Desktop entry created at: $DESKTOP_ENTRY_PATH"
echo "PyQt6 is installed."

echo "TUI installed to: $TUI_INSTALL_PATH"

# 4.5 TUI INSTALLATION
if [ "$INSTALL_TUI" = true ]; then
    echo -e "${YELLOW}[4.5] Installing TUI...${NC}"
    sudo cp "$TUI_NAME" "$TUI_INSTALL_PATH"
    sudo chmod +x "$TUI_INSTALL_PATH"
    echo "TUI installed to: $TUI_INSTALL_PATH"
fi

# 4.6 GUI INSTALLATION
    echo -e "${YELLOW}[4.6] Installing GUI...${NC}"
    sudo cp "$GUI_NAME" "$GUI_INSTALL_PATH"
    sudo chmod +x "$GUI_INSTALL_PATH"
    
    # Generate and install desktop entry dynamically
    echo "Creating desktop entry..."
    sudo bash -c "cat > $DESKTOP_ENTRY_PATH" <<EOF
[Desktop Entry]
Name=Lumos Control
Comment=Configure Lumos Auto-Brightness
Exec=$GUI_INSTALL_PATH
Icon=brightness-high
Terminal=false
Type=Application
Categories=Settings;HardwareSettings;
EOF
    sudo chmod 644 "$DESKTOP_ENTRY_PATH"
    
    # Update desktop database cache
    if command -v update-desktop-database &> /dev/null; then
        sudo update-desktop-database /usr/share/applications/
    fi
    
    echo "GUI installed to: $GUI_INSTALL_PATH"
    echo "Desktop entry created at: $DESKTOP_ENTRY_PATH"
fi

# 4. SERVICE CONFIGURATION
echo -e "${YELLOW}[4/5] Creating Systemd service...${NC}"

# Dynamically create the service file
sudo bash -c "cat > $SERVICE_PATH" <<EOF
[Unit]
Description=Lumos Intelligent Auto-Brightness
After=systemd-user-sessions.service

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