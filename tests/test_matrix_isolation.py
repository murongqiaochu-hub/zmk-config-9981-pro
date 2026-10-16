#!/usr/bin/env python3
"""Offline electrical graph + *unchanged* ZMK v0.3.0 native-C regression.

Run from the repository root: python3 -B tests/test_matrix_isolation.py -v
Runner integration (parent-owned): invoke the same command with check=True.
There is no download, SDK checkout, device, credential, HID or hardware access at
runtime. C binaries/objects live only in an automatically cleaned TemporaryDirectory.

BOUNDARY / ASSUMPTIONS
* Closed switches are ideal directed col->row diodes OR ideal bidirectional
  wires. The three hypotheses are not a determination of the actual PCB.
* Columns drive strong 1 / strong 0 / HiZ. Each row has a weak pulldown; a strong
  uncontested high overrides it. No leakage, capacitance, diode drop, thresholds,
  settling time or RC constants are invented. Results are settled graph limits.
* A strong-high -> strong-low path is CONTENTION, NOT a logic-0 reading. Its
  downstream region is conservatively unknown too. No low-wins voltage rule is
  used, and UNKNOWN cannot silently coerce to False in the scanner assertions.
* Native open-source GPIO mock: logical 0 disconnects its strong driver; logical
  1 is strong high. The native scan algorithm and cached getter are upstream
  bytes, not a rewritten scan. GPIO/kernel/DT are host mocks, not nRF silicon.
* Ghost coordinates remain in the full 6x10 raw matrix, including unmapped cells.
  Expected ghost tests deliberately PASS only when the risk is actually present.
* No hardware pass, diode absence, unchanged touch feel, key semantics or lighting
  behavior is established by these tests; no production file is edited here.
"""
from dataclasses import dataclass
from enum import Enum
from itertools import combinations
from pathlib import Path
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
MOCKS = TESTS / "matrix_isolation_mocks"
ROWS, COLS = 6, 10
MENU, SHOULDER, MECHANICAL = (4, 0), (4, 1), (4, 5)
CTRL, AA, SYM = (3, 0), (3, 8), (3, 6)

# URL + SHA256 for EVERY original-byte fixture; no runtime network access.
UPSTREAM = "https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/"
FIXTURES = {
    "tests/sdk_v0_3_0_matrix.c": (
        "app/module/drivers/kscan/kscan_gpio_matrix.c",
        "66d70992f8d3072ea122d5d04c322ec8d5cfeb052d87b798a8e72d56ac043702"),
    "tests/sdk_v0_3_0_kscan_gpio.c": (
        "app/module/drivers/kscan/kscan_gpio.c",
        "80510abdd747f648e21af2b4cc89be6573af2718f54cb150c7d060429325e8cf"),
    "tests/matrix_isolation_mocks/kscan_gpio.h": (
        "app/module/drivers/kscan/kscan_gpio.h",
        "544b2b1bb9afc6f0e3656102414aa9b53ccaf4541c687e7875d77ba9ae0200e9"),
    "tests/matrix_isolation_mocks/zmk/debounce.h": (
        "app/module/include/zmk/debounce.h",
        "b45e5373afa78043017e127c2839548ccb2d3328246dcde1251f28e78f84ac88"),
    "tests/matrix_isolation_mocks/sdk_v0_3_0_debounce.c": (
        "app/module/lib/zmk_debounce/debounce.c",
        "75578f439b9b5934f29e7352e70376b235b031670a97a3ceb6440c02e16b27ad"),
}


def strip_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def board_mapping():
    dts = strip_comments((ROOT / "config/boards/arm/bbp9981/bbp9981.dts").read_text())
    block = re.search(r"default_transform:.*?map\s*=\s*<(.*?)>;", dts, re.S).group(1)
    return tuple((int(r), int(c)) for r, c in re.findall(r"RC\(\s*(\d+)\s*,\s*(\d+)\s*\)", block))


MAPPING = board_mapping()
MAPPED = frozenset(MAPPING)
ALL_CELLS = frozenset((r, c) for r in range(ROWS) for c in range(COLS))


class Level(Enum):
    LOW = "0 (weak pulldown / no strong high)"
    HIGH = "1 (uncontested strong high)"
    CONTENTION = "CONTENTION / voltage unknown"

    def __bool__(self):
        raise TypeError("Use an explicit Level comparison; CONTENTION is not a measured 0/1")


class Drive(Enum):
    LOW = "strong0"
    HIGH = "strong1"
    HIZ = "HiZ"


class Topology(Enum):
    IDEAL = "all ideal col->row diodes"
    MIXED = "row4 bidirectional, all other keys ideal col->row diodes"
    NONE = "all bidirectional / no diodes (hypothesis only)"


def row_node(r):
    return r


def col_node(c):
    return ROWS + c


def reachable(graph, origins):
    found = set(origins)
    pending = list(origins)
    while pending:
        for other in graph[pending.pop()]:
            if other not in found:
                found.add(other)
                pending.append(other)
    return frozenset(found)


@dataclass(frozen=True)
class GraphRead:
    rows: tuple
    strong_high: frozenset
    strong_low: frozenset
    high_reachable: frozenset
    low_backreachable: frozenset
    contention_nodes: frozenset
    uncertain_nodes: frozenset
    edges: tuple


class MatrixGraph:
    def __init__(self, closed, topology):
        self.closed = frozenset(closed)
        if not self.closed <= MAPPED:
            raise ValueError("Only actual mapped physical switches may be closed")
        self.topology = topology
        self.graph = [set() for _ in range(ROWS + COLS)]
        self.reverse = [set() for _ in self.graph]
        for r, c in self.closed:
            self.graph[col_node(c)].add(row_node(r))
            if topology is Topology.NONE or (topology is Topology.MIXED and r == 4):
                self.graph[row_node(r)].add(col_node(c))
        for source, targets in enumerate(self.graph):
            for target in targets:
                self.reverse[target].add(source)

    def read(self, drives):
        if len(drives) != COLS or any(not isinstance(x, Drive) for x in drives):
            raise ValueError("Exactly ten explicit HIGH/LOW/HiZ column drives required")
        high = frozenset(col_node(c) for c, x in enumerate(drives) if x is Drive.HIGH)
        low = frozenset(col_node(c) for c, x in enumerate(drives) if x is Drive.LOW)
        forward = reachable(self.graph, high)
        backward = reachable(self.reverse, low)
        # Nodes on a forward-biased high-to-low path have conflicting strong
        # sources. Everything fed by them is unknown (conservative envelope).
        contention = forward & backward
        uncertain = reachable(self.graph, contention)
        rows = tuple(Level.CONTENTION if r in uncertain else
                     Level.HIGH if r in forward else Level.LOW for r in range(ROWS))
        edges = tuple((a, b) for a, targets in enumerate(self.graph) for b in sorted(targets))
        return GraphRead(rows, high, low, forward, backward, contention, uncertain, edges)

    def scan(self, isolated):
        raw, unknown, evidence = set(), set(), []
        for selected in range(COLS):
            drives = [Drive.HIZ if isolated else Drive.LOW] * COLS
            drives[selected] = Drive.HIGH
            read = self.read(drives)
            evidence.append(read)
            for r, level in enumerate(read.rows):
                if level is Level.HIGH:
                    raw.add((r, selected))
                elif level is Level.CONTENTION:
                    unknown.add((r, selected))
        return Scan(frozenset(raw), frozenset(unknown), tuple(evidence))


@dataclass(frozen=True)
class Scan:
    high: frozenset
    unknown: frozenset
    evidence: tuple

    @property
    def determinate(self):
        if self.unknown:
            raise ValueError("CONTENTIOUS rows cannot be converted into binary raw reads")
        return self.high

    def ghosts(self, closed):
        return self.high - frozenset(closed)


class GraphTests(unittest.TestCase):
    def test_real_dts_and_keymap_dimensions_mapping(self):
        dts = strip_comments((ROOT / "config/boards/arm/bbp9981/bbp9981.dts").read_text())
        self.assertRegex(dts, r"columns\s*=\s*<10>")
        self.assertRegex(dts, r"rows\s*=\s*<6>")
        self.assertRegex(dts, r'diode-direction\s*=\s*"col2row"')
        self.assertEqual(len(MAPPING), 42)
        self.assertEqual(len(MAPPED), 42)
        self.assertEqual(MAPPING[:7], (SHOULDER, (5, 1), (4, 2), MENU, MECHANICAL, (5, 0), (5, 3)))
        kscan = re.search(r"kscan0:.*?row-gpios\s*=\s*(.*?);.*?col-gpios\s*=\s*(.*?);", dts, re.S)
        for section, length in zip(kscan.groups(), (ROWS, COLS)):
            self.assertEqual(len(re.findall(r"<&gpio[01]\s+\d+\b", section)), length)
        self.assertEqual(kscan.group(1).count("GPIO_PULL_DOWN"), ROWS)
        # Bind the experiment hypothesis to the CURRENT DTS, not merely a
        # freestanding graph. Row electrical flags and all ten column flags
        # are explicit; this is still not nRF silicon/PCB verification.
        for section, expected in ((kscan.group(1), {"GPIO_ACTIVE_HIGH", "GPIO_PULL_DOWN"}),
                                  (kscan.group(2), {"GPIO_ACTIVE_HIGH", "GPIO_OPEN_SOURCE"})):
            for entry in re.findall(r"<(&gpio[^>]+)>", section):
                self.assertEqual(set(re.findall(r"GPIO_[A-Z_]+", entry)), expected)
        for relative in ("config/bbp9981.keymap", "config/boards/arm/bbp9981/bbp9981.keymap"):
            keymap = strip_comments((ROOT / relative).read_text())
            for layer in ("default_layer", "sym_layer", "upper_layer", "media_layer"):
                block = re.search(r"\b" + layer + r"\s*\{\s*bindings\s*=\s*<(.*?)>;", keymap, re.S).group(1)
                bindings = tuple(" ".join(x.split()) for x in re.findall(r"&[^&]+", block))
                self.assertEqual(len(bindings), 42, (relative, layer))
                if layer == "default_layer":
                    by_rc = dict(zip(MAPPING, bindings))
                    self.assertEqual(by_rc[MENU], "&kp LGUI")
                    self.assertEqual(by_rc[SHOULDER], "&microtap_shoulder")
                    self.assertEqual(by_rc[MECHANICAL], "&mkp LCLK")
                    self.assertEqual(by_rc[CTRL], "&sk_kp LCTRL LCTRL")
                    self.assertEqual(by_rc[AA], "&sl_mo 2 2")
                    self.assertEqual(by_rc[SYM], "&sl_mo 1 1")
                if layer == "upper_layer":
                    self.assertEqual(dict(zip(MAPPING, bindings))[SYM], "&aa_sym_ctrl")

    def test_all_42_single_keys_both_modes_all_hypotheses(self):
        count = 0
        for topology in Topology:
            for key in MAPPING:
                for isolated in (False, True):
                    with self.subTest(topology=topology.value, key=key, isolated=isolated):
                        result = MatrixGraph({key}, topology).scan(isolated)
                        self.assertEqual(result.determinate, {key})
                        self.assertFalse(result.ghosts({key}))
                        count += 1
        print(f"PASS graph singles: {count} scans = 42 x 3 hypotheses x 2 drive modes")

    def test_all_861_pairs_both_modes_all_hypotheses(self):
        counts = {t: 0 for t in Topology}
        for topology in Topology:
            for pair in combinations(MAPPING, 2):
                closed = frozenset(pair)
                isolated = MatrixGraph(closed, topology).scan(True)
                self.assertEqual(isolated.determinate, closed, (topology, pair))
                legacy = MatrixGraph(closed, topology).scan(False)
                same_row = pair[0][0] == pair[1][0]
                conflict = same_row and (topology is Topology.NONE or
                                        (topology is Topology.MIXED and pair[0][0] == 4))
                if conflict:
                    counts[topology] += 1
                    self.assertTrue(closed <= legacy.unknown, (topology, pair))
                    with self.assertRaises(ValueError):
                        _ = legacy.determinate
                else:
                    self.assertEqual(legacy.determinate, closed, (topology, pair))
                self.assertFalse(legacy.ghosts(closed), (topology, pair))
        self.assertEqual(counts, {Topology.IDEAL: 0, Topology.MIXED: 6, Topology.NONE: 154})
        print("PASS graph pairs: 5166 scans = 861 x 3 x 2; legacy contention pairs "
              "ideal=0, mixed-row4=6, no-diode=154; HiZ all pairs exact")

    def test_contention_is_auditable_and_never_a_fake_low_read(self):
        graph = MatrixGraph({MENU, MECHANICAL}, Topology.NONE)
        read = graph.read([Drive.HIGH] + [Drive.LOW] * 9)
        self.assertIs(read.rows[4], Level.CONTENTION)
        self.assertIn(col_node(0), read.contention_nodes)
        self.assertIn(col_node(5), read.contention_nodes)
        self.assertIn(row_node(4), read.contention_nodes)
        self.assertTrue(read.strong_high & read.high_reachable)
        self.assertTrue(read.strong_low & read.high_reachable)
        self.assertIn((col_node(0), row_node(4)), read.edges)
        self.assertIn((row_node(4), col_node(5)), read.edges)
        with self.assertRaises(TypeError):
            bool(read.rows[4])
        isolated = graph.read([Drive.HIGH] + [Drive.HIZ] * 9)
        self.assertFalse(isolated.contention_nodes)
        self.assertIs(isolated.rows[4], Level.HIGH)
        # Diode orientation matters: a LOW column behind a reverse-biased diode
        # is not a strong-low sink for a row raised by another column.
        self.assertEqual(MatrixGraph({MENU, MECHANICAL}, Topology.IDEAL).scan(False).determinate,
                         {MENU, MECHANICAL})

    def test_ctrl_aa_sym_shortcuts_do_not_change_matrix_semantics(self):
        modifiers = (CTRL, AA, SYM)
        cases = [set(combo) for n in (1, 2, 3) for combo in combinations(modifiers, n)]
        cases += [{MENU, *combo} for n in (1, 2, 3) for combo in combinations(modifiers, n)]
        cases += [{key, *combo} for key in ((1, 0), (2, 3), (0, 1))
                  for n in (1, 2) for combo in combinations(modifiers, n)]
        for topology in Topology:
            for closed in cases:
                isolated = MatrixGraph(closed, topology).scan(True)
                self.assertFalse(isolated.unknown)
                # Some 3/4-key modifier combinations can ghost with no diodes;
                # they are recorded, never filtered by keymap or modifiers.
                self.assertTrue(frozenset(closed) <= isolated.high)
                if topology is Topology.IDEAL:
                    self.assertEqual(isolated.high, closed)
                legacy = MatrixGraph(closed, topology).scan(False)
                if topology is Topology.IDEAL:
                    self.assertEqual(legacy.determinate, closed)
        # Ctrl + aA + row2col0 is a mapped 3-corner rectangle; SYM isn't involved.
        closed = {CTRL, AA, (2, 0)}
        self.assertEqual(MatrixGraph(closed, Topology.NONE).scan(True).ghosts(closed), {(2, 8)})
        print(f"PASS modifier cases: {len(cases)} sets x 3 hypotheses x 2 modes; "
              "no-diode Ctrl+aA+row2col0 explicitly ghosts row2col8")

    def test_menu_held_repeated_mechanical_shoulder_and_three_row4(self):
        sequence = [{MENU}]
        for _ in range(12):
            sequence += [{MENU, MECHANICAL}, {MENU}, {MENU, SHOULDER}, {MENU}]
        sequence += [{MENU, SHOULDER, MECHANICAL}] * 5 + [{MENU}, set()]
        for topology in Topology:
            for step, closed in enumerate(sequence):
                result = MatrixGraph(closed, topology).scan(True)
                self.assertEqual(result.determinate, closed, (topology, step))
                self.assertFalse(result.ghosts(closed))
                legacy = MatrixGraph(closed, topology).scan(False)
                if len(closed) > 1 and topology is not Topology.IDEAL:
                    self.assertIn(MENU, legacy.unknown)
                else:
                    self.assertEqual(legacy.determinate, closed)
        print(f"PASS held-MENU timeline: {len(sequence)} frames x 3 x 2; "
              "24 click cycles and simultaneous row4 trio; legacy conflict stays UNKNOWN")

    def test_all_mapped_three_corner_rectangles_expose_expected_ghost(self):
        rectangles = []
        for r1, r2 in combinations(range(ROWS), 2):
            for c1, c2 in combinations(range(COLS), 2):
                rectangle = frozenset(((r1, c1), (r1, c2), (r2, c1), (r2, c2)))
                if rectangle <= MAPPED:
                    rectangles.append(rectangle)
        self.assertEqual(len(rectangles), 194)
        ghost_counts = {t: 0 for t in Topology}
        for rectangle in rectangles:
            for missing in sorted(rectangle):
                closed = rectangle - {missing}
                for topology in Topology:
                    result = MatrixGraph(closed, topology).scan(True)
                    self.assertFalse(result.unknown)
                    self.assertTrue(closed <= result.determinate)
                    expected = (topology is Topology.NONE or
                                (topology is Topology.MIXED and missing[0] != 4 and
                                 sum(r == 4 for r, _ in closed) == 2))
                    self.assertEqual(result.ghosts(closed), {missing} if expected else set(),
                                     (topology, closed, missing))
                    ghost_counts[topology] += bool(result.ghosts(closed))
        self.assertEqual(ghost_counts, {Topology.IDEAL: 0, Topology.MIXED: 40, Topology.NONE: 776})
        print("EXPECTED RISK: 194 mapped rectangles / 776 physical triples / 2328 HiZ scans; "
              "ghost cases ideal=0, mixed-row4=40, no-diode=776 (NOT filtered)")

    def test_specific_menu_ghost_and_unmapped_raw_ghost_are_not_hidden(self):
        closed = {MENU, SHOULDER, (0, 0)}
        for topology in (Topology.MIXED, Topology.NONE):
            self.assertEqual(MatrixGraph(closed, topology).scan(True).ghosts(closed), {(0, 1)})
        # Three actual keys can excite an UNMAPPED fourth corner. Raw matrix
        # tests must still show it even if a later transform has no binding.
        closed = {(4, 1), (4, 2), (3, 2)}
        self.assertNotIn((3, 1), MAPPED)
        for topology in (Topology.MIXED, Topology.NONE):
            self.assertEqual(MatrixGraph(closed, topology).scan(True).ghosts(closed), {(3, 1)})
        print("EXPECTED RISK witnesses: MENU+shoulder+Q -> raw W; "
              "row4col1+row4col2+row3col2 -> UNMAPPED raw(3,1)")

    def test_idle_all_high_wakes_disable_low_or_hiz_is_quiet(self):
        sets = [set(), *({key} for key in MAPPING), {MENU, SHOULDER, MECHANICAL},
                {MENU, SHOULDER, (0, 0)}, set(MAPPED)]
        for topology in Topology:
            for closed in sets:
                graph = MatrixGraph(closed, topology)
                idle = graph.read([Drive.HIGH] * COLS)
                self.assertFalse(idle.contention_nodes)
                actual_rows = {r for r, level in enumerate(idle.rows) if level is Level.HIGH}
                self.assertEqual(actual_rows, {r for r, _ in closed})
                for disabled in (Drive.LOW, Drive.HIZ):
                    quiet = graph.read([disabled] * COLS)
                    self.assertEqual(quiet.rows, (Level.LOW,) * ROWS)
                    self.assertFalse(quiet.contention_nodes)
                if not closed:
                    for mode in (False, True):
                        self.assertEqual(graph.scan(mode).determinate, set())

    def test_mixed_direction_unknown_conservatively_propagates(self):
        # Conflicted row4 connects through column1 -> ideal row0. Its voltage
        # remains unknown; don't manufacture a definite reading downstream.
        graph = MatrixGraph({MENU, SHOULDER, (0, 1)}, Topology.MIXED)
        result = graph.read([Drive.HIGH] + [Drive.LOW] * 9)
        self.assertIs(result.rows[4], Level.CONTENTION)
        self.assertIs(result.rows[0], Level.CONTENTION)
        self.assertIn(row_node(0), result.uncertain_nodes)
        self.assertNotIn(row_node(0), result.contention_nodes)


class NativeTests(unittest.TestCase):
    def test_original_sdk_bytes_have_pinned_urls_and_sha256(self):
        for relative, (source, sha256) in FIXTURES.items():
            with self.subTest(path=relative, url=UPSTREAM + source):
                self.assertEqual(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(), sha256)
        print(f"PASS provenance: {len(FIXTURES)} byte-identical upstream SDK fixtures, pinned URL + SHA256")

    def test_actual_native_scan_cache_enable_disable_interrupt(self):
        # Fail closed on provenance BEFORE compiling, regardless of unittest order.
        for relative, (_, sha256) in FIXTURES.items():
            self.assertEqual(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(), sha256)
        # Check the native fixture's physical map against the actual DTS, not an
        # independently trusted hardcoded 42-key list.
        source = (TESTS / "test_matrix_native.c").read_text()
        rc_array = re.search(r"mapped_rc\[42\]\[2\]\s*=\s*\{(.*?)\};", source, re.S).group(1)
        native_mapping = tuple((int(r), int(c)) for r, c in re.findall(r"\{(\d+),(\d+)\}", rc_array))
        self.assertEqual(native_mapping, MAPPING)
        dts = strip_comments((ROOT / "config/boards/arm/bbp9981/bbp9981.dts").read_text())
        sections = re.search(r"kscan0:.*?row-gpios\s*=\s*(.*?);.*?col-gpios\s*=\s*(.*?);", dts, re.S).groups()
        for prefix, section in zip(("row", "col"), sections):
            actual_pins = tuple((int(p), int(pin)) for p, pin in re.findall(r"<&gpio([01])\s+(\d+)\b", section))
            native_arrays = []
            for suffix in ("port", "pin"):
                body = re.search(prefix + "_" + suffix + r"\[[RC]\]\s*=\s*\{(.*?)\};", source).group(1)
                native_arrays.append(tuple(map(int, re.findall(r"\d+", body))))
            self.assertEqual(tuple(zip(*native_arrays)), actual_pins)
        # No skip-on-missing-compiler: absence is a real incomplete test result.
        cc = shlex.split(os.environ.get("CC", "cc"))
        self.assertTrue(cc and shutil.which(cc[0]), "A host C11 compiler is required")
        with tempfile.TemporaryDirectory(prefix="bbp9981-matrix-") as temporary:
            exe = Path(temporary) / "test_matrix_native"
            command = [*cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
                       "-Wno-sign-compare", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                       "-I", str(MOCKS), str(TESTS / "test_matrix_native.c"), "-o", str(exe)]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main(verbosity=2)
