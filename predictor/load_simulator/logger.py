import csv
import os
import time


CSV_COLUMNS = [
    "record_type",
    "time",
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
    "task_num",
    "task_type",
    "start_time_ms",
    "end_time_ms",
    "queue_len_at_start",
]


class CsvLogger:
    def __init__(self, output_dir, device_name):
        os.makedirs(output_dir, exist_ok=True)
        ts = time.strftime("%Y%m%d_%H%M%S")
        filename = f"monitor_{device_name}_{ts}.csv"
        self.path = os.path.join(output_dir, filename)
        self._file = open(self.path, "w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._file)
        self._writer.writerow(CSV_COLUMNS)
        self._file.flush()

    def log_row(self, row):
        self._writer.writerow([row.get(col, "") for col in CSV_COLUMNS])
        self._file.flush()

    def log_normal(self, metrics):
        row = _build_base_row("normal_collect", metrics)
        self.log_row(row)

    def log_task_start(self, task_type, task_num, start_time_ms, queue_len_at_start, metrics):
        row = _build_base_row("node_task_start", metrics)
        row.update(
            {
                "task_num": task_num,
                "task_type": task_type,
                "start_time_ms": start_time_ms,
                "queue_len_at_start": queue_len_at_start,
            }
        )
        self.log_row(row)

    def log_task_end(self, task_type, task_num, start_time_ms, end_time_ms, metrics):
        row = _build_base_row("node_task_end", metrics)
        row.update(
            {
                "task_num": task_num,
                "task_type": task_type,
                "start_time_ms": start_time_ms,
                "end_time_ms": end_time_ms,
            }
        )
        self.log_row(row)

    def close(self):
        if self._file:
            self._file.close()
            self._file = None


def _build_base_row(record_type, metrics):
    row = {
        "record_type": record_type,
        "time": time.strftime("%H:%M:%S"),
    }
    if metrics:
        row.update(metrics)
    return row
