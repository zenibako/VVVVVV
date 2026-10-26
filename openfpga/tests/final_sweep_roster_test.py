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
