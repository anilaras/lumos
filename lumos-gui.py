#!/usr/bin/env python3
import sys
import socket
import json
from PyQt6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                             QLabel, QSlider, QPushButton, QMessageBox, QFrame, QCheckBox)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QFont, QIcon

try:
    from PyQt6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                                 QLabel, QSlider, QPushButton, QMessageBox, QFrame, QCheckBox)
except ImportError:
    pass # Handled by main try/except block usually, but being safe

SOCKET_PATH = "/run/lumos.sock"

class LumosGUI(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Lumos Control")
        self.setFixedWidth(400)
        
        # Try setting icon safely
        if QIcon.hasThemeIcon("brightness-high"):
             self.setWindowIcon(QIcon.fromTheme("brightness-high"))
        
        layout = QVBoxLayout()
        layout.setSpacing(10)
        layout.setContentsMargins(20, 20, 20, 20)
        self.setLayout(layout)

        # Title
        title = QLabel("Lumos Settings")
        title.setFont(QFont("Sans", 16, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        # --- Manual Control Section ---
        manual_frame = QFrame()
        manual_frame.setFrameShape(QFrame.Shape.StyledPanel)
        manual_layout = QVBoxLayout()
        manual_frame.setLayout(manual_layout)
        
        self.auto_checkbox = QCheckBox("Enable Auto Brightness")
        self.auto_checkbox.setFont(QFont("Sans", 11, QFont.Weight.Bold))
        self.auto_checkbox.clicked.connect(self.on_auto_toggled)
        manual_layout.addWidget(self.auto_checkbox)
        
        self.manual_slider = self.create_slider("Manual Brightness", 0, 100, 50, self.on_manual_brightness_change)
        manual_layout.addLayout(self.manual_slider.parent_layout) # Use the layout returned/stored
        
        layout.addWidget(manual_frame)
        
        # --- Config Section ---
        config_label = QLabel("Auto Configuration")
        config_label.setStyleSheet("color: #666; font-weight: bold; margin-top: 10px;")
        layout.addWidget(config_label)
        
        self.sensitivity_slider = self.create_slider("Sensitivity", 1, 30, 10, self.on_sensitivity_change, float_scale=10.0)
        layout.addLayout(self.sensitivity_slider.parent_layout)
        
        self.offset_slider = self.create_slider("Brightness Offset", -50, 50, 0, self.on_offset_change)
        layout.addLayout(self.offset_slider.parent_layout)
        
        self.min_slider = self.create_slider("Min Brightness (%)", 0, 100, 5, self.on_min_change)
        layout.addLayout(self.min_slider.parent_layout)
        
        self.max_slider = self.create_slider("Max Brightness (%)", 0, 100, 100, self.on_max_change)
        layout.addLayout(self.max_slider.parent_layout)

        # Buttons
        btn_layout = QHBoxLayout()
        btn_layout.setContentsMargins(0, 10, 0, 0)
        
        refresh_btn = QPushButton("Refresh")
        refresh_btn.clicked.connect(self.refresh_config)
        btn_layout.addWidget(refresh_btn)

        save_btn = QPushButton("Save (Persist)")
        save_btn.clicked.connect(self.persist_config)
        save_btn.setStyleSheet("background-color: #2ecc71; color: white; font-weight: bold; padding: 8px;")
        btn_layout.addWidget(save_btn)

        layout.addLayout(btn_layout)

        # Status Bar
        self.status_label = QLabel("Ready")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status_label.setStyleSheet("color: gray;")
        layout.addWidget(self.status_label)

        # Initial Load
        self.refresh_config()

    def create_slider(self, label_text, min_val, max_val, default_val, callback, float_scale=1.0):
        VBox = QVBoxLayout()
        
        header = QHBoxLayout()
        lbl = QLabel(label_text)
        # lbl.setFont(QFont("Sans", 10, QFont.Weight.Bold))
        val_lbl = QLabel(str(default_val / float_scale))
        header.addWidget(lbl)
        header.addStretch()
        header.addWidget(val_lbl)
        VBox.addLayout(header)

        slider = QSlider(Qt.Orientation.Horizontal)
        slider.setMinimum(min_val)
        slider.setMaximum(max_val)
        slider.setValue(default_val)
        slider.setTickPosition(QSlider.TickPosition.TicksBelow)
        slider.setTickInterval(int((max_val - min_val) / 10))
        
        # Monkey patch value label to slider for easy access
        slider.val_lbl = val_lbl 
        slider.float_scale = float_scale
        slider.parent_layout = VBox # Store layout to add it later
        
        slider.valueChanged.connect(lambda v: self.update_label(slider, v))
        slider.sliderReleased.connect(lambda: callback(slider.value()))
        
        VBox.addWidget(slider)
        
        return slider

    def update_label(self, slider, value):
        display_val = value / slider.float_scale
        slider.val_lbl.setText(f"{display_val:.1f}" if slider.float_scale > 1.0 else str(int(display_val)))

    def send_cmd(self, cmd):
        try:
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.settimeout(2.0)
            client.connect(SOCKET_PATH)
            client.sendall(cmd.encode())
            response = client.recv(1024).decode().strip()
            client.close()
            return response
        except Exception as e:
            self.status_label.setText(f"Error: {e}")
            self.status_label.setStyleSheet("color: red;")
            return None

    def on_auto_toggled(self, checked):
        if checked:
            self.send_cmd("SET mode auto")
            self.manual_slider.setEnabled(False)
        else:
            self.send_cmd("SET mode manual")
            self.manual_slider.setEnabled(True)
            # Send current manual slider value just in case
            self.send_cmd(f"SET brightness {self.manual_slider.value()}")

    def on_manual_brightness_change(self, val):
        # Setting brightness implicitly switches to manual
        self.auto_checkbox.setChecked(False) 
        self.send_cmd(f"SET brightness {val}")

    def on_sensitivity_change(self, val):
        real_val = val / 10.0
        self.send_cmd(f"SET sensitivity {real_val:.2f}")

    def on_offset_change(self, val):
        self.send_cmd(f"SET brightness_offset {val}")

    def on_min_change(self, val):
        self.send_cmd(f"SET min_brightness {val}")

    def on_max_change(self, val):
        self.send_cmd(f"SET max_brightness {val}")

    def persist_config(self):
        resp = self.send_cmd("PERSIST")
        if resp == "SAVED":
            self.status_label.setText("Configuration saved to disk.")
            self.status_label.setStyleSheet("color: green;")
            QTimer.singleShot(3000, lambda: self.status_label.setText(""))
        else:
            self.status_label.setText(f"Save failed: {resp}")

    def refresh_config(self):
        keys = ["mode", "manual_brightness", "min_brightness", "max_brightness", "brightness_offset", "sensitivity"]
        try:
            # Need to get mode first to set checkbox correctly
            mode_resp = self.send_cmd("GET mode")
            if mode_resp and not mode_resp.startswith("ERR"):
                is_auto = (mode_resp == "auto")
                self.auto_checkbox.setChecked(is_auto)
                self.manual_slider.setEnabled(not is_auto)

            manual_resp = self.send_cmd("GET manual_brightness")
            if manual_resp and not manual_resp.startswith("ERR"):
                val = int(manual_resp)
                self.manual_slider.blockSignals(True)
                self.manual_slider.setValue(val)
                self.update_label(self.manual_slider, val)
                self.manual_slider.blockSignals(False)

            # Other config
            for k in ["min_brightness", "max_brightness", "brightness_offset", "sensitivity"]:
                resp = self.send_cmd(f"GET {k}")
                if resp and not resp.startswith("ERR"):
                    val = resp
                    if k == "sensitivity":
                        self.sensitivity_slider.setValue(int(float(val) * 10))
                        self.update_label(self.sensitivity_slider, int(float(val) * 10))
                    elif k == "brightness_offset":
                        self.offset_slider.setValue(int(val))
                        self.update_label(self.offset_slider, int(val))
                    elif k == "min_brightness":
                        self.min_slider.setValue(int(val))
                        self.update_label(self.min_slider, int(val))
                    elif k == "max_brightness":
                        self.max_slider.setValue(int(val))
                        self.update_label(self.max_slider, int(val))
            
            self.status_label.setText("Settings loaded.")
            self.status_label.setStyleSheet("color: blue;")
        except Exception as e:
            print(e)
            self.status_label.setText("Failed to load settings.")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = LumosGUI()
    window.show()
    sys.exit(app.exec())
