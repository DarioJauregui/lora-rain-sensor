"""Run the real TX control functions with a deterministic radio/ADC on Linux CI."""
from pathlib import Path
import re
import subprocess
import tempfile
import sys

if sys.platform == "win32":
    raise SystemExit("Run in Linux CI; this check does not create/run Windows executables.")
root = Path(__file__).resolve().parents[1]
source = (root / "Emisor/Emisor.ino").read_text(encoding="utf-8")
management = (root / "libraries/RainLink/src/Management.h").read_text(encoding="utf-8")
functions = []
for name in ("wetNow", "queueState", "updateSensor", "applyManualSelection", "getOperational", "managementProcess"):
    selected = management if name == "managementProcess" else source
    match = re.search(r"^(?:bool|void|Operational) " + name + r"\([^\n]*\)\s*\{", selected, re.M)
    assert match, name
    start = match.start()
    position = match.end()
    depth = 1
    while depth:
        if selected[position] == "{":
            depth += 1
        elif selected[position] == "}":
            depth -= 1
        position += 1
    functions.append(selected[start:position])
with tempfile.TemporaryDirectory() as temp:
    generated = Path(temp) / "tx_functions.h"
    types = management[management.index("struct __attribute__"):management.index("bool applyOperational")]
    (Path(temp) / "management_types.h").write_text(types, encoding="utf-8")
    generated.write_text("\n".join(functions), encoding="utf-8")
    binary = Path(temp) / "firmware-test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-misleading-indentation", "-fsanitize=address,undefined",
                    "-I", str(root / "libraries/RainLink/src"), "-I", temp,
                    str(root / "tests/firmware_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
