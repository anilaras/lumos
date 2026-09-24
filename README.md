# Lumos

**Lumos** is a lightweight, intelligent auto-brightness daemon for Linux laptops and desktops. It adjusts your screen brightness based on ambient light captured from your webcam, without saving any images.

Now featuring real-time control, a desktop GUI, and a terminal interface (TUI).

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C-orange.svg)

---

## Features

* **Lightweight:** Core daemon written in C, with optional external monitor support through `ddcutil`.
* **Real-time & Instant:** Adjustments made in the GUI/TUI apply immediately.
* **Privacy Focused:** Captures data in RAM, calculates "Luma", and discards the frame. No images saved.
* **Dual Modes:**
    * **Auto:** Adjusts brightness based on ambient light.
    * **Manual:** Set a fixed brightness level when you need it.
* **Multiple Interfaces:**
    * **Daemon:** Runs silently in the background.
    * **GUI:** Qt6-based desktop application for easy configuration.
    * **TUI:** NCurses-based terminal interface for keyboard control.
* **Display support:** Automatically detects internal backlights (`intel_backlight`, `amdgpu_bl0`) and external DDC/CI monitors, applying the same brightness percentage to both.

## Requirements

* Linux distribution with `systemd` and `udev`.
* A webcam for automatic brightness (default: `/dev/video0`).
* A backlight interface at `/sys/class/backlight/`, or an external monitor with DDC/CI brightness support.
* `ddcutil` for external monitors (optional for internal backlights).

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
    ./install.sh
    ```

3.  **Follow the prompts:**
    *   **External monitors (ddcutil):** If missing, the installer offers to install it using `apt-get`, `dnf`, `pacman`, or `zypper`. Answer `y` to approve the displayed command. Declining or a failed installation still allows internal-backlight-only setups.
    *   **Terminal UI (lumos-tui):** Type `y` to install. Requires `ncurses`.
    *   **Desktop GUI (Lumos Control):** Type `y` to install. Requires `PyQt6`.

This will:
* Compile and install the `lumos` Daemon (Mandatory).
* Install selected optional components.
* Enable and start the systemd service.

Run the installer as your desktop user; it uses `sudo` for system files and the service. The GUI launcher is installed only for that user in `${XDG_DATA_HOME:-$HOME/.local/share}/applications/lumos-gui.desktop`, avoiding writes to `/usr/share/applications` on immutable distributions. Running `sudo ./install.sh` is also supported: the launcher is created as the original user, using their home directory by default. To use a custom `XDG_DATA_HOME`, run `./install.sh` so that `sudo` does not filter that environment variable.

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

### External monitors (DDC/CI)

The installer can install `ddcutil` after asking for confirmation on supported mutable distributions. On immutable systems (including OSTree/bootc systems and SteamOS), it shows host installation guidance instead of running a mutable-system package manager. You can also install `ddcutil` yourself; see the [ddcutil package documentation](https://www.ddcutil.com/install/). Systems without an internal backlight require `ddcutil` before installation can continue.

Enable DDC/CI in your monitor's on-screen settings, then check detection and brightness support:

```bash
sudo ddcutil detect --brief
sudo ddcutil --bus 7 getvcp 10 --terse
```

Replace `7` with the I2C bus reported for your monitor. If no I2C devices are available, load the driver with `sudo modprobe i2c-dev`; see the [ddcutil setup documentation](https://www.ddcutil.com/config/). The Lumos system service runs as root; a daemon started manually needs write access to the backlight and/or the monitor's I2C device.

Lumos applies the same target percentage to the internal screen and all detected DDC/CI monitors in both automatic and manual modes. It reads each monitor's brightness range before setting VCP feature `10`, so monitors with a maximum other than 100 are scaled correctly. See [ddcutil's brightness value format](https://www.ddcutil.com/command_getvcp/). GUI and TUI controls work for either output type, including systems without an internal backlight. Automatic mode still requires a webcam.

Connected monitors are rescanned during brightness updates, at most once every 30 seconds. Failed or unsupported monitors are skipped independently; slow DDC commands time out after 10 seconds. Use `lumos -v` for DDC diagnostics. Equal percentages do not necessarily produce equal perceived brightness on different panels.

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

To remove Lumos completely, run these commands as the desktop user who installed it:

```bash
sudo systemctl stop lumos
sudo systemctl disable lumos
sudo rm /etc/systemd/system/lumos.service
sudo rm /usr/local/bin/lumos /usr/local/bin/lumos-tui /usr/local/bin/lumos-gui.py
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/lumos-gui.desktop"
sudo rm /etc/lumos.conf
sudo systemctl daemon-reload
```

If you installed an older version, its system-wide launcher may still exist at `/usr/share/applications/lumos-gui.desktop`. Remove it with `sudo rm /usr/share/applications/lumos-gui.desktop` if that directory is writable.

## Contributing

Pull requests are welcome!

## License

MIT License. See LICENSE file for details.
