"""Feed every snippet of corpus.json to the host mermaid_cli and list what the engine rejects.

    python tests/mermaid-corpus/run.py [path/to/mermaid_cli.exe] [--mode recover]

Exit status 1 when a snippet fails that is not in KNOWN_GAPS (a regression, or a new gap to look at).
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
args = [a for a in sys.argv[1:] if not a.startswith("--")]
cli = args[0] if args else os.path.join(ROOT, "build-host-tests", "Debug", "mermaid_cli.exe")

# Syntax the engine does not read yet; each entry is a reason, so the list doubles as a to-do.
KNOWN_GAPS = json.load(open(os.path.join(HERE, "known-gaps.json"), encoding="utf-8")) if os.path.exists(os.path.join(HERE, "known-gaps.json")) else {}

corpus = json.load(open(os.path.join(HERE, "corpus.json"), encoding="utf-8"))
failed, fixed = [], []
for name, source in corpus.items():
    p = subprocess.run([cli], input=source.encode("utf-8"), capture_output=True)
    ok = p.returncode == 0
    if not ok and name not in KNOWN_GAPS:
        failed.append((name, p.stderr.decode("utf-8", "replace").strip()))
    if ok and name in KNOWN_GAPS:
        fixed.append(name)

print(f"{len(corpus)} snippets, {len(failed)} new failures, {len(KNOWN_GAPS)} known gaps")
for name, err in failed:
    print(f"  FAIL {name}: {err}")
for name in fixed:
    print(f"  now passes (remove from known-gaps.json): {name}")
sys.exit(1 if failed or fixed else 0)
