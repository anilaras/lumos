#!/bin/bash

# Renkler
GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${GREEN}Installing Lumos...${NC}"

# 1. Derleme
if ! command -v make &> /dev/null; then
    echo "Make not found. Installing..."
    # Distro tespiti yapılabilir ama şimdilik basit tutuyoruz
    echo "Please install 'make' and 'gcc' manually if this fails."
fi

echo "Compiling..."
make
if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

# 2. Binary Taşıma (Standard Linux Path)
echo "Installing binary to /usr/local/bin/..."
sudo cp lumos /usr/local/bin/
sudo chmod +x /usr/local/bin/lumos

# 3. Udev Kuralı (Dinamik Tespit)
echo "Configuring permissions..."
DRIVER=$(ls /sys/class/backlight/ | head -n 1)
if [ -z "$DRIVER" ]; then
    echo "Warning: No backlight driver found. Permissions might fail."
else
    echo "Detected driver: $DRIVER"
    RULE="SUBSYSTEM==\"backlight\", ACTION==\"add\", KERNEL==\"$DRIVER\", GROUP=\"video\", MODE=\"0664\""
    echo "$RULE" | sudo tee /etc/udev/rules.d/99-lumos.rules > /dev/null

    sudo usermod -aG video $USER
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=backlight
fi

# 4. Servis Kurulumu (User Service)
echo "Setting up Systemd service..."
mkdir -p ~/.config/systemd/user/
cp lumos.service ~/.config/systemd/user/

systemctl --user daemon-reload
systemctl --user enable lumos
systemctl --user restart lumos

echo -e "${GREEN}Installation Complete!${NC}"
echo "Lumos is running in the background."
echo "Check status with: systemctl --user status lumos"
