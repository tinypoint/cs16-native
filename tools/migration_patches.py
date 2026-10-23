"""Explicit changes authorized for the CS migration builds.

The uploaded binaries and the initial lift output are left intact. Both
platform builds apply the same change while materializing their C sources.
"""
GAMEUI_SHA256='2f1424c3e9a471c5b92d42aaaad6e63227fd6af5967915482c48c75028a8ced9'
def materialize(module,filename,source,input_sha256):
 if module!='gameui' or filename!='recomp_0004.c':return source
 if input_sha256!=GAMEUI_SHA256:raise RuntimeError('GameUI input differs; review the migration patch for this version')
 old='L_43022B20:\n    RECOMP_CALL(sub_4303EFF0);'
 assert source.count(old)==1 and source.count('L_43022BBB:')==1,'CD Key startup gate layout changed'
 new='L_43022B20:\n    /* User-authorized migration change: follow the existing success cleanup. */\n    fprintf(stderr,"[migration] CD Key startup gate bypassed\\n");\n    goto L_43022BBB;\n    RECOMP_CALL(sub_4303EFF0);'
 return '#include <stdio.h>\n'+source.replace(old,new)
