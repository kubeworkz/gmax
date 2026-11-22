#!/usr/bin/env python3
"""Self-test for check-doc-drift.py.

Builds throwaway repositories from the documents in this one and asserts that
the checker passes on them and fails on each kind of injected drift — one
check at a time, each on its own copy, so a failure cannot be blamed on the
previous case. Run it after touching the checker or the documents:

    python test-doc-drift.py

Exits 0 when every case behaves, 1 otherwise, naming the case that broke.
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKER = "check-doc-drift.py"
PLAN = "dft-atpg-implementation-plan.md"
ARCH = "dft-atpg-platform-architecture-grx930-g100.md"
DIAG = "diagnosis-benchmark-p3.md"
DOCS = (PLAN, ARCH, DIAG)
CHECKS = ("baseline-blob", "plan-hash", "ids-defined", "ids-unique")
IDENTITY = ("-c", "user.name=selftest", "-c", "user.email=selftest@example.invalid")
# a temp repo must never inherit a hook from the caller's config
NO_HOOKS = ("-c", "core.hooksPath=.git/no-such-hooks")


def git(repo, *args):
    r = subprocess.run(("git",) + NO_HOOKS + args, cwd=repo, capture_output=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def make_repo():
    """A repository holding this checkout's documents and checker, committed."""
    repo = tempfile.mkdtemp(prefix="docdrift-")
    for name in DOCS + (CHECKER,):
        src = os.path.join(HERE, name)
        if not os.path.exists(src):
            raise RuntimeError("missing %s next to the test" % name)
        shutil.copy2(src, os.path.join(repo, name))
    git(repo, "init", "-q")
    git(repo, "add", "--", *DOCS, CHECKER)
    git(repo, *IDENTITY, "commit", "-q", "-m", "baseline")
    return repo


def check(repo, *flags):
    r = subprocess.run([sys.executable, CHECKER] + list(flags), cwd=repo,
                       capture_output=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


def ok_lines(out):
    return {c for c in CHECKS if ("ok   " + c) in out}


def fail_lines(out):
    return {c for c in CHECKS if ("FAIL " + c) in out}


def re_pin(repo):
    """Point the plan at the staged architecture and recompute its digest.

    An architecture edit that carries a correct re-pin is not drift in the pin,
    so a case can inject something else there and see only its own check fail.
    """
    blob = git(repo, "rev-parse", ":" + ARCH).strip()
    path = os.path.join(repo, PLAN)
    # universal newlines and an LF write: the checker hashes LF text, and a
    # fresh clone with core.autocrlf=true has CRLF in the working tree
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    text = re.sub(r"(blob `)[0-9a-f]{40}(`)",
                  lambda m: m.group(1) + blob + m.group(2), text, count=1)
    text = re.sub(r"`sha256:[0-9a-f]{64}`", "`sha256:HASH`", text, count=1)
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    text = text.replace("`sha256:HASH`", "`sha256:" + digest + "`", 1)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    git(repo, "add", "--", PLAN)


def drift(repo, name, text):
    """Append `text` to `name`, stage it, and check the staged commit."""
    with open(os.path.join(repo, name), "a", encoding="utf-8") as fh:
        fh.write("\n" + text + "\n")
    git(repo, "add", "--", name)
    return check(repo, "--staged")


# each case: (check expected to fail, file to edit, the edit, why it is drift)
DRIFT = (
    ("baseline-blob", ARCH, "<!-- touched without re-pinning the plan -->",
     "architecture edited, plan still pins the old blob"),
    ("plan-hash", PLAN, "An edit that no revision row records.",
     "plan edited without a matching digest in section 15.1"),
    ("ids-defined", DIAG, "A dangling reference to WP-999 lives here.",
     "protocol cites a work package that does not exist"),
    ("ids-defined", DIAG, "The `C-GLS` sign-off run is out of scope for this protocol.",
     "protocol cites a component code that 4.1 does not define"),
    ("ids-unique", DIAG, "## Appendix Z — `D-DIAG-01` duplicate record",
     "a second appendix heading declares an existing record"),
)

results = []


def record(name, good, out=""):
    print("%s %s" % ("ok  " if good else "FAIL", name))
    if not good and out:
        for line in out.strip().splitlines():
            print("       " + line)
    results.append((name, good))


for label, flags in (("HEAD mode", ()), ("staged mode", ("--staged",))):
    repo = make_repo()
    try:
        rc, out = check(repo, *flags)
        good = rc == 0 and ok_lines(out) == set(CHECKS) and not fail_lines(out)
        record("the real documents pass all four checks (%s)" % label, good, out)
    finally:
        shutil.rmtree(repo, ignore_errors=True)

for name, filename, text, why in DRIFT:
    repo = make_repo()
    try:
        rc, out = drift(repo, filename, text)
        targeted = fail_lines(out) == {name}
        others_held = ok_lines(out) == set(CHECKS) - {name}
        record("%s catches %s" % (name, why),
               rc == 1 and targeted and others_held, out)
    finally:
        shutil.rmtree(repo, ignore_errors=True)

# a duplicate code row inside 4.1 needs the plan re-pinned to get past the pin
repo = make_repo()
try:
    path = os.path.join(repo, ARCH)
    with open(path, encoding="utf-8", newline="") as fh:
        text = fh.read()
    anchor = "| `FE` | Front end: elaboration, Liberty, SDC, DEF/LEF, STIL/SPF import | 7 |"
    assert text.count(anchor) == 1
    duplicate = anchor.replace("Front end: elaboration, Liberty, SDC, DEF/LEF, STIL/SPF import",
                               "Front end, declared a second time")
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text.replace(anchor, anchor + chr(10) + duplicate))
    git(repo, "add", "--", ARCH)
    re_pin(repo)
    rc, out = check(repo, "--staged")
    record("ids-unique catches a component code defined twice",
           rc == 1 and fail_lines(out) == {"ids-unique"}
           and ok_lines(out) == set(CHECKS) - {"ids-unique"}, out)
finally:
    shutil.rmtree(repo, ignore_errors=True)

broken = [n for n, good in results if not good]
if broken:
    print("\n%d of %d cases failed: %s" % (len(broken), len(results), "; ".join(broken)))
    sys.exit(1)
print("\n%d of %d cases passed" % (len(results), len(results)))
