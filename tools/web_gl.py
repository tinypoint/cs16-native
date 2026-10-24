"""Bounded WebGL legacy-emulation adaptation of our native GL bridge.

Only client-side arrays are exposed; buffer object APIs are not registered.
Track their declared state rather than querying desktop-only GL enum values.
"""
def patch(source):
 source=source.replace('#include <OpenGL/gl.h>','#define GL_GLEXT_PROTOTYPES 1\n#include <GL/gl.h>\n#include <GL/glext.h>\n#include <emscripten.h>')
 needle='#include "opengl_bindings.inc"'
 declarations='''extern int cs16_web_present(int,int);
typedef struct {uint32_t address;int size,type,stride,enabled;} web_array;
static web_array web_arrays[3];
static void cs16_web_client_state(GLenum cap,int enable){
 int index=cap==GL_VERTEX_ARRAY?0:cap==GL_TEXTURE_COORD_ARRAY?1:cap==GL_COLOR_ARRAY?2:-1;
 if(index<0)hle_fatal("Unsupported browser client-array capability %x",cap);
 web_arrays[index].enabled=enable;if(enable)glEnableClientState(cap);else glDisableClientState(cap);
}
'''
 declarations += """
/* Desktop GL creates an unused texture name on first bind. WebGL requires
 * generated objects, so keep the guest namespace separate from host IDs. */
typedef struct {GLuint guest,host;} web_texture;
static web_texture web_textures[8192];static GLuint web_next_texture=1;
static GLuint web_texture_host(GLuint guest,int create){
 if(!guest)return 0;unsigned free_slot=8192;
 for(unsigned i=0;i<8192;i++){if(web_textures[i].guest==guest)return web_textures[i].host;if(!web_textures[i].guest&&free_slot==8192)free_slot=i;}
 if(!create)return 0;if(free_slot==8192)hle_fatal("Browser texture namespace capacity exceeded");
 GLuint host;glGenTextures(1,&host);if(!host)hle_fatal("Browser texture creation failed");web_textures[free_slot]=(web_texture){guest,host};return host;
}
static GLuint web_texture_guest(GLuint host){if(!host)return 0;for(unsigned i=0;i<8192;i++)if(web_textures[i].host==host)return web_textures[i].guest;hle_fatal("Unmapped browser texture binding");return 0;}
static void web_bind_texture(GLenum target,GLuint guest){glBindTexture(target,web_texture_host(guest,1));}
static void web_gen_textures(GLsizei n,GLuint *out){for(GLsizei i=0;i<n;i++){while(!web_next_texture||web_texture_host(web_next_texture,0))web_next_texture++;GLuint guest=web_next_texture++;web_texture_host(guest,1);out[i]=guest;}}
static void web_delete_textures(GLsizei n,const GLuint *names){for(GLsizei k=0;k<n;k++)for(unsigned i=0;i<8192;i++)if(names[k]&&web_textures[i].guest==names[k]){glDeleteTextures(1,&web_textures[i].host);web_textures[i]=(web_texture){0,0};break;}}
"""
 # Desktop immediate mode stores current attributes until the next vertex.
 # Emscripten appends each attribute call to an interleaved stream instead;
 # submit a complete, identical layout at every original vertex boundary.
 declarations += """
static int web_in_begin;
static GLfloat web_current_color[4]={1,1,1,1},web_current_texcoord[2];
static void web_begin(GLenum mode){glBegin(mode);web_in_begin=1;}
static void web_end(void){glEnd();web_in_begin=0;glColor4fv(web_current_color);}
static void web_color(GLfloat r,GLfloat g,GLfloat b,GLfloat a){web_current_color[0]=r;web_current_color[1]=g;web_current_color[2]=b;web_current_color[3]=a;if(!web_in_begin)glColor4f(r,g,b,a);}
static void web_texcoord(GLfloat s,GLfloat t){web_current_texcoord[0]=s;web_current_texcoord[1]=t;}
static void web_vertex(GLfloat x,GLfloat y,GLfloat z,GLfloat w){glColor4fv(web_current_color);glTexCoord2fv(web_current_texcoord);glVertex4f(x,y,z,w);}
"""
 source=source.replace(needle,declarations+needle)
 source=source.replace('#include <emscripten.h>','#include <emscripten.h>\n#include <emscripten/html5.h>')
 source=source.replace('static SDL_GLContext context;','static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;')
 source=source.replace('SDL_GL_GetCurrentContext()!=context','emscripten_webgl_get_current_context()!=context')
 a=source.index('static void create_context(');b=source.index('static void proc_address(',a)
 source=source[:a]+"""static void create_context(void){
 if(context||!cs16_native_window(cs16_dc_window(A32(0))))RET(0,1);
 EmscriptenWebGLContextAttributes attributes;emscripten_webgl_init_context_attributes(&attributes);
 attributes.majorVersion=1;attributes.minorVersion=0;attributes.alpha=0;attributes.depth=1;attributes.stencil=1;attributes.antialias=0;attributes.preserveDrawingBuffer=1;attributes.enableExtensionsByDefault=0;
 attributes.explicitSwapControl=0;attributes.proxyContextToMainThread=EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
 context=emscripten_webgl_create_context("#canvas",&attributes);if(context<=0){context=0;fprintf(stderr,"[webgl] Worker WebGL context creation failed\\n");RET(0,1);}context_dc=A32(0);fprintf(stderr,"[webgl] original engine context created in Wasm worker\\n");RET(CONTEXT_HANDLE,1);
}
static void make_current(void){
 if(!A32(0)&&!A32(1))RET(emscripten_webgl_make_context_current(0)==EMSCRIPTEN_RESULT_SUCCESS,2);
 if(!context||A32(1)!=CONTEXT_HANDLE||!cs16_native_window(cs16_dc_window(A32(0))))RET(0,2);
 if(emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS)RET(0,2);
 static int legacy_ready;if(!legacy_ready){EM_ASM({Browser.useWebGL=true;Module['ctx']=GLctx;Browser.moduleContextCreatedCallbacks.forEach(function(callback){callback();});

});legacy_ready=1;}
 context_dc=A32(0);RET(1,2);
}
static void delete_context(void){if(!context||A32(0)!=CONTEXT_HANDLE)RET(0,1);emscripten_webgl_destroy_context(context);context=0;context_dc=0;RET(1,1);}
static void get_current_context(void){RET(context&&emscripten_webgl_get_current_context()==context?CONTEXT_HANDLE:0,0);}
static void get_current_dc(void){RET(context&&emscripten_webgl_get_current_context()==context?context_dc:0,0);}
"""+source[b:]
 source=source.replace('SDL_GL_SwapWindow(window);','int w,h;SDL_GetWindowSize(window,&w,&h);cs16_web_present(w,h);glFlush();')
 source=source.replace('if(cs16_presented_frames==1)fprintf(stderr,"[opengl] first guest buffer swap\\n");','')
 source=source.replace('cs16_native_pump();RET(1,1);','if(cs16_presented_frames==1)fprintf(stderr,"[opengl] first guest buffer swap\\n");cs16_native_pump();RET(1,1);')

 source=source.replace('GLint bound=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&bound);if(bound)hle_fatal("GL client pointer with VBO is not implemented");','if(which>2)hle_fatal("Browser normal arrays not implemented");')
 source=source.replace('switch(which){case 0:glVertexPointer','web_arrays[which].address=address;web_arrays[which].size=size;web_arrays[which].type=type;web_arrays[which].stride=stride;\n switch(which){case 0:glVertexPointer')
 a=source.index('static void validate_array(');b=source.index('static void draw_arrays(',a)
 source=source[:a]+'''static void validate_arrays(uint32_t highest){
 for(unsigned i=0;i<3;i++){web_array *a=&web_arrays[i];if(!a->enabled)continue;
  unsigned bytes=scalar_bytes(a->type);uint64_t width=(uint64_t)a->size*bytes,span=(uint64_t)highest*(a->stride?a->stride:width)+width;
  if(!a->address||a->size<1||a->size>4||!bytes||a->stride<0||span>UINT32_MAX)hle_fatal("Invalid browser client-array span");
  gl_buffer(a->address,(size_t)span,0);
 }
}
'''+source[b:]
 source=source.replace('glGenTextures(A32(0),gm_ptr(A32(1)));','web_gen_textures(A32(0),gm_ptr(A32(1)));').replace('glDeleteTextures(A32(0),gm_ptr(A32(1)));','web_delete_textures(A32(0),gm_ptr(A32(1)));')
 source=source.replace('glGetIntegerv(A32(0),gm_ptr(A32(1)));','glGetIntegerv(A32(0),gm_ptr(A32(1)));if(A32(0)==GL_TEXTURE_BINDING_2D)MEM32(A32(1))=web_texture_guest(MEM32(A32(1)));')
 source=source.replace('glGetFloatv(A32(0),gm_ptr(A32(1)));','glGetFloatv(A32(0),gm_ptr(A32(1)));if(A32(0)==GL_TEXTURE_BINDING_2D){GLfloat *value=gm_ptr(A32(1));*value=(GLfloat)web_texture_guest((GLuint)*value);}')
 source=source.replace('GLint bound;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&bound);','GLint bound=0; /* No buffer-object entry points on this target. */')
 source=source.replace('glTexImage2D(A32(0),A32(1),A32(2),A32(3),A32(4),A32(5),A32(6),A32(7),p);', '''GLint internal=A32(2);if(internal==1)internal=GL_LUMINANCE;else if(internal==2)internal=GL_LUMINANCE_ALPHA;else if(internal==3||internal==GL_RGB8)internal=GL_RGB;else if(internal==4||internal==GL_RGBA8||internal==GL_RGBA4||internal==GL_RGB5_A1)internal=GL_RGBA;
 static unsigned uploads;if(uploads++<24)fprintf(stderr,"[webgl-upload] internal=%x mapped=%x format=%x type=%x size=%ux%u\\n",A32(2),internal,A32(6),A32(7),A32(3),A32(4));
 GLenum format=A32(6);void *converted=NULL;
 if(internal!=(GLint)format){
  if(A32(7)!=GL_UNSIGNED_BYTE||!((internal==GL_RGB&&format==GL_RGBA)||(internal==GL_RGBA&&format==GL_RGB)))hle_fatal("Unsupported browser texture component conversion %x/%x",internal,format);
  unsigned from=format==GL_RGBA?4:3,to=internal==GL_RGBA?4:3;GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);size_t src_pitch=((size_t)A32(3)*from+alignment-1)&~(size_t)(alignment-1),dst_pitch=((size_t)A32(3)*to+alignment-1)&~(size_t)(alignment-1);
  if(p){converted=calloc(A32(4),dst_pitch);if(!converted)hle_fatal("Browser texture allocation failed");for(unsigned y=0;y<A32(4);y++)for(unsigned x=0;x<A32(3);x++){const uint8_t *src=(const uint8_t *)p+y*src_pitch+x*from;uint8_t *dst=(uint8_t *)converted+y*dst_pitch+x*to;memcpy(dst,src,3);if(to==4)dst[3]=255;}p=converted;}format=internal;
 }
 if(p&&(A32(2)==GL_RGBA4||A32(2)==GL_RGB5_A1)){
  if(format!=GL_RGBA||A32(7)!=GL_UNSIGNED_BYTE||converted)hle_fatal("Unsupported packed texture conversion");GLint align;glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);size_t pitch=((size_t)A32(3)*4+align-1)&~(size_t)(align-1);converted=calloc(A32(4),pitch);if(!converted)hle_fatal("Packed texture allocation failed");
  for(unsigned y=0;y<A32(4);y++)for(unsigned x=0;x<A32(3);x++)for(unsigned c=0;c<4;c++){size_t i=y*pitch+x*4+c;unsigned levels=A32(2)==GL_RGBA4?15:c==3?1:31;unsigned value=((const uint8_t *)p)[i];((uint8_t *)converted)[i]=(uint8_t)((((value*levels+127)/255)*255+levels/2)/levels);}p=converted;
 }
 glTexImage2D(A32(0),A32(1),internal,A32(3),A32(4),A32(5),format,A32(7),p);free(converted);''')
 # GLES exposes row alignment but not desktop row-length/skip state. No such
 # layout is accepted: the scalar pixel-store handler rejects these requests.
 a=source.index(' GLint row_length,skip_rows,skip_pixels;');b=source.index(' unsigned channels=',a)
 source=source[:a]+source[b:]
 # First-frame readback uses ordinary zero-offset pack state.
 a=source.index('static void capture_frame(');b=source.index('static void swap_buffers(',a)
 source=source[:a]+'''static void capture_frame(SDL_Window *window){(void)window;}
'''+source[b:]
 return source

def bindings(source,available=None):
 if available is not None:
  import re
  def replace(m):
   name=m.group(1)
   return m.group(0) if name in available else f'static void bridge_{name}(void){{gl_require();hle_fatal("Browser GL entry not implemented: {name}");}}'
  source=re.sub(r"static void bridge_(gl\w+)\(void\)\{[^\n]+",replace,source)
 # Normalize the desktop current-attribute API before Emscripten sees it.
 import re
 scalar={'b':('GLbyte',127),'ub':('GLubyte',255),'s':('GLshort',32767),'us':('GLushort',65535),'i':('GLint',2147483647),'ui':('GLuint',4294967295),'f':('GLfloat',None),'d':('GLdouble',None)}
 for suffix,(ctype,denominator) in scalar.items():
  for vector in [False,True]:
   for kind,dimensions in [('Color',[3,4]),('Vertex',[2,3,4]),('TexCoord',[2])]:
    for count in dimensions:
     name='gl'+kind+str(count)+suffix+('v' if vector else '')
     # Only replace entries already supported by the imported bridge.
     pattern=r'static void bridge_'+name+r'\(void\)\{[^\n]+'
     match=re.search(pattern,source)
     if not match or 'hle_fatal' in match[0]:continue
     if vector:
      args=[f'p[{i}]' for i in range(count)];prefix=f'gl_buffer(A32(0),{count}*sizeof({ctype}),0);const {ctype} *p=gm_ptr(A32(0));';slots=1
     else:
      args=[f'({ctype})'+(f'gl_double({i*2})' if suffix=='d' else f'gl_float({i})' if suffix=='f' else f'A32({i})') for i in range(count)];prefix='';slots=count*(2 if suffix=='d' else 1)
     if kind=='Color':
      if denominator:args=[f'fmaxf(-1.0f,(GLfloat)({a})/{denominator}.0f)' for a in args]
      if count==3:args.append('1')
      target='web_color'
     elif kind=='Vertex':
      if count==2:args.append('0')
      if count<4:args.append('1')
      target='web_vertex'
     else:target='web_texcoord'
     source=source[:match.start()]+f'static void bridge_{name}(void){{gl_require();{prefix}{target}('+','.join(args)+f');RET(0,{slots});}}'+source[match.end():]
 source=source.replace('glBegin((GLenum)A32(0));','web_begin(A32(0)==GL_POLYGON?GL_TRIANGLE_FAN:(GLenum)A32(0));').replace('glEnd();RET(0,0);','web_end();RET(0,0);')
 # OpenGL POLYGON is a convex polygon; its vertices form the same filled
 # area as a triangle fan. WebGL/Emscripten do not accept mode 9.
 source=source.replace('glBegin((GLenum)A32(0));','glBegin(A32(0)==GL_POLYGON?GL_TRIANGLE_FAN:(GLenum)A32(0));')
 source=source.replace('glBindTexture((GLenum)A32(0),(GLuint)A32(1));','web_bind_texture((GLenum)A32(0),(GLuint)A32(1));').replace('RET(glIsTexture((GLuint)A32(0)),1);','RET(glIsTexture(web_texture_host((GLuint)A32(0),0)),1);')
 source=source.replace('glEnableClientState((GLenum)A32(0));','cs16_web_client_state((GLenum)A32(0),1);')
 source=source.replace('glDisableClientState((GLenum)A32(0));','cs16_web_client_state((GLenum)A32(0),0);')
 # Desktop accepts enumerated texture environment modes through the float
 # entry point too. Emscripten's legacy float hook handles only scale values.
 source=source.replace('glTexEnvf((GLenum)A32(0),(GLenum)A32(1),(GLfloat)gl_float(2));','if(A32(1)==GL_RGB_SCALE||A32(1)==GL_ALPHA_SCALE)glTexEnvf((GLenum)A32(0),(GLenum)A32(1),(GLfloat)gl_float(2));else glTexEnvi((GLenum)A32(0),(GLenum)A32(1),(GLint)gl_float(2));')
 source=source.replace('glPixelStorei((GLenum)A32(0),(GLint)A32(1));','if(A32(0)!=GL_PACK_ALIGNMENT&&A32(0)!=GL_UNPACK_ALIGNMENT)hle_fatal("Unsupported browser pixel store %x",A32(0));glPixelStorei((GLenum)A32(0),(GLint)A32(1));')
 return source
