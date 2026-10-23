#!/usr/bin/env python3
"""Fetch the pinned original-binary translation toolkit only."""
from pathlib import Path
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    entry=json.loads((ROOT/'repos.lock.json').read_text())['pcrecomp']
    path=ROOT/'vendor/pcrecomp'
    if not path.exists():
        path.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['git','clone','--no-checkout',entry['url'],str(path)],check=True)
    else:
        dirty=subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=path)
        if dirty.strip():raise SystemExit('Preserve toolkit changes before updating.')
    subprocess.run(['git','fetch','--depth','1','origin',entry['commit']],cwd=path,check=True)
    subprocess.run(['git','checkout','--detach',entry['commit']],cwd=path,check=True)
