#!/usr/bin/env python3
"""Fail when the implementation plan and the architecture drift apart.

Every check reads committed blobs (HEAD), not the working tree, because the
failure this guards against is a record that was true when it was written and
is not true now. Run it before a commit that touches either document.

    1. baseline-blob  the plan's `**Baseline:** … blob` equals HEAD's architecture blob
    2. plan-hash      section 15.1's digest verifies against HEAD's plan
    3. ids-defined    every WP-nnn / Gx.y cited in either document is defined in the plan
    4. ids-unique     no work-package or gate id is defined twice

Two rules keep 3 and 4 from crying wolf:

  * A definition is a table row with the shape of a definition — six columns
    with an effort figure in the fourth for a package, five or more for a gate.
    A package also appears in the package-to-gate mapping table, and that row
    is a citation, not a second definition.
  * An id introduced by "no" is a statement that it does not exist, not a
    citation — "there is no G5.7 equivalent" is the document being careful,
    not the document dangling.

Exit status is 1 on any failure, so it can gate a commit or a CI job.
"""

import hashlib
import os
import re
import subprocess
import sys

PLAN = "dft-atpg-implementation-plan.md"
ARCH = "dft-atpg-platform-architecture-grx930-g100.md"


def git(*args):
    r = subprocess.run(("git",) + args, capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        sys.exit("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def blob(path):
    return git("show", "HEAD:" + path).replace("\r\n", "\n")


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

plan = blob(PLAN)
arch = blob(ARCH)

# 1. the plan pins the architecture it was reconciled against
m = re.search(r"\*\*Baseline:\*\*.*?blob `([0-9a-f]{40})`", plan)
head_blob = git("rev-parse", "HEAD:" + ARCH).strip()
if not m:
    report("baseline-blob", False, "no `blob <sha1>` on the Baseline line")
else:
    same = m.group(1) == head_blob
    report("baseline-blob", same, "" if same else
           "plan pins %s, HEAD architecture is %s" % (m.group(1)[:12], head_blob[:12]))

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
           "recorded %s, recomputed %s" % (record.group(1)[:12], digest[:12]))

# 3. no citation of an id that does not exist
wp_defs = package_defs(plan)
g_defs = gate_defs(plan)
defined = set(wp_defs) | set(g_defs)
cited = {}
for path, text in ((PLAN, plan), (ARCH, arch)):
    for hit in re.finditer(r"WP-\d{3}\b|G\d+\.\d+\b", text):
        if re.search(r"\bno\s+$", text[max(0, hit.start() - 6):hit.start()], re.I):
            continue                            # "there is no G5.7 equivalent"
        cited.setdefault(hit.group(0), set()).add(path)
dangling = sorted(t for t in cited if t not in defined)
report("ids-defined", not dangling, "" if not dangling else
       ", ".join("%s (%s)" % (t, "+".join(sorted(cited[t]))) for t in dangling))

# 4. no id defined twice, which would let 3 pass against the wrong row
all_ids = wp_defs + g_defs
dupes = sorted({i for i in all_ids if all_ids.count(i) > 1})
report("ids-unique", not dupes, "" if not dupes else ", ".join(dupes))

if subprocess.run(["git", "diff", "--quiet", "HEAD", "--", PLAN, ARCH]).returncode != 0:
    print("\nnote: the working tree differs from HEAD; everything above describes HEAD.")

if failures:
    print("\n%d of 4 checks failed: %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("\n4 of 4 checks passed")
