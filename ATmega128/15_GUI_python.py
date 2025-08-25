import tkinter as tk
from tkinter import ttk, scrolledtext
import serial
import serial.tools.list_ports
import threading
import queue
import re
import math

class UartApp:
    def __init__(self, root):
        self.root = root
        self.root.title("UART 통신 프로그램")
        self.root.geometry("600x750") # 세로 길이 조정

        self.ser = None
        self.is_connected = False
        self.reading_thread = None
        self.data_queue = queue.Queue()

        self.create_widgets()
        self.update_com_ports()

        self.root.after(100, self.process_queue)

    def create_widgets(self):
        # --- 1. 설정 프레임 ---
        settings_frame = ttk.LabelFrame(self.root, text="설정", padding=(10, 5))
        settings_frame.pack(padx=10, pady=10, fill="x")

        ttk.Label(settings_frame, text="COM 포트:").grid(row=0, column=0, padx=5, pady=5, sticky="w")
        self.com_port_combo = ttk.Combobox(settings_frame, state="readonly")
        self.com_port_combo.grid(row=0, column=1, padx=5, pady=5, sticky="we")
        ttk.Button(settings_frame, text="새로고침", command=self.update_com_ports).grid(row=0, column=2, padx=5, pady=5)

        ttk.Label(settings_frame, text="Baud Rate:").grid(row=1, column=0, padx=5, pady=5, sticky="w")
        self.baud_rate_combo = ttk.Combobox(settings_frame, values=[9600, 19200, 38400, 57600, 115200], state="readonly")
        self.baud_rate_combo.grid(row=1, column=1, columnspan=2, padx=5, pady=5, sticky="we")
        self.baud_rate_combo.set(9600)

        settings_frame.columnconfigure(1, weight=1)

        # --- 2. 연결 제어 프레임 ---
        connect_frame = ttk.Frame(self.root, padding=(10, 5))
        connect_frame.pack(padx=10, pady=5, fill="x")

        self.connect_button = ttk.Button(connect_frame, text="연결", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=5)
        
        self.status_label = ttk.Label(connect_frame, text="상태: 연결 끊김", foreground="red", anchor="w")
        self.status_label.pack(side="left", padx=5, fill="x", expand=True)
        
        # --- 3. 분석 데이터 표시 프레임 ---
        parsed_data_frame = ttk.LabelFrame(self.root, text="측정값", padding=(10, 5))
        parsed_data_frame.pack(padx=10, pady=10, fill="x")
        parsed_data_frame.columnconfigure(1, weight=1)

        ttk.Label(parsed_data_frame, text="ADC:", font=("Helvetica", 12)).grid(row=0, column=0, sticky="w")
        self.adc_value_label = ttk.Label(parsed_data_frame, text="N/A", font=("Helvetica", 14, "bold"), foreground="blue")
        self.adc_value_label.grid(row=0, column=1, sticky="w", padx=5)
        
        self.gauge_canvas = tk.Canvas(parsed_data_frame, width=200, height=110, bg='white', highlightthickness=0)
        self.gauge_canvas.grid(row=1, column=0, columnspan=2, pady=10)
        self.draw_gauge_background()
        self.needle = None
        self.update_gauge(0)

        # --- 4. 수신 데이터 프레임 ---
        receive_frame = ttk.LabelFrame(self.root, text="수신 데이터 (Raw)", padding=(10, 5))
        receive_frame.pack(padx=10, pady=10, fill="both", expand=True)
        
        # ## 삭제: HEX/ASCII 선택 라디오 버튼 부분 삭제 ##

        self.receive_text = scrolledtext.ScrolledText(receive_frame, wrap=tk.WORD, state="disabled")
        self.receive_text.pack(fill="both", expand=True)

        # --- 5. 송신 데이터 프레임 ---
        send_frame = ttk.LabelFrame(self.root, text="송신 데이터", padding=(10, 5))
        send_frame.pack(padx=10, pady=(0, 10), fill="x")

        self.send_entry = ttk.Entry(send_frame)
        self.send_entry.pack(side="left", padx=(0, 5), fill="x", expand=True)
        self.send_button = ttk.Button(send_frame, text="전송", command=self.send_data)
        self.send_button.pack(side="left")

    def draw_gauge_background(self):
        self.gauge_canvas.delete("all")
        self.gauge_canvas.create_arc(10, 10, 190, 190, start=0, extent=180, style=tk.ARC, outline='gray', width=2)
        
        for i in range(-90, 91, 30):
            angle = math.radians(-i + 90) 
            x1, y1 = 100 - 80 * math.cos(angle), 100 - 80 * math.sin(angle)
            x2, y2 = 100 - 90 * math.cos(angle), 100 - 90 * math.sin(angle)
            self.gauge_canvas.create_line(x1, y1, x2, y2, width=2, fill='black')
            xt, yt = 100 - 70 * math.cos(angle), 100 - 70 * math.sin(angle)
            self.gauge_canvas.create_text(xt, yt, text=str(i), fill='black', font=('Helvetica', 8))

    def update_gauge(self, deg):
        if self.needle:
            self.gauge_canvas.delete(self.needle)
        
        deg = max(-90, min(90, deg))
        angle = math.radians(-deg + 90)
        x, y = 100 - 85 * math.cos(angle), 100 - 85 * math.sin(angle)
        self.needle = self.gauge_canvas.create_line(100, 100, x, y, width=2, fill='red', arrow=tk.LAST)
    
    def process_queue(self):
        # ## 수정: HEX 처리 로직 삭제 및 코드 간소화 ##
        for _ in range(100):
            if self.data_queue.empty():
                break

            try:
                data = self.data_queue.get_nowait()
                if data is None: 
                    self.toggle_connection()
                    break
                
                display_data = ""
                try:
                    decoded_data = data.decode('utf-8', errors='ignore')
                    match = re.search(r'ADC:\s*(\d+)\s*\|\s*Angle:\s*(-?\d+)\s*deg', decoded_data)
                    
                    if match:
                        adc_val, deg_val = match.groups()
                        self.adc_value_label.config(text=adc_val)
                        self.update_gauge(int(deg_val))
                        continue
                    else:
                        display_data = decoded_data
                except UnicodeDecodeError:
                    display_data = repr(data)

                if display_data:
                    self.receive_text.config(state="normal")
                    self.receive_text.insert(tk.END, display_data)
                    self.receive_text.see(tk.END)
                    self.receive_text.config(state="disabled")

            except queue.Empty:
                break
    
        self.root.after(100, self.process_queue)

    def update_com_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.com_port_combo['values'] = ports
        if ports:
            self.com_port_combo.set(ports[0])

    def toggle_connection(self):
        if not self.is_connected:
            port = self.com_port_combo.get()
            baud = int(self.baud_rate_combo.get())
            if not port:
                self.update_status("오류: COM 포트를 선택하세요.", "red")
                return
            try:
                self.ser = serial.Serial(port, baud, timeout=0.1,
                                         bytesize=serial.EIGHTBITS,
                                         parity=serial.PARITY_NONE,
                                         stopbits=serial.STOPBITS_ONE)
                self.is_connected = True
                self.connect_button.config(text="연결 해제")
                self.update_status(f"상태: {port}에 연결됨", "green")
                self.reading_thread = threading.Thread(target=self.read_serial_data, daemon=True)
                self.reading_thread.start()
            except serial.SerialException as e:
                self.update_status(f"오류: {e}", "red")
        else:
            self.is_connected = False
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.connect_button.config(text="연결")
            self.update_status("상태: 연결 끊김", "red")

    def read_serial_data(self):
        while self.is_connected and self.ser.is_open:
            try:
                data = self.ser.read_until(b'\n')
                if data:
                    self.data_queue.put(data)
            except serial.SerialException:
                self.data_queue.put(None)
                break

    def send_data(self):
        if not self.is_connected:
            self.update_status("오류: 먼저 연결하세요.", "red")
            return
        data_to_send = self.send_entry.get()
        if data_to_send:
            try:
                self.ser.write((data_to_send + '\n').encode('utf-8'))
                self.send_entry.delete(0, tk.END)
            except serial.SerialException as e:
                self.update_status(f"전송 오류: {e}", "red")

    def update_status(self, message, color):
        self.status_label.config(text=message, foreground=color)
    
    def on_closing(self):
        if self.is_connected:
            self.is_connected = False
            if self.ser and self.ser.is_open:
                self.ser.close()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = UartApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
