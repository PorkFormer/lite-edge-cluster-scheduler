import os
import time


def process_yolo_task(input_dir, output_dir, task_num):
    if not os.path.isdir(input_dir):
        return 0
    subdirs = [
        os.path.join(input_dir, name)
        for name in sorted(os.listdir(input_dir))
        if os.path.isdir(os.path.join(input_dir, name))
    ]
    processed = 0
    if subdirs:
        targets = subdirs[: max(1, int(task_num))]
        for folder in targets:
            processed += _process_folder(folder, output_dir)
    else:
        processed += _process_folder(input_dir, output_dir)
    return processed


def _process_folder(folder, output_dir):
    count = 0
    for root, _, files in os.walk(folder):
        for name in files:
            path = os.path.join(root, name)
            try:
                with open(path, "rb") as f:
                    _ = f.read(1024)
            except Exception:
                continue
            _busy_work()
            count += 1
    _write_marker(output_dir, os.path.basename(folder), count)
    return count


def _busy_work():
    acc = 0
    for _ in range(50000):
        acc = (acc * 1103515245 + 12345) & 0x7FFFFFFF
    return acc


def _write_marker(output_dir, folder_name, count):
    os.makedirs(output_dir, exist_ok=True)
    ts = int(time.time() * 1000)
    marker = os.path.join(output_dir, f"yolo_{folder_name}_{ts}.done")
    try:
        with open(marker, "w", encoding="utf-8") as f:
            f.write(f"processed_files={count}\n")
    except Exception:
        pass
