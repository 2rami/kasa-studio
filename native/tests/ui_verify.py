import json
import os
from pathlib import Path
import subprocess
import sys

app = Path(sys.argv[1])
root = Path(sys.argv[2])


def render(name, *extra, frames=18, settings=None):
    directory = root / name
    directory.mkdir()
    if settings is not None:
        (directory / "data").mkdir()
        (directory / "data/settings.json").write_text(json.dumps(settings))
    environment = dict(os.environ, KASA_STUDIO_NATIVE_DATA=str(directory / "data"), KASA_STUDIO_AGENT_PORT="0")
    command = [str(app), "--headless", "--frames", str(frames), "--snapshot", str(directory / "screen.png"), "--report", str(directory / "report.json"), *extra]
    with (directory / "run.log").open("w") as log:
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
    report = json.loads((directory / "report.json").read_text())
    assert not report["mainVisible"] and not report["petVisible"]
    assert report["glError"] == report["avatarGlError"] == report["uiGlError"] == 0
    actions = report["ui"]
    identifiers = [item["id"] for item in actions]
    assert len(identifiers) == len(set(identifiers)), (name, identifiers)
    for identifier in ["pet.toggle", "record.toggle", "stream.open"]:
        assert identifiers.count(identifier) == 1
    by_id = {item["id"]: item for item in actions}
    for identifier in ["record.toggle", "stream.open"]:
        assert not by_id[identifier]["enabled"] and by_id[identifier]["reason"]
    assert not report["tracking"]["active"]
    return report, by_id


for page, expected in [("models", 1), ("tracking", 2), ("voice", 3), ("agent", 4), ("connections", 5)]:
    report, buttons = render(page, "--page", page)
    assert report["page"] == expected
    if page == "models":
        assert buttons["model.import"]["enabled"]
    if page == "tracking":
        assert not buttons["tracking.calibrate"]["enabled"]
        assert not buttons["tracking.toggle"]["enabled"]
    if page == "voice":
        assert buttons["rvc.connect"]["enabled"]
        assert not buttons["rvc.import"]["enabled"]
        assert not buttons["rvc.convert"]["enabled"]
        assert 0 < buttons["rvc.convert"]["rect"][1] < 900
    if page == "agent":
        assert not buttons["agent.send"]["enabled"]

invalid = {"version": 3, "obsUrl": "invalid", "rvcUrl": "invalid", "agent": {"endpoint": "invalid"}}
for page, identifier in [("connections", "obs.connect"), ("voice", "rvc.connect"), ("agent", "agent.probe")]:
    _, buttons = render("invalid-" + page, "--page", page, settings=invalid)
    assert not buttons[identifier]["enabled"]

identifiers = ["nav.models", "nav.tracking", "nav.voice", "nav.agent", "nav.connections", "nav.studio"]
events = [{"frame": 1, "id": "record.toggle"}] + [{"frame": 5 + index * 8, "id": identifier} for index, identifier in enumerate(identifiers)]
script = root / "clicks.json"
script.write_text(json.dumps(events))
report, _ = render("clicks", "--ui-script", str(script), frames=60)
assert [item["id"] for item in report["uiClicks"]] == identifiers
assert report["page"] == 0

pose = root / "face.json"
pose.write_text(json.dumps({"version": 1, "code": "12345678", "session": "ui-fixture", "sequence": 1, "tracked": True, "rotation": {"yaw": 20, "pitch": -10, "roll": 8}, "blendShapes": {"eyeBlinkLeft": 1, "eyeBlinkRight": 0, "jawOpen": 0.65}}))
report, _ = render("face", "--tracking-fixture", str(pose))
parameters = report["avatar"]["parameters"]
for name, expected in {"ParamAngleX": 20, "ParamAngleY": -10, "ParamAngleZ": 8, "ParamEyeLOpen": 0, "ParamEyeROpen": 1, "ParamMouthOpenY": 0.65}.items():
    assert abs(parameters[name] - expected) < 0.02, (name, parameters[name])
assert report["renderTarget"]["textureLimit"] == 2048
assert report["renderTarget"]["width"] >= report["renderTarget"]["logicalWidth"]
assert report["renderTarget"]["height"] >= report["renderTarget"]["logicalHeight"]
print("화면 이동·중복 버튼·비활성 입력·변환 버튼 위치·얼굴 파라미터 검증 통과")
