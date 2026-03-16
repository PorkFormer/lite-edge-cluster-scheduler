import platform
import subprocess
import threading
import time

import psutil


class Monitor:
    def __init__(self, interval_sec, metrics_store, background_manager, logger):
        self.interval_sec = max(0.1, float(interval_sec))
        self.metrics_store = metrics_store
        self.background_manager = background_manager
        self.logger = logger
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="monitor")
        self._last_net = psutil.net_io_counters()
        self._last_time = time.time()

    def start(self):
        self._thread.daemon = True
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self):
        while not self._stop.is_set():
            metrics = self.collect_metrics()
            self.metrics_store.update(metrics)
            self.logger.log_normal(metrics)
            time.sleep(self.interval_sec)

    def collect_metrics(self):
        cpu_util = psutil.cpu_percent(interval=None)
        vm = psutil.virtual_memory()
        host_mem_total = vm.total // (1024 * 1024)
        host_mem_used = vm.used // (1024 * 1024)
        host_mem_util = vm.percent

        net_up_kb, net_down_kb = self._get_net_bandwidth()
        net_latency = _ping_latency("127.0.0.1")

        npu_metrics = {
            "npu_ai_core_util": 0,
            "npu_ai_cpu_util": 0,
            "npu_ctrl_cpu_util": 0,
            "npu_mem_total_mb": 0,
            "npu_mem_used_mb": 0,
            "npu_mem_bw_util": 0,
            "npu_temp": 0,
        }

        metrics = {
            "host_cpu_util": cpu_util,
            "host_mem_util": host_mem_util,
            "host_mem_used": host_mem_used,
            "host_mem_total": host_mem_total,
            "net_up_kb": net_up_kb,
            "net_down_kb": net_down_kb,
            "net_latency": net_latency,
        }
        metrics.update(npu_metrics)
        return metrics

    def _get_net_bandwidth(self):
        current = psutil.net_io_counters()
        now = time.time()
        dt = now - self._last_time
        if dt <= 0:
            return 0.0, 0.0
        sent = current.bytes_sent - self._last_net.bytes_sent
        recv = current.bytes_recv - self._last_net.bytes_recv
        self._last_net = current
        self._last_time = now
        return round(sent / 1024 / dt, 1), round(recv / 1024 / dt, 1)


def _ping_latency(target):
    system = platform.system().lower()
    if system == "windows":
        command = ["ping", "-n", "1", "-w", "1000", target]
    else:
        command = ["ping", "-c", "1", "-W", "1", target]
    try:
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            return -1.0
        for token in result.stdout.split():
            if "time=" in token:
                value = token.split("=")[-1].replace("ms", "")
                return float(value)
        return -1.0
    except Exception:
        return -1.0
