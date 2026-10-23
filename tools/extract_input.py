#!/usr/bin/env python3
"""Extract the audited upload once, without running installers or helper tools."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import zipfile

ROOT=Path(__file__).resolve().parents[1]
SHA='0aca7809be8bb5b164bb4a826b468f9bff1776342febdf4f477d95e746633ad8'
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('archive',type=Path);args=ap.parse_args()
    with args.archive.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
    if digest!=SHA:ap.error('Input differs from audited upload; audit the new input first.')
    target=ROOT/'game/upload-531009895'
    if target.exists():ap.error('Extraction destination already exists; existing input is preserved.')
    with zipfile.ZipFile(args.archive) as archive:
        infos=archive.infolist();size=sum(i.file_size for i in infos)
        if size>8*1024**3:ap.error('Archive expands beyond the expected limit.')
        for info in infos:
            path=PurePosixPath(info.filename.replace('\\','/'))
            if path.is_absolute() or '..' in path.parts or any(':' in p for p in path.parts) or stat.S_ISLNK(info.external_attr>>16):
                ap.error('Unsafe archive path or symbolic link.')
        target.mkdir(parents=True)
        archive.extractall(target) # zipfile checks entry CRCs while reading.
    report={'input':args.archive.name,'bytes':args.archive.stat().st_size,'sha256':digest,'entries':len(infos),'unpackedBytes':size,'directory':str(target),'archiveCrcReadDuringExtraction':True,'programsExecuted':False}
    (ROOT/'logs').mkdir(exist_ok=True)
    (ROOT/'logs/input-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Extracted',len(infos),'entries to',target)
