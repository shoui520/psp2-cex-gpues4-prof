"""Cross-language serialization contract; observations are synthetic, not GPU data."""
import importlib.util
import io
from pathlib import Path
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    "capture_reader", Path(__file__).resolve().parents[1] / "tools/analyze.py")
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)

for overflow in (False, True):
    output = subprocess.check_output([sys.argv[1]] + (["overflow"] if overflow else []),
                                     text=True, encoding="utf-8")
    events, dropped = reader.read_capture(io.StringIO(output, newline=""))
    report = reader.summarize(events, dropped)
    assert dropped == int(overflow)
    assert len(report["draws"]) == 3
    assert len(report["scenes"]) == 2
    assert all(s["closed"] for s in report["scenes"])
    first, instanced, reused = report["draws"]
    assert first["pass"] == 'Water, "reflection"\npass'
    assert first["frame"] == 55 and first["name"] == "Mesh"
    assert first["fragment_shader"]["name"] == "Surface shader"
    assert instanced["call_kind"] == 5
    assert reused["fragment_shader"]["generation"] == 2
    assert reused["fragment_shader"]["name"] == ""
    assert report["scenes"][0]["fragment_notification"] == 81
    assert len(report["scenes"][0]["flush_sequences"]) == 1
    assert all(d["gpu_time_us"] is None for d in report["draws"])
    assert all(d["identity_uncertain"] == overflow for d in report["draws"])
    assert bool(report["warnings"]) == overflow
