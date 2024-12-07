import sys
import serial
import json
import time
import vlc
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, QPushButton, QLabel, QWidget
)
from PyQt5.QtGui import QFont
from PyQt5.QtCore import QTimer, Qt, pyqtSignal, QThread, pyqtSlot

# 串口配置
ARDUINO_PORT = 'COM3'  # 替換為您的 Arduino 所連接的串口端口
BAUD_RATE = 115200

# 定義類別映射
categories = ["Close", "Open"]  # 根據您的模型類別進行調整

class SerialThread(QThread):
    """串口通信線程，負責與 Arduino 進行通信"""
    data_received = pyqtSignal(dict)

    def __init__(self, port, baud_rate):
        super().__init__()
        self.port = port
        self.baud_rate = baud_rate
        self.serial_conn = None
        self.running = True

    def run(self):
        try:
            self.serial_conn = serial.Serial(self.port, self.baud_rate, timeout=1)
            time.sleep(2)  # 等待 Arduino 重啟
            print(f"串口已打開：{self.port}，波特率：{self.baud_rate}")
        except Exception as e:
            print(f"無法打開串口：{e}")
            self.running = False
            return

        while self.running:
            if self.serial_conn.in_waiting > 0:
                line = self.serial_conn.readline().decode(errors='ignore').strip()
                if line:
                    print(f"從 Arduino 接收到數據：{line}")
                    try:
                        # 尝试解析 JSON 数据
                        data = json.loads(line)
                        self.data_received.emit(data)
                    except json.JSONDecodeError:
                        print(f"無法解析的串口數據：{line}")
            else:
                time.sleep(0.1)

    def send_command(self, command):
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.write((command + '\n').encode())
            print(f"發送命令到 Arduino：{command}")
        else:
            print("串口未打開，無法發送命令")

    def close(self):
        self.running = False
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("串口已關閉")

class MainWindow(QMainWindow):
    def __init__(self, serial_thread):
        super().__init__()
        self.setWindowTitle("疲勞檢測系統")
        self.setGeometry(100, 100, 1200, 800)

        self.serial_thread = serial_thread
        self.serial_thread.data_received.connect(self.handle_serial_data)

        # 主容器
        main_widget = QWidget()
        self.setCentralWidget(main_widget)

        # 布局
        layout = QVBoxLayout()
        main_widget.setLayout(layout)

        # 上方視頻播放器（使用 VLC）
        self.video_label = QLabel("等待視頻播放...")
        self.video_label.setFixedSize(900, 480)
        self.video_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.video_label)

        # 創建 VLC 播放器
        self.instance = vlc.Instance()
        self.media_player = self.instance.media_player_new()

        # 設置視頻輸出為 PyQt5 的窗口句柄
        if sys.platform.startswith("linux"):  # Linux 系統需要特殊處理
            self.media_player.set_xwindow(self.video_label.winId())
        elif sys.platform == "win32":  # Windows 系統
            self.media_player.set_hwnd(self.video_label.winId())
        elif sys.platform == "darwin":  # macOS
            self.media_player.set_nsobject(int(self.video_label.winId()))

        # 初始化時不播放 RTSP 流，等待用戶手動開始
        self.rtsp_url = None

        # 顯示 Arduino IP 地址的部分
        self.ip_display_label = QLabel("RTSP URL: 未知")
        self.ip_display_label.setFont(QFont("微軟正黑體", 14, QFont.Bold))
        self.ip_display_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.ip_display_label)

        # 開啟鏡頭的按鈕
        self.start_camera_button = QPushButton("開啟鏡頭")
        self.start_camera_button.setFixedSize(150, 80)
        self.start_camera_button.setFont(QFont("微軟正黑體", 16, QFont.Bold))
        self.start_camera_button.clicked.connect(self.start_rtsp_stream)
        layout.addWidget(self.start_camera_button)

        # 中間狀態顯示
        self.status_label = QLabel("預測狀態：等待中")
        self.status_label.setFont(QFont("微軟正黑體", 14, QFont.Bold))
        self.status_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.status_label)

        # 新增兩個標籤顯示接收到的數據
        self.label_display = QLabel("類別：無")
        self.label_display.setFont(QFont("微軟正黑體", 12))
        self.label_display.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.label_display)

        self.prob_display = QLabel("置信度：0.00")
        self.prob_display.setFont(QFont("微軟正黑體", 12))
        self.prob_display.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.prob_display)

        # 下方控制區
        control_layout = QHBoxLayout()

        # 警報按鈕
        self.alert_button = QPushButton("關閉警報")
        self.alert_button.setEnabled(False)
        self.alert_button.setFixedSize(150, 80)
        font = QFont("微軟正黑體", 16, QFont.Bold)
        self.alert_button.setFont(font)
        self.alert_button.clicked.connect(self.dismiss_alert)
        control_layout.addWidget(self.alert_button)

        # 新增的切換按鈕
        self.toggle_button = QPushButton("切換狀態")
        self.toggle_button.setFixedSize(150, 80)
        self.toggle_button.setFont(font)
        self.toggle_button.clicked.connect(self.toggle_state)
        control_layout.addWidget(self.toggle_button)

        layout.addLayout(control_layout)

        # 狀態變量
        self.prediction = None
        self.fatigue_count = 0
        self.alert_active = False  # 警報是否激活
        self.fatigue_threshold = 5  # 連續疲勞判定閾值（秒）

    def start_rtsp_stream(self):
        """開始播放 RTSP 流"""
        if self.rtsp_url:
            self.media_player.set_media(self.instance.media_new(self.rtsp_url))
            self.media_player.play()
            print(f"開始播放 RTSP 流：{self.rtsp_url}")

    def handle_serial_data(self, data):
        """處理從 Arduino 接收到的 JSON 數據"""
        rtsp_url = data.get("RTSP", "未知")
        detected_class = data.get("Detected", "未知")
        confidence = data.get("Confidence", 0.0)

        # 更新界面
        self.ip_display_label.setText(f"RTSP URL: {rtsp_url}")
        self.label_display.setText(f"類別：{detected_class}")
        self.prob_display.setText(f"置信度：{confidence:.2f}")

        if not self.rtsp_url:
            self.rtsp_url = rtsp_url

    def dismiss_alert(self):
        self.alert_button.setEnabled(False)
        self.alert_active = False
        self.status_label.setText("預測狀態：等待中")
        # 發送關閉警報命令到 Arduino
        self.serial_thread.send_command("ALARM_OFF")

    def toggle_state(self):
        # 發送切換警報狀態命令到 Arduino
        self.serial_thread.send_command("ALARM_TOGGLE")

    def closeEvent(self, event):
        """關閉窗口釋放資源"""
        self.media_player.stop()  # 停止 VLC 播放器
        self.serial_thread.close()  # 關閉串口通信
        super().closeEvent(event)

if __name__ == "__main__":
    # 初始化串口通信線程
    serial_thread = SerialThread(ARDUINO_PORT, BAUD_RATE)
    serial_thread.start()

    # 啟動主窗口
    app_qt = QApplication(sys.argv)
    window = MainWindow(serial_thread)
    window.show()

    try:
        sys.exit(app_qt.exec_())
    except KeyboardInterrupt:
        print("程序被用戶中斷")
    finally:
        serial_thread.close()
