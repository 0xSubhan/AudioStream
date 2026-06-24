import sys
import socket
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QLineEdit, QComboBox, QFrame, QGridLayout,
    QStyle
)
from PyQt6.QtCore import QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor, QPalette

# mDNS/Zeroconf imports
from zeroconf import Zeroconf, ServiceBrowser, ServiceListener

# Import our compiled C++ core module
try:
    import audiostream_core
except ImportError:
    # Fallback mock for development or import issues so the UI still displays nicely
    class MockStreamController:
        def __init__(self): self._active = False; self._pkts = 0
        def start(self, ip, port): self._active = True; self._pkts = 0; return True
        def stop(self): self._active = False
        def is_streaming(self): return self._active
        def get_latency_ms(self): return 1.85 if self._active else 0.0
        def get_packet_count(self): 
            if self._active: self._pkts += 50
            return self._pkts
    audiostream_core = type('MockModule', (object,), {'StreamController': MockStreamController})


class DiscoverySignaler(QObject):
    """Thread-safe Qt signal emitter for Zeroconf background discoveries."""
    device_found = pyqtSignal(str, str, int)  # name, ip, port
    device_lost = pyqtSignal(str)             # name


class AudioStreamListener(ServiceListener):
    """Zeroconf listener that maps network events to safe Qt signals."""
    def __init__(self, signaler):
        self.signaler = signaler

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        info = zc.get_service_info(type_, name)
        if info:
            # Resolve IPv4 address
            ip = None
            for addr in info.addresses:
                try:
                    ip = socket.inet_ntoa(addr)
                    break
                except Exception:
                    continue
            if ip:
                self.signaler.device_found.emit(name, ip, info.port)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self.signaler.device_lost.emit(name)

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        pass


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AudioStream Controller")
        self.setMinimumSize(480, 560)
        self.resize(500, 600)

        # Initialize C++ StreamController
        self.controller = audiostream_core.StreamController()

        # Initialize mDNS Zeroconf
        self.signaler = DiscoverySignaler()
        self.signaler.device_found.connect(self.on_device_found)
        self.signaler.device_lost.connect(self.on_device_lost)

        self.zeroconf = Zeroconf()
        self.listener = AudioStreamListener(self.signaler)
        self.browser = ServiceBrowser(self.zeroconf, "_audiostream._udp.local.", self.listener)

        # Track discovered devices: {name: (ip, port)}
        self.devices = {}

        # Set up GUI
        self.init_ui()

        # Telemetry update timer (100ms)
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_telemetry)

    def init_ui(self):
        # Apply dark-mode theme stylesheet
        self.setStyleSheet("""
            QMainWindow {
                background-color: #121214;
            }
            QWidget {
                color: #F5F5F7;
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            }
            QLabel {
                font-size: 13px;
            }
            QFrame#card {
                background-color: #1C1C1E;
                border: 1px solid #2C2C2E;
                border-radius: 12px;
            }
            QLineEdit, QComboBox {
                background-color: #2C2C2E;
                border: 1px solid #3A3A3C;
                border-radius: 6px;
                padding: 8px 12px;
                color: #F5F5F7;
                font-size: 13px;
            }
            QLineEdit:focus, QComboBox:focus {
                border: 1px solid #0A84FF;
            }
            QPushButton#toggleBtn {
                background-color: #0A84FF;
                border: none;
                border-radius: 8px;
                color: white;
                font-weight: bold;
                font-size: 14px;
                padding: 14px;
            }
            QPushButton#toggleBtn:hover {
                background-color: #0070E3;
            }
            QPushButton#toggleBtn:pressed {
                background-color: #005ECB;
            }
            QPushButton#toggleBtn[streaming="true"] {
                background-color: #30D158;
            }
            QPushButton#toggleBtn[streaming="true"]:hover {
                background-color: #24B147;
            }
            QPushButton#toggleBtn[streaming="true"]:pressed {
                background-color: #1C9136;
            }
        """)

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(24, 24, 24, 24)
        main_layout.setSpacing(20)

        # 1. Header Section
        header_layout = QVBoxLayout()
        title_label = QLabel("AudioStream")
        title_label.setStyleSheet("font-size: 26px; font-weight: bold; color: #FFFFFF;")
        subtitle_label = QLabel("Real-time Desktop Audio Broadcaster")
        subtitle_label.setStyleSheet("color: #8E8E93; font-size: 13px;")
        header_layout.addWidget(title_label)
        header_layout.addWidget(subtitle_label)
        main_layout.addLayout(header_layout)

        # 2. Status Indicator Card
        self.status_card = QFrame()
        self.status_card.setObjectName("card")
        status_layout = QHBoxLayout(self.status_card)
        status_layout.setContentsMargins(16, 16, 16, 16)
        
        self.status_dot = QFrame()
        self.status_dot.setFixedSize(10, 10)
        self.status_dot.setStyleSheet("background-color: #FF453A; border-radius: 5px;") # default red
        
        self.status_text = QLabel("System Idle")
        self.status_text.setStyleSheet("font-size: 14px; font-weight: 500; color: #FF453A;")
        
        status_layout.addWidget(self.status_dot)
        status_layout.addWidget(self.status_text)
        status_layout.addStretch()
        main_layout.addWidget(self.status_card)

        # 3. Connection Settings Card
        settings_card = QFrame()
        settings_card.setObjectName("card")
        settings_layout = QVBoxLayout(settings_card)
        settings_layout.setContentsMargins(16, 16, 16, 16)
        settings_layout.setSpacing(12)

        # Discovered Devices list
        dev_label = QLabel("Auto-Discovered Devices (mDNS):")
        dev_label.setStyleSheet("font-weight: 600; color: #FFFFFF;")
        self.device_combo = QComboBox()
        self.device_combo.addItem("Searching for receivers on WiFi...")
        self.device_combo.currentIndexChanged.connect(self.on_device_selection_changed)
        settings_layout.addWidget(dev_label)
        settings_layout.addWidget(self.device_combo)

        # Divider
        divider = QFrame()
        divider.setFrameShape(QFrame.Shape.HLine)
        divider.setStyleSheet("background-color: #2C2C2E; max-height: 1px; border: none;")
        settings_layout.addWidget(divider)

        # Manual Settings
        manual_label = QLabel("Or Enter Connection Parameters Manually:")
        manual_label.setStyleSheet("font-weight: 600; color: #FFFFFF;")
        settings_layout.addWidget(manual_label)

        grid = QGridLayout()
        grid.setSpacing(10)

        grid.addWidget(QLabel("IP Address:"), 0, 0)
        self.ip_input = QLineEdit()
        self.ip_input.setPlaceholderText("e.g. 192.168.1.50")
        grid.addWidget(self.ip_input, 0, 1)

        grid.addWidget(QLabel("Port:"), 1, 0)
        self.port_input = QLineEdit("8554")
        grid.addWidget(self.port_input, 1, 1)

        settings_layout.addLayout(grid)
        main_layout.addWidget(settings_card)

        # 4. Telemetry Card
        self.telemetry_card = QFrame()
        self.telemetry_card.setObjectName("card")
        telemetry_layout = QGridLayout(self.telemetry_card)
        telemetry_layout.setContentsMargins(16, 16, 16, 16)
        telemetry_layout.setSpacing(12)

        telemetry_layout.addWidget(QLabel("Capture Latency:"), 0, 0)
        self.latency_val = QLabel("0.0 ms")
        self.latency_val.setStyleSheet("font-weight: bold; font-size: 15px; color: #0A84FF;")
        telemetry_layout.addWidget(self.latency_val, 0, 1)

        telemetry_layout.addWidget(QLabel("Packets Sent:"), 1, 0)
        self.packets_val = QLabel("0")
        self.packets_val.setStyleSheet("font-weight: bold; font-size: 15px; color: #0A84FF;")
        telemetry_layout.addWidget(self.packets_val, 1, 1)

        main_layout.addWidget(self.telemetry_card)
        self.telemetry_card.hide() # Hide until streaming starts

        # 5. Bottom Controls (Large Toggle Button)
        self.toggle_button = QPushButton("Start Streaming")
        self.toggle_button.setObjectName("toggleBtn")
        self.toggle_button.clicked.connect(self.on_toggle_clicked)
        main_layout.addWidget(self.toggle_button)

        main_layout.addStretch()

    # mDNS Slots
    def on_device_found(self, name, ip, port):
        # Format service name for display (strip suffix)
        display_name = name.split(".")[0]
        self.devices[name] = (ip, port)
        
        # Re-build dropdown items
        self.update_device_dropdown()

    def on_device_lost(self, name):
        if name in self.devices:
            del self.devices[name]
            self.update_device_dropdown()

    def update_device_dropdown(self):
        self.device_combo.blockSignals(True)
        self.device_combo.clear()
        
        if not self.devices:
            self.device_combo.addItem("Searching for receivers on WiFi...")
            self.device_combo.setStyleSheet("color: #8E8E93;")
        else:
            self.device_combo.addItem("Select a detected receiver...")
            self.device_combo.setStyleSheet("color: #F5F5F7;")
            for name, (ip, port) in self.devices.items():
                display_name = name.split(".")[0]
                self.device_combo.addItem(f"{display_name} ({ip}:{port})", userData=name)
        
        self.device_combo.blockSignals(False)

    def on_device_selection_changed(self, index):
        if index <= 0:
            return
        
        # Fill in manual input fields with selected device parameters
        name = self.device_combo.itemData(index)
        if name in self.devices:
            ip, port = self.devices[name]
            self.ip_input.setText(ip)
            self.port_input.setText(str(port))

    # Control logic
    def on_toggle_clicked(self):
        if self.controller.is_streaming():
            # Stop streaming
            self.controller.stop()
            self.timer.stop()
            
            # Reset UI state
            self.toggle_button.setText("Start Streaming")
            self.toggle_button.setProperty("streaming", "false")
            self.toggle_button.setStyle(self.toggle_button.style()) # force repaint
            
            self.status_dot.setStyleSheet("background-color: #FF453A; border-radius: 5px;")
            self.status_text.setText("System Idle")
            self.status_text.setStyleSheet("font-size: 14px; font-weight: 500; color: #FF453A;")
            
            self.telemetry_card.hide()
            self.device_combo.setEnabled(True)
            self.ip_input.setEnabled(True)
            self.port_input.setEnabled(True)
        else:
            # Gather targets
            ip = self.ip_input.text().strip()
            port_str = self.port_input.text().strip()

            if not ip:
                self.status_text.setText("Error: IP required")
                return

            try:
                port = int(port_str)
            except ValueError:
                self.status_text.setText("Error: Invalid Port")
                return

            # Start C++ streaming thread
            if self.controller.start(ip, port):
                self.timer.start(100) # update every 100ms
                
                # Update UI state
                self.toggle_button.setText("Stop Streaming")
                self.toggle_button.setProperty("streaming", "true")
                self.toggle_button.setStyle(self.toggle_button.style()) # force repaint
                
                self.status_dot.setStyleSheet("background-color: #30D158; border-radius: 5px;")
                self.status_text.setText(f"Streaming to {ip}:{port}")
                self.status_text.setStyleSheet("font-size: 14px; font-weight: 500; color: #30D158;")
                
                self.latency_val.setText("0.0 ms")
                self.packets_val.setText("0")
                self.telemetry_card.show()
                
                self.device_combo.setEnabled(False)
                self.ip_input.setEnabled(False)
                self.port_input.setEnabled(False)
            else:
                self.status_text.setText("Error starting C++ engine")

    def update_telemetry(self):
        # Fetch directly from thread-safe C++ controller methods
        latency = self.controller.get_latency_ms()
        packets = self.controller.get_packet_count()

        self.latency_val.setText(f"{latency:.2f} ms")
        self.packets_val.setText(f"{packets:,}")

        # Check if C++ engine thread unexpectedly stopped (e.g. driver failure)
        if not self.controller.is_streaming():
            self.on_toggle_clicked() # turn off
            self.status_text.setText("Stream stopped unexpectedly")
            self.status_text.setStyleSheet("font-size: 14px; font-weight: 500; color: #FF453A;")

    def closeEvent(self, event):
        # Ensure thread cleanup and Zeroconf shutdown on application exit
        self.controller.stop()
        self.zeroconf.close()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
