#!/usr/bin/env python3
"""
IP子目录图片监控与发送程序（严格模式）
功能：严格监控存在的input目录，优先处理最旧IP子目录，批量发送图片
改进：
1. 修复空目录阻塞问题
2. 支持输入参数和默认值
3. 支持自定义目标端口
使用方式：
# 使用默认目录和端口
python3 rst_send.py

# 指定目录和间隔
python3 rst_send.py --input-dir /path/to/dir --interval 10

# 使用自定义端口
python3 rst_send.py --input-dir /path/to/dir --target-port 9999

# 使用短参数
python3 rst_send.py -i /path/to/dir -t 3 -p 9999
"""

import argparse
import csv
import http.client
import json
import os
import socket
import subprocess
import threading
import time
from urllib.parse import urlparse
from typing import List, Tuple

import psutil

CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(CURRENT_DIR, "../../../"))

# 默认扫描根目录（多 service 模式下主要由 --config 决定 result_dir）
# Default scan root (multi-service mode uses --config result_dir)
DEFAULT_INPUT_DIR = os.path.join(PROJECT_ROOT, "workspace", "slave", "data")
LOG_DIR = os.path.join(PROJECT_ROOT, "workspace", "slave", "log")
SUB_REQ_DIR = os.path.join(LOG_DIR, "sub_reqs")
TASK_MAP_FILE = os.path.join(LOG_DIR, "task_map.jsonl")
DEFAULT_CSV_PATH = os.path.join(LOG_DIR, "sub_req_metrics.csv")
DEFAULT_SAMPLE_INTERVAL_SEC = 3
LOAD_SIM_STATE_FILE = os.path.join(LOG_DIR, "load_sim_state.json")

# 默认配置（仅默认值；建议由命令行显式传入）
DEFAULT_GATEWAY_HOST = "127.0.0.1"
DEFAULT_GATEWAY_PORT = 6666
DEFAULT_DEVICE_ID = "unknown"
DEFAULT_SLAVE_BACKEND_CONFIG = os.path.join(PROJECT_ROOT, "config_files", "slave_backend.json")

os.makedirs(LOG_DIR, exist_ok=True)
os.makedirs(SUB_REQ_DIR, exist_ok=True)

def read_load_sim_state(path: str) -> dict:
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}



def _safe_name(raw: str) -> str:
    out = []
    for ch in raw:
        if ch.isalnum() or ch in ("-", "_"):
            out.append(ch)
        else:
            out.append("_")
    return "".join(out) if out else "unknown"


def parse_ascend_dmi_output(output: str) -> dict:
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


def collect_npu_metrics() -> dict:
    try:
        result = subprocess.run(
            ["ascend-dmi", "-i", "-dt"],
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
        return parse_ascend_dmi_output(result.stdout)
    except Exception:
        return {
            "ai_core_info": {"usage_percent": 0},
            "cpu_info": {"ai_cpu_usage_percent": 0, "control_cpu_usage_percent": 0},
            "memory_info": {"total_mb": 0, "used_mb": 0, "bandwidth_usage_percent": 0},
            "temperature_c": 0.0,
        }


class MetricsSampler:
    def __init__(self, gateway_host: str, gateway_port: int, interval_sec: int):
        self.gateway_host = gateway_host
        self.gateway_port = int(gateway_port)
        self.interval_sec = interval_sec
        self._lock = threading.Lock()
        self._last_sample = {}
        self._last_net = psutil.net_io_counters()
        try:
            self._last_sample = self._sample()
        except Exception as exc:
            print(f"[metrics] initial sample failed: {exc}")
            self._last_sample = {}
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _measure_latency_ms(self) -> float:
        try:
            start = time.perf_counter()
            with socket.create_connection((self.gateway_host, self.gateway_port), timeout=1):
                pass
            return (time.perf_counter() - start) * 1000.0
        except Exception:
            return 0.0

    def _sample(self) -> dict:
        mem = psutil.virtual_memory()
        cpu = psutil.cpu_percent(interval=None)
        net_now = psutil.net_io_counters()
        up_kb = (net_now.bytes_sent - self._last_net.bytes_sent) / 1024.0
        down_kb = (net_now.bytes_recv - self._last_net.bytes_recv) / 1024.0
        self._last_net = net_now
        npu = collect_npu_metrics()
        if "read_load_sim_state" in globals():
            load_state = read_load_sim_state(LOAD_SIM_STATE_FILE)
        else:
            load_state = {}
        return {
            "timestamp_ms": int(time.time() * 1000),
            "host_cpu_util": cpu,
            "host_mem_util": mem.percent,
            "host_mem_used": mem.used / (1024 * 1024),
            "host_mem_total": mem.total / (1024 * 1024),
            "net_up_kb": up_kb,
            "net_down_kb": down_kb,
            "net_latency": self._measure_latency_ms(),
            "npu_ai_core_util": npu["ai_core_info"]["usage_percent"],
            "npu_ai_cpu_util": npu["cpu_info"]["ai_cpu_usage_percent"],
            "npu_ctrl_cpu_util": npu["cpu_info"]["control_cpu_usage_percent"],
            "npu_mem_total_mb": npu["memory_info"]["total_mb"],
            "npu_mem_used_mb": npu["memory_info"]["used_mb"],
            "npu_mem_bw_util": npu["memory_info"]["bandwidth_usage_percent"],
            "npu_temp": npu["temperature_c"],
            "active_io": 1 if load_state.get("active_io") else 0,
            "active_net": 1 if load_state.get("active_net") else 0,
            "active_yolo": 1 if load_state.get("active_yolo") else 0,
        }

    def _loop(self):
        psutil.cpu_percent(interval=None)
        while True:
            try:
                sample = self._sample()
            except Exception as exc:
                print(f"[metrics] sample failed: {exc}")
                time.sleep(self.interval_sec)
                continue
            with self._lock:
                self._last_sample = sample
            time.sleep(self.interval_sec)

    def latest(self) -> dict:
        with self._lock:
            return dict(self._last_sample)


class TaskMapIndex:
    def __init__(self, path: str):
        self.path = path
        self._pos = 0
        self._map = {}
        self._lock = threading.Lock()

    def _refresh(self):
        if not os.path.isfile(self.path):
            return
        with open(self.path, "r", encoding="utf-8") as f:
            f.seek(self._pos)
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    payload = json.loads(line)
                except Exception:
                    continue
                task_id = payload.get("task_id")
                sub_req_id = payload.get("sub_req_id")
                req_id = payload.get("req_id")
                if isinstance(task_id, str) and isinstance(sub_req_id, str):
                    self._map[task_id] = {
                        "sub_req_id": sub_req_id,
                        "req_id": req_id,
                        "tasktype": payload.get("tasktype", ""),
                    }
            self._pos = f.tell()

    def get(self, task_id: str) -> dict:
        with self._lock:
            if task_id not in self._map:
                self._refresh()
            return self._map.get(task_id, {})


class SubReqTracker:
    def __init__(self, sub_req_dir: str):
        self.sub_req_dir = sub_req_dir
        self._lock = threading.Lock()
        self._state = {}

    def _load_meta(self, sub_req_id: str) -> dict:
        path = os.path.join(self.sub_req_dir, f"{_safe_name(sub_req_id)}.json")
        if not os.path.isfile(path):
            return {"sub_req_id": sub_req_id, "sub_req_count": 0}
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {"sub_req_id": sub_req_id, "sub_req_count": 0}

    def on_task_done(self, sub_req_id: str) -> dict:
        with self._lock:
            state = self._state.get(sub_req_id)
            if not state:
                meta = self._load_meta(sub_req_id)
                state = {"done": 0, "meta": meta, "done_recorded": False}
                self._state[sub_req_id] = state
            state["done"] += 1
            total = int(state["meta"].get("sub_req_count") or 0)
            if total > 0 and state["done"] >= total and not state["done_recorded"]:
                state["done_recorded"] = True
                return state["meta"]
        return {}


class SubReqCsvWriter:
    HEADER = [
        "record_type",
        "timestamp_ms",
        "req_id",
        "sub_req_id",
        "tasktype",
        "client_ip",
        "service",
        "dst_device_id",
        "dst_device_ip",
        "sub_req_count",
        "start_time_ms",
        "end_time_ms",
        "expected_end_time_ms",
        "queue_len_at_start",
        "host_cpu_util",
        "host_mem_util",
        "host_mem_used",
        "host_mem_total",
        "net_up_kb",
        "net_down_kb",
        "net_latency",
        "npu_ai_core_util",
        "npu_ai_cpu_util",
        "npu_ctrl_cpu_util",
        "npu_mem_total_mb",
        "npu_mem_used_mb",
        "npu_mem_bw_util",
        "npu_temp",
        "active_io",
        "active_net",
        "active_yolo",
    ]

    def __init__(self, path: str):
        self.path = path
        self._lock = threading.Lock()
        if not os.path.isfile(self.path):
            with open(self.path, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(self.HEADER)

    def write_row(self, meta: dict, metrics: dict, record_type: str):
        row = [
            record_type,
            metrics.get("timestamp_ms", int(time.time() * 1000)),
            meta.get("req_id", ""),
            meta.get("sub_req_id", ""),
            meta.get("tasktype", ""),
            meta.get("client_ip", ""),
            meta.get("service", ""),
            meta.get("dst_device_id", ""),
            meta.get("dst_device_ip", ""),
            meta.get("sub_req_count", 0),
            meta.get("start_time_ms", ""),
            meta.get("end_time_ms", ""),
            meta.get("expected_end_time_ms", ""),
            meta.get("queue_len_at_start", ""),
            metrics.get("host_cpu_util", 0),
            metrics.get("host_mem_util", 0),
            metrics.get("host_mem_used", 0),
            metrics.get("host_mem_total", 0),
            metrics.get("net_up_kb", 0),
            metrics.get("net_down_kb", 0),
            metrics.get("net_latency", 0),
            metrics.get("npu_ai_core_util", 0),
            metrics.get("npu_ai_cpu_util", 0),
            metrics.get("npu_ctrl_cpu_util", 0),
            metrics.get("npu_mem_total_mb", 0),
            metrics.get("npu_mem_used_mb", 0),
            metrics.get("npu_mem_bw_util", 0),
            metrics.get("npu_temp", 0),
            metrics.get("active_io", 0),
            metrics.get("active_net", 0),
            metrics.get("active_yolo", 0),
        ]
        with self._lock:
            with open(self.path, "a", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(row)


class StrictIPSender:
    def __init__(
        self,
        input_dir: str,
        interval: int = 5,
        target_port: int = 8889,
        gateway_host: str = DEFAULT_GATEWAY_HOST,
        gateway_port: int = DEFAULT_GATEWAY_PORT,
        device_id: str = DEFAULT_DEVICE_ID,
        config_path: str = DEFAULT_SLAVE_BACKEND_CONFIG,
        service: str = "",
        csv_path: str = DEFAULT_CSV_PATH,
        sample_interval_sec: int = DEFAULT_SAMPLE_INTERVAL_SEC,
        enable_csv: bool = True,
    ):
        """
        Args:
            input_dir: 必须存在的input目录路径
            interval: 检查间隔(秒)
            target_port: 目标端口，用于发送文件
        """
        self.input_dir = os.path.abspath(input_dir)
        self.interval = interval
        self.target_port = target_port
        self.gateway_host = gateway_host
        self.gateway_port = int(gateway_port)
        self.device_id = device_id
        self.config_path = os.path.abspath(config_path) if config_path else ""
        self.service = service.strip()
        self.service_dirs = self._load_service_result_dirs()
        self.enable_csv = enable_csv
        self.task_map = TaskMapIndex(TASK_MAP_FILE)
        self.sub_req_tracker = SubReqTracker(SUB_REQ_DIR)
        self._done_tasks = set()
        self._done_lock = threading.Lock()
        if self.enable_csv:
            self.metrics_sampler = MetricsSampler(self.gateway_host, self.gateway_port, sample_interval_sec)
            self.csv_writer = SubReqCsvWriter(csv_path)
            self._start_collect_writer(sample_interval_sec)
        else:
            self.metrics_sampler = None
            self.csv_writer = None

        # 兼容旧模式：未配置服务目录时，仍按 input_dir 扫描 <ip>/...
        if not self.service_dirs:
            if not os.path.exists(self.input_dir):
                raise FileNotFoundError(f"directory not found: {self.input_dir}")
            if not os.path.isdir(self.input_dir):
                raise NotADirectoryError(f"not a directory: {self.input_dir}")
            print(f"[rst_send] single-dir mode: {self.input_dir}")
        else:
            for _, p in self.service_dirs:
                os.makedirs(p, exist_ok=True)
            print("[rst_send] multi-service mode:")
            for svc, p in self.service_dirs:
                print(f"  - {svc}: {p}")

    def _load_service_result_dirs(self) -> List[Tuple[str, str]]:
        if not self.config_path or not os.path.isfile(self.config_path):
            return []
        try:
            with open(self.config_path, "r", encoding="utf-8") as f:
                cfg = json.load(f)
        except Exception:
            return []

        services = cfg.get("services") if isinstance(cfg, dict) else None
        if not isinstance(services, dict):
            return []

        out: List[Tuple[str, str]] = []
        for name, entry in services.items():
            if not isinstance(name, str) or not isinstance(entry, dict):
                continue
            if self.service and name != self.service:
                continue
            result_dir = entry.get("result_dir")
            if not isinstance(result_dir, str) or not result_dir.strip():
                continue
            if not os.path.isabs(result_dir):
                result_dir = os.path.abspath(os.path.join(PROJECT_ROOT, result_dir))
            out.append((name, result_dir))
        return out

    def notify_gateway_task_completed(self, task_id: str, client_ip: str, service: str, status: str = "success") -> bool:
        """Notify gateway that a task finished (success or failure)."""
        try:
            conn = http.client.HTTPConnection(self.gateway_host, self.gateway_port, timeout=5)
            payload = json.dumps(
                {
                    "task_id": task_id,
                    "device_id": self.device_id,
                    "client_ip": client_ip,
                    "service": service,
                    "status": status,
                }
            ).encode("utf-8")
            headers = {"Content-Type": "application/json", "Content-Length": str(len(payload))}
            conn.request("POST", "/task_completed", body=payload, headers=headers)
            resp = conn.getresponse()
            _ = resp.read()
            return resp.status == 200
        except Exception as e:
            print(f"gateway notify error: {e}")
            return False
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def notify_gateway_task_result_ready(self, task_id: str) -> bool:
        """Notify gateway that the result is ready to send."""
        try:
            conn = http.client.HTTPConnection(self.gateway_host, self.gateway_port, timeout=5)
            payload = json.dumps({"task_id": task_id}).encode("utf-8")
            headers = {"Content-Type": "application/json", "Content-Length": str(len(payload))}
            conn.request("POST", "/task_result_ready", body=payload, headers=headers)
            resp = conn.getresponse()
            _ = resp.read()
            return resp.status == 200
        except Exception as e:
            print(f"gateway result-ready notify error: {e}")
            return False
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def notify_gateway_sub_req_done(self, meta: dict, status: str = "success") -> bool:
        """Notify gateway that a sub-request finished."""
        try:
            conn = http.client.HTTPConnection(self.gateway_host, self.gateway_port, timeout=5)
            payload = json.dumps(
                {
                    "req_id": meta.get("req_id", ""),
                    "sub_req_id": meta.get("sub_req_id", ""),
                    "status": status,
                }
            ).encode("utf-8")
            headers = {"Content-Type": "application/json", "Content-Length": str(len(payload))}
            conn.request("POST", "/sub_req_done", body=payload, headers=headers)
            resp = conn.getresponse()
            _ = resp.read()
            return resp.status == 200
        except Exception as e:
            print(f"gateway sub_req_done notify error: {e}")
            return False
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def _on_sub_req_completed(self, meta: dict):
        if not self.enable_csv:
            return
        sub_req_id = meta.get("sub_req_id")
        if not sub_req_id:
            return
        if not meta.get("start_time_ms"):
            meta["start_time_ms"] = int(time.time() * 1000)
        if not meta.get("expected_end_time_ms"):
            meta["expected_end_time_ms"] = 0
        meta["end_time_ms"] = int(time.time() * 1000)
        end_metrics = dict(self.metrics_sampler.latest())
        end_metrics["timestamp_ms"] = meta.get("end_time_ms")
        meta["end_metrics"] = end_metrics
        sub_req_path = os.path.join(SUB_REQ_DIR, f"{_safe_name(sub_req_id)}.json")
        try:
            tmp = sub_req_path + ".part"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(meta, f, ensure_ascii=False)
            os.replace(tmp, sub_req_path)
        except Exception:
            pass
        self.csv_writer.write_row(meta, end_metrics, "yolo_end")

    def _mark_task_done_once(self, task_id: str) -> bool:
        with self._done_lock:
            if task_id in self._done_tasks:
                return False
            self._done_tasks.add(task_id)
        mapping = self.task_map.get(task_id) if self.task_map else {}
        sub_req_id = mapping.get("sub_req_id") if isinstance(mapping, dict) else ""
        if sub_req_id and self.sub_req_tracker:
            meta = self.sub_req_tracker.on_task_done(sub_req_id)
            if meta:
                self.notify_gateway_sub_req_done(meta)
                if self.enable_csv:
                    self._on_sub_req_completed(meta)
        return True

    def _start_collect_writer(self, interval_sec: int):
        def loop():
            start_logged = set()
            while True:
                metrics = self.metrics_sampler.latest()
                for name in os.listdir(SUB_REQ_DIR):
                    if not name.endswith(".json"):
                        continue
                    path = os.path.join(SUB_REQ_DIR, name)
                    try:
                        with open(path, "r", encoding="utf-8") as f:
                            meta = json.load(f)
                    except Exception:
                        continue
                    sub_req_id = meta.get("sub_req_id")
                    if not sub_req_id or sub_req_id in start_logged:
                        continue
                    if meta.get("start_time_ms"):
                        start_metrics = dict(self.metrics_sampler.latest())
                        start_metrics["timestamp_ms"] = meta.get("start_time_ms")
                        meta["start_metrics"] = start_metrics
                        try:
                            tmp = path + ".part"
                            with open(tmp, "w", encoding="utf-8") as f:
                                json.dump(meta, f, ensure_ascii=False)
                            os.replace(tmp, path)
                        except Exception:
                            pass
                        meta_row = dict(meta)
                        meta_row["end_time_ms"] = ""
                        self.csv_writer.write_row(meta_row, start_metrics, "yolo_start")
                        start_logged.add(sub_req_id)
                self.csv_writer.write_row({}, metrics, "normal_collect")
                time.sleep(interval_sec)
        t = threading.Thread(target=loop, daemon=True)
        t.start()

    def get_ip_dirs_sorted(self, root_dir: str):
        """获取包含文件的IP子目录列表（旧优先）"""
        valid_ip_dirs = []
        try:
            for item in os.listdir(root_dir):
                item_path = os.path.join(root_dir, item)
                if os.path.isdir(item_path) and self._is_valid_ip(item):
                    # 检查目录是否包含文件
                    if any(
                        os.path.isfile(os.path.join(item_path, f))
                        for f in os.listdir(item_path)
                    ):
                        mtime = os.path.getmtime(item_path)
                        valid_ip_dirs.append((mtime, item, item_path))

            # 按修改时间排序（旧优先）
            valid_ip_dirs.sort(key=lambda x: x[0])
            return [(ip, path) for _, ip, path in valid_ip_dirs]
        except Exception as e:
            print(f"扫描目录出错: {e}")
            return []

    def _is_valid_ip(self, ip_str):
        """严格验证IP地址格式，同时允许localhost"""
        if ip_str.lower() == "localhost":
            return True

        parts = ip_str.split(".")
        if len(parts) != 4:
            return False
        try:
            return all(0 <= int(part) <= 255 for part in parts)
        except ValueError:
            return False

    def send_files_to_ip(self, service: str, ip: str, ip_path: str):
        """Send all files under a single IP directory."""
        files_to_send = []

        for filename in os.listdir(ip_path):
            file_path = os.path.join(ip_path, filename)
            if os.path.isfile(file_path):
                files_to_send.append((filename, file_path))

        if not files_to_send:
            print(f"[rst_send] no files under {ip}")
            return

        print(f"[rst_send] processing {ip} ({len(files_to_send)} files)")

        success_count = 0
        for filename, file_path in files_to_send:
            self.notify_gateway_task_result_ready(task_id=filename)
            mapping = self.task_map.get(filename) if self.task_map else {}
            req_id = mapping.get("req_id", "") if isinstance(mapping, dict) else ""
            sub_req_id = mapping.get("sub_req_id", "") if isinstance(mapping, dict) else ""
            if self._send_single_file(file_path, ip, service, req_id=req_id, sub_req_id=sub_req_id, task_id=filename):
                self._mark_task_done_once(filename)
                self.notify_gateway_task_completed(task_id=filename, client_ip=ip, service=service, status="success")
                success_count += 1
                try:
                    os.remove(file_path)
                    print(f"[rst_send] removed {filename}")
                except Exception as e:
                    print(f"[rst_send] remove failed {filename}: {e}")

        print(f"[rst_send] done {ip}: {success_count}/{len(files_to_send)} ok")

    def _send_single_file(self, file_path: str, ip: str, service: str, req_id: str = "", sub_req_id: str = "", task_id: str = "") -> bool:
        """发送单个文件到指定IP"""
        try:
            target_url = f"http://{self.gateway_host}:{self.gateway_port}/recv_rst"
            url_parts = urlparse(target_url)

            conn = http.client.HTTPConnection(
                host=url_parts.hostname, port=url_parts.port, timeout=10
            )

            boundary = "----" + str(time.time()).encode().hex()
            headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}

            with open(file_path, "rb") as f:
                file_content = f.read()

            filename = os.path.basename(file_path)
            body = b""
            body += f"--{boundary}\r\n".encode()
            body += f'Content-Disposition: form-data; name="service"\r\n\r\n{service}\r\n'.encode()
            body += f"--{boundary}\r\n".encode()
            body += f'Content-Disposition: form-data; name="client_ip"\r\n\r\n{ip}\r\n'.encode()
            body += f"--{boundary}\r\n".encode()
            body += f'Content-Disposition: form-data; name="client_port"\r\n\r\n{self.target_port}\r\n'.encode()
            if task_id:
                body += f"--{boundary}\r\n".encode()
                body += f'Content-Disposition: form-data; name="task_id"\r\n\r\n{task_id}\r\n'.encode()
            if req_id:
                body += f"--{boundary}\r\n".encode()
                body += f'Content-Disposition: form-data; name="req_id"\r\n\r\n{req_id}\r\n'.encode()
            if sub_req_id:
                body += f"--{boundary}\r\n".encode()
                body += f'Content-Disposition: form-data; name="sub_req_id"\r\n\r\n{sub_req_id}\r\n'.encode()
            body += f"--{boundary}\r\n".encode()
            body += (
                f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
                f"Content-Type: application/octet-stream\r\n\r\n"
            ).encode()
            body += file_content
            body += f"\r\n--{boundary}--\r\n".encode()

            conn.request("POST", url_parts.path, body, headers)
            response = conn.getresponse()
            response.read()  # 必须读取响应数据

            if response.status == 200:
                print(f"📤 发送成功: {filename} -> gateway {self.gateway_host}:{self.gateway_port}")
                return True
            else:
                print(f"❌ 发送失败到 gateway [{response.status}]")
                return False

        except Exception as e:
            print(f"⚠️  发送到 gateway 出错: {e}")
            return False

    def process_next_ip(self):
        """Process the oldest IP directory across services."""
        candidates: List[Tuple[float, str, str, str]] = []

        if self.service_dirs:
            for svc, root_dir in self.service_dirs:
                ip_dirs = self.get_ip_dirs_sorted(root_dir)
                if not ip_dirs:
                    continue
                ip, ip_path = ip_dirs[0]
                try:
                    mtime = os.path.getmtime(ip_path)
                except Exception:
                    mtime = time.time()
                candidates.append((mtime, svc, ip, ip_path))
        else:
            ip_dirs = self.get_ip_dirs_sorted(self.input_dir)
            if ip_dirs:
                ip, ip_path = ip_dirs[0]
                try:
                    mtime = os.path.getmtime(ip_path)
                except Exception:
                    mtime = time.time()
                candidates.append((mtime, "default", ip, ip_path))

        if not candidates:
            print(f"[rst_send] no valid ip dir, wait {self.interval}s...")
            return False

        candidates.sort(key=lambda x: x[0])
        _, svc, ip, ip_path = candidates[0]
        self.send_files_to_ip(svc, ip, ip_path)
        return True

    def run(self):
        """Start strict monitoring loop."""
        print("[rst_send] strict monitor start...")
        print("=" * 50)

        try:
            while True:
                if not self.process_next_ip():
                    time.sleep(self.interval)
        except KeyboardInterrupt:
            print("[rst_send] stopped")
        except Exception as e:
            print(f"[rst_send] error: {e}")

def parse_args():
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="rst_send monitor and sender")
    parser.add_argument(
        "--input-dir",
        "-i",
        default=DEFAULT_INPUT_DIR,
        help=f"监控目录路径 (默认: {DEFAULT_INPUT_DIR})",
    )
    parser.add_argument(
        "--config",
        default=DEFAULT_SLAVE_BACKEND_CONFIG,
        help=f"slave backend config (default: {DEFAULT_SLAVE_BACKEND_CONFIG})",
    )
    parser.add_argument(
        "--service",
        default="",
        help="only send results for a specific service name (e.g. YoloV5)",
    )
    parser.add_argument(
        "--interval", "-t", type=int, default=5, help="检查间隔时间(秒) (默认: 5)"
    )
    parser.add_argument(
        "--target-port",
        "-p",
        type=int,
        default=8889,
        help="目标端口 (默认: 8889)",
    )
    parser.add_argument(
        "--gateway-host",
        default=DEFAULT_GATEWAY_HOST,
        help=f"gateway host (default: {DEFAULT_GATEWAY_HOST})",
    )
    parser.add_argument(
        "--gateway-port",
        type=int,
        default=DEFAULT_GATEWAY_PORT,
        help=f"gateway port (default: {DEFAULT_GATEWAY_PORT})",
    )
    parser.add_argument(
        "--device-id",
        default=DEFAULT_DEVICE_ID,
        help=f"device id (default: {DEFAULT_DEVICE_ID})",
    )
    parser.add_argument(
        "--csv-path",
        default=DEFAULT_CSV_PATH,
        help=f"sub_req csv output path (default: {DEFAULT_CSV_PATH})",
    )
    parser.add_argument(
        "--sample-interval",
        type=int,
        default=DEFAULT_SAMPLE_INTERVAL_SEC,
        help=f"metrics sample interval seconds (default: {DEFAULT_SAMPLE_INTERVAL_SEC})",
    )
    parser.add_argument(
        "--enable-csv",
        action="store_true",
        help="enable csv output for sub_req metrics",
    )
    parser.add_argument(
        "--disable-csv",
        action="store_true",
        help="disable csv output for sub_req metrics",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    enable_csv = True
    if args.disable_csv:
        enable_csv = False
    if args.enable_csv:
        enable_csv = True
    try:
        sender = StrictIPSender(
            input_dir=args.input_dir,
            interval=args.interval,
            target_port=args.target_port,
            gateway_host=args.gateway_host,
            gateway_port=args.gateway_port,
            device_id=args.device_id,
            config_path=args.config,
            service=args.service,
            csv_path=args.csv_path,
            sample_interval_sec=args.sample_interval,
            enable_csv=enable_csv,
        )
        sender.run()
    except (FileNotFoundError, NotADirectoryError) as e:
        print(f"[rst_send] init failed: {e}")
        print("Please ensure:")
        print(f"1. input dir exists: {args.input_dir}")
        print("2. path is a directory")
        exit(1)
