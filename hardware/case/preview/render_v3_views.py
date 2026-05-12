"""Render SVG views of v3 (front + back + assembled + exploded)."""
from pathlib import Path
import cadquery as cq
from cadquery import exporters

out = Path(__file__).parent

# Load exploded (most informative - shows separation)
exploded = cq.importers.importStep(str(out / "case_v3_exploded.step"))
assembled = cq.importers.importStep(str(out / "case_v3_assembled.step"))
front = cq.importers.importStep(str(out / "case_v3_front.step"))
back = cq.importers.importStep(str(out / "case_v3_back.step"))

views = [
    ("v3_iso_assembled", assembled, (1.2, -1.0, 0.8)),
    ("v3_iso_exploded", exploded, (1.2, -1.0, 0.8)),
    ("v3_front_only", front, (1.2, -1.0, 0.8)),
    ("v3_back_only", back, (1.2, -1.0, 0.8)),
    ("v3_top", assembled, (0, 1, 0)),
    ("v3_side", assembled, (1, 0, 0)),
    ("v3_front_face", assembled, (0, 0, 1)),
]

for name, obj, proj in views:
    opts = {
        "projectionDir": proj,
        "strokeColor": (40, 40, 40),
        "hiddenColor": (200, 200, 200),
        "showHidden": False,
        "width": 900,
        "height": 700,
        "marginLeft": 40,
        "marginTop": 40,
    }
    exporters.export(obj, str(out / f"view_{name}.svg"), opt=opts)
    print(f"wrote view_{name}.svg")

print("done.")
