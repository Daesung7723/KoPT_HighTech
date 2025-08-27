import tkinter as tk
from tkinter import ttk, scrolledtext
import serial
import serial.tools.list_ports
import threading
import queue
import re
import math
import socket
import json
import logging
import os

class CommApp:
    # --- 상수 정의 ---
    # 파일명
    CONFIG_FILE_NAME = "config.json"
    LOG_FILE_NAME = "comm_app.log"

    # 통신 설정
    DEFAULT_PORT = 54321
    TCP_BUFFER_SIZE = 1024

    # UI 및 데이터 처리 관련
    LCD_LINE_LENGTH = 16
    QUEUE_CHECK_INTERVAL_MS = 100
    
    # 센서 값 범위
    ILLUMINANCE_ADC_MIN, ILLUMINANCE_ADC_MAX = 100, 900
    GENERIC_ADC_MIN, GENERIC_ADC_MAX = 0, 1023
    
    # TCP 모드
    MODE_CLIENT, MODE_SERVER = "Client", "Server"

    def __init__(self, root):
        self.root = root
        self.root.title("UART & TCP/IP 통신 프로그램 (최종판)")
        self.root.geometry("720x980")

        self._setup_logging()

        # --- 통신 및 상태 변수 초기화 ---
        self.ser, self.is_uart_connected, self.uart_thread = None, False, None
        self.tcp_socket, self.server_socket, self.is_tcp_running, self.tcp_server_thread = None, None, False, None
        
        self.uart_queue = queue.Queue()
        self.tcp_data_queue = queue.Queue()
        self.client_sockets = []
        
        # --- UI 데이터 변수 ---
        self.tcp_mode = tk.StringVar(value=self.MODE_CLIENT)
        self.lcd_line0_text, self.lcd_line1_text = "", ""
        self.adc0_val, self.adc1_val = "N/A", "N/A"
        self.jukebox_list = [
            "Twinkle Star", "School Bell", "Airplane", "Butterfly", "Mountain Rabbit", 
            "Mary's Lamb", "Ode to Joy", "Jingle Bells", "Happy Birthday"
        ]
        self.selected_song = tk.IntVar(value=1)

        self._create_widgets()
        self.update_com_ports()
        self.auto_detect_local_ip()
        self._load_config() # 설정 불러오기

        # 큐 처리기 시작
        self.root.after(self.QUEUE_CHECK_INTERVAL_MS, self.process_uart_queue)
        self.root.after(self.QUEUE_CHECK_INTERVAL_MS, self.process_tcp_queue)
        self.logger.info("애플리케이션이 시작되었습니다.")

    def _setup_logging(self):
        """로깅 시스템을 설정하는 함수"""
        self.logger = logging.getLogger(__name__)
        self.logger.setLevel(logging.INFO)

        # 핸들러가 이미 추가되었는지 확인하여 중복 로깅 방지
        if self.logger.hasHandlers():
            self.logger.handlers.clear()

        # 로그 포맷 설정
        formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')

        # 파일 핸들러 설정 (로그 파일 생성)
        file_handler = logging.FileHandler(self.LOG_FILE_NAME, encoding='utf-8')
        file_handler.setFormatter(formatter)
        self.logger.addHandler(file_handler)

        # 콘솔 핸들러 설정 (터미널/콘솔에 출력)
        stream_handler = logging.StreamHandler()
        stream_handler.setFormatter(formatter)
        self.logger.addHandler(stream_handler)
    
    def _load_config(self):
        """config.json 파일에서 설정을 불러오는 함수"""
        try:
            if os.path.exists(self.CONFIG_FILE_NAME):
                with open(self.CONFIG_FILE_NAME, 'r') as f:
                    config = json.load(f)
                    self.com_port_combo.set(config.get("com_port", ""))
                    self.baud_rate_combo.set(config.get("baud_rate", 9600))
                    self.remote_ip_entry.delete(0, tk.END)
                    self.remote_ip_entry.insert(0, config.get("remote_ip", ""))
                    self.port_entry.delete(0, tk.END)
                    self.port_entry.insert(0, config.get("port", self.DEFAULT_PORT))
                    self.tcp_mode.set(config.get("tcp_mode", self.MODE_CLIENT))
                    self.logger.info("저장된 설정을 불러왔습니다.")
        except Exception as e:
            self.logger.error(f"설정 불러오기 실패: {e}")

    def _save_config(self):
        """현재 설정을 config.json 파일에 저장하는 함수"""
        config = {
            "com_port": self.com_port_combo.get(),
            "baud_rate": self.baud_rate_combo.get(),
            "remote_ip": self.remote_ip_entry.get(),
            "port": self.port_entry.get(),
            "tcp_mode": self.tcp_mode.get()
        }
        try:
            with open(self.CONFIG_FILE_NAME, 'w') as f:
                json.dump(config, f, indent=4)
            self.logger.info("현재 설정을 저장했습니다.")
        except Exception as e:
            self.logger.error(f"설정 저장 실패: {e}")

    def _create_widgets(self):
        self.root.columnconfigure(0, weight=6)
        self.root.columnconfigure(1, weight=4)
        
        self._create_uart_widgets(row=0)
        self._create_sensor_widgets(row=1)
        self._create_jukebox_widgets(row=2)
        self._create_tcp_widgets(row=3)

    def _create_uart_widgets(self, row):
        settings_frame = ttk.LabelFrame(self.root, text="UART 설정", padding=(10, 5))
        settings_frame.grid(row=row, column=0, padx=(10, 5), pady=(10, 5), sticky="nsew")
        settings_frame.columnconfigure(1, weight=1)

        ttk.Label(settings_frame, text="COM 포트:").grid(row=0, column=0, sticky="w", padx=5)
        self.com_port_combo = ttk.Combobox(settings_frame, state="readonly", width=15)
        self.com_port_combo.grid(row=0, column=1, sticky="we", padx=5)
        ttk.Button(settings_frame, text="새로고침", command=self.update_com_ports).grid(row=0, column=2, padx=5)
        
        ttk.Label(settings_frame, text="Baud Rate:").grid(row=1, column=0, sticky="w", padx=5)
        self.baud_rate_combo = ttk.Combobox(settings_frame, state="readonly", width=15, values=[9600, 19200, 38400, 57600, 115200])
        self.baud_rate_combo.grid(row=1, column=1, columnspan=2, sticky="we", padx=5)
        self.baud_rate_combo.set(9600)

        connect_frame = ttk.LabelFrame(self.root, text="UART 연결", padding=(10, 5))
        connect_frame.grid(row=row, column=1, padx=(5, 10), pady=(10, 5), sticky="nsew")
        
        self.uart_connect_button = ttk.Button(connect_frame, text="연결", command=self.toggle_uart_connection)
        self.uart_connect_button.grid(row=0, column=0, pady=5, padx=5)
        self.uart_status_label = ttk.Label(connect_frame, text="상태: 연결 끊김", foreground="red")
        self.uart_status_label.grid(row=1, column=0, sticky="w", padx=5)

    def _create_sensor_widgets(self, row):
        lcd_frame = ttk.LabelFrame(self.root, text="Virtual 16x2 LCD", padding=(10, 5))
        lcd_frame.grid(row=row, column=0, padx=(10, 5), pady=5, sticky="nsew")
        lcd_frame.rowconfigure(0, weight=1)
        lcd_frame.columnconfigure(0, weight=1)

        self.lcd_bg_color, self.lcd_fg_color = "#336699", "#CCFFFF"
        self.lcd_font = ("Courier New", 18, "bold")
        self.lcd_canvas = tk.Canvas(lcd_frame, width=270, height=50, bg=self.lcd_bg_color, highlightthickness=1, highlightbackground="gray")
        self.lcd_canvas.grid(row=0, column=0)
        self.update_lcd("", "")

        sensor_frame = ttk.LabelFrame(self.root, text="센서 측정값", padding=(10, 5))
        sensor_frame.grid(row=row, column=1, padx=(5, 10), pady=5, sticky="nsew")
        sensor_frame.columnconfigure(0, weight=1)

        self.illuminance_canvas = tk.Canvas(sensor_frame, width=250, height=50, highlightthickness=1)
        self.illuminance_canvas.grid(row=0, column=0, pady=(0, 5))
        self.draw_illuminance_bar()

        self.adc1_canvas = tk.Canvas(sensor_frame, width=250, height=50, highlightthickness=1)
        self.adc1_canvas.grid(row=1, column=0, pady=(5, 0))
        self.draw_adc1_bar()
        
    def _create_jukebox_widgets(self, row):
        jukebox_frame = ttk.LabelFrame(self.root, text="Jukebox", padding=(10, 5))
        jukebox_frame.grid(row=row, column=0, columnspan=2, padx=10, pady=5, sticky="nsew")
        jukebox_frame.columnconfigure(3, weight=1)

        for i, song_name in enumerate(self.jukebox_list):
            r, c = i // 3, i % 3
            ttk.Radiobutton(jukebox_frame, text=f"{i+1}. {song_name}", variable=self.selected_song, value=i+1).grid(row=r, column=c, sticky="w", padx=5, pady=2)
        
        self.play_button = ttk.Button(jukebox_frame, text="▶ 재생", command=self.play_song)
        self.play_button.grid(row=0, column=4, rowspan=3, sticky="nsew", padx=10, pady=5)

    def _create_tcp_widgets(self, row):
        tcp_main_frame = ttk.LabelFrame(self.root, text="TCP/IP 통신", padding=(10, 5))
        tcp_main_frame.grid(row=row, column=0, columnspan=2, padx=10, pady=5, sticky="nsew")
        self.root.rowconfigure(row, weight=1)
        
        tcp_settings_frame = ttk.Frame(tcp_main_frame)
        tcp_settings_frame.pack(fill="x")
        tcp_settings_frame.columnconfigure(1, weight=1)
        
        ttk.Label(tcp_settings_frame, text="모드:").grid(row=0, column=0, sticky="w", padx=5)
        ttk.Radiobutton(tcp_settings_frame, text="클라이언트", variable=self.tcp_mode, value=self.MODE_CLIENT, command=self.update_tcp_ui).grid(row=0, column=1, sticky="w")
        ttk.Radiobutton(tcp_settings_frame, text="서버", variable=self.tcp_mode, value=self.MODE_SERVER, command=self.update_tcp_ui).grid(row=0, column=2, sticky="w")
        
        ttk.Label(tcp_settings_frame, text="내 IP:").grid(row=1, column=0, sticky="w", padx=5)
        self.local_ip_entry = ttk.Entry(tcp_settings_frame)
        self.local_ip_entry.grid(row=1, column=1, sticky="ew") 
        
        ttk.Label(tcp_settings_frame, text="상대 IP:").grid(row=2, column=0, sticky="w", padx=5)
        self.remote_ip_entry = ttk.Entry(tcp_settings_frame)
        self.remote_ip_entry.grid(row=2, column=1, sticky="ew", columnspan=3) 
        
        ttk.Label(tcp_settings_frame, text="포트:").grid(row=1, column=2, sticky="e", padx=5)
        self.port_entry = ttk.Entry(tcp_settings_frame, width=10)
        self.port_entry.grid(row=1, column=3, sticky="w")
        self.port_entry.insert(0, str(self.DEFAULT_PORT))
        
        self.tcp_connect_button = ttk.Button(tcp_settings_frame, text="연결 시작", command=self.toggle_tcp_connection)
        self.tcp_connect_button.grid(row=3, column=0, columnspan=4, sticky="ew", pady=5)
        self.tcp_status_label = ttk.Label(tcp_settings_frame, text="상태: 중지됨", foreground="red")
        self.tcp_status_label.grid(row=4, column=0, columnspan=4, sticky="w", padx=5)
        
        self.tcp_receive_text = scrolledtext.ScrolledText(tcp_main_frame, wrap=tk.WORD, state="disabled", height=10)
        self.tcp_receive_text.pack(fill="both", expand=True, pady=5)

        tcp_send_frame = ttk.Frame(tcp_main_frame)
        tcp_send_frame.pack(fill="x")
        
        self.tcp_send_entry = ttk.Entry(tcp_send_frame)
        self.tcp_send_entry.pack(side="left", fill="x", expand=True, padx=(0, 5))
        ttk.Button(tcp_send_frame, text="TCP 전송", command=self.send_tcp_data_from_entry).pack(side="left")

    def process_uart_queue(self):
        for _ in range(100):
            if self.uart_queue.empty(): break
            try:
                data = self.uart_queue.get_nowait()
                decoded_data = data.decode('utf-8', errors='ignore').strip()
                self.logger.info(f"UART 수신: {decoded_data}")
                self._parse_uart_message(decoded_data)
            except queue.Empty:
                break
        self.root.after(self.QUEUE_CHECK_INTERVAL_MS, self.process_uart_queue)
        
    def _parse_uart_message(self, message):
        adc0_match = re.search(r'\[ADC0\](\d+)', message)
        adc1_match = re.search(r'\[ADC1\](\d+)', message)
        lcd0_match = re.search(r'\[LCD Data-0\](.*)', message)
        lcd1_match = re.search(r'\[LCD Data-1\](.*)', message)

        if adc0_match:
            self.adc0_val = adc0_match.group(1)
            self.update_illuminance_bar(int(self.adc0_val))
        elif adc1_match:
            self.adc1_val = int(adc1_match.group(1))
            self.update_adc1_bar(self.adc1_val)
        elif lcd0_match:
            self.lcd_line0_text = lcd0_match.group(1)
            self.update_lcd(self.lcd_line0_text, self.lcd_line1_text)
        elif lcd1_match:
            self.lcd_line1_text = lcd1_match.group(1)
            self.update_lcd(self.lcd_line0_text, self.lcd_line1_text)

    def process_tcp_queue(self):
        """TCP 큐를 처리하고, 수신된 메시지를 UART로 전달하는 함수"""
        try:
            while not self.tcp_data_queue.empty():
                msg = self.tcp_data_queue.get_nowait()
                
                # 메시지가 [SYSTEM]으로 시작하지 않는 일반 데이터일 경우
                if not msg.startswith("[SYSTEM]"):
                    # 1. UART로 메시지를 전송
                    if self.is_uart_connected:
                        self.send_uart_data(msg)
                        self.logger.info(f"TCP->UART 자동 전송: {msg}")
                    else:
                        self.logger.warning(f"TCP->UART 전송 실패: UART가 연결되지 않았습니다.")
                    
                    # 2. 화면 표시를 위해 [수신] 텍스트를 추가
                    display_msg = f"[수신] {msg}"
                # 시스템 메시지일 경우
                else:
                    display_msg = msg

                # 최종적으로 포맷된 메시지를 화면에 표시하고 로그를 남김
                self._display_tcp_message(display_msg)
                self.logger.info(f"TCP: {display_msg}")
        finally:
            self.root.after(self.QUEUE_CHECK_INTERVAL_MS, self.process_tcp_queue)
            
    def _display_tcp_message(self, message):
        self.tcp_receive_text.config(state="normal")
        self.tcp_receive_text.insert(tk.END, message + "\n")
        self.tcp_receive_text.see(tk.END)
        self.tcp_receive_text.config(state="disabled")

    def update_lcd(self, line1, line2):
        self.lcd_canvas.delete("all")
        line1_text = line1.ljust(self.LCD_LINE_LENGTH)[:self.LCD_LINE_LENGTH]
        line2_text = line2.ljust(self.LCD_LINE_LENGTH)[:self.LCD_LINE_LENGTH]
        self.lcd_canvas.create_text(8, 14, text=line1_text, font=self.lcd_font, fill=self.lcd_fg_color, anchor='w')
        self.lcd_canvas.create_text(8, 38, text=line2_text, font=self.lcd_font, fill=self.lcd_fg_color, anchor='w')

    def draw_illuminance_bar(self):
        self.illuminance_canvas.delete("all")
        self.illuminance_canvas.create_rectangle(0, 0, 250, 50, fill="#007F00", outline="gray", tags="bg")
        self.illuminance_canvas.create_text(30, 40, text="밝음", fill="white")
        self.illuminance_canvas.create_text(220, 40, text="어두움", fill="white")
        self.illuminance_canvas.create_text(125, 20, text="N/A", font=("Helvetica", 12, "bold"), fill="white", tags="adc_text")
        self.illuminance_canvas.create_line(125, 5, 125, 30, fill="red", width=3, tags="needle")

    def update_illuminance_bar(self, adc_value):
        c_adc = max(self.ILLUMINANCE_ADC_MIN, min(self.ILLUMINANCE_ADC_MAX, adc_value))
        b_level = (self.ILLUMINANCE_ADC_MAX - c_adc) / (self.ILLUMINANCE_ADC_MAX - self.ILLUMINANCE_ADC_MIN)
        g = int(255 * b_level)
        bg_color = f'#00{g:02x}00'
        n_pos = (c_adc - self.ILLUMINANCE_ADC_MIN) / (self.ILLUMINANCE_ADC_MAX - self.ILLUMINANCE_ADC_MIN)
        x_pos = 10 + n_pos * 230
        self.illuminance_canvas.itemconfig("bg", fill=bg_color)
        self.illuminance_canvas.coords("needle", x_pos, 5, x_pos, 30)
        self.illuminance_canvas.itemconfig("adc_text", text=str(c_adc), fill="white")

    def draw_adc1_bar(self):
        self.adc1_canvas.delete("all")
        self.adc1_canvas.create_rectangle(0, 0, 250, 50, fill="gray", outline="black")
        padding, width = 10, 230
        for i in range(0, self.GENERIC_ADC_MAX + 1, 50):
            x_pos = padding + (i / self.GENERIC_ADC_MAX) * width
            tick_height = 15 if i % 100 == 0 else 10
            if i % 100 == 0: self.adc1_canvas.create_text(x_pos, 15, text=str(i), font=("Helvetica", 8))
            self.adc1_canvas.create_line(x_pos, 40 - tick_height, x_pos, 40, fill="black")
        self.adc1_canvas.create_line(padding, 25, padding, 40, fill="red", width=2, tags="adc1_needle")

    def update_adc1_bar(self, value):
        c_val = max(self.GENERIC_ADC_MIN, min(self.GENERIC_ADC_MAX, value))
        padding, width = 10, 230
        x_pos = padding + (c_val / self.GENERIC_ADC_MAX) * width
        self.adc1_canvas.coords("adc1_needle", x_pos, 25, x_pos, 40)
        
    def update_uart_status(self, message, color):
        self.uart_status_label.config(text=message, foreground=color)
        
    def update_tcp_status(self, message, color):
        self.tcp_status_label.config(text=message, foreground=color)

    def toggle_uart_connection(self):
        if self.is_uart_connected:
            self.is_uart_connected = False
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.uart_connect_button.config(text="연결")
            self.update_uart_status("상태: 연결 끊김", "red")
            self.logger.info("UART 연결이 해제되었습니다.")
        else:
            port = self.com_port_combo.get()
            baud = int(self.baud_rate_combo.get())
            if not port:
                self.update_uart_status("오류: COM 포트를 선택하세요.", "red")
                self.logger.warning("COM 포트가 선택되지 않았습니다.")
                return
            try:
                self.ser = serial.Serial(port, baud, timeout=0.1)
                self.is_uart_connected = True
                self.uart_thread = threading.Thread(target=self.read_uart_data, daemon=True)
                self.uart_thread.start()
                self.uart_connect_button.config(text="연결 해제")
                self.update_uart_status(f"상태: {port}에 연결됨", "green")
                self.logger.info(f"UART 연결 성공: {port}, {baud} bps")
            except serial.SerialException as e:
                self.update_uart_status(f"연결 오류: {e}", "red")
                self.logger.error(f"UART 연결 오류: {e}")
                
    def read_uart_data(self):
        while self.is_uart_connected:
            try:
                data = self.ser.read_until(b'\n')
                if data: self.uart_queue.put(data)
            except serial.SerialException as e:
                self.logger.error(f"UART 읽기 오류 발생: {e}")
                self.is_uart_connected = False
                break
    
    def play_song(self):
        command = f"[Song-{self.selected_song.get()}]"
        self.logger.info(f"명령 전송 시도: {command}")
        if self.is_uart_connected: self.send_uart_data(command)
        elif self.is_tcp_running: self.send_tcp_data(command)
        else:
            msg = "오류: 통신을 먼저 연결하세요."
            self.update_uart_status(msg, "red")
            self.update_tcp_status(msg, "red")
            self.logger.warning("명령 전송 실패: 활성화된 통신 없음")

    def send_uart_data(self, data):
        if not self.is_uart_connected or not data: return
        try:
            self.ser.write((data + '\n').encode('utf-8'))
            self.logger.info(f"UART 전송: {data}")
        except serial.SerialException as e:
            self.update_uart_status(f"UART 전송 오류: {e}", "red")
            self.logger.error(f"UART 전송 오류: {e}")

    def send_tcp_data_from_entry(self):
        data = self.tcp_send_entry.get()
        if self.send_tcp_data(data):
            self.tcp_send_entry.delete(0, tk.END)
            
    def send_tcp_data(self, data):
        if not self.is_tcp_running or not data: return False
        try:
            formatted_data = (data + '\n').encode('utf-8')
            sockets_to_send = self.client_sockets if self.tcp_mode.get() == self.MODE_SERVER else [self.tcp_socket]
            for sock in sockets_to_send:
                if sock: sock.sendall(formatted_data)
            self.tcp_data_queue.put(f"[송신] {data.strip()}")
            return True
        except Exception as e:
            self.tcp_data_queue.put(f"[SYSTEM] 전송 오류: {e}")
            self.logger.error(f"TCP 전송 오류: {e}")
            return False

    def start_client(self):
        remote_ip, port = self.remote_ip_entry.get(), int(self.port_entry.get())
        try:
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.connect((remote_ip, port))
            self.is_tcp_running = True
            threading.Thread(target=self.tcp_receive_loop, args=(self.tcp_socket,), daemon=True).start()
            self.update_tcp_status(f"서버({remote_ip}:{port})에 연결됨", "green")
            self.tcp_connect_button.config(text="연결 끊기")
            self.logger.info(f"TCP 클라이언트 연결 성공: {remote_ip}:{port}")
        except Exception as e:
            self.update_tcp_status(f"연결 실패: {e}", "red")
            self.logger.error(f"TCP 클라이언트 연결 실패: {e}")
            self.tcp_socket = None

    def start_server(self):
        local_ip, port = self.local_ip_entry.get(), int(self.port_entry.get())
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.bind((local_ip, port))
            self.server_socket.listen(5)
            self.is_tcp_running = True
            self.tcp_server_thread = threading.Thread(target=self.server_accept_loop, daemon=True)
            self.tcp_server_thread.start()
            self.update_tcp_status(f"서버 시작됨 ({local_ip}:{port}). 클라이언트 접속 대기 중...", "blue")
            self.tcp_connect_button.config(text="서버 중지")
            self.logger.info(f"TCP 서버 시작됨: {local_ip}:{port}")
        except Exception as e:
            self.update_tcp_status(f"서버 시작 실패: {e}", "red")
            self.logger.error(f"TCP 서버 시작 실패: {e}")
            self.server_socket = None

    def stop_tcp(self):
        self.is_tcp_running = False
        if self.server_socket: self.server_socket.close()
        for sock in self.client_sockets: sock.close()
        if self.tcp_socket: self.tcp_socket.close()
        self.client_sockets.clear()
        self.server_socket, self.tcp_socket = None, None
        self.update_tcp_status("상태: 중지됨", "red")
        self.update_tcp_ui()
        self.logger.info("TCP 통신이 중지되었습니다.")

    def server_accept_loop(self):
        while self.is_tcp_running:
            try:
                client_socket, addr = self.server_socket.accept()
                self.client_sockets.append(client_socket)
                threading.Thread(target=self.tcp_receive_loop, args=(client_socket, addr), daemon=True).start()
                self.tcp_data_queue.put(f"[SYSTEM] 클라이언트 연결됨: {addr}")
            except OSError: 
                self.logger.info("서버 소켓이 닫혔습니다. accept 루프를 종료합니다.")
                break
            except Exception as e:
                if self.is_tcp_running: self.logger.error(f"클라이언트 accept 오류: {e}")
                break

    def tcp_receive_loop(self, sock, addr=None):
        """TCP 데이터를 수신하고 순수 메시지를 큐에 넣는 함수"""
        addr_str = f" from {addr}" if addr else ""
        while self.is_tcp_running:
            try:
                data = sock.recv(self.TCP_BUFFER_SIZE)
                if not data: break
                # 순수 데이터만 큐에 넣음
                self.tcp_data_queue.put(data.decode('utf-8').strip())
            except (ConnectionResetError, BrokenPipeError, OSError): 
                self.logger.warning(f"TCP 연결이 비정상적으로 종료되었습니다{addr_str}.")
                break
            except Exception as e:
                self.logger.error(f"TCP 수신 루프 오류: {e}")
                break

        if sock in self.client_sockets: self.client_sockets.remove(sock)
        sock.close()
        if self.is_tcp_running: self.tcp_data_queue.put(f"[SYSTEM] 연결 끊김{addr_str}")
        if self.tcp_mode.get() == self.MODE_CLIENT: self.stop_tcp()

    def on_closing(self):
        """창을 닫을 때 호출되는 함수"""
        self.logger.info("애플리케이션을 종료합니다.")
        self._save_config() # 종료 전 설정 저장
        self.stop_tcp()
        if self.is_uart_connected:
            self.is_uart_connected = False
            if self.ser and self.ser.is_open:
                self.ser.close()
        self.root.destroy()
    
    def update_com_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.com_port_combo['values'] = ports
        # config 로딩 기능이 있으므로 첫 항목을 강제로 선택하지 않음
        if ports and not self.com_port_combo.get():
             self.com_port_combo.set(ports[0])

    def auto_detect_local_ip(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(("8.8.8.8", 80))
                self.local_ip_entry.insert(0, s.getsockname()[0])
        except Exception:
            self.local_ip_entry.insert(0, "127.0.0.1")

    def update_tcp_ui(self):
        if self.is_tcp_running: return
        is_client = self.tcp_mode.get() == self.MODE_CLIENT
        self.remote_ip_entry.config(state="normal" if is_client else "disabled")
        self.tcp_connect_button.config(text="서버에 연결" if is_client else "서버 시작")

    def toggle_tcp_connection(self):
        if self.is_tcp_running: self.stop_tcp()
        else:
            if self.tcp_mode.get() == self.MODE_CLIENT: self.start_client()
            else: self.start_server()

if __name__ == "__main__":
    root = tk.Tk()
    app = CommApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
