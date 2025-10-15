#!/usr/bin/env python3

import json
import os
import re
import sys
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, ElementTree
import yaml


def parse_array(value) -> list[str]:
    if isinstance(value, list):
        return [str(x) for x in value]
    if not isinstance(value, str):
        return []
    txt = value.strip()
    if not txt:
        return []
    # Try JSON
    try:
        return [str(x) for x in json.loads(txt)]
    except Exception:
        pass
    # Normalize single quotes then JSON
    try:
        fixed = txt.replace("'", '"')
        return [str(x) for x in json.loads(fixed)]
    except Exception:
        pass
    # Fallback: CSV
    return [p.strip() for p in txt.strip("[]").split(",") if p.strip()]


def _parse_ci_yml(content: str) -> dict:
    if not content:
        return {}
    try:
        data = yaml.safe_load(content) or {}
        if not isinstance(data, dict):
            return {}
        return data
    except Exception:
        return {}


def _fqbn_counts_from_yaml(ci: dict) -> dict[str, int]:
    counts: dict[str, int] = {}
    if not isinstance(ci, dict):
        return counts
    fqbn = ci.get("fqbn")
    if not isinstance(fqbn, dict):
        return counts
    for target, entries in fqbn.items():
        if isinstance(entries, list):
            counts[str(target)] = len(entries)
        elif entries is not None:
            # Single value provided as string
            counts[str(target)] = 1
    return counts


def _sdkconfig_meets(ci_cfg: dict, sdk_text: str) -> bool:
    if not sdk_text:
        return True
    for req in ci_cfg.get("requires", []):
        if not req or not isinstance(req, str):
            continue
        if not any(line.startswith(req) for line in sdk_text.splitlines()):
            return False
    req_any = ci_cfg.get("requires_any", [])
    if req_any:
        if not any(any(line.startswith(r.strip()) for line in sdk_text.splitlines()) for r in req_any if isinstance(r, str)):
            return False
    return True


def expected_from_artifacts(build_root: Path) -> dict[tuple[str, str, str, str], int]:
    """Compute expected runs using ci.yml and sdkconfig found in build artifacts.
    Returns mapping (platform, target, type, sketch) -> expected_count
    """
    expected: dict[tuple[str, str, str, str], int] = {}
    if not build_root.exists():
        return expected
    print(f"[DEBUG] Scanning build artifacts in: {build_root}", file=sys.stderr)
    for artifact_dir in build_root.iterdir():
        if not artifact_dir.is_dir():
            continue
        m = re.match(r"test-bin-([A-Za-z0-9_\-]+)-([A-Za-z0-9_\-]+)", artifact_dir.name)
        if not m:
            continue
        target = m.group(1)
        test_type = m.group(2)
        print(f"[DEBUG] Artifact group target={target} type={test_type} dir={artifact_dir}", file=sys.stderr)
        # Locate ci.yml next to build*.tmp folders and derive sketch/target
        for ci_path in artifact_dir.rglob("ci.yml"):
            if not ci_path.is_file():
                continue
            build_tmp = ci_path.parent
            if not re.search(r"build\d*\.tmp$", build_tmp.name):
                continue
            sketch = build_tmp.parent.name
            target_guess = build_tmp.parent.parent.name if build_tmp.parent.parent else ""
            if target_guess != target:
                continue
            sdk_path = build_tmp / "sdkconfig"
            try:
                ci_text = ci_path.read_text(encoding="utf-8") if ci_path.exists() else ""
            except Exception:
                ci_text = ""
            try:
                sdk_text = sdk_path.read_text(encoding="utf-8", errors="ignore") if sdk_path.exists() else ""
            except Exception:
                sdk_text = ""
            ci = _parse_ci_yml(ci_text)
            fqbn_counts = _fqbn_counts_from_yaml(ci)
            # Determine allowed platforms for this test
            allowed_platforms = []
            platforms_cfg = ci.get("platforms") if isinstance(ci, dict) else None
            for plat in ("hardware", "wokwi", "qemu"):
                dis = None
                if isinstance(platforms_cfg, dict):
                    dis = platforms_cfg.get(plat)
                if dis is False:
                    continue
                allowed_platforms.append(plat)
            # Requirements check
            minimal = {
                "requires": ci.get("requires") or [],
                "requires_any": ci.get("requires_any") or [],
            }
            if not _sdkconfig_meets(minimal, sdk_text):
                print(f"[DEBUG]  Skip (requirements not met): target={target} type={test_type} sketch={sketch}", file=sys.stderr)
                continue
            # Expected runs per target driven by fqbn count; default 1 when 0
            exp_runs = fqbn_counts.get(target, 0) or 1
            for plat in allowed_platforms:
                expected[(plat, target, test_type, sketch)] = max(expected.get((plat, target, test_type, sketch), 0), exp_runs)
                print(f"[DEBUG] Expected: plat={plat} target={target} type={test_type} sketch={sketch} runs={exp_runs}", file=sys.stderr)
    return expected


def scan_executed_xml(xml_root: Path) -> dict[tuple[str, str, str, str], int]:
    """Return executed counts per (platform, target, type, sketch)."""
    counts: dict[tuple[str, str, str, str], int] = {}
    if not xml_root.exists():
        print(f"[DEBUG] Results root not found: {xml_root}", file=sys.stderr)
        return counts
    print(f"[DEBUG] Scanning executed XMLs in: {xml_root}", file=sys.stderr)
    for xml_path in xml_root.rglob("*.xml"):
        if not xml_path.is_file():
            continue
        rel = str(xml_path)
        platform = "hardware"
        if "test-results-wokwi-" in rel:
            platform = "wokwi"
        elif "test-results-qemu-" in rel:
            platform = "qemu"
        # Expect tests/<type>/<sketch>/<target>/<sketch>.xml
        # Find 'tests' segment
        parts = xml_path.parts
        try:
            t_idx = parts.index("tests")
        except ValueError:
            continue
        if t_idx + 4 >= len(parts):
            continue
        test_type = parts[t_idx + 1]
        sketch = parts[t_idx + 2]
        target = parts[t_idx + 3]
        key = (platform, target, test_type, sketch)
        counts[key] = counts.get(key, 0) + 1
    print(f"[DEBUG] Executed entries discovered: {len(counts)}", file=sys.stderr)
    return counts


def write_missing_xml(out_root: Path, platform: str, target: str, test_type: str, sketch: str, missing_count: int):
    out_tests_dir = out_root / f"test-results-{platform}" / "tests" / test_type / sketch / target
    out_tests_dir.mkdir(parents=True, exist_ok=True)
    # Create one XML per missing index
    for idx in range(missing_count):
        suite_name = f"{test_type}_{platform}_{target}_{sketch}"
        root = Element("testsuite", name=suite_name, tests="1", failures="1", errors="0")
        case = SubElement(root, "testcase", classname=f"{test_type}.{sketch}", name="missing-run")
        fail = SubElement(case, "failure", message="Expected test run missing")
        fail.text = "This placeholder indicates an expected test run did not execute."
        tree = ElementTree(root)
        out_file = out_tests_dir / f"{sketch}_missing_{idx}.xml"
        tree.write(out_file, encoding="utf-8", xml_declaration=True)


def main():
    # Args: <build_artifacts_dir> <test_results_dir> <output_junit_dir>
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <build_artifacts_dir> <test_results_dir> <output_junit_dir>", file=sys.stderr)
        return 2

    build_root = Path(sys.argv[1]).resolve()
    results_root = Path(sys.argv[2]).resolve()
    out_root = Path(sys.argv[3]).resolve()

    # Validate inputs
    if not build_root.is_dir():
        print(f"ERROR: Build artifacts directory not found: {build_root}", file=sys.stderr)
        return 2
    if not results_root.is_dir():
        print(f"ERROR: Test results directory not found: {results_root}", file=sys.stderr)
        return 2
    # Ensure output directory exists
    try:
        out_root.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        print(f"ERROR: Failed to create output directory {out_root}: {e}", file=sys.stderr)
        return 2

    # Read matrices from environment variables injected by workflow
    hw_enabled = (os.environ.get("HW_TESTS_ENABLED", "false").lower() == "true")
    wokwi_enabled = (os.environ.get("WOKWI_TESTS_ENABLED", "false").lower() == "true")
    qemu_enabled = (os.environ.get("QEMU_TESTS_ENABLED", "false").lower() == "true")

    hw_targets = parse_array(os.environ.get("HW_TARGETS", "[]"))
    wokwi_targets = parse_array(os.environ.get("WOKWI_TARGETS", "[]"))
    qemu_targets = parse_array(os.environ.get("QEMU_TARGETS", "[]"))

    hw_types = parse_array(os.environ.get("HW_TYPES", "[]"))
    wokwi_types = parse_array(os.environ.get("WOKWI_TYPES", "[]"))
    qemu_types = parse_array(os.environ.get("QEMU_TYPES", "[]"))

    expected = expected_from_artifacts(build_root)  # (platform, target, type, sketch) -> expected_count
    executed = scan_executed_xml(results_root)      # (platform, target, type, sketch) -> count
    print(f"[DEBUG] Expected entries computed: {len(expected)}", file=sys.stderr)

    # Filter expected by enabled platforms and target/type matrices
    enabled_plats = set()
    if hw_enabled:
        enabled_plats.add("hardware")
    if wokwi_enabled:
        enabled_plats.add("wokwi")
    if qemu_enabled:
        enabled_plats.add("qemu")

    target_set = set(hw_targets + wokwi_targets + qemu_targets)
    type_set = set(hw_types + wokwi_types + qemu_types)

    missing_total = 0
    for (plat, target, test_type, sketch), exp_count in expected.items():
        if plat not in enabled_plats:
            continue
        if target not in target_set or test_type not in type_set:
            continue
        got = executed.get((plat, target, test_type, sketch), 0)
        if got < exp_count:
            print(f"[DEBUG] Missing: plat={plat} target={target} type={test_type} sketch={sketch} expected={exp_count} got={got}", file=sys.stderr)
            write_missing_xml(out_root, plat, target, test_type, sketch, exp_count - got)
            missing_total += (exp_count - got)

    print(f"Generated {missing_total} placeholder JUnit files for missing runs.", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())


