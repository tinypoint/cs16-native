#!/usr/bin/env python3
"""Copy ARM64 SDL2 and SDL2_ttf frameworks into this project's ignored tree."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frameworks',type=Path,required=True,help='Directory containing official SDL2.framework and SDL2_ttf.framework')
    args=parser.parse_args();records={};output=ROOT/'.local/Frameworks';output.mkdir(parents=True,exist_ok=True)
    for name in ('SDL2','SDL2_ttf'):
        source=args.frameworks.resolve()/(name+'.framework');binary=source/'Versions/A'/name
        arch=subprocess.check_output(['lipo','-archs',str(binary)],text=True).split()
        if 'arm64' not in arch:raise RuntimeError(name+' framework has no ARM64 slice')
        destination=output/source.name
        if source!=destination:
            if destination.exists():
                if hashlib.sha256((destination/'Versions/A'/name).read_bytes()).digest()!=hashlib.sha256(binary.read_bytes()).digest():raise RuntimeError('Different framework already installed: '+name)
            else:shutil.copytree(source,destination,symlinks=True)
        info=plistlib.loads((source/'Resources/Info.plist').read_bytes())
        records[name]={'version':info['CFBundleShortVersionString'],'architectures':arch,'binarySha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
    (ROOT/'.local/native-display-dependencies.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(records,indent=2))
if __name__=='__main__':main()
