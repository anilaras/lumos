#!/bin/bash

# --- Configuration & Colors ---
BINARY_NAME="lumos"
GUI_NAME="lumos-gui.py"
TUI_NAME="lumos-tui"
INSTALL_PATH="/usr/local/bin/$BINARY_NAME"
GUI_INSTALL_PATH="/usr/local/bin/$GUI_NAME"
TUI_INSTALL_PATH="/usr/local/bin/$TUI_NAME"
SERVICE_PATH="/etc/systemd/system/${BINARY_NAME}.service"

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

offer_ddcutil_install() {
    echo "ddcutil is not installed. It is needed to control external DDC/CI monitors."

    # Host packages on immutable systems need distribution-specific handling.
    if [ -e /run/ostree-booted ] || command -v rpm-ostree &> /dev/null ||
       command -v bootc &> /dev/null || command -v steamos-readonly &> /dev/null; then
        echo "Automatic package installation is skipped on immutable systems."
        if command -v rpm-ostree &> /dev/null; then
            echo "If your distribution supports package layering, run: sudo rpm-ostree install ddcutil"
            echo "Reboot into the updated deployment, then rerun ./install.sh."
        else
            echo "Install ddcutil on the host using your distribution's supported method, then rerun ./install.sh."
        fi
        return
    fi

    local install_command=()
    if command -v apt-get &> /dev/null; then
        install_command=(apt-get install -y ddcutil)
    elif command -v dnf &> /dev/null; then
        install_command=(dnf install -y ddcutil)
    elif command -v pacman &> /dev/null; then
        install_command=(pacman -S --needed --noconfirm ddcutil)
    elif command -v zypper &> /dev/null; then
        install_command=(zypper --non-interactive install ddcutil)
    else
        echo "No supported package manager found. Install ddcutil manually, then rerun ./install.sh."
        return
    fi

    echo "Install command: sudo ${install_command[*]}"
    if ! ask_yes_no "Install ddcutil now for external monitor support?"; then
        echo "Skipping ddcutil installation."
        return
    fi

    if ! sudo "${install_command[@]}"; then
        echo -e "${YELLOW}Warning: ddcutil installation failed. Install it manually to enable external monitors.${NC}"
    elif ! command -v ddcutil &> /dev/null; then
        echo -e "${YELLOW}Warning: ddcutil is still unavailable. Check the package installation and PATH.${NC}"
    else
        echo "ddcutil is installed. Enable DDC/CI in your monitor settings."
    fi
}

# 1. PREREQUISITE CHECKS
echo -e "${YELLOW}[1/5] Checking prerequisites...${NC}"

if ! command -v ddcutil &> /dev/null; then
    offer_ddcutil_install
fi

# A DDC/CI monitor can provide brightness control without a sysfs backlight.
if [ -z "$(ls -A /sys/class/backlight/ 2>/dev/null)" ]; then
    if ! command -v ddcutil &> /dev/null; then
        echo -e "${RED}Error: No sysfs backlight found and ddcutil is not installed.${NC}"
        echo "For external monitors, install ddcutil and enable DDC/CI in the monitor settings."
        exit 1
    fi
    echo "Checking for DDC/CI monitors..."
    if ! DDC_DISPLAYS="$(sudo env LC_ALL=C ddcutil detect --brief)" ||
       ! grep -q '^Display [0-9]' <<< "$DDC_DISPLAYS"; then
        echo -e "${RED}Error: No controllable backlight or DDC/CI monitor found.${NC}"
        echo "Check DDC/CI settings and I2C access with: sudo ddcutil detect"
        exit 1
    fi
elif ! command -v ddcutil &> /dev/null; then
    echo "Continuing with internal backlight support only."
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
BUILD_TARGETS=("$BINARY_NAME")
if [ "$INSTALL_TUI" = true ]; then
    BUILD_TARGETS+=("$TUI_NAME")
fi
if ! make "${BUILD_TARGETS[@]}"; then
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

# 4.5 TUI INSTALLATION
if [ "$INSTALL_TUI" = true ]; then
    echo -e "${YELLOW}[4.5] Installing TUI...${NC}"
    sudo cp "$TUI_NAME" "$TUI_INSTALL_PATH"
    sudo chmod +x "$TUI_INSTALL_PATH"
    echo "TUI installed to: $TUI_INSTALL_PATH"
fi

# 4.6 GUI INSTALLATION
if [ "$INSTALL_GUI" = true ]; then
    echo -e "${YELLOW}[4.6] Installing GUI...${NC}"
    sudo cp "$GUI_NAME" "$GUI_INSTALL_PATH" || exit 1
    sudo chmod +x "$GUI_INSTALL_PATH" || exit 1

    # Keep the launcher in the invoking user's writable data directory,
    # including when the installer was started with sudo.
    DESKTOP_RUN=()
    DESKTOP_HOME="$HOME"
    if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
        DESKTOP_RUN=(sudo -u "$SUDO_USER" -H)
        DESKTOP_HOME="$("${DESKTOP_RUN[@]}" sh -c 'printf "%s" "$HOME"')" || exit 1
    fi

    # XDG paths must be absolute; ignore empty or relative values.
    case "${XDG_DATA_HOME:-}" in
        /*) DESKTOP_DATA_HOME="$XDG_DATA_HOME" ;;
        *) DESKTOP_DATA_HOME="$DESKTOP_HOME/.local/share" ;;
    esac
    DESKTOP_ENTRY_DIR="$DESKTOP_DATA_HOME/applications"
    DESKTOP_ENTRY_PATH="$DESKTOP_ENTRY_DIR/lumos-gui.desktop"
    
    # Generate and install desktop entry dynamically
    echo "Creating desktop entry..."
    "${DESKTOP_RUN[@]}" mkdir -p -- "$DESKTOP_ENTRY_DIR" || exit 1
    "${DESKTOP_RUN[@]}" tee "$DESKTOP_ENTRY_PATH" > /dev/null <<EOF || exit 1
[Desktop Entry]
Name=Lumos Control
Comment=Configure Lumos Auto-Brightness
Exec=$GUI_INSTALL_PATH
Icon=brightness-high
Terminal=false
Type=Application
Categories=Settings;HardwareSettings;
EOF
    "${DESKTOP_RUN[@]}" chmod 644 "$DESKTOP_ENTRY_PATH" || exit 1
    
    # Update desktop database cache
    if command -v update-desktop-database &> /dev/null; then
        "${DESKTOP_RUN[@]}" update-desktop-database "$DESKTOP_ENTRY_DIR"
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
