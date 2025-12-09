# Lumos

**Lumos** is a lightweight, efficient, and dependency-free auto-brightness daemon for Linux laptops.

Unlike other solutions that rely on heavy Python libraries (like OpenCV) or GUI tools, Lumos is written in **pure C**. It utilizes the **V4L2 (Video4Linux2)** API to capture ambient light data from your webcam and adjusts your screen brightness via the **Sysfs** interface.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C-orange.svg)

Note: Lumos is designed for laptops and integrated screens managed via ACPI/GPU drivers. It does not support external monitors (HDMI/DP) controlled via DDC/CI.

## Features

* **Zero Bloat:** No Python, No OpenCV, No GStreamer. Just standard C libraries.
* **Ultra Lightweight:** The compiled binary is ~20KB and uses negligible RAM/CPU.
* **Privacy Focused:** Captures data in RAM, calculates the average "Luma" value, and discards the frame immediately. No images are ever saved to disk.
* **Smart:** Automatically detects your system's backlight controller (`intel_backlight`, `amdgpu_bl0`, etc.).
* **Systemd Integrated:** Runs silently in the background as a service.
* **Hardware Optimized:** Requests **YUYV** format directly from the webcam to extract brightness data without expensive color conversion.

## Requirements

* A Linux distribution (Fedora, Ubuntu, Arch, etc.)
* A webcam (default: `/dev/video0`)
* `gcc` and `make` (for compilation)
* Backlight control interface at `/sys/class/backlight/`

## Installation

### Quick Install (Recommended)

Lumos comes with an installation script that handles compilation, udev rules, and service creation.

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

The script will:
* Compile the source code.
* Install the binary to `/usr/local/bin/lumos`.
* Enable and start the systemd user service.

### Manual Build

If you prefer to build it yourself:

```bash
# Compile
make

# Run manually (Testing)
./lumos -v -i 5
````

##  Usage & Configuration

Lumos runs automatically in the background. However, you can modify its behavior by editing the systemd service file or running it manually.

**Command Line Arguments:**

```text
Usage: lumos [OPTIONS]

Options:
  -i <seconds>   Set the check interval (Default: 60 seconds)
  -v             Verbose mode (Enable logging to stdout)
  -h             Show help message
```

**Changing the Check Interval:**

To change how often Lumos checks for light (e.g., every 30 seconds):

1.  Edit the service file:
    ```bash
    nano /etc/systemd/system/lumos.service
    ```
2.  Modify the `ExecStart` line:
    ```ini
    ExecStart=/usr/local/bin/lumos -i 30
    ```
3.  Reload and restart:
    ```bash
    systemctl daemon-reload
    systemctl restart lumos
    ```

## How It Works

1.  **Auto-Discovery:** On startup, Lumos scans `/sys/class/backlight` to find the active display driver.
2.  **V4L2 Capture:** It connects to the webcam using the Video4Linux2 API. It specifically requests a low-resolution frame in **YUYV** format.
3.  **Luma Calculation:** Since YUYV format separates brightness (Y) from color (UV), Lumos simply averages the 'Y' bytes. This avoids CPU-intensive RGB-to-Grayscale conversion.
4.  **Hysteresis:** To prevent screen flickering, the brightness is only updated if the calculated change exceeds a 5% threshold.

## Uninstall

To remove Lumos completely from your system:

```bash
# Stop the service
sudo systemctl stop lumos
sudo systemctl disable lumos

# Remove files
rm /etc/systemd/system/lumos.service
sudo rm /usr/local/bin/lumos

# Reload system
sudo systemctl daemon-reload
```

## Contributing

Pull requests are welcome\! For major changes, please open an issue first to discuss what you would like to change.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
