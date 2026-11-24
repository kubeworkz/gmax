#!/usr/bin/env python3
"""Run the independent SAT checks for the c432 smoke evidence.

The C++ side writes the internal solver result for one testable and one
redundant fault. This script builds a two-copy miter around the benchmark,
checks the same faults with Yosys `sat`, and dumps the CNF for each case. When
a MiniSat binary is supplied, it also solves those CNFs and records a second
independent verdict. It fails on a mismatch instead of writing an optimistic
report.

The G0.2 gate is enabled with GMAX_G0_2_GATE=1. In that mode a missing MiniSat
binary is an error: the Yosys-only result is a developer check, not gate
evidence.
"""

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path


PIN_LOCK = Path(__file__).resolve().parents[2] / "third_party" / "minisat-2.2.1.lock"


def pin_value(key: str) -> str:
    try:
        lines = PIN_LOCK.read_text(encoding="utf-8").splitlines()
    except OSError:
        return ""
    prefix = key + "="
    for line in lines:
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    return ""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def module_ports(source: Path, module: str):
    text = source.read_text(encoding="utf-8")
    match = re.search(
        r"\bmodule\s+" + re.escape(module) + r"\s*\((.*?)\);(.*?)\bendmodule",
        text,
        re.S,
    )
    if match is None:
        raise RuntimeError(f"cannot find module {module} in {source}")

    ports = [item.strip() for item in match.group(1).replace("\n", " ").split(",")]
    inputs = []
    outputs = []
    for direction, names in re.findall(
        r"^\s*(input|output)\s+(.*?);", match.group(2), re.M | re.S
    ):
        values = [item.strip() for item in names.replace("\n", " ").split(",")]
        (inputs if direction == "input" else outputs).extend(values)
    if ports != inputs + outputs:
        raise RuntimeError(f"module {module} has an unexpected port order")
    return inputs, outputs


def write_wrapper(source: Path, module: str, destination: Path):
    inputs, outputs = module_ports(source, module)
    lines = [
        "module top(" + ", ".join("input " + name for name in inputs) + ", output diff);",
    ]
    for name in outputs:
        lines.append(f"wire g_{name}, f_{name};")
    connections = ", ".join("." + name + "(" + name + ")" for name in inputs)
    good_connections = ", ".join("." + name + "(g_" + name + ")" for name in outputs)
    fault_connections = ", ".join("." + name + "(f_" + name + ")" for name in outputs)
    lines.append(f"{module} u_good({connections}, {good_connections});")
    lines.append(f"{module} u_fault({connections}, {fault_connections});")
    lines.append("assign diff = " + " | ".join(
        f"(g_{name} ^ f_{name})" for name in outputs
    ) + ";")
    lines.append("endmodule")
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")


def yosys_version(yosys: Path) -> str:
    result = subprocess.run(
        [str(yosys), "-V"], capture_output=True, text=True, check=False
    )
    text = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or not text:
        raise RuntimeError(f"cannot query Yosys version: {text}")
    return text.splitlines()[0]


def run_case(yosys: Path, source: Path, wrapper: Path, work: Path, case: dict):
    suffix = case["name"].rsplit(":", 1)[-1]
    if not suffix:
        raise RuntimeError(f"fault {case['net']} has no net name")
    cnf = work / f"case-{case['net']}-{case['stuck']}.cnf"
    script = "\n".join(
        [
            f'read_verilog "{source}" "{wrapper}"',
            "hierarchy -top top",
            "proc",
            "flatten",
            "opt_clean",
            f"select -set fault w:*u_fault*{suffix}",
            "select -assert-count 1 @fault",
            f"connect -set @fault {case['stuck']}",
            f'sat -prove diff 0 -show-inputs -dump_cnf "{cnf}"',
        ]
    ) + "\n"
    script_path = work / f"case-{case['net']}-{case['stuck']}.ys"
    script_path.write_text(script, encoding="utf-8")
    result = subprocess.run(
        [str(yosys), "-s", str(script_path)],
        capture_output=True,
        text=True,
        check=False,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise RuntimeError(
            f"Yosys failed for net {case['net']} {case['stuck']} "
            f"(exit {result.returncode}):\n{output}"
        )
    if "SAT proof finished - model found" in output:
        verdict = "sat"
        proof = "model"
    elif "SAT proof finished - no model found" in output:
        verdict = "unsat"
        proof = "no model"
    else:
        raise RuntimeError(
            f"Yosys produced no SAT verdict for net {case['net']} {case['stuck']}:\n{output}"
        )
    return {
        "net": case["net"],
        "stuck": case["stuck"],
        "verdict": verdict,
        "proof": proof,
        "cnf": cnf.name,
    }, cnf


def run_minisat(minisat: Path, cnf: Path):
    result = subprocess.run(
        [str(minisat), str(cnf)], capture_output=True, text=True, check=False
    )
    output = result.stdout + result.stderr
    lines = {line.strip() for line in output.splitlines()}
    if result.returncode == 10 and "SATISFIABLE" in lines:
        return "sat", "SATISFIABLE", result.returncode
    if result.returncode == 20 and "UNSATISFIABLE" in lines:
        return "unsat", "UNSATISFIABLE", result.returncode
    raise RuntimeError(
        f"MiniSat produced no SAT verdict for {cnf} "
        f"(exit {result.returncode}):\n{output}"
    )


def main(argv):
    if len(argv) not in (6, 7):
        print(
            "usage: sat_smoke.py <yosys> <source> <report> <work-dir> "
            "<source-label> [minisat]\n"
            "       GMAX_G0_2_GATE=1 requires the MiniSat argument",
            file=sys.stderr,
        )
        return 2
    yosys = Path(argv[1])
    source = Path(argv[2]).resolve()
    report_path = Path(argv[3])
    work = Path(argv[4])
    source_label = argv[5]
    minisat = Path(argv[6]) if len(argv) == 7 and argv[6] else None
    gate = os.environ.get("GMAX_G0_2_GATE", "").strip().lower() in (
        "1",
        "on",
        "true",
        "yes",
    )
    if gate and minisat is None:
        print(
            "sat_smoke: G0.2 gate requires the pinned MiniSat cross-check; "
            "Yosys-only evidence is not sufficient",
            file=sys.stderr,
        )
        return 2
    if not yosys.is_file():
        print(f"sat_smoke: no Yosys executable at {yosys}", file=sys.stderr)
        return 2
    if minisat is not None and not minisat.is_file():
        print(f"sat_smoke: no MiniSat executable at {minisat}", file=sys.stderr)
        return 2

    report = json.loads(report_path.read_text(encoding="utf-8"))
    module = report["design"]
    work.mkdir(parents=True, exist_ok=True)
    wrapper = work / f"{module}_miter.v"
    write_wrapper(source, module, wrapper)

    checks = []
    minisat_checks = []
    for case in report["cases"]:
        check, cnf = run_case(yosys, source, wrapper, work, case)
        checks.append(check)
        expected = case["verdict"]
        if check["verdict"] != expected:
            print(
                f"sat_smoke: Yosys says {check['verdict']} for net {case['net']} "
                f"{case['stuck']}, internal says {expected}",
                file=sys.stderr,
            )
            return 1
        if minisat is not None:
            verdict, proof, exit_code = run_minisat(minisat, cnf)
            minisat_checks.append(
                {
                    "net": case["net"],
                    "stuck": case["stuck"],
                    "verdict": verdict,
                    "proof": proof,
                    "exit_code": exit_code,
                }
            )
            if verdict != expected:
                print(
                    f"sat_smoke: MiniSat says {verdict} for net {case['net']} "
                    f"{case['stuck']}, internal says {expected}",
                    file=sys.stderr,
                )
                return 1

    report["cross_check"] = {
        "tool": "Yosys SAT",
        "version": yosys_version(yosys),
        "source": source_label,
        "cases": checks,
        "agree": True,
    }
    if minisat is not None:
        report["minisat_check"] = {
            "tool": "MiniSat",
            "version": pin_value("MINISAT_VERSION") or "unknown",
            "source_sha256": pin_value("MINISAT_SOURCE_SHA256") or "unknown",
            "binary_sha256": sha256_file(minisat),
            "cases": minisat_checks,
            "agree": True,
        }
    report_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    print(f"sat_smoke: independent Yosys check agrees on {len(checks)} cases")
    if minisat is not None:
        print(f"sat_smoke: independent MiniSat check agrees on {len(minisat_checks)} cases")
        if gate:
            print("sat_smoke: G0.2 gate satisfied by Yosys and MiniSat")
    else:
        print("sat_smoke: MiniSat check skipped (no binary supplied)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Exception as error:  # noqa: BLE001 - preserve a useful test failure
        print(f"sat_smoke: {error}", file=sys.stderr)
        sys.exit(1)
