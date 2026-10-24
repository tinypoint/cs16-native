/*
 * gdidc.c - GDI device contexts, fonts and text, on SDL2_ttf.
 *
 * A DC is a small state record (font, colours, background mode, alignment,
 * origins) that may be bound to pixels: a DirectDraw surface's, through its
 * GetDC (ddraw.c calls hle_gdi_surface_dc). TextOutA renders into those
 * pixels; on a DC with none (a window's: nothing here draws windows) it
 * draws nothing, and the measuring calls answer either way.
 *
 * Fonts are CreateFont's requests matched to TrueType files the way a
 * desktop's font substitution would: Arial and MS Sans Serif to Liberation
 * Sans (Arial's metrics), Times to Liberation Serif, Courier to Liberation
 * Mono, else DejaVu, found by fontconfig (fc-match) or in the usual places.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <SDL2_ttf/SDL_ttf.h>
#include "win32hle.h"

#define TRANSPARENT 1
#define OPAQUE      2

typedef struct {
    int used;
    uint8_t *px; int w, h, pitch, bpp;
    uint32_t font, text, bk, brush, pen, pal, bitmap, hwnd;
    int bkmode, align, wox, woy, vox, voy, cpx, cpy;
} dc_t;
#define MAX_DC 64
static dc_t g_dc[MAX_DC], g_saved[MAX_DC];
#define DC_BASE 0x0DC10000u

typedef struct { int used; int height, weight, italic, quality; char face[32]; TTF_Font *ttf; } font_t;
#define MAX_FONTS 64
static font_t g_font[MAX_FONTS];
#define FONT_BASE 0x0DF10000u
#define SYSTEM_FONT (FONT_BASE + MAX_FONTS)              /* the stock font: 13 px MS Sans Serif */
#define DEFAULT_BITMAP 0x0DB0FFFEu

static dc_t *DC(uint32_t h) {
    uint32_t i = h - DC_BASE;
    return h >= DC_BASE && i < MAX_DC && g_dc[i].used ? &g_dc[i] : NULL;
}
static uint32_t new_dc(void) {
    for (int i = 0; i < MAX_DC; i++)
        if (!g_dc[i].used) {
            memset(&g_dc[i], 0, sizeof g_dc[i]);
            g_dc[i].used = 1, g_dc[i].font = SYSTEM_FONT, g_dc[i].bk = 0xFFFFFFu, g_dc[i].bkmode = OPAQUE;
            return DC_BASE + (uint32_t)i;
        }
    return 0;
}
uint32_t hle_gdi_surface_dc(uint8_t *px, int w, int h, int pitch, int bpp) {
    uint32_t d = new_dc();
    dc_t *x = DC(d);
    if (x) x->px = px, x->w = w, x->h = h, x->pitch = pitch, x->bpp = bpp;
    return d;
}
void hle_gdi_release_dc(uint32_t h) { dc_t *x = DC(h); if (x) x->used = 0; }

/* ---- fonts ---- */
static const char *font_file(const char *face, int bold, int italic) {
    static char path[512];
    const char *family="Arial";
    if(!strncasecmp(face,"Times",5))family="Times New Roman";
    else if(!strncasecmp(face,"Courier",7))family="Courier New";
    snprintf(path,sizeof path,"/System/Library/Fonts/Supplemental/%s%s.ttf",family,
             bold&&italic?" Bold Italic":bold?" Bold":italic?" Italic":"");
    return access(path,R_OK)==0?path:NULL;
}
static font_t *FONT(uint32_t h) {
    if (h == SYSTEM_FONT || h < FONT_BASE || h - FONT_BASE >= MAX_FONTS || !g_font[h - FONT_BASE].used) {
        static font_t sys = { 1, -11, 400, 0, 0, "MS Sans Serif", NULL };   /* 8 pt at 96 dpi: 11 px em, 13 px cell */
        return &sys;
    }
    return &g_font[h - FONT_BASE];
}
static TTF_Font *ttf_of(font_t *f) {
    if (f->ttf) return f->ttf;
    if (!TTF_WasInit() && TTF_Init() != 0) return NULL;
    const char *file = font_file(f->face, f->weight >= 600, f->italic);
    if (!file) { fprintf(stderr, "[gdi] no TrueType font for \"%s\": text is not drawn\n", f->face); return NULL; }
    /* A negative height is the em in pixels; a positive one the cell (ascent
     * plus descent), which for these faces is about 1.15 em. */
    int em = f->height < 0 ? -f->height : f->height > 0 ? f->height * 87 / 100 : 11;
    f->ttf = TTF_OpenFont(file, em > 0 ? em : 11);
    if (f->ttf && f->quality != 3 /* NONANTIALIASED */) TTF_SetFontHinting(f->ttf, TTF_HINTING_NORMAL);
    return f->ttf;
}

static void g_CreateFontA(void) {                        /* (h, w, esc, orient, weight, italic, underline, strike, charset, out, clip, quality, pitch, face) */
    for (int i = 0; i < MAX_FONTS; i++)
        if (!g_font[i].used) {
            font_t *f = &g_font[i];
            memset(f, 0, sizeof *f);
            f->used = 1, f->height = (int)A32(0), f->weight = (int)A32(4), f->italic = A32(5) != 0, f->quality = (int)A32(11);
            snprintf(f->face, sizeof f->face, "%s", ASTR(13) ? ASTR(13) : "Arial");
            RET(FONT_BASE + (uint32_t)i, 14);
        }
    RET(SYSTEM_FONT, 14);
}
static void char_abc(int wide) {
 dc_t *d=DC(A32(0));uint32_t first=A32(1),last=A32(2),out=A32(3);
 if(!d || first>last || last>(wide?65535u:255u) || !gm_accessible(out,(last-first+1)*12,1)){hle_set_last_error(87);RET(0,4);}
 TTF_Font *font=ttf_of(FONT(d->font));if(!font)RET(0,4);
 static const uint16_t cp1252[32]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
 for(uint32_t c=first;c<=last;c++){
  uint16_t code=!wide && c>=128 && c<160?cp1252[c-128]:(uint16_t)c;
  int left,right,bottom,top,advance;
  if(TTF_GlyphMetrics(font,code,&left,&right,&bottom,&top,&advance))RET(0,4);
  uint32_t at=out+(c-first)*12;MEM32(at)=(uint32_t)left;MEM32(at+4)=(uint32_t)(right-left);MEM32(at+8)=(uint32_t)(advance-right);
 }
 RET(1,4);
}
static void g_GetCharABCWidthsA(void){char_abc(0);}
static void g_GetCharABCWidthsW(void){char_abc(1);}
static void g_CreateFontIndirectA(void) {                /* LOGFONTA: height, width, esc, orient, weight, italic(byte)..., face at +28 */
    uint32_t lf = A32(0);
    for (int i = 0; i < MAX_FONTS; i++)
        if (!g_font[i].used) {
            font_t *f = &g_font[i];
            memset(f, 0, sizeof *f);
            f->used = 1, f->height = (int)MEM32(lf), f->weight = (int)MEM32(lf + 16), f->italic = MEM8(lf + 20) != 0, f->quality = MEM8(lf + 26);
            snprintf(f->face, sizeof f->face, "%s", (const char *)gm_ptr(lf+28));
            RET(FONT_BASE + (uint32_t)i, 1);
        }
    RET(SYSTEM_FONT, 1);
}
static void g_EnumFontFamiliesExA(void){
 uint32_t lf=A32(1),callback=A32(2),parameter=A32(3);
 if(!DC(A32(0)) || !gm_accessible(lf,60,0) || !callback || A32(4)){hle_set_last_error(87);RET(0,5);}
 char requested[33];memcpy(requested,gm_ptr(lf+28),32);requested[32]=0;
 static const char *families[]={"Arial","Tahoma","Verdana","MS Sans Serif","MS Shell Dlg","Trebuchet MS","Courier New","Times New Roman","Marlett","System"};
 uint32_t elf=gm_va(gm_calloc(1,188)),metrics=gm_va(gm_calloc(1,96)),result=1;unsigned found=0;
 for(unsigned i=0;i<sizeof families/sizeof *families;i++){
  if(*requested && strcasecmp(requested,families[i]))continue;
  font_t temporary={0};temporary.height=-16;temporary.weight=400;snprintf(temporary.face,sizeof temporary.face,"%s",families[i]);
  TTF_Font *font=ttf_of(&temporary);if(!font)continue;
  memset(gm_ptr(elf),0,188);memcpy(gm_ptr(elf),gm_ptr(lf),60);MEM32(elf)=(uint32_t)-16;MEM32(elf+16)=400;
  snprintf(gm_ptr(elf+28),32,"%s",families[i]);snprintf(gm_ptr(elf+60),64,"%s",families[i]);snprintf(gm_ptr(elf+124),32,"Regular");snprintf(gm_ptr(elf+156),32,"Western");
  memset(gm_ptr(metrics),0,96);int asc=TTF_FontAscent(font),desc=-TTF_FontDescent(font),average,height,width;
  TTF_SizeText(font,"x",&average,&height);TTF_SizeText(font,"W",&width,&height);
  MEM32(metrics)=(uint32_t)(asc+desc);MEM32(metrics+4)=(uint32_t)asc;MEM32(metrics+8)=(uint32_t)desc;MEM32(metrics+20)=(uint32_t)average;MEM32(metrics+24)=(uint32_t)width;MEM32(metrics+28)=400;
  MEM8(metrics+44)=32;MEM8(metrics+45)=255;MEM8(metrics+46)='?';MEM8(metrics+47)=32;MEM8(metrics+51)=0x26;MEM8(metrics+52)=MEM8(elf+23);
  uint32_t args[]={elf,metrics,4,parameter};found++;result=hle_call_guest(callback,4,args);TTF_CloseFont(font);
  if(!result)break;
 }
 gm_free(gm_ptr(elf));gm_free(gm_ptr(metrics));RET(found?result:1,5);
}

/* ---- GDI objects: brushes, pens, bitmaps ----
 * One handle space (0x0DB1....). A brush/pen carry a COLORREF; a bitmap carries
 * its own pixels so it can be selected into a memory DC and blitted. */
enum { GO_BRUSH = 1, GO_PEN, GO_BITMAP, GO_PALETTE };
#define PS_NULL 5
#define HS_HOLLOW 5
typedef struct {
    int used, type;
    uint32_t color; int style, width;
    uint8_t *px; int w, h, pitch, bpp, owns;             /* bitmap */
    uint32_t *pal; int paln;                             /* palette: paln COLORREFs */
} gobj_t;
#define MAX_GOBJ 1024
static gobj_t g_obj[MAX_GOBJ];
#define GOBJ_BASE 0x0DB10000u
static gobj_t *GOBJ(uint32_t h) { uint32_t i = h - GOBJ_BASE; return h >= GOBJ_BASE && i < MAX_GOBJ && g_obj[i].used ? &g_obj[i] : NULL; }
static uint32_t new_obj(int type) {
    for (int i = 0; i < MAX_GOBJ; i++) if (!g_obj[i].used) {
        memset(&g_obj[i], 0, sizeof g_obj[i]); g_obj[i].used = 1; g_obj[i].type = type;
        return GOBJ_BASE + (uint32_t)i;
    }
    return 0;
}
static uint32_t make_brush(uint32_t color, int hatch) { uint32_t h = new_obj(GO_BRUSH); gobj_t *o = GOBJ(h); if (o) o->color = color, o->style = hatch; return h; }
static uint32_t make_pen(int style, int width, uint32_t color) { uint32_t h = new_obj(GO_PEN); gobj_t *o = GOBJ(h); if (o) o->color = color, o->style = style, o->width = width; return h; }

static uint32_t g_stock[20];
static uint32_t stock(int i) {
    if (i == 13 || i == 17 || i == 12 || i == 10) return SYSTEM_FONT;       /* font stocks: one font */
    if (i < 0 || i >= 20) return 0;
    if (!g_stock[i]) switch (i) {
        case 0: g_stock[i] = make_brush(0xFFFFFF, 0); break;               /* WHITE_BRUSH */
        case 1: g_stock[i] = make_brush(0xC0C0C0, 0); break;               /* LTGRAY_BRUSH */
        case 2: g_stock[i] = make_brush(0x808080, 0); break;               /* GRAY_BRUSH */
        case 3: g_stock[i] = make_brush(0x404040, 0); break;               /* DKGRAY_BRUSH */
        case 4: g_stock[i] = make_brush(0x000000, 0); break;               /* BLACK_BRUSH */
        case 5: g_stock[i] = make_brush(0, HS_HOLLOW); break;              /* NULL_BRUSH (hollow) */
        case 6: g_stock[i] = make_pen(0, 1, 0xFFFFFF); break;              /* WHITE_PEN */
        case 7: g_stock[i] = make_pen(0, 1, 0x000000); break;              /* BLACK_PEN */
        case 8: g_stock[i] = make_pen(PS_NULL, 1, 0); break;               /* NULL_PEN */
        default: g_stock[i] = make_brush(0xC0C0C0, 0); break;              /* DC_BRUSH etc.: a plausible one */
    }
    return g_stock[i];
}
/* The COLORREF a DC fills/strokes with (default: white brush, black pen). */
static uint32_t brush_color(const dc_t *d, int *hollow) {
    gobj_t *o = GOBJ(d->brush); if (hollow) *hollow = o ? o->style == HS_HOLLOW : 0;
    return o ? o->color : 0xFFFFFF;
}
static uint32_t pen_color(const dc_t *d, int *null) {
    gobj_t *o = GOBJ(d->pen); if (null) *null = o ? o->style == PS_NULL : 0;
    return o ? o->color : 0x000000;
}

static void g_CreateSolidBrush(void)    { RET(make_brush(A32(0), 0), 1); }
static void g_CreateHatchBrush(void)    { RET(make_brush(A32(1), 0), 2); }     /* hatch drawn as solid */
static void g_CreatePen(void)           { RET(make_pen((int)A32(0), (int)A32(1), A32(2)), 3); }
static void g_CreateBrushIndirect(void) { uint32_t lb = A32(0); RET(make_brush(lb ? MEM32(lb + 4) : 0, lb && MEM32(lb) == 1 ? HS_HOLLOW : 0), 1); } /* LOGBRUSH: style, color */
static void g_CreatePenIndirect(void)   { uint32_t lp = A32(0); RET(make_pen(lp ? (int)MEM32(lp) : 0, lp ? (int)MEM32(lp + 4) : 1, lp ? MEM32(lp + 8) : 0), 1); }

/* ---- palettes ----
 * A palette is up to 256 COLORREFs. SimCity 2000 builds one from its master
 * palette bitmap and animates it; with a 32bpp device we don't drive real
 * hardware, but an 8bpp DIB blits through the DC's selected palette (see the
 * blit path), and AnimatePalette changing an entry recolours the next blit. */
static void g_CreatePalette(void) {                      /* LOGPALETTE: version, count, PALETTEENTRY[] */
    uint32_t lp = A32(0);
    int n = lp ? (int)MEM16(lp + 2) : 0;
    if (n > 256) n = 256;
    if (n < 0) n = 0;
    uint32_t h = new_obj(GO_PALETTE); gobj_t *o = GOBJ(h);
    if (o) {
        o->pal = (uint32_t *)calloc(256, 4); o->paln = n;
        for (int i = 0; i < n; i++) { uint32_t e = lp + 4 + 4u * (uint32_t)i; o->pal[i] = MEM8(e) | (MEM8(e + 1) << 8) | (MEM8(e + 2) << 16); }
    }
    RET(h, 1);
}
static void g_GetPaletteEntries(void) {                  /* (hpal, start, count, lpentries) */
    gobj_t *o = GOBJ(A32(0)); uint32_t start = A32(1), count = A32(2), lp = A32(3), got = 0;
    if (!o || !o->pal) RET(0, 4);
    for (uint32_t i = 0; i < count && start + i < 256; i++, got++) {
        uint32_t c = o->pal[start + i];
        if (lp) { MEM8(lp + 4 * i) = c & 0xFF; MEM8(lp + 4 * i + 1) = (c >> 8) & 0xFF; MEM8(lp + 4 * i + 2) = (c >> 16) & 0xFF; MEM8(lp + 4 * i + 3) = 0; }
    }
    RET(got, 4);
}
static void g_SetPaletteEntries(void) {                  /* (hpal, start, count, lpentries) */
    gobj_t *o = GOBJ(A32(0)); uint32_t start = A32(1), count = A32(2), lp = A32(3), set = 0;
    if (!o || !o->pal) RET(0, 4);
    for (uint32_t i = 0; i < count && start + i < 256; i++, set++)
        if (lp) o->pal[start + i] = MEM8(lp + 4 * i) | (MEM8(lp + 4 * i + 1) << 8) | (MEM8(lp + 4 * i + 2) << 16);
    if ((int)(start + count) > o->paln) o->paln = (start + count) > 256 ? 256 : (int)(start + count);
    RET(set, 4);
}
static void g_AnimatePalette(void) { g_SetPaletteEntries(); }    /* same store; the next blit recolours */
static void g_GetNearestPaletteIndex(void) {             /* (hpal, color) */
    gobj_t *o = GOBJ(A32(0)); uint32_t c = A32(1); int best = 0; long bd = -1;
    int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    if (o && o->pal) for (int i = 0; i < o->paln; i++) {
        uint32_t p = o->pal[i]; int dr = r - (int)(p & 0xFF), dg = g - (int)((p >> 8) & 0xFF), db = b - (int)((p >> 16) & 0xFF);
        long d = (long)dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) bd = d, best = i;
    }
    RET((uint32_t)best, 2);
}
static void g_SelectPalette(void) { dc_t *d = DC(A32(0)); uint32_t old = d ? d->pal : 0; if (d) d->pal = A32(1); RET(old, 3); }
static void g_RealizePalette(void) { dc_t *d = DC(A32(0)); gobj_t *o = d ? GOBJ(d->pal) : NULL; RET(o ? (uint32_t)o->paln : 0u, 1); }
static void g_ResizePalette(void) { gobj_t *o = GOBJ(A32(0)); if (o) o->paln = (int)A32(1) > 256 ? 256 : (int)A32(1); RET(1, 2); }
static void g_GetSystemPaletteEntries(void) { RET(0, 4); }       /* truecolour device: no system palette */
static void g_SetSystemPaletteUse(void) { RET(1, 2); }           /* SYSPAL_STATIC */
static void g_GetSystemPaletteUse(void) { RET(1, 1); }
static void g_UpdateColors(void) { RET(1, 1); }

/* ---- objects ---- */
int hle_gdi_dib_delete(uint32_t h);                      /* gdi32.c */
static void g_DeleteObject(void) {
    uint32_t h = A32(0);
    if (h >= FONT_BASE && h - FONT_BASE < MAX_FONTS && g_font[h - FONT_BASE].used) {
        font_t *f = &g_font[h - FONT_BASE];
        if (f->ttf) TTF_CloseFont(f->ttf);
        f->used = 0, f->ttf = NULL;
        RET(1, 1);
    }
    gobj_t *o = GOBJ(h);
    if (o) { if (o->owns) free(o->px); free(o->pal); o->used = 0; RET(1, 1); }
    RET(hle_gdi_dib_delete(h) || 1, 1);
}
static void g_GetStockObject(void) { RET(stock((int)A32(0)), 1); }
extern int hle_gdi_dib_view(uint32_t,uint8_t **,int *,int *,int *,int *);
static void g_SelectObject(void) {                       /* (hdc, obj) -> the previous of its kind */
    dc_t *d = DC(A32(0));
    uint32_t o = A32(1), old;
    if(d && o==DEFAULT_BITMAP){old=d->bitmap?d->bitmap:DEFAULT_BITMAP;d->bitmap=0;d->px=NULL;d->w=d->h=1;d->bpp=1;d->pitch=0;RET(old,2);}
    if (o >= FONT_BASE && o <= SYSTEM_FONT) {
        if (!d) RET(SYSTEM_FONT, 2);
        old = d->font, d->font = o; RET(old, 2);
    }
    gobj_t *g = GOBJ(o);
    uint8_t *pixels;int width,height,pitch,bpp;
    if(d && hle_gdi_dib_view(o,&pixels,&width,&height,&pitch,&bpp)) {
        old=d->bitmap?d->bitmap:DEFAULT_BITMAP;d->bitmap=o;
        d->px=pixels;d->w=width;d->h=height;d->pitch=pitch;d->bpp=bpp;RET(old,2);
    }
    if (d && g && g->type == GO_BRUSH) { old = d->brush ? d->brush : stock(0); d->brush = o; RET(old, 2); }
    if (d && g && g->type == GO_PEN)   { old = d->pen ? d->pen : stock(7);   d->pen = o;   RET(old, 2); }
    if (d && g && g->type == GO_BITMAP) {                /* select a bitmap into a memory DC */
        old=d->bitmap?d->bitmap:DEFAULT_BITMAP;d->bitmap=o;
        d->px = g->px, d->w = g->w, d->h = g->h, d->pitch = g->pitch, d->bpp = g->bpp;
        RET(old, 2);
    }
    RET(stock(0), 2);
}

/* make a selectable bitmap object with its own pixels (CreateCompatibleBitmap
 * / CreateBitmap). gdi32.c's CreateDIBSection registers its DIB this way too. */
uint32_t hle_gdi_make_bitmap(int w, int h, int bpp, uint8_t *px, int owns) {
    uint32_t handle = new_obj(GO_BITMAP); gobj_t *o = GOBJ(handle);
    if (o) { o->w = w, o->h = h, o->bpp = bpp, o->pitch = w * (bpp / 8); o->px = px; o->owns = owns; }
    return handle;
}
static void g_CreateCompatibleBitmap(void) {             /* (hdc, w, h) -> 32bpp bitmap */
    int w = (int)A32(1), h = (int)A32(2); if (w < 1) w = 1; if (h < 1) h = 1;
    RET(hle_gdi_make_bitmap(w, h, 32, (uint8_t *)calloc((size_t)w * h, 4), 1), 3);
}
static void g_CreateBitmap(void) {                       /* (w, h, planes, bpp, bits) */
    int w = (int)A32(0), h = (int)A32(1), bpp = (int)A32(3); if (bpp < 8) bpp = 8;
    RET(hle_gdi_make_bitmap(w, h, bpp == 24 ? 32 : bpp, (uint8_t *)calloc((size_t)w * h, bpp / 8 ? bpp / 8 : 1), 1), 5);
}

/* ---- DCs ---- */
uint32_t cs16_dc_window(uint32_t dc){dc_t *d=DC(dc);return d?d->hwnd:0;}
static void g_GetDC(void) {uint32_t h=new_dc();dc_t *d=DC(h);if(d)d->hwnd=A32(0);RET(h,1);}
static void g_SetMapMode(void){if(!DC(A32(0)) || A32(1)!=1){hle_set_last_error(87);RET(0,2);}RET(1,2);}
static void transform_points(int inverse){
 dc_t *d=DC(A32(0));uint32_t p=A32(1),count=A32(2);
 if(!d || count>UINT32_MAX/8 || !gm_accessible(p,count*8,1)){hle_set_last_error(87);RET(0,3);}
 for(uint32_t i=0;i<count;i++){MEM32(p+i*8)+=(uint32_t)(inverse?d->wox-d->vox:d->vox-d->wox);MEM32(p+i*8+4)+=(uint32_t)(inverse?d->woy-d->voy:d->voy-d->woy);}RET(1,3);
}
static void g_LPtoDP(void){transform_points(0);}
static void g_DPtoLP(void){transform_points(1);}
static void g_ReleaseDC(void) { hle_gdi_release_dc(A32(1)); RET(1, 2); }
static void g_SaveDC(void)    { dc_t *d = DC(A32(0)); if (d) g_saved[d - g_dc] = *d; RET(d ? 1u : 0u, 1); }
static void g_RestoreDC(void) { dc_t *d = DC(A32(0)); if (d && g_saved[d - g_dc].used) *d = g_saved[d - g_dc]; RET(d ? 1u : 0u, 2); }
#define SETTER(name, field, def, argc) \
    static void g_##name(void) { dc_t *d = DC(A32(0)); uint32_t old = d ? (uint32_t)d->field : def; if (d) d->field = A32(1); RET(old, argc); }
#define GETTER(name, field, def) \
    static void g_##name(void) { dc_t *d = DC(A32(0)); RET(d ? (uint32_t)d->field : def, 1); }
SETTER(SetTextColor, text, 0u, 2)
SETTER(SetBkColor, bk, 0xFFFFFFu, 2)
SETTER(SetBkMode, bkmode, OPAQUE, 2)
SETTER(SetTextAlign, align, 0u, 2)
GETTER(GetTextColor, text, 0u)
GETTER(GetBkColor, bk, 0xFFFFFFu)
GETTER(GetBkMode, bkmode, OPAQUE)
static void g_SetViewportOrgEx(void) {                   /* (hdc, x, y, &old) */
    dc_t *d = DC(A32(0));
    if (A32(3)) MEM32(A32(3)) = d ? (uint32_t)d->vox : 0, MEM32(A32(3) + 4) = d ? (uint32_t)d->voy : 0;
    if (d) d->vox = (int)A32(1), d->voy = (int)A32(2);
    RET(1, 4);
}
static void g_SetWindowOrgEx(void) {
    dc_t *d = DC(A32(0));
    if (A32(3)) MEM32(A32(3)) = d ? (uint32_t)d->wox : 0, MEM32(A32(3) + 4) = d ? (uint32_t)d->woy : 0;
    if (d) d->wox = (int)A32(1), d->woy = (int)A32(2);
    RET(1, 4);
}
static void g_SetGraphicsMode(void) { RET(1u, 2); }
static void g_ModifyWorldTransform(void) { RET(1, 3); }

/* ---- text ---- */
static void g_GetTextExtentPoint32A(void) {              /* (hdc, text, n, &SIZE) */
    dc_t *d = DC(A32(0));
    font_t *f = FONT(d ? d->font : SYSTEM_FONT);
    TTF_Font *t = ttf_of(f);
    int n = (int)A32(2), w = 6 * n, h = 13;
    if (t && n > 0) {
        char buf[1024];
        if (n >= (int)sizeof buf) n = sizeof buf - 1;
        memcpy(buf, APTR(1), (size_t)n), buf[n] = 0;
        TTF_SizeText(t, buf, &w, &h);
    } else if (t) {
        w = 0, h = TTF_FontHeight(t);
    }
    if (A32(3)) MEM32(A32(3)) = (uint32_t)w, MEM32(A32(3) + 4) = (uint32_t)h;
    RET(1, 4);
}
static void g_GetTextMetricsA(void) {                    /* TEXTMETRICA */
    dc_t *d = DC(A32(0));
    TTF_Font *t = ttf_of(FONT(d ? d->font : SYSTEM_FONT));
    uint32_t m = A32(1);
    if (!m) RET(0, 2);
    memset(gm_ptr(m), 0, 56);
    int asc = t ? TTF_FontAscent(t) : 10, desc = t ? -TTF_FontDescent(t) : 3, ave = 6, max = 12;
    if (t) { int w; TTF_SizeText(t, "x", &ave, &w); TTF_SizeText(t, "W", &max, &w); }
    MEM32(m) = (uint32_t)(asc + desc), MEM32(m + 4) = (uint32_t)asc, MEM32(m + 8) = (uint32_t)desc;
    MEM32(m + 20) = (uint32_t)ave, MEM32(m + 24) = (uint32_t)max, MEM32(m + 28) = 400;
    MEM8(m + 44) = 0x20, MEM8(m + 45) = 0xFF, MEM8(m + 46) = '?', MEM8(m + 47) = 0x20;
    MEM8(m + 51) = (uint8_t)(0x26 | (t && TTF_FontFaceIsFixedWidth(t)?0:1));
    RET(1, 2);
}

static uint32_t pack(const dc_t *d, uint32_t cr) {       /* COLORREF 0x00BBGGRR -> the surface's pixel */
    uint32_t r = cr & 0xFF, g = (cr >> 8) & 0xFF, b = (cr >> 16) & 0xFF;
    if (d->bpp == 16) return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
    return r << 16 | g << 8 | b;
}
static void put(dc_t *d, int x, int y, uint32_t v) {
    uint8_t *p = d->px + (ptrdiff_t)y * d->pitch + (ptrdiff_t)x * (d->bpp / 8);
    if (d->bpp == 16) *(uint16_t *)p = (uint16_t)v;
    else if (d->bpp == 32) *(uint32_t *)p = v;
}
/* IViewObject snapshots enter the same real guest-selected DIB as fonts. */
int cs16_gdi_write_rgba(uint32_t hdc,const uint8_t *rgba,int sw,int sh,int x,int y,int w,int h){
 dc_t *d=DC(hdc);if(!d||!d->px||!rgba||sw<1||sh<1||w<1||h<1||(d->bpp!=16&&d->bpp!=32))return 0;
 int64_t dx=(int64_t)x-d->wox+d->vox,dy=(int64_t)y-d->woy+d->voy;
 for(int row=0;row<h;row++){int64_t ty=dy+row;if(ty<0||ty>=d->h)continue;int sy=(int)((int64_t)row*sh/h);
  for(int col=0;col<w;col++){int64_t tx=dx+col;if(tx<0||tx>=d->w)continue;int sx=(int)((int64_t)col*sw/w);const uint8_t *p=rgba+((size_t)sy*sw+sx)*4;
   put(d,(int)tx,(int)ty,pack(d,(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)));}}
 return 1;
}
static uint32_t get(dc_t *d, int x, int y) {
    const uint8_t *p = d->px + (ptrdiff_t)y * d->pitch + (ptrdiff_t)x * (d->bpp / 8);
    return d->bpp == 16 ? *(const uint16_t *)p : *(const uint32_t *)p;
}
static uint32_t mix(const dc_t *d, uint32_t under, uint32_t over, int a) {
    if (a >= 255) return over;
    if (d->bpp == 16) {
        int r = ((under >> 11) * (255 - a) + (over >> 11) * a) / 255;
        int g = (((under >> 5) & 63) * (255 - a) + ((over >> 5) & 63) * a) / 255;
        int b = ((under & 31) * (255 - a) + (over & 31) * a) / 255;
        return (uint32_t)(r << 11 | g << 5 | b);
    }
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8)
        out |= (uint32_t)((((under >> s) & 255) * (255 - a) + ((over >> s) & 255) * a) / 255) << s;
    return out;
}

static int text_out(dc_t *d,int logical_x,int logical_y,const char *buf,int utf8,const int *clip){
    if(!d || !d->px || (d->bpp!=16 && d->bpp!=32))return 0;
    if(!*buf)return 1;
    font_t *f=FONT(d->font);TTF_Font *t=ttf_of(f);if(!t)return 0;
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *s = f->quality == 3 ? (utf8?TTF_RenderUTF8_Solid(t, buf, white):TTF_RenderText_Solid(t, buf, white)) : (utf8?TTF_RenderUTF8_Blended(t, buf, white):TTF_RenderText_Blended(t, buf, white));
    if (!s) return 0;
    int x = (d->align&1?d->cpx:logical_x) - d->wox + d->vox, y = (d->align&1?d->cpy:logical_y) - d->woy + d->voy;
    if ((d->align & 6) == 6) x -= s->w / 2;              /* TA_CENTER */
    else if (d->align & 2) x -= s->w;                    /* TA_RIGHT */
    if ((d->align & 24) == 24) y -= TTF_FontAscent(t);   /* TA_BASELINE */
    else if (d->align & 8) y -= s->h;                    /* TA_BOTTOM */
    uint32_t fg = pack(d, d->text), bg = pack(d, d->bk);
    SDL_LockSurface(s);
    for (int j = 0; j < s->h; j++) {
        int py = y + j;
        if (py < 0 || py >= d->h || (clip && (py<clip[1] || py>=clip[3]))) continue;
        for (int i = 0; i < s->w; i++) {
            int px = x + i;
            if (px < 0 || px >= d->w || (clip && (px<clip[0] || px>=clip[2]))) continue;
            int a;
            if (s->format->BytesPerPixel == 1) a = ((const uint8_t *)s->pixels)[j * s->pitch + i] ? 255 : 0;
            else a = (int)(((const uint32_t *)((const uint8_t *)s->pixels + j * s->pitch))[i] >> 24);
            if (d->bkmode == OPAQUE) put(d, px, py, mix(d, bg, fg, a));
            else if (a) put(d, px, py, mix(d, get(d, px, py), fg, a));
        }
    }
    SDL_UnlockSurface(s);
    if(d->align&1)d->cpx+=s->w;
    SDL_FreeSurface(s);
    return 1;
}
static void g_TextOutA(void){
 uint32_t n=A32(4);if(n>65535 || (n&&!gm_accessible(A32(3),n,0))){hle_set_last_error(87);RET(0,5);}
 char *text=malloc((size_t)n+1);if(!text)RET(0,5);if(n)memcpy(text,APTR(3),n);text[n]=0;
 int result=text_out(DC(A32(0)),(int)A32(1),(int)A32(2),text,0,NULL);free(text);RET(result,5);
}
static void rectfill(dc_t *,int,int,int,int,uint32_t);
static void ext_text_out(int wide){
 dc_t *d=DC(A32(0));uint32_t flags=A32(3),rect=A32(4),source=A32(5),n=A32(6);
 if(!d||n>65535||(n&&!gm_accessible(source,n*(wide?2:1),0))||((flags&6)&&!gm_accessible(rect,16,0))){hle_set_last_error(87);RET(0,8);}
 /* Glyph-index/PDY, custom advances and bidi shaping are not implemented. */
 if((flags&~6u)||A32(7)){hle_set_last_error(120);RET(0,8);}
 char *text=malloc((size_t)n*(wide?3:1)+1);if(!text)RET(0,8);unsigned used=0;
 for(unsigned i=0;i<n;i++){
  uint32_t c=wide?MEM16(source+2*i):MEM8(source+i);
  if(wide&&c>=0xd800&&c<=0xdbff){if(i+1>=n||MEM16(source+2*i+2)<0xdc00||MEM16(source+2*i+2)>0xdfff){free(text);hle_set_last_error(1113);RET(0,8);}c=0x10000+((c-0xd800)<<10)+(MEM16(source+2*++i)-0xdc00);}
  else if(wide&&c>=0xdc00&&c<=0xdfff){free(text);hle_set_last_error(1113);RET(0,8);}
  if(!wide||c<0x80)text[used++]=(char)c;
  else if(c<0x800){text[used++]=0xc0|(c>>6);text[used++]=0x80|(c&63);}
  else if(c<0x10000){text[used++]=0xe0|(c>>12);text[used++]=0x80|((c>>6)&63);text[used++]=0x80|(c&63);}
  else{text[used++]=0xf0|(c>>18);text[used++]=0x80|((c>>12)&63);text[used++]=0x80|((c>>6)&63);text[used++]=0x80|(c&63);}
 }
 text[used]=0;int clip[4];if(flags&6)for(unsigned i=0;i<4;i++)clip[i]=(int32_t)MEM32(rect+4*i);
 if(flags&2)rectfill(d,clip[0],clip[1],clip[2],clip[3],d->bk);
 if(flags&4){clip[0]+=d->vox-d->wox;clip[2]+=d->vox-d->wox;clip[1]+=d->voy-d->woy;clip[3]+=d->voy-d->woy;}
 int result=n?text_out(d,(int)A32(1),(int)A32(2),text,wide,flags&4?clip:NULL):1;free(text);RET(result,8);
}
static void g_ExtTextOutA(void){ext_text_out(0);}
static void g_ExtTextOutW(void){ext_text_out(1);}

/* ---- drawing ---- */
static void rectfill(dc_t *d, int l, int t, int r, int b, uint32_t cr) {
    if (!d || !d->px) return;
    int ox = d->wox - d->vox, oy = d->woy - d->voy;
    l -= ox; r -= ox; t -= oy; b -= oy;
    if (l > r) { int x = l; l = r; r = x; }
    if (t > b) { int y = t; t = b; b = y; }
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > d->w) r = d->w;
    if (b > d->h) b = d->h;
    uint32_t v = pack(d, cr);
    for (int y = t; y < b; y++) for (int x = l; x < r; x++) put(d, x, y, v);
}
static void g_FillRect(void) {                           /* (hdc, &RECT, hbrush) */
    dc_t *d = DC(A32(0)); uint32_t rc = A32(1); gobj_t *b = GOBJ(A32(2));
    if (d && rc && b && b->style != HS_HOLLOW)
        rectfill(d, (int)MEM32(rc), (int)MEM32(rc + 4), (int)MEM32(rc + 8), (int)MEM32(rc + 12), b->color);
    RET(1, 3);
}
static void g_FrameRect(void) {                          /* (hdc, &RECT, hbrush): 1px border */
    dc_t *d = DC(A32(0)); uint32_t rc = A32(1); gobj_t *b = GOBJ(A32(2));
    if (d && rc && b) {
        int l = (int)MEM32(rc), t = (int)MEM32(rc + 4), r = (int)MEM32(rc + 8), bo = (int)MEM32(rc + 12);
        rectfill(d, l, t, r, t + 1, b->color); rectfill(d, l, bo - 1, r, bo, b->color);
        rectfill(d, l, t, l + 1, bo, b->color); rectfill(d, r - 1, t, r, bo, b->color);
    }
    RET(1, 3);
}
static void g_Rectangle(void) {                          /* (hdc, l, t, r, b): brush fill + pen frame */
    dc_t *d = DC(A32(0)); if (!d) RET(1, 5);
    int l = (int)A32(1), t = (int)A32(2), r = (int)A32(3), b = (int)A32(4), hollow = 0, pnull = 0;
    uint32_t bc = brush_color(d, &hollow), pc = pen_color(d, &pnull);
    if (!hollow) rectfill(d, l + 1, t + 1, r - 1, b - 1, bc);
    if (!pnull) { rectfill(d, l, t, r, t + 1, pc); rectfill(d, l, b - 1, r, b, pc); rectfill(d, l, t, l + 1, b, pc); rectfill(d, r - 1, t, r, b, pc); }
    RET(1, 5);
}
static void g_PatBlt(void) {                             /* (hdc,x,y,w,h,rop): BLACKNESS/WHITENESS/PATCOPY */
    dc_t *d = DC(A32(0)); int x = (int)A32(1), y = (int)A32(2), w = (int)A32(3), h = (int)A32(4); uint32_t rop = A32(5), hollow = 0;
    uint32_t cr = rop == 0x42u ? 0x000000u : rop == 0x00FF0062u ? 0xFFFFFFu : brush_color(d, (int *)&hollow);
    if (d) rectfill(d, x, y, x + w, y + h, cr);
    RET(1, 6);
}
static void g_SetPixel(void) { dc_t *d = DC(A32(0)); if (d && d->px) { int x = (int)A32(1) - d->wox + d->vox, y = (int)A32(2) - d->woy + d->voy; if (x >= 0 && y >= 0 && x < d->w && y < d->h) put(d, x, y, pack(d, A32(3))); } RET(A32(3), 4); }
static void g_GetPixel(void){
 dc_t *d=DC(A32(0));if(!d||!d->px||(d->bpp!=16&&d->bpp!=32))RET(UINT32_MAX,3);
 int x=(int)A32(1)-d->wox+d->vox,y=(int)A32(2)-d->woy+d->voy;if(x<0||y<0||x>=d->w||y>=d->h)RET(UINT32_MAX,3);
 uint32_t pixel=get(d,x,y),r,g,b;if(d->bpp==16){r=((pixel>>11)&31)*255/31;g=((pixel>>5)&63)*255/63;b=(pixel&31)*255/31;}else{r=(pixel>>16)&255;g=(pixel>>8)&255;b=pixel&255;}RET(r|(g<<8)|(b<<16),3);
}
static void g_MoveToEx(void) { dc_t *d = DC(A32(0)); if (A32(3) && d) { MEM32(A32(3)) = (uint32_t)d->cpx; MEM32(A32(3) + 4) = (uint32_t)d->cpy; } if (d) { d->cpx = (int)A32(1); d->cpy = (int)A32(2); } RET(1, 4); }
static void g_LineTo(void) {                             /* Bresenham cp -> (x,y) in pen colour */
    dc_t *d = DC(A32(0)); int x1 = (int)A32(1), y1 = (int)A32(2);
    if (d && d->px) {
        int pn = 0; uint32_t v = pack(d, pen_color(d, &pn)); int x0 = d->cpx, y0 = d->cpy;
        if (!pn) {
            int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
            for (;;) {
                int px = x0 - d->wox + d->vox, py = y0 - d->woy + d->voy;
                if (px >= 0 && py >= 0 && px < d->w && py < d->h) put(d, px, py, v);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * e;
                if (e2 >= dy) { e += dy; x0 += sx; }
                if (e2 <= dx) { e += dx; y0 += sy; }
            }
        }
    }
    if (d) { d->cpx = x1; d->cpy = y1; } RET(1, 3);
}
/* ---- blits (DC -> DC); like-for-like bpp, which is what a game's own
 * back-buffer-to-window blit is ---- */
static void blt(dc_t *dst, int dx, int dy, int w, int h, dc_t *src, int sx, int sy, int sw, int sh) {
    if (!dst || !dst->px || !src || !src->px || w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int py = dy + j; if (py < 0 || py >= dst->h) continue;
        int sj = sy + (sh == h ? j : j * sh / h); if (sj < 0 || sj >= src->h) continue;
        for (int i = 0; i < w; i++) {
            int px = dx + i; if (px < 0 || px >= dst->w) continue;
            int si = sx + (sw == w ? i : i * sw / w); if (si < 0 || si >= src->w) continue;
            put(dst, px, py, get(src, si, sj));
        }
    }
}
static void g_BitBlt(void)     { dc_t *d = DC(A32(0)), *s = DC(A32(5)); blt(d,(int)A32(1),(int)A32(2),(int)A32(3),(int)A32(4),s,(int)A32(6),(int)A32(7),(int)A32(3),(int)A32(4)); RET(1, 9); }
static void g_StretchBlt(void) { dc_t *d = DC(A32(0)), *s = DC(A32(5)); blt(d,(int)A32(1),(int)A32(2),(int)A32(3),(int)A32(4),s,(int)A32(6),(int)A32(7),(int)A32(8),(int)A32(9)); RET(1, 11); }
static void g_CreateCompatibleDC(void) { RET(new_dc(), 1); }   /* a memory DC; a bitmap is selected into it */
static void g_DeleteDC(void) { hle_gdi_release_dc(A32(0)); RET(1, 1); }

/* Windows glyph bitmap ABI, rasterized from the selected real TrueType font. */
static void g_GetGlyphOutlineA(void){
 dc_t *dc=DC(A32(0));uint32_t format=A32(2),metrics=A32(3),capacity=A32(4),buffer=A32(5),matrix=A32(6);
 if(!dc||!metrics||!gm_accessible(metrics,20,1)||!matrix||!gm_accessible(matrix,16,0)){hle_set_last_error(87);RET(UINT32_MAX,7);}
 if(MEM32(matrix)!=0x10000||MEM32(matrix+4)||MEM32(matrix+8)||MEM32(matrix+12)!=0x10000||(format!=0&&format!=1&&format!=6)){hle_set_last_error(120);RET(UINT32_MAX,7);}
 TTF_Font *font=ttf_of(FONT(dc->font));int minx,maxx,miny,maxy,advance;
 uint32_t character=A32(1);if(!font||character>65535||TTF_GlyphMetrics(font,(Uint16)character,&minx,&maxx,&miny,&maxy,&advance)){hle_set_last_error(87);RET(UINT32_MAX,7);}
 int width=maxx-minx,height=maxy-miny;if(width<0||height<0||width>4096||height>4096){hle_set_last_error(87);RET(UINT32_MAX,7);}
 memset(gm_ptr(metrics),0,20);MEM32(metrics)=(uint32_t)width;MEM32(metrics+4)=(uint32_t)height;MEM32(metrics+8)=(uint32_t)minx;MEM32(metrics+12)=(uint32_t)maxy;MEM16(metrics+16)=(uint16_t)advance;
 if(!format)RET(0,7);
 uint32_t pitch=format==1?((uint32_t)width+31)/32*4:((uint32_t)width+3)&~3u,bytes=pitch*(uint32_t)height;
 if(!capacity||!buffer)RET(bytes,7);if(capacity<bytes||!gm_accessible(buffer,bytes,1)){hle_set_last_error(122);RET(UINT32_MAX,7);}
 if(!bytes)RET(0,7);
 SDL_Color white={255,255,255,255};SDL_Surface *rendered=TTF_RenderGlyph_Blended(font,(Uint16)character,white);if(!rendered){hle_set_last_error(8);RET(UINT32_MAX,7);}
 SDL_Surface *surface=SDL_ConvertSurfaceFormat(rendered,SDL_PIXELFORMAT_RGBA32,0);SDL_FreeSurface(rendered);if(!surface){hle_set_last_error(8);RET(UINT32_MAX,7);}
 uint8_t *out=gm_ptr(buffer);memset(out,0,bytes);int left=minx>0?minx:0,top=TTF_FontAscent(font)-maxy;
 SDL_LockSurface(surface);
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){
  int sx=x+left,sy=y+top;uint8_t alpha=0;
  if(sx>=0&&sy>=0&&sx<surface->w&&sy<surface->h)alpha=((uint8_t *)surface->pixels)[sy*surface->pitch+sx*4+3];
  if(format==6)out[(size_t)y*pitch+x]=(uint8_t)((alpha*64u+127u)/255u);else if(alpha>=128)out[(size_t)y*pitch+x/8]|=(uint8_t)(0x80u>>(x&7));
 }
 SDL_UnlockSurface(surface);SDL_FreeSurface(surface);RET(bytes,7);
}

/* Copy the selected bitmap's actual pixels using Windows DIB row order. */
extern int hle_gdi_dib_view(uint32_t,uint8_t **,int *,int *,int *,int *);
static void g_GetDIBits(void){
 uint32_t info=A32(5),bits=A32(4),start=A32(2),count=A32(3);gobj_t *object=GOBJ(A32(1));
 uint8_t *pixels=NULL;int width=0,height=0,pitch=0,depth=0;
 if(object&&object->type==GO_BITMAP){pixels=object->px;width=object->w;height=object->h;pitch=object->pitch;depth=object->bpp;}
 else hle_gdi_dib_view(A32(1),&pixels,&width,&height,&pitch,&depth);
 if(!pixels||!info||!gm_accessible(info,40,1)||MEM32(info)<40||A32(6)||width<1||height<1||(depth!=16&&depth!=24&&depth!=32)){hle_set_last_error(87);RET(0,7);}
 uint32_t output_depth=MEM16(info+14);if(!output_depth)output_depth=32;
 if(output_depth!=24&&output_depth!=32){hle_set_last_error(120);RET(0,7);}
 int topdown=(int32_t)MEM32(info+8)<0;
 uint64_t stride=(((uint64_t)width*output_depth+31)/32)*4;
 if(stride*(unsigned)height>UINT32_MAX){hle_set_last_error(87);RET(0,7);}
 MEM32(info+4)=(uint32_t)width;MEM32(info+8)=topdown?(uint32_t)-height:(uint32_t)height;MEM16(info+12)=1;MEM16(info+14)=(uint16_t)output_depth;MEM32(info+16)=0;MEM32(info+20)=(uint32_t)(stride*height);MEM32(info+32)=0;MEM32(info+36)=0;
 if(!bits)RET(1,7);if(start>=(uint32_t)height)RET(0,7);if(count>(uint32_t)height-start)count=(uint32_t)height-start;
 if(!count)RET(0,7);if(!gm_accessible(bits,(uint32_t)(stride*count),1)){hle_set_last_error(122);RET(0,7);}
 uint8_t *out=gm_ptr(bits);memset(out,0,(size_t)stride*count);
 for(uint32_t y=0;y<count;y++){
  int row=topdown?(int)(start+y):height-1-(int)(start+y);const uint8_t *source=pixels+(ptrdiff_t)row*pitch;
  for(int x=0;x<width;x++){
   unsigned r,g,b;if(depth==16){uint16_t p;memcpy(&p,source+x*2,2);r=((p>>11)&31)*255/31;g=((p>>5)&63)*255/63;b=(p&31)*255/31;}else{b=source[x*(depth/8)];g=source[x*(depth/8)+1];r=source[x*(depth/8)+2];}
   uint8_t *p=out+(size_t)y*stride+x*(output_depth/8);p[0]=(uint8_t)b;p[1]=(uint8_t)g;p[2]=(uint8_t)r;
  }
 }
 RET(count,7);
}

const win32hle_shim win32hle_gdidc[] = {
    { "GetDIBits", g_GetDIBits },
    { "GetGlyphOutlineA", g_GetGlyphOutlineA },
    { "EnumFontFamiliesExA", g_EnumFontFamiliesExA },
    { "GetCharABCWidthsW", g_GetCharABCWidthsW },
    { "SetMapMode", g_SetMapMode },{ "LPtoDP", g_LPtoDP },{ "DPtoLP", g_DPtoLP },
    { "GetCharABCWidthsA", g_GetCharABCWidthsA },
    { "CreateSolidBrush", g_CreateSolidBrush }, { "CreateHatchBrush", g_CreateHatchBrush },
    { "CreateBrushIndirect", g_CreateBrushIndirect }, { "CreatePen", g_CreatePen }, { "CreatePenIndirect", g_CreatePenIndirect },
    { "CreateCompatibleBitmap", g_CreateCompatibleBitmap }, { "CreateBitmap", g_CreateBitmap },
    { "CreateCompatibleDC", g_CreateCompatibleDC }, { "DeleteDC", g_DeleteDC },
    { "CreatePalette", g_CreatePalette }, { "GetPaletteEntries", g_GetPaletteEntries },
    { "SetPaletteEntries", g_SetPaletteEntries }, { "AnimatePalette", g_AnimatePalette },
    { "GetNearestPaletteIndex", g_GetNearestPaletteIndex }, { "SelectPalette", g_SelectPalette },
    { "RealizePalette", g_RealizePalette }, { "ResizePalette", g_ResizePalette },
    { "GetSystemPaletteEntries", g_GetSystemPaletteEntries }, { "SetSystemPaletteUse", g_SetSystemPaletteUse },
    { "GetSystemPaletteUse", g_GetSystemPaletteUse }, { "UpdateColors", g_UpdateColors },
    { "FillRect", g_FillRect }, { "FrameRect", g_FrameRect }, { "Rectangle", g_Rectangle }, { "PatBlt", g_PatBlt },
    { "SetPixel", g_SetPixel }, { "SetPixelV", g_SetPixel }, { "GetPixel", g_GetPixel },
    { "MoveToEx", g_MoveToEx }, { "LineTo", g_LineTo }, { "BitBlt", g_BitBlt }, { "StretchBlt", g_StretchBlt },
    { "CreateFontA", g_CreateFontA }, { "CreateFontIndirectA", g_CreateFontIndirectA },
    { "DeleteObject", g_DeleteObject }, { "GetStockObject", g_GetStockObject }, { "SelectObject", g_SelectObject },
    { "GetDC", g_GetDC }, { "ReleaseDC", g_ReleaseDC }, { "SaveDC", g_SaveDC }, { "RestoreDC", g_RestoreDC },
    { "SetTextColor", g_SetTextColor }, { "SetBkColor", g_SetBkColor }, { "SetBkMode", g_SetBkMode },
    { "SetTextAlign", g_SetTextAlign }, { "GetTextColor", g_GetTextColor }, { "GetBkColor", g_GetBkColor },
    { "GetBkMode", g_GetBkMode }, { "SetViewportOrgEx", g_SetViewportOrgEx }, { "SetWindowOrgEx", g_SetWindowOrgEx },
    { "SetGraphicsMode", g_SetGraphicsMode }, { "ModifyWorldTransform", g_ModifyWorldTransform },
    { "GetTextExtentPoint32A", g_GetTextExtentPoint32A }, { "GetTextExtentPointA", g_GetTextExtentPoint32A },
    { "ExtTextOutA", g_ExtTextOutA }, { "ExtTextOutW", g_ExtTextOutW },
    { "GetTextMetricsA", g_GetTextMetricsA }, { "TextOutA", g_TextOutA },
    { 0, 0 }
};
