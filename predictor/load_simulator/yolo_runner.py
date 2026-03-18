import os
from pathlib import Path
from types import SimpleNamespace

from yolo.acl_net import Net  # type: ignore
from yolo.constant import IMG_EXT  # type: ignore
from yolo.detect_yolov5_ascend import (  # type: ignore
    check_ret,
    load_label,
    process_image,
)
import acl  # type: ignore


class YoloRunner:
    def __init__(
        self,
        weights=None,
        labels=None,
        imgsz=(640, 640),
        device=0,
        conf_thres=0.25,
        iou_thres=0.45,
        max_det=1000,
        agnostic_nms=False,
        output_format="all",
        verbose=False,
    ):
        yolo_root = Path(__file__).resolve().parent / "yolo"
        self._acl = acl
        self._net_cls = Net
        self._check_ret = check_ret
        self._load_label = load_label
        self._process_image = process_image
        self._img_exts = {ext.lower() for ext in IMG_EXT}
        self._conf_thres = conf_thres
        self._iou_thres = iou_thres
        self._max_det = max_det
        self._agnostic_nms = agnostic_nms
        self._verbose = verbose

        if weights is None:
            weights = yolo_root / "ascend" / "yolov5s.om"
        if labels is None:
            labels = yolo_root / "ascend" / "yolov5.label"

        self._weights = str(weights)
        self._labels = str(labels)
        self._imgsz = tuple(int(x) for x in imgsz)
        self._device = int(device)
        self._output_format = output_format

        ret = self._acl.init()
        self._check_ret("acl.init", ret)
        self._net = self._net_cls(self._device, self._weights)
        self._label_list = self._load_label(self._labels)
        self._opt = SimpleNamespace(output_format=self._output_format)
        print(
            f"[YOLO] model loaded weights={self._weights} labels={self._labels}",
            flush=True,
        )

    def close(self):
        if self._net is not None:
            self._net.destroy()
            self._net = None
        if self._acl is not None:
            self._acl.finalize()
            self._acl = None

    def _collect_images(self, input_path, max_images=None, file_prefix=None):
        if os.path.isfile(input_path):
            return [(input_path, os.path.basename(input_path))], Path(input_path).parent
        if not os.path.isdir(input_path):
            return [], None

        images = []
        for root, _, files in os.walk(input_path):
            for name in files:
                if Path(name).suffix.lower() in self._img_exts:
                    full_path = Path(root) / name
                    rel_path = full_path.relative_to(input_path)
                    if file_prefix and not os.path.basename(rel_path).startswith(file_prefix):
                        continue
                    images.append((str(full_path), str(rel_path)))
        if max_images is not None:
            images = images[: max(1, int(max_images))]
        return images, Path(input_path)

    def run_folder(self, input_path, output_dir, max_images=None, file_prefix=None):
        images, input_root = self._collect_images(
            input_path, max_images=max_images, file_prefix=file_prefix
        )
        if not images:
            return 0

        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        processed = 0
        for image_path, rel_path in images:
            result, elapsed = self._process_image(
                image_path,
                rel_path,
                self._net,
                self._label_list,
                self._imgsz,
                self._conf_thres,
                self._iou_thres,
                self._agnostic_nms,
                self._max_det,
                None,
                self._opt,
                str(input_root),
                str(output_dir),
                verbose=self._verbose,
            )
            processed += 1
            if self._verbose:
                summary = result if result else "No detections"
                print(f"[{processed}/{len(images)}] {rel_path} -> {summary} ({elapsed:.3f}s)")
        return processed
