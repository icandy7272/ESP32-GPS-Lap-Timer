"""Render SVG views of new sandwich-layout case."""
from pathlib import Path
import cadquery as cq
from cadquery import exporters

out = Path(__file__).parent
case = cq.importers.importStep(str(out / "case_preview_base.step"))

views = [
    ("iso",        (1.2, -1.0, 0.8)),   # 等轴侧
    ("front",      (0, 0, 1)),          # 正面（TFT 那一面）
    ("back",       (0, 0, -1)),         # 背面
    ("top",        (0, 1, 0)),          # 顶面（GPS 那一面）
    ("side_right", (1, 0, 0)),          # 右面
    ("side_left",  (-1, 0, 0)),         # 左面（按键）
]

for name, proj in views:
    opts = {
        "projectionDir": proj,
        "strokeColor": (40, 40, 40),
        "hiddenColor": (200, 200, 200),
        "showHidden": False,
        "width": 800,
        "height": 600,
        "marginLeft": 40,
        "marginTop": 40,
    }
    exporters.export(case, str(out / f"view_{name}.svg"), opt=opts)
    print(f"wrote view_{name}.svg")

print("done.")
