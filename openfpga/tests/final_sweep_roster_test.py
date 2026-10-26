"""The performance sweep must cover every authored main final-level room."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
source = (root / 'desktop_version/src/Finalclass.cpp').read_text().split('case rn(108,109):')[0]
expected = {(int(x), int(y)) for x, y in re.findall(r'case rn\((\d+),\s*(\d+)\)', source)}
expected |= {(49, 52), (51, 53)}  # The two 100-row mini-towers live in Map.cpp.
roster = (root / 'openfpga/src/pocket_final_rooms.h').read_text()
actual = [(int(x), int(y)) for x, y in re.findall(r'^\s*\{(\d+),\s*(\d+),', roster, re.M)]
assert len(actual) == len(set(actual)), 'Duplicate room in sweep'
assert set(actual) == expected, (expected - set(actual), set(actual) - expected)
assert len(actual) == 34
print('Final sweep roster: all 34 main final-level rooms, including both mini-towers')

focus = {(int(x), int(y)) for x, y in re.findall(
    r'^\s*\{(\d+),\s*(\d+),\s*"[^"]*",\s*(?:true|false),\s*true\}', roster, re.M)}
assert focus == {(48, 52), (44, 52), (50, 53), (52, 53), (50, 54), (42, 52),
                 (49, 52), (51, 53)}
print('Final focus roster: six animated rooms and both mini-towers')
