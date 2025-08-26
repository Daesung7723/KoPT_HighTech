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
        self.root.geometry("720x750") # 세로 길이 조정

        self.ser = None
        self.is_connected = False
        self.reading_thread = None
        self.data_queue = queue.Queue()

        # 각 데이터의 마지막 상태를 저장할 변수
        self.lcd_line0_text = ""
        self.lcd_line1_text = ""
        self.adc0_val = "N/A"
        self.adc1_val = "N/A"

        # Jukebox 관련 변수
        self.jukebox_list = [
            "Twinkle Star", "School Bell", "Airplane",
            "Butterfly", "Mountain Rabbit", "Mary's Lamb",
            "Ode to Joy", "Jingle Bells", "Happy Birthday"
        ]
        self.selected_song = tk.IntVar(value=1)


        self.create_widgets()
        self.update_com_ports()

        self.root.after(100, self.process_queue)

    def create_widgets(self):
        # --- 메인 그리드 설정 (2개의 열, 6:4 비율) ---
        self.root.columnconfigure(0, weight=6)
        self.root.columnconfigure(1, weight=4)

        # --- 1. 첫 번째 줄 (row=0) ---
        settings_frame = ttk.LabelFrame(self.root, text="설정", padding=(10, 5))
        settings_frame.grid(row=0, column=0, padx=(10, 5), pady=(10, 5), sticky="nsew")

        ttk.Label(settings_frame, text="COM 포트:").grid(row=0, column=0, padx=5, pady=5, sticky="w")
        self.com_port_combo = ttk.Combobox(settings_frame, state="readonly", width=15)
        self.com_port_combo.grid(row=0, column=1, padx=5, pady=5, sticky="we")
        ttk.Button(settings_frame, text="새로고침", command=self.update_com_ports).grid(row=0, column=2, padx=5, pady=5)
        ttk.Label(settings_frame, text="Baud Rate:").grid(row=1, column=0, padx=5, pady=5, sticky="w")
        self.baud_rate_combo = ttk.Combobox(settings_frame, values=[9600, 19200, 38400, 57600, 115200], state="readonly", width=15)
        self.baud_rate_combo.grid(row=1, column=1, columnspan=2, padx=5, pady=5, sticky="we")
        self.baud_rate_combo.set(9600)
        settings_frame.columnconfigure(1, weight=1)

        connect_frame = ttk.LabelFrame(self.root, text="연결", padding=(10, 5))
        connect_frame.grid(row=0, column=1, padx=(5, 10), pady=(10, 5), sticky="nsew")

        self.connect_button = ttk.Button(connect_frame, text="연결", command=self.toggle_connection)
        self.connect_button.grid(row=0, column=0, padx=5, pady=5)
        self.status_label = ttk.Label(connect_frame, text="상태: 연결 끊김", foreground="red")
        self.status_label.grid(row=1, column=0, padx=5, pady=5, sticky="w")

        # --- 2. 두 번째 줄 (row=1) ---
        lcd_frame = ttk.LabelFrame(self.root, text="Virtual 16x2 LCD", padding=(10, 5))
        lcd_frame.grid(row=1, column=0, padx=(10, 5), pady=5, sticky="nsew")

        lcd_frame.rowconfigure(0, weight=1)
        lcd_frame.columnconfigure(0, weight=1)

        self.lcd_bg_color = "#336699"
        self.lcd_fg_color = "#CCFFFF"
        self.lcd_font = ("Courier New", 18, "bold")
        self.lcd_canvas = tk.Canvas(lcd_frame, width=270, height=50, bg=self.lcd_bg_color, highlightthickness=1, highlightbackground="gray")
        self.lcd_canvas.grid(row=0, column=0)
        self.update_lcd("", "")

        sensor_frame = ttk.LabelFrame(self.root, text="센서 측정값", padding=(10, 5))
        sensor_frame.grid(row=1, column=1, padx=(5, 10), pady=5, sticky="nsew")

        sensor_frame.columnconfigure(0, weight=1)

        self.illuminance_canvas = tk.Canvas(sensor_frame, width=250, height=50, bg='white', highlightthickness=1)
        self.illuminance_canvas.grid(row=0, column=0, pady=(0, 5))
        self.draw_illuminance_bar()

        self.adc1_canvas = tk.Canvas(sensor_frame, width=250, height=50, bg='white', highlightthickness=1)
        self.adc1_canvas.grid(row=1, column=0, pady=(5, 0))
        self.draw_adc1_bar()


        # --- ## 수정: 3. Jukebox 프레임 (위치 이동) ---
        jukebox_frame = ttk.LabelFrame(self.root, text="Jukebox", padding=(10, 5))
        jukebox_frame.grid(row=2, column=0, columnspan=2, padx=10, pady=5, sticky="nsew")
        jukebox_frame.columnconfigure(3, weight=1)

        # 라디오 버튼 3x3 배치
        for i, song_name in enumerate(self.jukebox_list):
            row_num = i // 3
            col_num = i % 3
            ttk.Radiobutton(
                jukebox_frame,
                text=f"{i+1}. {song_name}",
                variable=self.selected_song,
                value=i + 1
            ).grid(row=row_num, column=col_num, sticky="w", padx=5, pady=2)
        
        # 재생 버튼
        self.play_button = ttk.Button(jukebox_frame, text="▶ 재생", command=self.play_song)
        self.play_button.grid(row=0, column=4, rowspan=3, sticky="nsew", padx=10, pady=5)


        # --- ## 수정: 4. 수신 데이터 프레임 (위치 이동 및 크기 조절) ---
        receive_frame = ttk.LabelFrame(self.root, text="수신 데이터 (Raw)", padding=(10, 5))
        receive_frame.grid(row=3, column=0, columnspan=2, padx=10, pady=5, sticky="nsew")

        self.receive_text = scrolledtext.ScrolledText(receive_frame, wrap=tk.WORD, state="disabled", height=5)
        self.receive_text.pack(fill="both", expand=True)

        # --- ## 수정: 5. 송신 데이터 프레임 (위치 이동) ---
        send_frame = ttk.LabelFrame(self.root, text="송신 데이터", padding=(10, 5))
        send_frame.grid(row=4, column=0, columnspan=2, padx=10, pady=(5, 10), sticky="nsew")

        self.send_entry = ttk.Entry(send_frame)
        self.send_entry.pack(side="left", padx=(0, 5), fill="x", expand=True)
        self.send_button = ttk.Button(send_frame, text="전송", command=self.send_data)
        self.send_button.pack(side="left")

    def update_lcd(self, line1, line2):
        self.lcd_canvas.delete("all")
        line1_text = line1.ljust(16)[:16]
        line2_text = line2.ljust(16)[:16]
        self.lcd_canvas.create_text(8, 14, text=line1_text, font=self.lcd_font, fill=self.lcd_fg_color, anchor='w')
        self.lcd_canvas.create_text(8, 38, text=line2_text, font=self.lcd_font, fill=self.lcd_fg_color, anchor='w')

    def process_queue(self):
        for _ in range(100):
            if self.data_queue.empty(): break
            try:
                data = self.data_queue.get_nowait()
                if data is None:
                    self.toggle_connection()
                    break

                display_data = ""
                try:
                    decoded_data = data.decode('utf-8', errors='ignore').strip()

                    adc0_match = re.search(r'\[ADC0\](\d+)', decoded_data)
                    adc1_match = re.search(r'\[ADC1\](\d+)', decoded_data)
                    lcd0_match = re.search(r'\[LCD Data-0\](.*)', decoded_data)
                    lcd1_match = re.search(r'\[LCD Data-1\](.*)', decoded_data)

                    if adc0_match:
                        self.adc0_val = adc0_match.group(1)
                        self.update_illuminance_bar(int(self.adc0_val))
                        continue

                    elif adc1_match:
                        self.adc1_val = int(adc1_match.group(1))
                        self.update_adc1_bar(self.adc1_val)
                        continue

                    elif lcd0_match:
                        self.lcd_line0_text = lcd0_match.group(1)
                        self.update_lcd(self.lcd_line0_text, self.lcd_line1_text)
                        continue

                    elif lcd1_match:
                        self.lcd_line1_text = lcd1_match.group(1)
                        self.update_lcd(self.lcd_line0_text, self.lcd_line1_text)
                        continue

                    else:
                        display_data = decoded_data + '\n'

                except UnicodeDecodeError:
                    display_data = repr(data) + '\n'

                if display_data:
                    self.receive_text.config(state="normal")
                    self.receive_text.insert(tk.END, display_data)
                    self.receive_text.see(tk.END)
                    self.receive_text.config(state="disabled")

            except queue.Empty:
                break

        self.root.after(100, self.process_queue)

    def draw_illuminance_bar(self):
        self.illuminance_canvas.delete("all")
        self.illuminance_canvas.create_rectangle(0, 0, 250, 50, fill="#007F00", outline="gray", tags="bg")
        self.illuminance_canvas.create_text(30, 40, text="밝음", fill="white")
        self.illuminance_canvas.create_text(220, 40, text="어두움", fill="white")
        self.illuminance_canvas.create_text(125, 20, text="N/A", font=("Helvetica", 12, "bold"), fill="white", tags="adc_text")
        self.illuminance_canvas.create_line(125, 5, 125, 30, fill="red", width=3, tags="needle")

    def update_illuminance_bar(self, adc_value):
        clamped_adc = max(100, min(900, adc_value))
        brightness_level = (900 - clamped_adc) / (900 - 100)

        g = int(0 + (255 - 0) * brightness_level)
        bg_color = f'#00{g:02x}00'

        normalized_pos = (clamped_adc - 100) / (900 - 100)
        x_pos = 10 + normalized_pos * (240 - 10)

        self.illuminance_canvas.itemconfig("bg", fill=bg_color)
        self.illuminance_canvas.coords("needle", x_pos, 5, x_pos, 30)
        self.illuminance_canvas.itemconfig("adc_text", text=str(clamped_adc), fill="white")

    def draw_adc1_bar(self):
        self.adc1_canvas.delete("all")
        self.adc1_canvas.create_rectangle(0, 0, 250, 50, fill="gray", outline="black")

        padding = 10
        width = 240 - padding
        for i in range(0, 1024, 10):
            normalized_pos = i / 1023
            x_pos = padding + normalized_pos * width

            if i % 100 == 0:
                self.adc1_canvas.create_line(x_pos, 25, x_pos, 40, fill="black", width=2)
                self.adc1_canvas.create_text(x_pos, 15, text=str(i), font=("Helvetica", 8))
            elif i % 50 == 0:
                self.adc1_canvas.create_line(x_pos, 30, x_pos, 40, fill="black")
            else:
                self.adc1_canvas.create_line(x_pos, 35, x_pos, 40, fill="black")

        self.adc1_canvas.create_line(padding, 25, padding, 40, fill="red", width=2, tags="adc1_needle")

    def update_adc1_bar(self, value):
        clamped_val = max(0, min(1023, value))

        padding = 10
        width = 240 - padding
        normalized_pos = clamped_val / 1023.0
        x_pos = padding + normalized_pos * width

        self.adc1_canvas.coords("adc1_needle", x_pos, 25, x_pos, 40)

    def play_song(self):
        if not self.is_connected:
            self.update_status("오류: 먼저 연결하세요.", "red")
            return

        song_num = self.selected_song.get()
        command = f"[Song-{song_num}]"

        try:
            self.ser.write((command + '\n').encode('utf-8'))
            self.update_status(f"전송: {command}", "blue")
        except serial.SerialException as e:
            self.update_status(f"전송 오류: {e}", "red")

    def update_com_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.com_port_combo['values'] = ports
        if ports: self.com_port_combo.set(ports[0])

    def toggle_connection(self):
        if not self.is_connected:
            port = self.com_port_combo.get()
            baud = int(self.baud_rate_combo.get())
            if not port:
                self.update_status("오류: COM 포트를 선택하세요.", "red")
                return
            try:
                self.ser = serial.Serial(port, baud, timeout=0.1, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE)
                self.is_connected = True
                self.connect_button.config(text="연결 해제")
                self.update_status(f"상태: {port}에 연결됨", "green")
                self.reading_thread = threading.Thread(target=self.read_serial_data, daemon=True)
                self.reading_thread.start()
            except serial.SerialException as e: self.update_status(f"오류: {e}", "red")
        else:
            self.is_connected = False
            if self.ser and self.ser.is_open: self.ser.close()
            self.connect_button.config(text="연결")
            self.update_status("상태: 연결 끊김", "red")

    def read_serial_data(self):
        while self.is_connected and self.ser.is_open:
            try:
                data = self.ser.read_until(b'\n')
                if data: self.data_queue.put(data)
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
                self.update_status(f"전송: {data_to_send}", "blue")
            except serial.SerialException as e:
                self.update_status(f"전송 오류: {e}", "red")

    def update_status(self, message, color):
        self.status_label.config(text=message, foreground=color)

    def on_closing(self):
        if self.is_connected:
            self.is_connected = False
            if self.ser and self.ser.is_open: self.ser.close()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = UartApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
