// fontrender.cpp: UTF-8 helpers and runtime CJK glyph rendering (stb_truetype)

#include "cube.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// ---------------------------------------------------------------------------
// UTF-8 helpers
//
// Every byte is read through uchar: we build with -fsigned-char, so plain
// `char` would sign-extend the high bytes that make up a multi-byte sequence.
// ---------------------------------------------------------------------------

// decode the codepoint starting at s; len receives its byte length (>= 1).
// invalid or truncated sequences are reported as the raw lead byte with len 1,
// so callers always make forward progress and never read past the terminator.
int utf8_decode(const char *s, int &len)
{
    const uchar *p = (const uchar *)s;
    uchar c = p[0];
    if(c < 0x80) { len = 1; return c; }

    int n, cp;
    if((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
    else if((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
    else if((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
    else { len = 1; return c; }

    loopi(n)
    {
        uchar cc = p[i + 1];
        if((cc & 0xC0) != 0x80) { len = 1; return c; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    len = n + 1;
    return cp;
}

int utf8_charlen(const char *s)
{
    int len;
    utf8_decode(s, len);
    return len;
}

// byte offset of the codepoint before pos (< pos); clamps to 0
int utf8_prev(const char *s, int pos)
{
    if(pos <= 0) return 0;
    int i = pos - 1;
    while(i > 0 && ((uchar)s[i] & 0xC0) == 0x80) i--;
    return i;
}

// byte offset of the codepoint after pos (> pos); clamps to strlen(s)
int utf8_next(const char *s, int pos)
{
    int len = (int)strlen(s);
    if(pos >= len) return len;
    int i = pos + 1;
    while(i < len && ((uchar)s[i] & 0xC0) == 0x80) i++;
    return i;
}

// ---------------------------------------------------------------------------
// CJK font + dynamic glyph atlas
//
// The bundled subset is rasterized on demand and packed into a 2048x1024 RGBA
// atlas per raster size. Sizes are keyed by the current font's FONTH, so the
// default/mono/serif fonts each get correctly sized glyphs. Rasterizing at
// FONTH pixels and drawing the quads at FONTH virtual units keeps 1 atlas pixel
// == 1 virtual unit, exactly like the shipped bitmap fonts.
//
// The atlas is filled *before* the glBegin/glEnd batch (cjk_prepare): binding a
// texture or uploading pixels inside glBegin is not allowed.
// ---------------------------------------------------------------------------

#define CJK_FONT_PATH "packages" PATHDIVS "misc" PATHDIVS "fonts" PATHDIVS "AcZhSans.ttf"
#define CJK_ATLAS_W 2048
#define CJK_ATLAS_H 1024
#define CJK_MAX_SIZES 4

struct cjksize
{
    int pxsize;
    unsigned int texid;
    int penx, peny, rowh;
    int baseline;                       // baseline offset from the line top
    hashtable<int, cjkglyph *> index;   // codepoint -> glyph
    vector<cjkglyph *> owned;
    bool warnedfull;
};

static stbtt_fontinfo cjkfont;
static unsigned char *cjkdata = NULL;   // kept alive: stbtt keeps pointers into it
static bool cjkready = false, cjktried = false;
static vector<cjksize *> cjksizes;

// Tuning, as a percentage of the current font's FONTH. The shipped bitmap
// fonts put their baseline at ~80% of the cell and draw caps ~75% tall, so an
// em of ~82% lands CJK ink right on the Latin cap height. Changing either value
// drops the cached glyphs so the next frame rasterizes at the new metrics.
VARFP(cjkfontsize, 40, 82, 150, cjk_reload());
VARFP(cjkbaseline, 40, 80, 120, cjk_reload());

bool cjk_available() { return cjkready; }

static bool cjkload()
{
    stream *f = openfile(CJK_FONT_PATH, "rb");
    if(!f)
    {
        conoutf("\f3could not open bundled CJK font \"%s\" - Chinese text will not render", CJK_FONT_PATH);
        return false;
    }
    int len = f->size();
    if(len <= 0) { delete f; return false; }
    cjkdata = new unsigned char[len];
    int got = f->read(cjkdata, len);
    delete f;
    if(got != len)
    {
        conoutf("\f3could not read bundled CJK font \"%s\"", CJK_FONT_PATH);
        DELETEA(cjkdata);
        return false;
    }

    int off = stbtt_GetFontOffsetForIndex(cjkdata, 0);
    if(off < 0 || !stbtt_InitFont(&cjkfont, cjkdata, off))
    {
        conoutf("\f3bundled CJK font \"%s\" is not usable", CJK_FONT_PATH);
        DELETEA(cjkdata);
        return false;
    }
    return true;
}

// em size in pixels for a given FONTH: ScaleForMappingEmToPixels makes the
// ideographic em exactly this size, which is what we calibrated against the
// bitmap fonts (Noto's own ascent/descent are far taller and would shrink CJK)
static float cjkscale(int pxsize)
{
    return stbtt_ScaleForMappingEmToPixels(&cjkfont, float(pxsize) * cjkfontsize / 100.0f);
}

static void cjktryload()
{
    if(cjktried) return;
    cjktried = true;
    cjkready = cjkload();
}

bool cjk_hasglyph(int codepoint)
{
    cjktryload();
    if(!cjkready || codepoint < 128) return false;
    return stbtt_FindGlyphIndex(&cjkfont, codepoint) != 0;
}

int cjk_advance(int codepoint, int pxsize)
{
    if(!cjk_hasglyph(codepoint)) return 0;
    float scale = cjkscale(pxsize);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&cjkfont, codepoint, &adv, &lsb);
    return (int)(adv * scale + 0.5f);
}

static cjksize *cjkgetsize(int pxsize)
{
    loopv(cjksizes) if(cjksizes[i]->pxsize == pxsize) return cjksizes[i];
    if(cjksizes.length() >= CJK_MAX_SIZES) return cjksizes[0]; // degenerate: share the first

    cjksize *s = new cjksize;
    s->pxsize = pxsize;
    s->texid = 0;
    s->penx = s->peny = s->rowh = 0;
    s->baseline = (int)(pxsize * cjkbaseline / 100.0f + 0.5f);
    s->warnedfull = false;
    cjksizes.add(s);
    return s;
}

static void cjkesetsize(cjksize *s)
{
    loopv(s->owned) delete s->owned[i];
    s->owned.setsize(0);
    s->index.clear();
    s->penx = s->peny = s->rowh = 0;
}

static bool cjkcreatetex(cjksize *s)
{
    if(s->texid) return true;
    glGenTextures(1, &s->texid);
    if(!s->texid) return false;
    glBindTexture(GL_TEXTURE_2D, s->texid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CJK_ATLAS_W, CJK_ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    return true;
}

static bool cjkplace(cjksize *s, int w, int h, int &px, int &py)
{
    if(w > CJK_ATLAS_W || h > CJK_ATLAS_H) return false;
    if(s->penx + w > CJK_ATLAS_W) { s->penx = 0; s->peny += s->rowh + 1; s->rowh = 0; }
    if(s->peny + h > CJK_ATLAS_H) return false;
    px = s->penx;
    py = s->peny;
    s->penx += w + 1;
    s->rowh = max(s->rowh, h);
    return true;
}

static cjkglyph *cjkrasterize(cjksize *s, int codepoint)
{
    if(!cjkcreatetex(s)) return NULL;

    float scale = cjkscale(s->pxsize);
    int gi = stbtt_FindGlyphIndex(&cjkfont, codepoint);
    if(!gi) return NULL;

    int adv, lsb;
    stbtt_GetGlyphHMetrics(&cjkfont, gi, &adv, &lsb);

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&cjkfont, gi, scale, scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;

    cjkglyph *g = new cjkglyph;
    g->texid = s->texid;
    g->advance = (int)(adv * scale + 0.5f);
    g->w = (short)w;
    g->h = (short)h;
    g->ox = (short)x0;
    g->oy = (short)(s->baseline + y0);
    g->left = g->right = g->top = g->bottom = 0.0f;

    if(w <= 0 || h <= 0) return g;      // blank glyph (e.g. a full-width space)

    int px, py;
    if(!cjkplace(s, w, h, px, py))
    { // atlas full: drop everything and repack from scratch
        if(!s->warnedfull)
        {
            conoutf("\f4CJK glyph atlas for size %d is full, rebuilding", s->pxsize);
            s->warnedfull = true;
        }
        cjkesetsize(s);
        if(!cjkplace(s, w, h, px, py))
        {
            s->penx = s->peny = s->rowh = 0;
            glBindTexture(GL_TEXTURE_2D, s->texid);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CJK_ATLAS_W, CJK_ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            if(!cjkplace(s, w, h, px, py)) { delete g; return NULL; }
        }
        g->texid = s->texid;
    }

    unsigned char *bitmap = new unsigned char[w * h];
    stbtt_MakeGlyphBitmap(&cjkfont, bitmap, w, h, w, scale, scale, gi);

    unsigned char *rgba = new unsigned char[w * h * 4];
    loopi(w * h)
    {
        unsigned char a = bitmap[i];
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = a;
    }
    delete[] bitmap;

    glBindTexture(GL_TEXTURE_2D, s->texid);
    glTexSubImage2D(GL_TEXTURE_2D, 0, px, py, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    delete[] rgba;

    g->left   = float(px) / CJK_ATLAS_W;
    g->top    = float(py) / CJK_ATLAS_H;
    g->right  = float(px + w) / CJK_ATLAS_W;
    g->bottom = float(py + h) / CJK_ATLAS_H;
    return g;
}

static cjkglyph *cjklookup(cjksize *s, int codepoint)
{
    cjkglyph **found = s->index.access(codepoint);
    if(found) return *found;
    cjkglyph *g = cjkrasterize(s, codepoint);
    if(!g) return NULL;
    s->index[codepoint] = g;
    s->owned.add(g);
    return g;
}

const cjkglyph *cjk_getglyph(int codepoint, int pxsize)
{
    if(!cjk_hasglyph(codepoint)) return NULL;
    return cjklookup(cjkgetsize(pxsize), codepoint);
}

// rasterize every glyph the string will need, before the draw batch opens
void cjk_prepare(const char *str, int pxsize)
{
    if(!str) return;
    cjktryload();
    if(!cjkready) return;

    bool nonascii = false;
    for(const uchar *p = (const uchar *)str; *p; p++)
        if(*p >= 0x80) { nonascii = true; break; }
    if(!nonascii) return;

    cjksize *s = cjkgetsize(pxsize);
    for(int i = 0; str[i]; )
    {
        int len = 1;
        int c = utf8_decode(str + i, len);
        if(c >= 128) cjklookup(s, c);
        i += len;
    }
}

void cjk_reload()
{
    // the GL context is gone, so just drop the caches; glyphs are re-rasterized
    // on demand into freshly allocated textures
    loopv(cjksizes)
    {
        cjksize *s = cjksizes[i];
        cjkesetsize(s);
        if(s->texid) { glDeleteTextures(1, &s->texid); s->texid = 0; }
        s->warnedfull = false;
    }
}
