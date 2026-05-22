import tkinter as tk
from tkinter import ttk, scrolledtext, filedialog
import serial
import serial.tools.list_ports
import threading
import queue
import socket
import struct
import json
import logging
import os
import time
from datetime import datetime

# ─── Protocol constants ───────────────────────────────────────────────────────

UART_SOF         = 0xAA
UART_MAX_PAYLOAD = 28
UART_TYPE_DATA   = 0x01
UART_TYPE_CMD    = 0x02
UART_TYPE_ACK    = 0x03
UART_TYPE_ERR    = 0x04

TCP_MAGIC         = b'\xAB\xCD'
TCP_VERSION       = 0x01
TCP_TYPE_DATA     = 0x01
TCP_TYPE_HB       = 0x02
TCP_TYPE_SYSTEM   = 0x03
TCP_DST_BROADCAST = 0xFF
TCP_ID_SERVER     = 0x00

HEARTBEAT_INTERVAL = 5.0
HEARTBEAT_TIMEOUT  = 12.0

CONFIG_FILE = "repeater_config.json"
LOG_FILE    = "repeater_log.txt"


# ─── ProtocolCodec ────────────────────────────────────────────────────────────

class ProtocolCodec:

    @staticmethod
    def calc_crc8(frame_type: int, length: int, payload: bytes) -> int:
        crc = frame_type ^ length
        for b in payload:
            crc ^= b
        return crc & 0xFF

    @staticmethod
    def calc_crc16(data: bytes) -> int:
        crc = 0xFFFF
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
        return crc & 0xFFFF

    @staticmethod
    def encode_uart(frame_type: int, payload: bytes) -> bytes:
        if len(payload) > UART_MAX_PAYLOAD:
            payload = payload[:UART_MAX_PAYLOAD]
        length = len(payload)
        crc = ProtocolCodec.calc_crc8(frame_type, length, payload)
        return bytes([UART_SOF, frame_type, length]) + payload + bytes([crc])

    @staticmethod
    def encode_tcp(frame_type: int, src_id: int, dst_id: int,
                   seq_num: int, payload: bytes) -> bytes:
        length      = len(payload)
        header_body = struct.pack('>BBBHH', frame_type, src_id, dst_id, seq_num, length)
        crc         = ProtocolCodec.calc_crc16(header_body + payload)
        return TCP_MAGIC + bytes([TCP_VERSION]) + header_body + payload + struct.pack('>H', crc)


class UartFrameParser:
    def __init__(self):
        self._reset()

    def _reset(self):
        self._state   = 'SOF'
        self._type    = 0
        self._length  = 0
        self._payload = bytearray()

    def feed(self, byte: int):
        if self._state == 'SOF':
            if byte == UART_SOF:
                self._state = 'TYPE'
        elif self._state == 'TYPE':
            self._type  = byte
            self._state = 'LENGTH'
        elif self._state == 'LENGTH':
            if byte > UART_MAX_PAYLOAD:
                self._reset()
            else:
                self._length  = byte
                self._payload = bytearray()
                self._state   = 'CRC' if byte == 0 else 'PAYLOAD'
        elif self._state == 'PAYLOAD':
            self._payload.append(byte)
            if len(self._payload) >= self._length:
                self._state = 'CRC'
        elif self._state == 'CRC':
            expected = ProtocolCodec.calc_crc8(self._type, self._length, self._payload)
            frame = None
            if byte == expected:
                frame = {'type': self._type, 'length': self._length,
                         'payload': bytes(self._payload)}
            self._reset()
            return frame
        return None


class TcpFrameParser:
    def __init__(self):
        self._buf = bytearray()

    def feed_bytes(self, data: bytes):
        self._buf.extend(data)
        while True:
            frame = self._try_parse()
            if frame is None:
                break
            yield frame

    def _try_parse(self):
        buf = self._buf
        while len(buf) >= 2 and buf[:2] != b'\xAB\xCD':
            del buf[0]
        if len(buf) < 10:
            return None
        length = struct.unpack_from('>H', buf, 8)[0]
        total  = 10 + length + 2
        if len(buf) < total:
            return None
        frame_bytes = bytes(buf[:total])
        crc_data    = frame_bytes[3:10 + length]
        expected    = ProtocolCodec.calc_crc16(crc_data)
        received    = struct.unpack_from('>H', buf, 10 + length)[0]
        del buf[:total]
        if expected != received:
            return None
        return {
            'version': frame_bytes[2],
            'type':    frame_bytes[3],
            'src_id':  frame_bytes[4],
            'dst_id':  frame_bytes[5],
            'seq_num': struct.unpack_from('>H', frame_bytes, 6)[0],
            'length':  length,
            'payload': frame_bytes[10:10 + length],
        }


# ─── UartManager ─────────────────────────────────────────────────────────────

class UartManager:
    def __init__(self, on_frame, on_log):
        self._on_frame  = on_frame
        self._on_log    = on_log
        self._ser       = None
        self._running   = False
        self._tx_lock   = threading.Lock()
        self.rx_packets = 0
        self.rx_bytes   = 0
        self.tx_packets = 0
        self.tx_bytes   = 0
        self.crc_errors = 0

    @property
    def connected(self):
        return self._ser is not None and self._ser.is_open

    def connect(self, port: str, baud: int):
        self._ser     = serial.Serial(port, baud, timeout=0.1)
        self._running = True
        threading.Thread(target=self._rx_loop, daemon=True).start()

    def disconnect(self):
        self._running = False
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def send(self, frame_type: int, payload: bytes) -> bool:
        with self._tx_lock:
            if not self.connected:
                return False
            try:
                raw = ProtocolCodec.encode_uart(frame_type, payload)
                self._ser.write(raw)
                self.tx_packets += 1
                self.tx_bytes   += len(raw)
                self._on_log('UART TX', f"type=0x{frame_type:02X} {payload!r}")
                return True
            except serial.SerialException:
                return False

    def _rx_loop(self):
        parser = UartFrameParser()
        while self._running:
            try:
                byte = self._ser.read(1)
                if not byte:
                    continue
                frame = parser.feed(byte[0])
                if frame:
                    self.rx_packets += 1
                    self.rx_bytes   += 3 + frame['length'] + 1
                    self._on_log('UART RX', f"type=0x{frame['type']:02X} {frame['payload']!r}")
                    self._on_frame(frame)
            except serial.SerialException:
                break
        self._running = False


# ─── TcpManager ──────────────────────────────────────────────────────────────

class ClientInfo:
    def __init__(self, sock, addr, client_id: int):
        self.sock      = sock
        self.addr      = addr
        self.client_id = client_id
        self.last_hb   = time.time()
        self.parser    = TcpFrameParser()


class TcpManager:
    def __init__(self, on_frame, on_log, on_status):
        self._on_frame  = on_frame
        self._on_log    = on_log
        self._on_status = on_status
        self._mode      = None
        self._running   = False
        self._my_id     = TCP_ID_SERVER
        self._lock      = threading.Lock()

        self._server_sock = None
        self._clients     = {}        # cid -> ClientInfo
        self._free_ids    = [1, 2, 3]

        self._client_sock   = None
        self._client_parser = TcpFrameParser()
        self._client_seq    = 0
        self._client_hb_ts  = 0.0

        self.tx_packets = 0
        self.tx_bytes   = 0
        self.rx_packets = 0
        self.rx_bytes   = 0

    @property
    def running(self):
        return self._running

    def set_my_id(self, cid: int):
        self._my_id = cid

    def get_clients(self):
        with self._lock:
            return list(self._clients.values())

    # ── Server ──
    def start_server(self, host: str, port: int):
        self._mode    = 'server'
        self._my_id   = TCP_ID_SERVER
        self._running = True
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((host, port))
        self._server_sock.listen(3)
        threading.Thread(target=self._accept_loop, daemon=True).start()
        threading.Thread(target=self._server_hb_loop, daemon=True).start()
        self._on_status(f"서버 시작됨 ({host}:{port})", "blue")

    def _accept_loop(self):
        while self._running:
            try:
                sock, addr = self._server_sock.accept()
            except OSError:
                break
            with self._lock:
                if not self._free_ids:
                    sock.close()
                    self._on_log('TCP SYS', f"최대 연결 초과, 거부: {addr}")
                    continue
                cid  = self._free_ids.pop(0)
                info = ClientInfo(sock, addr, cid)
                self._clients[cid] = info
            self._send_raw(sock, self._build(TCP_TYPE_SYSTEM, TCP_ID_SERVER,
                                             cid, 0, f"ASID:{cid:02d}".encode()))
            self._broadcast_system(f"CONN:{cid:02d}", exclude=cid)
            self._on_log('TCP SYS', f"Client {cid} 연결됨 {addr}")
            self._on_status(self._status_str(), "green")
            threading.Thread(target=self._server_rx_loop, args=(cid,), daemon=True).start()

    def _server_rx_loop(self, cid: int):
        with self._lock:
            info = self._clients.get(cid)
        if not info:
            return
        while self._running:
            try:
                data = info.sock.recv(1024)
                if not data:
                    break
                self.rx_packets += 1
                self.rx_bytes   += len(data)
                for frame in info.parser.feed_bytes(data):
                    if frame['type'] == TCP_TYPE_HB:
                        info.last_hb = time.time()
                        self._send_raw(info.sock,
                                       self._build(TCP_TYPE_HB, TCP_ID_SERVER, cid, 0, b''))
                    else:
                        self._on_frame(frame, cid)
            except OSError:
                break
        self._remove_client(cid)

    def _remove_client(self, cid: int):
        with self._lock:
            info = self._clients.pop(cid, None)
            if info:
                self._free_ids.append(cid)
                self._free_ids.sort()
                try:
                    info.sock.close()
                except OSError:
                    pass
        if info:
            self._broadcast_system(f"DISC:{cid:02d}")
            self._on_log('TCP SYS', f"Client {cid} 연결 해제")
            n = len(self._clients)
            self._on_status(self._status_str(), "green" if n > 0 else "blue")

    def _server_hb_loop(self):
        while self._running:
            time.sleep(HEARTBEAT_INTERVAL)
            now = time.time()
            with self._lock:
                timed_out = [cid for cid, info in self._clients.items()
                             if now - info.last_hb > HEARTBEAT_TIMEOUT]
                snap = list(self._clients.values())
            for info in snap:
                self._send_raw(info.sock,
                               self._build(TCP_TYPE_HB, TCP_ID_SERVER,
                                           info.client_id, 0, b''))
            for cid in timed_out:
                self.kick_client(cid)

    def kick_client(self, cid: int):
        with self._lock:
            info = self._clients.get(cid)
        if info:
            try:
                info.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def _broadcast_system(self, msg: str, exclude: int = -1):
        payload = msg.encode()
        with self._lock:
            targets = [(cid, info) for cid, info in self._clients.items()
                       if cid != exclude]
        for cid, info in targets:
            self._send_raw(info.sock,
                           self._build(TCP_TYPE_SYSTEM, TCP_ID_SERVER, cid, 0, payload))

    def _status_str(self):
        return f"서버 동작 중 ({len(self._clients)}/3 클라이언트)"

    # ── Client ──
    def start_client(self, host: str, port: int):
        self._mode    = 'client'
        self._running = True
        threading.Thread(target=self._connect_loop, args=(host, port), daemon=True).start()

    def _connect_loop(self, host: str, port: int):
        while self._running:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((host, port))
                self._client_sock  = sock
                self._client_hb_ts = time.time()
                self._on_status(f"서버({host}:{port}) 연결됨", "green")
                threading.Thread(target=self._client_hb_sender, daemon=True).start()
                self._client_recv_loop()
                self._on_status("서버 연결 끊김 — 재연결 중...", "orange")
            except OSError:
                self._on_status("연결 실패 — 3초 후 재시도...", "red")
            if self._running:
                time.sleep(3)

    def _client_recv_loop(self):
        while self._running and self._client_sock:
            try:
                data = self._client_sock.recv(1024)
                if not data:
                    break
                self.rx_packets += 1
                self.rx_bytes   += len(data)
                for frame in self._client_parser.feed_bytes(data):
                    if frame['type'] == TCP_TYPE_HB:
                        self._client_hb_ts = time.time()
                        self._send_raw(self._client_sock,
                                       self._build(TCP_TYPE_HB, self._my_id,
                                                   TCP_ID_SERVER, 0, b''))
                    else:
                        self._on_frame(frame, None)
            except OSError:
                break

    def _client_hb_sender(self):
        while self._running and self._client_sock:
            time.sleep(HEARTBEAT_INTERVAL)
            if time.time() - self._client_hb_ts > HEARTBEAT_TIMEOUT:
                try:
                    self._client_sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                break

    # ── Common ──
    def send_to(self, dst_id: int, frame_type: int, payload: bytes):
        if self._mode == 'server':
            raw = self._build(frame_type, TCP_ID_SERVER, dst_id, 0, payload)
            with self._lock:
                if dst_id == TCP_DST_BROADCAST:
                    targets = list(self._clients.values())
                else:
                    info = self._clients.get(dst_id)
                    targets = [info] if info else []
            for info in targets:
                self._send_raw(info.sock, raw)
        elif self._mode == 'client' and self._client_sock:
            raw = self._build(frame_type, self._my_id, dst_id,
                              self._client_seq, payload)
            self._client_seq = (self._client_seq + 1) & 0xFFFF
            self._send_raw(self._client_sock, raw)

    def _build(self, frame_type, src_id, dst_id, seq_num, payload):
        return ProtocolCodec.encode_tcp(frame_type, src_id, dst_id, seq_num, payload)

    def _send_raw(self, sock, raw: bytes):
        try:
            sock.sendall(raw)
            self.tx_packets += 1
            self.tx_bytes   += len(raw)
        except OSError:
            pass

    def stop(self):
        self._running = False
        if self._server_sock:
            try:
                self._server_sock.close()
            except OSError:
                pass
        with self._lock:
            for info in self._clients.values():
                try:
                    info.sock.close()
                except OSError:
                    pass
            self._clients.clear()
            self._free_ids = [1, 2, 3]
        if self._client_sock:
            try:
                self._client_sock.close()
            except OSError:
                pass
        self._server_sock = self._client_sock = None
        self._mode = None


# ─── RelayEngine ─────────────────────────────────────────────────────────────

class RelayEngine:
    def __init__(self):
        self.uart_to_tcp = True
        self.tcp_to_uart = True
        self.broadcast_all = True
        self.target_ids    = [1, 2, 3]

        self.uart_rx_pkts  = 0
        self.uart_rx_bytes = 0
        self.uart_tx_pkts  = 0
        self.uart_tx_bytes = 0
        self.tcp_rx_pkts   = 0
        self.tcp_rx_bytes  = 0
        self.tcp_tx_pkts   = 0
        self.tcp_tx_bytes  = 0
        self.crc_errors    = 0
        self.dropped       = 0

    def on_uart_rx(self, frame: dict, tcp_mgr: 'TcpManager', log_cb):
        self.uart_rx_pkts  += 1
        self.uart_rx_bytes += frame['length']
        if not self.uart_to_tcp or not tcp_mgr or not tcp_mgr.running:
            self.dropped += 1
            return
        payload = frame['payload']
        if self.broadcast_all:
            tcp_mgr.send_to(TCP_DST_BROADCAST, TCP_TYPE_DATA, payload)
            self.tcp_tx_pkts  += 1
            self.tcp_tx_bytes += len(payload)
        else:
            for tid in self.target_ids:
                tcp_mgr.send_to(tid, TCP_TYPE_DATA, payload)
                self.tcp_tx_pkts  += 1
                self.tcp_tx_bytes += len(payload)
        log_cb('UART→TCP', payload.decode('utf-8', errors='replace'))

    def on_tcp_rx(self, frame: dict, uart_mgr: 'UartManager', log_cb):
        self.tcp_rx_pkts  += 1
        self.tcp_rx_bytes += frame['length']
        if frame['type'] != TCP_TYPE_DATA:
            return
        if not self.tcp_to_uart or not uart_mgr or not uart_mgr.connected:
            self.dropped += 1
            return
        payload = frame['payload']
        uart_mgr.send(UART_TYPE_CMD, payload)
        self.uart_tx_pkts  += 1
        self.uart_tx_bytes += len(payload)
        log_cb('TCP→UART', payload.decode('utf-8', errors='replace'))


# ─── RepeaterApp ─────────────────────────────────────────────────────────────

class RepeaterApp:
    POLL_MS       = 100
    STATS_POLL_MS = 500
    CLIENT_POLL_MS = 800

    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("UART ↔ TCP/IP Repeater  v1.0")
        root.geometry("860x820")
        root.resizable(True, True)

        self._setup_file_log()
        self._log_q: queue.Queue = queue.Queue()

        self._relay = RelayEngine()
        self._uart  = UartManager(self._on_uart_frame, self._enqueue_log)
        self._tcp   = TcpManager(self._on_tcp_frame, self._enqueue_log, self._on_tcp_status)

        self._auto_scroll    = tk.BooleanVar(value=True)
        self._tcp_mode       = tk.StringVar(value='server')
        self._broadcast_mode = tk.StringVar(value='all')
        self._sel_ids        = {1: tk.BooleanVar(value=True),
                                2: tk.BooleanVar(value=True),
                                3: tk.BooleanVar(value=True)}

        self._build_ui()
        self._refresh_ports()
        self._detect_local_ip()
        self._load_config()

        self.root.after(self.POLL_MS,        self._poll_log)
        self.root.after(self.STATS_POLL_MS,  self._poll_stats)
        self.root.after(self.CLIENT_POLL_MS, self._poll_clients)

    # ── File logging ──
    def _setup_file_log(self):
        self._flog = logging.getLogger('Repeater')
        self._flog.setLevel(logging.DEBUG)
        if not self._flog.handlers:
            fh = logging.FileHandler(LOG_FILE, encoding='utf-8')
            fh.setFormatter(logging.Formatter('%(asctime)s %(message)s'))
            self._flog.addHandler(fh)

    def _enqueue_log(self, direction: str, text: str):
        ts  = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        msg = f"[{ts}] {direction:<12} {text}"
        self._log_q.put(msg)
        self._flog.info(msg)

    def _poll_log(self):
        for _ in range(50):
            try:
                msg = self._log_q.get_nowait()
            except queue.Empty:
                break
            self._log_text.config(state='normal')
            self._log_text.insert(tk.END, msg + '\n')
            if self._auto_scroll.get():
                self._log_text.see(tk.END)
            self._log_text.config(state='disabled')
        self.root.after(self.POLL_MS, self._poll_log)

    # ── Frame callbacks ──
    def _on_uart_frame(self, frame: dict):
        self._relay.on_uart_rx(frame, self._tcp, self._enqueue_log)

    def _on_tcp_frame(self, frame: dict, client_id):
        if frame['type'] == TCP_TYPE_SYSTEM:
            msg = frame['payload'].decode('utf-8', errors='replace')
            self._enqueue_log('TCP SYS', msg)
            if msg.startswith('ASID:'):
                cid = int(msg[5:])
                self._tcp.set_my_id(cid)
        elif frame['type'] == TCP_TYPE_DATA:
            self._relay.on_tcp_rx(frame, self._uart, self._enqueue_log)

    def _on_tcp_status(self, msg: str, color: str):
        self._enqueue_log('SYSTEM', msg)
        self.root.after(0, lambda: self._tcp_status_lbl.config(
            text=f"상태: {msg}", foreground=color))

    # ── UI ──
    def _build_ui(self):
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(3, weight=1)
        self._build_top_panels(0)
        self._build_client_list(1)
        self._build_relay_panel(2)
        self._build_log_panel(3)
        self._build_stats_panel(4)
        self._build_manual_panel(5)

    def _build_top_panels(self, row):
        top = ttk.Frame(self.root)
        top.grid(row=row, column=0, sticky='ew', padx=8, pady=4)
        top.columnconfigure(0, weight=1)
        top.columnconfigure(1, weight=1)
        self._build_uart_panel(top)
        self._build_tcp_panel(top)

    def _build_uart_panel(self, parent):
        uf = ttk.LabelFrame(parent, text="UART 설정")
        uf.grid(row=0, column=0, sticky='nsew', padx=(0, 4))
        uf.columnconfigure(1, weight=1)

        ttk.Label(uf, text="COM 포트:").grid(row=0, column=0, sticky='w', padx=4, pady=2)
        self._com_combo = ttk.Combobox(uf, state='readonly', width=10)
        self._com_combo.grid(row=0, column=1, sticky='ew', padx=4)
        ttk.Button(uf, text="새로고침", width=7,
                   command=self._refresh_ports).grid(row=0, column=2, padx=4)

        ttk.Label(uf, text="Baud Rate:").grid(row=1, column=0, sticky='w', padx=4, pady=2)
        self._baud_combo = ttk.Combobox(uf, state='readonly', width=10,
                                         values=[9600, 19200, 38400, 57600, 115200])
        self._baud_combo.set(9600)
        self._baud_combo.grid(row=1, column=1, sticky='ew', padx=4)

        self._uart_btn = ttk.Button(uf, text="연결", command=self._toggle_uart)
        self._uart_btn.grid(row=2, column=0, columnspan=3, sticky='ew', padx=4, pady=4)
        self._uart_status = ttk.Label(uf, text="● 연결 안됨", foreground='red')
        self._uart_status.grid(row=3, column=0, columnspan=3, sticky='w', padx=6, pady=2)

    def _build_tcp_panel(self, parent):
        tf = ttk.LabelFrame(parent, text="TCP/IP 설정")
        tf.grid(row=0, column=1, sticky='nsew', padx=(4, 0))
        tf.columnconfigure(1, weight=1)

        ttk.Label(tf, text="모드:").grid(row=0, column=0, sticky='w', padx=4, pady=2)
        mf = ttk.Frame(tf)
        mf.grid(row=0, column=1, columnspan=3, sticky='w')
        ttk.Radiobutton(mf, text="Server", variable=self._tcp_mode,
                        value='server', command=self._update_tcp_ui).pack(side='left')
        ttk.Radiobutton(mf, text="Client", variable=self._tcp_mode,
                        value='client', command=self._update_tcp_ui).pack(side='left')

        ttk.Label(tf, text="내 IP:").grid(row=1, column=0, sticky='w', padx=4, pady=2)
        self._local_ip = ttk.Entry(tf, width=16)
        self._local_ip.grid(row=1, column=1, sticky='ew', padx=4)
        ttk.Label(tf, text="포트:").grid(row=1, column=2, sticky='w', padx=2)
        self._port_entry = ttk.Entry(tf, width=7)
        self._port_entry.insert(0, '54321')
        self._port_entry.grid(row=1, column=3, padx=4)

        ttk.Label(tf, text="상대 IP:").grid(row=2, column=0, sticky='w', padx=4, pady=2)
        self._remote_ip = ttk.Entry(tf, width=16)
        self._remote_ip.grid(row=2, column=1, sticky='ew', padx=4)
        self._remote_ip_note = ttk.Label(tf, text="(Client 전용)", foreground='gray')
        self._remote_ip_note.grid(row=2, column=2, columnspan=2, sticky='w')

        self._tcp_btn = ttk.Button(tf, text="서버 시작", command=self._toggle_tcp)
        self._tcp_btn.grid(row=3, column=0, columnspan=4, sticky='ew', padx=4, pady=4)
        self._tcp_status_lbl = ttk.Label(tf, text="상태: 중지됨", foreground='red')
        self._tcp_status_lbl.grid(row=4, column=0, columnspan=4, sticky='w', padx=6, pady=2)

    def _build_client_list(self, row):
        cf = ttk.LabelFrame(self.root, text="연결된 클라이언트 목록")
        cf.grid(row=row, column=0, sticky='ew', padx=8, pady=2)

        hdr = ttk.Frame(cf)
        hdr.pack(fill='x', padx=4)
        for txt, w in [('ID', 5), ('IP 주소', 18), ('상태', 10), ('', 10)]:
            ttk.Label(hdr, text=txt, width=w, anchor='center',
                      font=('', 9, 'bold')).pack(side='left', padx=2)

        self._client_rows: dict = {}
        for cid in range(1, 4):
            fr = ttk.Frame(cf)
            fr.pack(fill='x', padx=4, pady=1)
            lbl_id    = ttk.Label(fr, text=str(cid), width=5, anchor='center')
            lbl_ip    = ttk.Label(fr, text='-', width=18, anchor='center')
            lbl_state = ttk.Label(fr, text='대기 중 ○', width=10,
                                  foreground='gray', anchor='center')
            btn_kick  = ttk.Button(fr, text='강제해제', width=10,
                                   state='disabled',
                                   command=lambda c=cid: self._tcp.kick_client(c))
            for w in [lbl_id, lbl_ip, lbl_state, btn_kick]:
                w.pack(side='left', padx=2)
            self._client_rows[cid] = (lbl_ip, lbl_state, btn_kick)

    def _build_relay_panel(self, row):
        rf = ttk.LabelFrame(self.root, text="중계 설정")
        rf.grid(row=row, column=0, sticky='ew', padx=8, pady=2)

        self._u2t_var = tk.BooleanVar(value=True)
        self._t2u_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(rf, text="UART→TCP 중계", variable=self._u2t_var,
                        command=self._apply_relay).pack(side='left', padx=8, pady=4)
        ttk.Checkbutton(rf, text="TCP→UART 중계", variable=self._t2u_var,
                        command=self._apply_relay).pack(side='left', padx=8)

        ttk.Separator(rf, orient='vertical').pack(side='left', fill='y', padx=8, pady=4)
        ttk.Label(rf, text="브로드캐스트 대상:").pack(side='left')
        ttk.Radiobutton(rf, text="전체", variable=self._broadcast_mode,
                        value='all', command=self._apply_relay).pack(side='left')
        ttk.Radiobutton(rf, text="선택", variable=self._broadcast_mode,
                        value='select', command=self._apply_relay).pack(side='left')
        for cid, var in self._sel_ids.items():
            ttk.Checkbutton(rf, text=f"ID:{cid}", variable=var,
                            command=self._apply_relay).pack(side='left')

    def _build_log_panel(self, row):
        lf = ttk.LabelFrame(self.root, text="통신 로그")
        lf.grid(row=row, column=0, sticky='nsew', padx=8, pady=2)
        lf.columnconfigure(0, weight=1)
        lf.rowconfigure(1, weight=1)

        ctrl = ttk.Frame(lf)
        ctrl.grid(row=0, column=0, sticky='ew', padx=4, pady=2)
        ttk.Checkbutton(ctrl, text="자동 스크롤",
                        variable=self._auto_scroll).pack(side='left')
        ttk.Button(ctrl, text="지우기",
                   command=self._clear_log).pack(side='left', padx=6)
        ttk.Button(ctrl, text="저장...",
                   command=self._save_log).pack(side='left')

        self._log_text = scrolledtext.ScrolledText(
            lf, state='disabled', height=10, font=('Courier New', 9))
        self._log_text.grid(row=1, column=0, sticky='nsew', padx=4, pady=(0, 4))

    def _build_stats_panel(self, row):
        sf = ttk.LabelFrame(self.root, text="통계")
        sf.grid(row=row, column=0, sticky='ew', padx=8, pady=2)

        self._stat = {}
        items = [
            ('uart_rx', 'UART RX'),
            ('uart_tx', 'UART TX'),
            ('tcp_rx',  'TCP RX'),
            ('tcp_tx',  'TCP TX'),
            ('crc_err', 'CRC 오류'),
            ('dropped', '드롭'),
            ('hb',      'Heartbeat'),
        ]
        for i, (key, lbl) in enumerate(items):
            c = (i % 4) * 2
            r = i // 4
            ttk.Label(sf, text=f"{lbl}:").grid(row=r, column=c, sticky='e', padx=(8, 2), pady=2)
            v = ttk.Label(sf, text="0", width=16, anchor='w')
            v.grid(row=r, column=c + 1, sticky='w', padx=(0, 8))
            self._stat[key] = v

    def _build_manual_panel(self, row):
        mf = ttk.LabelFrame(self.root, text="수동 송신 (테스트용)")
        mf.grid(row=row, column=0, sticky='ew', padx=8, pady=(2, 8))
        mf.columnconfigure(4, weight=1)

        self._manual_target = tk.StringVar(value='uart')
        ttk.Radiobutton(mf, text="UART", variable=self._manual_target,
                        value='uart').grid(row=0, column=0, padx=6, pady=4)
        ttk.Radiobutton(mf, text="TCP",  variable=self._manual_target,
                        value='tcp').grid(row=0, column=1, padx=4)
        ttk.Label(mf, text="ID:").grid(row=0, column=2, padx=2)
        self._manual_id = ttk.Combobox(mf, values=['전체', '1', '2', '3'],
                                        width=5, state='readonly')
        self._manual_id.set('전체')
        self._manual_id.grid(row=0, column=3, padx=2)
        self._manual_entry = ttk.Entry(mf)
        self._manual_entry.grid(row=0, column=4, sticky='ew', padx=6)
        self._manual_entry.bind('<Return>', lambda e: self._manual_send())
        ttk.Button(mf, text="전송",
                   command=self._manual_send).grid(row=0, column=5, padx=6)

    # ── UART controls ──
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self._com_combo['values'] = ports
        if ports and not self._com_combo.get():
            self._com_combo.set(ports[0])

    def _toggle_uart(self):
        if self._uart.connected:
            self._uart.disconnect()
            self._uart_btn.config(text="연결")
            self._uart_status.config(text="● 연결 안됨", foreground='red')
            self._enqueue_log('SYSTEM', 'UART 연결 해제')
        else:
            port = self._com_combo.get()
            baud = int(self._baud_combo.get() or 9600)
            if not port:
                return
            try:
                self._uart.connect(port, baud)
                self._uart_btn.config(text="연결 해제")
                self._uart_status.config(text=f"● {port} 연결됨", foreground='green')
                self._enqueue_log('SYSTEM', f'UART 연결 {port} {baud}bps')
            except serial.SerialException as e:
                self._uart_status.config(text=f"오류: {e}", foreground='red')

    # ── TCP controls ──
    def _update_tcp_ui(self):
        is_client = self._tcp_mode.get() == 'client'
        self._tcp_btn.config(text="서버에 연결" if is_client else "서버 시작")
        self._remote_ip.config(state='normal' if is_client else 'disabled')

    def _toggle_tcp(self):
        if self._tcp.running:
            self._tcp.stop()
            self._tcp = TcpManager(self._on_tcp_frame, self._enqueue_log, self._on_tcp_status)
            self._update_tcp_ui()
            self._tcp_status_lbl.config(text="상태: 중지됨", foreground='red')
            self._enqueue_log('SYSTEM', 'TCP 중지')
        else:
            host = self._local_ip.get().strip()
            try:
                port = int(self._port_entry.get().strip())
            except ValueError:
                return
            if self._tcp_mode.get() == 'server':
                try:
                    self._tcp.start_server(host, port)
                    self._tcp_btn.config(text="서버 중지")
                except OSError as e:
                    self._tcp_status_lbl.config(text=f"오류: {e}", foreground='red')
            else:
                remote = self._remote_ip.get().strip()
                if not remote:
                    return
                self._tcp.start_client(remote, port)
                self._tcp_btn.config(text="연결 끊기")

    # ── Relay ──
    def _apply_relay(self):
        self._relay.uart_to_tcp   = self._u2t_var.get()
        self._relay.tcp_to_uart   = self._t2u_var.get()
        self._relay.broadcast_all = (self._broadcast_mode.get() == 'all')
        self._relay.target_ids    = [c for c, v in self._sel_ids.items() if v.get()]

    # ── Manual send ──
    def _manual_send(self):
        text = self._manual_entry.get().strip()
        if not text:
            return
        if self._manual_target.get() == 'uart':
            self._uart.send(UART_TYPE_CMD, text.encode('utf-8'))
            self._enqueue_log('MANUAL UART', text)
        else:
            id_str = self._manual_id.get()
            dst = TCP_DST_BROADCAST if id_str == '전체' else int(id_str)
            self._tcp.send_to(dst, TCP_TYPE_DATA, text.encode('utf-8'))
            self._enqueue_log(f'MANUAL TCP→{id_str}', text)
        self._manual_entry.delete(0, tk.END)

    # ── Log controls ──
    def _clear_log(self):
        self._log_text.config(state='normal')
        self._log_text.delete('1.0', tk.END)
        self._log_text.config(state='disabled')

    def _save_log(self):
        path = filedialog.asksaveasfilename(
            defaultextension='.txt',
            filetypes=[('Text files', '*.txt'), ('All files', '*.*')])
        if not path:
            return
        content = self._log_text.get('1.0', tk.END)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(content)

    # ── Polling ──
    def _poll_stats(self):
        r  = self._relay
        n  = len(self._tcp.get_clients()) if self._tcp.running else 0
        self._stat['uart_rx'].config(text=f"{r.uart_rx_pkts} pkts  {r.uart_rx_bytes} B")
        self._stat['uart_tx'].config(text=f"{r.uart_tx_pkts} pkts  {r.uart_tx_bytes} B")
        self._stat['tcp_rx'].config( text=f"{r.tcp_rx_pkts}  pkts  {r.tcp_rx_bytes} B")
        self._stat['tcp_tx'].config( text=f"{r.tcp_tx_pkts}  pkts  {r.tcp_tx_bytes} B")
        self._stat['crc_err'].config(text=str(r.crc_errors))
        self._stat['dropped'].config(text=str(r.dropped))
        self._stat['hb'].config(text=f"{n}/3" if self._tcp.running else "-")
        self.root.after(self.STATS_POLL_MS, self._poll_stats)

    def _poll_clients(self):
        clients = {info.client_id: info for info in self._tcp.get_clients()}
        for cid, (lbl_ip, lbl_state, btn_kick) in self._client_rows.items():
            if cid in clients:
                info = clients[cid]
                lbl_ip.config(text=str(info.addr[0]))
                lbl_state.config(text='연결됨 ●', foreground='green')
                btn_kick.config(state='normal')
            else:
                lbl_ip.config(text='-')
                lbl_state.config(text='대기 중 ○', foreground='gray')
                btn_kick.config(state='disabled')
        self.root.after(self.CLIENT_POLL_MS, self._poll_clients)

    # ── Local IP ──
    def _detect_local_ip(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(("8.8.8.8", 80))
                ip = s.getsockname()[0]
        except OSError:
            ip = "127.0.0.1"
        self._local_ip.insert(0, ip)

    # ── Config ──
    def _load_config(self):
        if not os.path.exists(CONFIG_FILE):
            self._update_tcp_ui()
            return
        try:
            with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
                cfg = json.load(f)
            self._com_combo.set(cfg.get('com_port', ''))
            self._baud_combo.set(cfg.get('baud_rate', 9600))
            self._remote_ip.delete(0, tk.END)
            self._remote_ip.insert(0, cfg.get('remote_ip', ''))
            self._port_entry.delete(0, tk.END)
            self._port_entry.insert(0, cfg.get('port', '54321'))
            self._tcp_mode.set(cfg.get('tcp_mode', 'server'))
        except Exception:
            pass
        self._update_tcp_ui()

    def _save_config(self):
        cfg = {
            'com_port':  self._com_combo.get(),
            'baud_rate': self._baud_combo.get(),
            'remote_ip': self._remote_ip.get(),
            'port':      self._port_entry.get(),
            'tcp_mode':  self._tcp_mode.get(),
        }
        try:
            with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
                json.dump(cfg, f, indent=2, ensure_ascii=False)
        except Exception:
            pass

    # ── Shutdown ──
    def on_closing(self):
        self._save_config()
        self._tcp.stop()
        self._uart.disconnect()
        self.root.destroy()


# ─── Entry point ─────────────────────────────────────────────────────────────

if __name__ == '__main__':
    root = tk.Tk()
    app  = RepeaterApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
