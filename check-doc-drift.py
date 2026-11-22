#!/usr/bin/env python3
"""Fail when the implementation plan and the architecture drift apart.

Checks four things and exits 1 on any failure, so it can gate a commit:

    1. baseline-blob  the plan's `**Baseline:** … blob` equals the architecture blob
    2. plan-hash      section 15.1's digest verifies against the plan text
    3. ids-defined    every WP-nnn / Gx.y cited in any of the three documents is
                      defined in the plan, every D-DIAG-* id cited anywhere is
                      defined in the diagnosis protocol, and every C-CODE
                      citation is defined in architecture 4.1's component table
    4. ids-unique     no work-package, gate, D-DIAG or component code is defined
                      twice

Two sources of truth, picked by flag:

    (default)   HEAD, for a standalone run: "is the repository consistent now?"
    --staged    the index, for a pre-commit hook: "is the commit I am about to
                make consistent?" Only this mode can block the commit that
                introduces drift, because HEAD still holds the old, consistent
                pair while the bad change sits in the index.

Three rules keep 3 and 4 from crying wolf:

  * A definition is a table row with the shape of a definition — six columns
    with an effort figure in the fourth for a package, five or more for a gate.
    A package also appears in the package-to-gate mapping table, and that row
    is a citation, not a second definition.
  * An id introduced by "no" is a statement that it does not exist, not a
    citation — "there is no G5.7 equivalent" is the document being careful,
    not the document dangling.
  * A D-DIAG id is defined only by its appendix heading, backticked:
    "## Appendix A — `D-DIAG-01` …". A section heading that mentions the id
    in passing is a citation, not a second definition.
  * A component code is cited as `C-` plus the code — `C-ATPG` — so the scan can
    tell it from the acronym it abbreviates. Only the table under 4.1 defines
    one; a sentence that happens to name a component does not.
"""

import hashlib
import os
import re
import subprocess
import sys

PLAN = "dft-atpg-implementation-plan.md"
ARCH = "dft-atpg-platform-architecture-grx930-g100.md"
DIAG = "diagnosis-benchmark-p3.md"

MODE = "staged" if "--staged" in sys.argv[1:] else "head"
SPEC = ":" if MODE == "staged" else "HEAD:"


def git(*args, text=True):
    r = subprocess.run(("git",) + args, capture_output=True)
    if r.returncode != 0:
        sys.exit("git %s failed: %s" % (" ".join(args), r.stderr.decode("utf-8", "replace").strip()))
    return r.stdout.decode("utf-8") if text else r.stdout


def content(path):
    return git("show", SPEC + path).replace("\r\n", "\n")


def rev(path):
    return git("rev-parse", SPEC + path).strip()


def cells(line):
    return [c.strip() for c in line.split("|")]


def package_defs(text):
    """Rows of a phase table: | ID | name | deliverables | ew | depends | owner |"""
    out = []
    for line in text.splitlines():
        if re.match(r"^\| WP-\d{3} \|", line):
            c = cells(line)
            if len(c) == 8 and re.fullmatch(r"\d+(\.\d+)?", c[4]):
                out.append(c[1])
    return out


def gate_defs(text):
    out = []
    for line in text.splitlines():
        if re.match(r"^\| G\d+\.\d+ \|", line):
            if len(cells(line)) >= 7:          # 5 or 6 columns, never a 2-column mapping
                out.append(cells(line)[1])
    return out


def diag_defs(text):
    """A `D-DIAG-…` record is defined only by its appendix heading.

    `## Appendix A — `D-DIAG-01` …` defines the record. A section heading that
    merely mentions it — "### What `D-DIAG-01` records" — does not.
    """
    out = []
    for line in text.splitlines():
        if re.match(r"^#{1,6}\s+Appendix\b", line, re.I):
            out.extend(re.findall(r"`(D-DIAG(?:-[A-Z]+)*-\d+)`", line))
    return out


def component_defs(text):
    """Codes are defined only by the component table in architecture 4.1.

    The code sits in the first cell of a row. The table is found by its `4.1`
    heading, so a code mentioned in another table is a citation, not a
    second definition.
    """
    out, inside = [], False
    for line in text.splitlines():
        if re.match(r"^#{1,6}\s+4\.1\b", line):
            inside = True
            continue
        if inside and re.match(r"^#{1,6}\s", line):
            break
        if inside and line.startswith("|"):
            c = cells(line)
            code = c[1].strip("`") if len(c) > 2 else ""
            if re.fullmatch("[A-Z]{2,5}", code):
                out.append(code)
    return out


failures = []


def report(name, ok, detail=""):
    print("%s %-13s %s" % ("ok  " if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


root = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True).stdout.strip()
if not root:
    sys.exit("not inside a git repository")
os.chdir(root)

plan = content(PLAN)
arch = content(ARCH)
diag = content(DIAG)
where = "the staged commit" if MODE == "staged" else "HEAD"

# 1. the plan pins the architecture it was reconciled against
m = re.search(r"\*\*Baseline:\*\*.*?blob `([0-9a-f]{40})`", plan)
arch_blob = rev(ARCH)
if not m:
    report("baseline-blob", False, "no `blob <sha1>` on the Baseline line")
else:
    same = m.group(1) == arch_blob
    report("baseline-blob", same, "" if same else
           "plan pins %s, %s architecture is %s" % (m.group(1)[:12], where, arch_blob[:12]))

# 2. section 15.1's newest digest, under the rule section 15.1 states:
#    SHA-256 of the file with the digest in the newest row replaced by HASH
record = re.search(r"`sha256:([0-9a-f]{64})`", plan)
if not record:
    report("plan-hash", False, "no `sha256:<64 hex>` revision record")
else:
    canonical = plan.replace(record.group(0), "`sha256:HASH`", 1)
    digest = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    same = digest == record.group(1)
    report("plan-hash", same, "" if same else
           "recorded %s, %s text hashes to %s" % (record.group(1)[:12], where, digest[:12]))

# 3. no citation of an id that does not exist
wp_defs = package_defs(plan)
g_defs = gate_defs(plan)
d_defs = diag_defs(diag)
c_defs = component_defs(arch)
defined = set(wp_defs) | set(g_defs) | set(d_defs) | set(c_defs)
cited = {}
for path, text in ((PLAN, plan), (ARCH, arch), (DIAG, diag)):
    for hit in re.finditer(r"WP-\d{3}\b|G\d+\.\d+\b|D-DIAG(?:-[A-Z]+)*-\d+\b|C-[A-Z]{2,5}\b", text):
        token = hit.group(0)
        if token.startswith("C-"):
            token = token[2:]                   # the table holds the code alone
        if re.search(r"\bno\s+$", text[max(0, hit.start() - 6):hit.start()], re.I):
            continue                            # "there is no G5.7 equivalent"
        cited.setdefault(token, set()).add(path)
dangling = sorted(t for t in cited if t not in defined)
report("ids-defined", not dangling, "" if not dangling else
       ", ".join("%s (%s)" % (t, "+".join(sorted(cited[t]))) for t in dangling))

# 4. no id defined twice, which would let 3 pass against the wrong row
all_ids = wp_defs + g_defs + d_defs + c_defs
dupes = sorted({i for i in all_ids if all_ids.count(i) > 1})
report("ids-unique", not dupes, "" if not dupes else ", ".join(dupes))

if MODE == "head" and subprocess.run(["git", "diff", "--quiet", "HEAD", "--", PLAN, ARCH, DIAG]).returncode != 0:
    print("\nnote: the working tree differs from HEAD; everything above describes HEAD.")

if failures:
    print("\n%d of 4 checks failed: %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("\n4 of 4 checks passed")
