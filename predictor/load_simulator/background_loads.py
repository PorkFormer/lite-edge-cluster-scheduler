import os
import socket
import tempfile
import threading
import time
from multiprocessing import Event, Process

import psutil


class LoadInstance:
    def __init__(self, load_type, runner):
        self.load_type = load_type
        self.runner = runner

    def stop(self):
        self.runner.stop()


class CpuLoadRunner:
    def __init__(self, work_units):
        self.work_units = int(work_units)
        self._stop = Event()
        self._proc = Process(target=_cpu_worker, args=(self.work_units, self._stop))

    def start(self):
        self._proc.start()

    def stop(self):
        self._stop.set()
        if self._proc.is_alive():
            self._proc.join(timeout=2.0)
        if self._proc.is_alive():
            self._proc.terminate()
            self._proc.join(timeout=2.0)


class IoLoadRunner:
    def __init__(self, mb_per_sec):
        self.mb_per_sec = float(mb_per_sec)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="io_load")
        self._path = os.path.join(
            tempfile.gettempdir(), f"loadsim_io_{os.getpid()}_{id(self)}.dat"
        )

    def start(self):
        self._thread.daemon = True
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self):
        chunk = b"0" * (1024 * 1024)
        while not self._stop.is_set():
            target_mb = max(0.0, self.mb_per_sec)
            if target_mb <= 0:
                time.sleep(0.2)
                continue
            start = time.perf_counter()
            written = 0
            with open(self._path, "wb") as f:
                while written < target_mb and not self._stop.is_set():
                    f.write(chunk)
                    written += 1
                f.flush()
            elapsed = time.perf_counter() - start
            if elapsed < 1.0:
                time.sleep(1.0 - elapsed)


class NetLoadRunner:
    def __init__(self, mb_per_sec, target_addr, bind_ip=None, iface=None):
        self.mb_per_sec = float(mb_per_sec)
        self.target_addr = target_addr
        self.bind_ip = bind_ip
        self.iface = iface
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="net_load")

    def start(self):
        self._thread.daemon = True
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if self.iface:
            try:
                sock.setsockopt(socket.SOL_SOCKET, 25, self.iface.encode())
            except OSError:
                pass
        if self.bind_ip:
            try:
                sock.bind((self.bind_ip, 0))
            except OSError:
                pass
        target_addr = self.target_addr
        payload = b"1" * 1400
        payload_mb = len(payload) / (1024 * 1024)
        try:
            while not self._stop.is_set():
                target = max(0.0, self.mb_per_sec)
                if target <= 0:
                    time.sleep(0.2)
                    continue
                start = time.perf_counter()
                sent_mb = 0.0
                while sent_mb < target and not self._stop.is_set():
                    sock.sendto(payload, target_addr)
                    sent_mb += payload_mb
                elapsed = time.perf_counter() - start
                if elapsed < 1.0:
                    time.sleep(1.0 - elapsed)
        finally:
            sock.close()


def _cpu_worker(work_units, stop_event):
    work_units = max(1, int(work_units))
    while not stop_event.is_set():
        start = time.perf_counter()
        acc = 0
        for _ in range(work_units):
            acc = (acc * 1664525 + 1013904223) & 0xFFFFFFFF
        elapsed = time.perf_counter() - start
        if elapsed < 1.0:
            time.sleep(1.0 - elapsed)


class BackgroundManager:
    def __init__(self):
        self._lock = threading.Lock()
        self._active_counts = {"io": 0, "net": 0, "cpu": 0}

    def start_load(self, load_cfg):
        load_type = load_cfg.get("type")
        count = int(load_cfg.get("count", 1))
        if count < 1:
            return []
        if load_type == "cpu":
            runners = [
                CpuLoadRunner(load_cfg.get("work_units", 1000000))
                for _ in range(count)
            ]
        elif load_type == "io":
            runners = [IoLoadRunner(load_cfg.get("mb_per_sec", 10)) for _ in range(count)]
        elif load_type == "net":
            target_ip = load_cfg.get("target_ip", "127.0.0.1")
            target_port = int(load_cfg.get("target_port", 9999))
            iface = load_cfg.get("iface")
            bind_ip = _resolve_iface_ip(iface) if iface else None
            runners = [
                NetLoadRunner(
                    load_cfg.get("mb_per_sec", 5),
                    target_addr=(target_ip, target_port),
                    bind_ip=bind_ip,
                    iface=iface,
                )
                for _ in range(count)
            ]
        else:
            return []
        for runner in runners:
            runner.start()
        with self._lock:
            if load_type in self._active_counts:
                self._active_counts[load_type] += len(runners)
        return [LoadInstance(load_type, runner) for runner in runners]

    def stop_loads(self, instances):
        for inst in instances:
            inst.stop()
            with self._lock:
                if inst.load_type in self._active_counts:
                    self._active_counts[inst.load_type] -= 1

    def snapshot_counts(self):
        with self._lock:
            return dict(self._active_counts)


def _resolve_iface_ip(iface):
    if not iface:
        return None
    addrs = psutil.net_if_addrs().get(iface, [])
    for addr in addrs:
        if addr.family == socket.AF_INET:
            return addr.address
    return None
