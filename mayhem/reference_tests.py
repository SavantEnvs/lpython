#!/usr/bin/env python3
"""Golden-output oracle: replay upstream's tests/reference/*.json (the same records run_tests.py
checks) for the non-LLVM frontends/backends and compare stdout + return code with the committed
reference output. Prints `PASSED <n>` / `FAILED <n>` and the names of failing tests."""
import glob, json, os, subprocess, sys

KINDS = ("tokens", "ast", "ast_new", "asr", "c", "cpp", "python", "wat")
lpython = sys.argv[1]
passed = failed = 0
bad = []
for ref in sorted(glob.glob("tests/reference/*.json")):
    with open(ref) as f:
        j = json.load(f)
    kind = os.path.basename(ref).split("-", 1)[0]
    cmd = j.get("cmd", "")
    if kind not in KINDS or not cmd.startswith("lpython ") or j.get("outfile"):
        continue
    if "{infile}" not in cmd or not j.get("stdout"):
        continue
    args = cmd.replace(" -o {outfile}", "").split()[1:]
    args = [a.replace("{infile}", j["infile"]) for a in args]
    args.insert(0, "--no-error-banner")
    try:
        p = subprocess.run([lpython] + args, capture_output=True, timeout=60)
        out = p.stdout.decode("utf-8", "replace")
        rc = p.returncode
    except subprocess.TimeoutExpired:
        out, rc = None, None
    with open(os.path.join("tests/reference", j["stdout"]), encoding="utf-8", errors="replace") as f:
        want = f.read()
    if out is not None and rc == j.get("returncode", 0) and out.rstrip("\n") == want.rstrip("\n"):
        passed += 1
    else:
        failed += 1
        bad.append(j["basename"])
for b in bad:
    print("FAIL", b)
print("PASSED", passed)
print("FAILED", failed)
