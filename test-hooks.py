#!/usr/bin/env python3
"""Self-test for hooks/pre-commit and hooks/pre-push.

Builds throwaway repositories — and, for the push cases, a throwaway bare
remote — so both hooks are exercised the way git invokes them rather than by
running the scripts by hand. Run it after touching either hook:

    python test-hooks.py

Exits 0 when every case behaves, 1 otherwise, naming the case that broke.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLAN = "dft-atpg-implementation-plan.md"
ARCH = "dft-atpg-platform-architecture-grx930-g100.md"
DIAG = "diagnosis-benchmark-p3.md"
FILES = (PLAN, ARCH, DIAG, "check-doc-drift.py", "test-doc-drift.py",
         "hooks/pre-commit", "hooks/pre-push")
NO_HOOKS = ("-c", "core.hooksPath=.git/no-such-hooks")


def git(repo, *args, env=None):
    """Run git, raising on failure; returns (returncode, output)."""
    r = subprocess.run(("git",) + args, cwd=repo, capture_output=True,
                       encoding="utf-8", errors="replace", env=env)
    return r.returncode, r.stdout + r.stderr


def must(repo, *args):
    rc, out = git(repo, *args)
    if rc != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), out.strip()))
    return out


def copy_in(repo, name):
    src, dst = os.path.join(HERE, name), os.path.join(repo, name)
    if not os.path.exists(src):
        raise RuntimeError("missing %s next to the test" % name)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    if name.startswith("hooks/"):
        os.chmod(dst, 0o755)


def make_repo(branch="main"):
    """A repo holding this checkout's documents and hooks, on a clean baseline."""
    repo = tempfile.mkdtemp(prefix="hooktest-")
    for name in FILES:
        copy_in(repo, name)
    must(repo, "init", "-q", "-b", branch)
    must(repo, "config", "user.name", "selftest")
    must(repo, "config", "user.email", "selftest@example.invalid")
    must(repo, *NO_HOOKS, "add", "--", *FILES)
    # the baseline commit is scaffolding, not something under test
    must(repo, *NO_HOOKS, "commit", "-q", "-m", "baseline")
    must(repo, "config", "core.hooksPath", "hooks")
    return repo


def with_remote(repo):
    remote = tempfile.mkdtemp(prefix="hookremote-")
    must(remote, "init", "-q", "--bare")
    must(repo, "remote", "add", "origin", remote)
    return remote


def drift_the_architecture(repo):
    with open(os.path.join(repo, ARCH), "a", encoding="utf-8") as fh:
        fh.write("\n<!-- edited without re-pinning the plan -->\n")
    must(repo, "add", "--", ARCH)


def head(repo):
    return must(repo, "rev-parse", "HEAD").strip()


def remote_head(repo):
    rc, out = git(repo, "rev-parse", "--verify", "main")
    return out.strip() if rc == 0 else ""


SUMMARY = re.compile(r"\b(\d+) of \1 cases passed\b")


def summary_ok(out):
    """The self-test reported a non-empty pass tally and no failures."""
    m = SUMMARY.search(out)
    return bool(m) and int(m.group(1)) > 0 and "cases failed" not in out


results = []


def record(name, good, out=""):
    print("%s %s" % ("ok  " if good else "FAIL", name))
    if not good and out:
        for line in out.strip().splitlines():
            print("       " + line)
    results.append((name, good))


def case(name, expect_ok, action, needles=(), check=None):
    """Require the exit status, any fixed needles, and any predicate on output."""
    rc, out = action()
    good = ((rc == 0) == expect_ok
            and all(n in out for n in needles)
            and (check is None or check(out)))
    record(name, good, "" if good else out)


# 1. the commit guard
repo = make_repo()
try:
    before = head(repo)
    drift_the_architecture(repo)
    case("pre-commit refuses architecture drift", False,
         lambda: git(repo, "commit", "-m", "drift"), ("FAIL baseline-blob",))
    record("...and the refused commit did not happen", head(repo) == before)
finally:
    shutil.rmtree(repo, ignore_errors=True)

repo = make_repo()
try:
    drift_the_architecture(repo)
    env = dict(os.environ, SKIP_DRIFT_CHECK="1")
    case("SKIP_DRIFT_CHECK=1 bypasses the commit guard", True,
         lambda: git(repo, "commit", "-m", "drift", env=env), ("skipped",))
    record("...and the bypassed commit did happen",
           head(repo) != must(repo, "rev-parse", "HEAD~1").strip())
finally:
    shutil.rmtree(repo, ignore_errors=True)

# 2. the push guard
repo = make_repo()
remote = with_remote(repo)
try:
    case("pre-push allows a healthy push", True,
         lambda: git(repo, "push", "origin", "HEAD:main"), check=summary_ok)
    record("...and the remote received the branch", remote_head(remote) == head(repo))
finally:
    shutil.rmtree(repo, ignore_errors=True)
    shutil.rmtree(remote, ignore_errors=True)

repo = make_repo()
remote = with_remote(repo)
try:
    must(repo, "push", "-q", "origin", "HEAD:main")
    before = remote_head(remote)
    with open(os.path.join(repo, "test-doc-drift.py"), "w", encoding="utf-8") as fh:
        fh.write("import sys\nsys.exit(1)\n")
    must(repo, "commit", "-qam", "break the self-test")
    case("pre-push refuses a failing self-test", False,
         lambda: git(repo, "push", "origin", "HEAD:main"), ("failed to push",))
    record("...and the remote did not move", remote_head(remote) == before)
finally:
    shutil.rmtree(repo, ignore_errors=True)
    shutil.rmtree(remote, ignore_errors=True)

repo = make_repo()
remote = with_remote(repo)
try:
    with open(os.path.join(repo, "test-doc-drift.py"), "w", encoding="utf-8") as fh:
        fh.write("import sys\nsys.exit(1)\n")
    must(repo, "commit", "-qam", "break the self-test")
    env = dict(os.environ, SKIP_SELFTEST="1")
    case("SKIP_SELFTEST=1 bypasses the push guard", True,
         lambda: git(repo, "push", "origin", "HEAD:main", env=env), ("skipped",))
    record("...and the remote received the branch", remote_head(remote) == head(repo))
finally:
    shutil.rmtree(repo, ignore_errors=True)
    shutil.rmtree(remote, ignore_errors=True)

broken = [n for n, good in results if not good]
if broken:
    print("\n%d of %d cases failed: %s" % (len(broken), len(results), "; ".join(broken)))
    sys.exit(1)
print("\n%d of %d cases passed" % (len(results), len(results)))
