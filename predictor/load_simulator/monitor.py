import platform
import subprocess
import threading
import time

import psutil


class Monitor:
    def __init__(self, interval_sec, metrics_store, background_manager, logger, latency_target):
        self.interval_sec = max(0.1, float(interval_sec))
        self.metrics_store = metrics_store
        self.background_manager = background_manager
        self.logger = logger
        self.latency_target = latency_target
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

    # collect_metrics gathers host and network metrics, and placeholders for NPU metrics
    def collect_metrics(self):
        # cpu
        cpu_util = psutil.cpu_percent(interval=None)
        # memory
        vm = psutil.virtual_memory()
        host_mem_total = vm.total // (1024 * 1024)
        host_mem_used = vm.used // (1024 * 1024)
        host_mem_util = vm.percent
        # network
        net_up_kb, net_down_kb = self._get_net_bandwidth()
        net_latency = _ping_latency(self.latency_target)
        # npu
        npu = _collect_npu_metrics()
        npu_metrics = {
            "npu_ai_core_util": npu["ai_core_info"]["usage_percent"],
            "npu_ai_cpu_util": npu["cpu_info"]["ai_cpu_usage_percent"],
            "npu_ctrl_cpu_util": npu["cpu_info"]["control_cpu_usage_percent"],
            "npu_mem_total_mb": npu["memory_info"]["total_mb"],
            "npu_mem_used_mb": npu["memory_info"]["used_mb"],
            "npu_mem_bw_util": npu["memory_info"]["bandwidth_usage_percent"],
            "npu_temp": npu["temperature_c"],
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


def _parse_ascend_dmi_output(output):
    npu_data = {
        "ai_core_info": {"usage_percent": 0},
        "cpu_info": {"ai_cpu_usage_percent": 0, "control_cpu_usage_percent": 0},
        "memory_info": {"total_mb": 0, "used_mb": 0, "bandwidth_usage_percent": 0},
        "temperature_c": 0.0,
    }
    current_section = None
    for line in output.split("\n"):
        line = line.strip()
        if not line:
            continue
        if "Information" in line:
            current_section = line.strip()
        parts = line.split(":", 1)
        if len(parts) != 2:
            continue
        key, value = parts[0].strip(), parts[1].strip()
        if key.startswith("Temperature"):
            try:
                npu_data["temperature_c"] = float(value.split()[0])
            except ValueError:
                pass
            continue
        if current_section == "AI Core Information":
            if key == "AI Core Usage (%)":
                try:
                    npu_data["ai_core_info"]["usage_percent"] = int(value)
                except ValueError:
                    pass
        elif current_section == "CPU Information":
            if key == "AI CPU Usage (%)":
                try:
                    npu_data["cpu_info"]["ai_cpu_usage_percent"] = int(value)
                except ValueError:
                    pass
            elif key == "Control CPU Usage (%)":
                try:
                    npu_data["cpu_info"]["control_cpu_usage_percent"] = int(value)
                except ValueError:
                    pass
        elif current_section == "Memory Information":
            if key == "Total (MB)":
                try:
                    npu_data["memory_info"]["total_mb"] = int(value)
                except ValueError:
                    pass
            elif key == "Used (MB)":
                try:
                    npu_data["memory_info"]["used_mb"] = int(value)
                except ValueError:
                    pass
            elif key == "Bandwidth Usage (%)":
                try:
                    npu_data["memory_info"]["bandwidth_usage_percent"] = int(value)
                except ValueError:
                    pass
    return npu_data


def _collect_npu_metrics():
    try:
        result = subprocess.run(
            ["ascend-dmi", "-i", "-dt"],
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
        return _parse_ascend_dmi_output(result.stdout)
    except Exception:
        return {
            "ai_core_info": {"usage_percent": 0},
            "cpu_info": {"ai_cpu_usage_percent": 0, "control_cpu_usage_percent": 0},
            "memory_info": {"total_mb": 0, "used_mb": 0, "bandwidth_usage_percent": 0},
            "temperature_c": 0.0,
        }


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
