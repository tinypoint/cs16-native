#!/usr/bin/env python3
"""Run the AOT launcher on a separate copy of the uploaded game tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    probes=ap.add_mutually_exclusive_group()
    probes.add_argument('--probe-filesystem',action='store_true')
    probes.add_argument('--probe-version',action='store_true')
    probes.add_argument('--probe-ui',action='store_true')
    probes.add_argument('--probe-x87',action='store_true')
    probes.add_argument('--probe-browser',action='store_true')
    probes.add_argument('--probe-exception',action='store_true');args=ap.parse_args()
    source=ROOT/'game/upload-531009895/CounterStrike1.6';stage=ROOT/'.local/run/game'
    if not stage.exists():
        stage.parent.mkdir(parents=True,exist_ok=True)
        # APFS copy-on-write clone; this does not link writes back to inputs.
        p=subprocess.run(['cp','-cR',str(source),str(stage)],capture_output=True,text=True)
        if p.returncode:
            if stage.exists():raise RuntimeError('Partial staging copy; inspect before retrying')
            shutil.copytree(source,stage)
    names={'hl':'hl.exe','filesystem':'FileSystem_Stdio.dll','vgui':'vgui.dll','steam':'Steam.dll','mss32':'Mss32.dll','dbg':'dbg.dll','hw':'hw.dll','cstrike-client':'cstrike/cl_dlls/client.dll','particleman':'valve/cl_dlls/particleman.dll'}
    names.update({'vgui2':'vgui2.dll','gameui':'valve/cl_dlls/GameUI.dll','demoplayer':'DemoPlayer.dll'})
    hashes={}
    for module,name in names.items():
        expected=json.loads((ROOT/'.local/lift'/module/'result.json').read_text())['inputSha256']
        with (stage/name).open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
        if digest!=expected:raise RuntimeError('Staged module differs from translated input: '+name)
        hashes[name]=digest
    binary=ROOT/'.local/launcher-native/cs16-original-launcher'
    probe=next((name for name in ('filesystem','version','ui','x87','exception','browser') if getattr(args,'probe_'+name)),None)
    mode=probe+'-probe' if probe else 'launcher-run'
    log=ROOT/'logs'/(mode+'.log')
    command=[str(binary),'hl.exe']+(['--probe-'+probe] if probe else [])
    with log.open('w') as stream:
        p=subprocess.run(command,cwd=stage,env={**os.environ,'HLE_FILETRACE':'1','CS16_FRAME_CAPTURE':str(ROOT/'.local/first-game-buffer.bmp')},stdout=stream,stderr=subprocess.STDOUT,timeout=25)
    text=log.read_text(errors='replace');faults=[line for line in text.splitlines() if any(s in line for s in ['FATAL:','[cs16-boot] signal=','watchdog','RaiseException(','[guest-exception]'])]
    record={'exitCode':p.returncode,'inputModulesVerified':hashes,
            'originalLauncherEntryReached':'original launcher CRT entry=' in text,
            'originalDllMainEntries':re.findall(r'entering original DllMain ([0-9a-fA-F]+)',text),
            'originalDllMainSuccesses':text.count('original DllMain returned 1'),
            'originalFactoryResolved':'original export filesystem_stdio.dll -> 40005570' in text,
            'engineFileOpened':bool(re.search(r'\[file\] CreateFileA .*\\hw\.dll.* ok',text)),
            'stopDiagnostics':faults[-4:],'gamePlayable':False,
            'nativeWindows':re.findall(r'\[native-window\] created (.*)',text),
            'presentedDirectDrawSurface':'first DirectDraw presentation' in text}
    record['originalExceptionCatches']=re.findall(r'\[guest-eh\] handling (.*)',text)
    record['originalExceptionContinuations']=re.findall(r'\[guest-eh\] resume=(.*)',text)
    record['createdOpenGLContext']='native context created' in text
    record['presentedOpenGLBuffer']='first guest buffer swap' in text
    if args.probe_filesystem:
        for line in text.splitlines():
            if line.startswith('{"originalFilesystemProbe"'):record['filesystemProbe']=json.loads(line)
    if args.probe_version:
        import pefile
        lines=[line for line in text.splitlines() if line.startswith('{"versionProbe"')]
        if not lines:raise RuntimeError('Version probe did not complete')
        record['versionProbe']=json.loads(lines[-1])
        pe=pefile.PE(str(stage/'Steam.dll'));fixed=pe.VS_FIXEDFILEINFO[0]
        expected_text=[s.entries[b'FileVersion'].decode('ascii') for g in pe.FileInfo for x in g for s in getattr(x,'StringTable',[]) if s.LangID==b'040904b0'][0]
        actual=record['versionProbe']
        assert (actual['fileVersionMS'],actual['fileVersionLS'],actual['fileVersionText'])==(fixed.FileVersionMS,fixed.FileVersionLS,expected_text)
        record['versionMatchesIndependentParser']=True;pe.close()
    if args.probe_ui or args.probe_x87 or args.probe_exception or args.probe_browser:
        key=probe+'Probe'
        lines=[line for line in text.splitlines() if line.startswith('{"'+key+'"')]
        if p.returncode or not lines:raise RuntimeError('Probe failed; see '+str(log))
        record[key]=json.loads(lines[-1])
        if args.probe_x87:
            from check_x87_reference import verify
            record['x86Reference']=verify(record[key])
        if args.probe_exception:
            from exception_metadata import verify_fixtures
            record['exceptionMetadataFixtures']=verify_fixtures()
    output=ROOT/'logs'/(probe+'-probe-result.json' if probe else 'original-startup-result.json')
    output.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
    return p.returncode if p.returncode>=0 else 128-p.returncode

if __name__=='__main__':raise SystemExit(main())
