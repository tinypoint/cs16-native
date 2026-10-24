"""Explicit changes authorized for the CS migration builds.

The uploaded binaries and the initial lift output are left intact. Both
platform builds apply the same change while materializing their C sources.
"""
GAMEUI_SHA256='2f1424c3e9a471c5b92d42aaaad6e63227fd6af5967915482c48c75028a8ced9'
def materialize(module,filename,source,input_sha256):
 if module=='hw' and filename=='recomp_0022.c':
  if input_sha256!='525bdbddf2f180824944ee2576702e040c2f5554ca4290f40faec842e2cf02bb':raise RuntimeError('Engine input differs; review the local CD Key gate')
  old='    if (TEST_LE(_flag_a, _flag_b)) goto L_01DA4A70; /* 0x01DA49C6: jle 0x1da4a70 */'
  assert source.count(old)==1 and source.count('L_01DA49E3:')==1,'Local certificate gate layout changed'
  local='    if(TEST_LE(_flag_a,_flag_b)){uint32_t peer=MEM32(esp+0x10);fprintf(stderr,"[migration-auth] peer=%u lan=%08x protocol=%u\\n",MEM32(peer),MEM32(0x1ED78FC),esi);if(MEM32(peer)==1 && MEM32(0x1ED78FC)==0x3F800000u){fprintf(stderr,"[migration] CD Key certificate requirement skipped for instance-local LAN peer\\n");goto L_01DA49E3;}}\n'
  source=source.replace(old,local+old)
  marker='L_01DA48EC:\n'
  assert source.count(marker)==1 and source.count('L_01DA4970:')==1,'Local hashed-key gate layout changed'
  source=source.replace(marker,marker+'    if(MEM32(MEM32(esp+0xC))==1 && MEM32(0x1ED78FC)==0x3F800000u){fprintf(stderr,"[migration] CD Key hash requirement skipped for instance-local LAN peer\\n");goto L_01DA4970;}\n')
  return '#include <stdio.h>\n'+source
 if module=='mp' and filename=='recomp_0012.c':
  if input_sha256!='0e7f1764f7ff9705153247b5a282acd16b57f488323f85c38457c3b73f2fa677':raise RuntimeError('Server DLL input differs; review the migration patch')
  old='RECOMP_ICALL(MEM32(0x530F6000)); /* 0x53075B0E: call dword ptr [0x530f6000] */\n    RECOMP_FLAGS_IN();'
  assert source.count(old)==1 and source.count('L_53075B7D:')==1,'Server CD Key gate layout changed'
  return '#include <stdio.h>\n'+source.replace(old,old+'\n    /* The user requested removal of the CD Key gate in migration builds. */\n    fprintf(stderr,"[migration] server CD Key gate bypassed\\n");\n    goto L_53075B7D;')
 if module!='gameui' or filename!='recomp_0004.c':return source
 if input_sha256!=GAMEUI_SHA256:raise RuntimeError('GameUI input differs; review the migration patch for this version')
 old='L_43022B20:\n    RECOMP_CALL(sub_4303EFF0);'
 assert source.count(old)==1 and source.count('L_43022BBB:')==1,'CD Key startup gate layout changed'
 new='L_43022B20:\n    /* User-authorized migration change: follow the existing success cleanup. */\n    fprintf(stderr,"[migration] CD Key startup gate bypassed\\n");\n    goto L_43022BBB;\n    RECOMP_CALL(sub_4303EFF0);'
 return '#include <stdio.h>\n'+source.replace(old,new)
