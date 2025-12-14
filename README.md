# Lumos

**Lumos** is a lightweight, intelligent auto-brightness daemon for Linux laptops. It adjusts your screen brightness based on ambient light captured from your webcam, without saving any images.

Now featuring real-time control, a desktop GUI, and a terminal interface (TUI).

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C-orange.svg)

---

## Features

* **Zero Bloat:** Core daemon written in pure C. ~20KB binary.
* **Real-time & Instant:** Adjustments made in the GUI/TUI apply immediately.
* **Privacy Focused:** Captures data in RAM, calculates "Luma", and discards the frame. No images saved.
* **Dual Modes:**
    * **Auto:** Adjusts brightness based on ambient light.
    * **Manual:** Set a fixed brightness level when you need it.
* **Multiple Interfaces:**
    * **Daemon:** Runs silently in the background.
    * **GUI:** Qt6-based desktop application for easy configuration.
    * **TUI:** NCurses-based terminal interface for keyboard control.
* **Smart:** Automatically detects backlight controllers (`intel_backlight`, `amdgpu_bl0`).

## Requirements

* Linux distribution with `systemd` and `udev`.
* A webcam (default: `/dev/video0`).
* Backlight control interface at `/sys/class/backlight/`.

**Build Dependencies:**
* `gcc`, `make`
* `ncurses-devel` (or `libncurses-dev`) - *Only for TUI*
* `python3-pyqt6` - *Only for GUI*

## Installation

### Automated Install (Recommended)

The included script handles compilation, dependency checks, and service setup. It will ask if you want to install optional components (GUI/TUI).

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/anilaras/lumos.git
    cd lumos
    ```

2.  **Run the installer:**
    ```bash
    chmod +x install.sh
    sudo ./install.sh
    ```

3.  **Follow the prompts:**
    *   **Terminal UI (lumos-tui):** Type `y` to install. Requires `ncurses`.
    *   **Desktop GUI (Lumos Control):** Type `y` to install. Requires `PyQt6`.

This will:
* Compile and install the `lumos` Daemon (Mandatory).
* Install selected optional components.
* Enable and start the systemd service.

## Usage

### 1. Desktop GUI (Lumos Control)

Launch **Lumos Control** from your application menu or run:
```bash
lumos-gui.py
```

* **Auto Brightness:** Toggle to enable/disable ambient light detection.
* **Manual Brightness:** Set a fixed brightness level (disables Auto).
* **Sensitivity:** Adjust how aggressively the brightness changes in Auto mode.
* **Offset:** Add a constant value to the calculated brightness.
* **Save (Persist):** Writes current settings to `/etc/lumos.conf`.

### 2. Terminal UI (TUI)

For keyboard-driven control or SSH sessions:
```bash
lumos-tui
```

* **Arrow Keys:** Navigate (Up/Down) and Adjust (Left/Right).
* **S / Enter:** Save configuration.
* **Q:** Quit.

### 3. Daemon Configuration

Settings are stored in `/etc/lumos.conf`. While the GUI/TUI is recommended, you can edit this file manually:

```ini
# /etc/lumos.conf
mode=auto             # or 'manual'
manual_brightness=50  # 0-100
interval=60           # Check interval in seconds
sensitivity=1.0       # Multiplier (>1.0 brighter, <1.0 dimmer)
brightness_offset=0   # Constant adder
min_brightness=5
max_brightness=100
```

After manual edits, restart the service or send a signal, but using the GUI/TUI is easier as they reload the daemon automatically.

## Uninstall

To remove Lumos completely:

```bash
sudo systemctl stop lumos
sudo systemctl disable lumos
sudo rm /etc/systemd/system/lumos.service
sudo rm /usr/local/bin/lumos /usr/local/bin/lumos-tui /usr/local/bin/lumos-gui.py
sudo rm /usr/share/applications/lumos-gui.desktop
sudo rm /etc/lumos.conf
sudo systemctl daemon-reload
```

## Contributing

Pull requests are welcome!

## License

MIT License. See LICENSE file for details.
