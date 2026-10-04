// rendertext.cpp: font rendering

#include "cube.h"

int VIRTW;
bool ignoreblinkingbit = false; // for remote-n-temp override of '\fb'
static hashtable<const char *, font> fonts;
static font *fontdef = NULL;

font *curfont = NULL;

VARP(allowblinkingtext, 0, 0, 1); // if you're so inclined

void newfont(char *name, char *tex, int *defaultw, int *defaulth, int *offsetx, int *offsety, int *offsetw, int *offseth)
{
    if(*defaulth < 10) return;          // (becomes FONTH)
    Texture *_tex = textureload(tex);
    if(_tex == notexture || !_tex->xs || !_tex->ys) return;
    font *f = fonts.access(name);
    if(!f)
    {
        name = newstring(name);
        f = &fonts[name];
        f->name = name;
    }

    f->tex = _tex;
    f->chars.shrink(0);
    f->defaultw = *defaultw;
    f->defaulth = *defaulth;
    f->offsetx = *offsetx;
    f->offsety = *offsety;
    f->offsetw = *offsetw;
    f->offseth = *offseth;
    f->skip = 33;

    fontdef = f;
}
COMMANDN(font, newfont, "ssiiiiii");

void fontchar(int *x, int *y, int *w, int *h)
{
    if(!fontdef) return;

    font::charinfo &c = fontdef->chars.add();
    c.w = *w ? *w : fontdef->defaultw;
    c.h = *h ? *h : fontdef->defaulth;
    c.left    = (*x + fontdef->offsetx) / float(fontdef->tex->xs);
    c.top     = (*y + fontdef->offsety) / float(fontdef->tex->ys);
    c.right   = (*x + c.w + fontdef->offsetw) / float(fontdef->tex->xs);
    c.bottom  = (*y + c.h + fontdef->offseth) / float(fontdef->tex->ys);
}
COMMAND(fontchar, "iiii");

void fontskip(int *n)
{
    if(!fontdef) return;

    fontdef->skip = *n;
}
COMMAND(fontskip, "i");

bool setfont(const char *name)
{
    font *f = fonts.access(name);
    if(!f) return false;
    curfont = f;
    return true;
}
COMMAND(setfont, "s");

COMMANDF(curfont, "", () { result(curfont && curfont->name ? curfont->name : ""); });

font *getfont(const char *name)
{
    return fonts.access(name);
}

static vector<font *> fontstack;

void pushfont(const char *name)
{
    fontstack.add(curfont);
    setfont(name);
}

void popfont()
{
    if(!fontstack.empty()) curfont = fontstack.pop();
}

int text_width(const char *str)
{
    int width, height;
    text_bounds(str, width, height); // text_bounds does the tr() itself
    return width;
}

void draw_textf(const char *fstr, int left, int top, ...)
{
    fstr = tr(fstr); // translate the template, so the arguments survive
    defvformatstring(str, top, fstr);
    draw_text(str, left, top);
}

inline int draw_char_contd(font &f, font::charinfo &info, int charcode, int x, int y)
{
    glTexCoord2f(info.left,  info.top   ); glVertex2f(x,          y);
    glTexCoord2f(info.right, info.top   ); glVertex2f(x + info.w, y);
    glTexCoord2f(info.right, info.bottom); glVertex2f(x + info.w, y + info.h);
    glTexCoord2f(info.left,  info.bottom); glVertex2f(x,          y + info.h);

    xtraverts += 4;
    return info.w;
}

// texture the glyph quads are currently batched against; CJK glyphs live in a
// different atlas than the bitmap font, so we have to flush the batch to switch
static unsigned int text_lasttex = 0;

static void text_bindtex(unsigned int id, int bpp)
{
    if(text_lasttex == id) return;
    glEnd();
    glBindTexture(GL_TEXTURE_2D, id);
    glBlendFunc(GL_SRC_ALPHA, bpp == 32 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
    glBegin(GL_QUADS);
    text_lasttex = id;
}

// drawable by the current font: either a bitmap glyph, or a CJK fallback glyph
static bool text_renderable(int c)
{
    if(curfont->chars.inrange(c - curfont->skip)) return true;
    return c >= 128 && cjk_hasglyph(c);
}

// advance width in virtual units; callers add the usual +1 spacing themselves
static int text_advance(int c)
{
    if(curfont->chars.inrange(c - curfont->skip)) return curfont->chars[c - curfont->skip].w;
    if(c >= 128) return cjk_advance(c, FONTH);
    return 0;
}

static int draw_char(int c, int x, int y)
{
    if(curfont->chars.inrange(c-curfont->skip))
    {
        text_bindtex(curfont->tex->id, curfont->tex->bpp);
        font::charinfo &info = curfont->chars[c-curfont->skip];

        return draw_char_contd(*curfont, info, c, x, y);
    }

    if(c >= 128)
    {
        const cjkglyph *g = cjk_getglyph(c, FONTH);
        if(g)
        {
            text_bindtex(g->texid, 32);
            int gx = x + g->ox, gy = y + g->oy;
            glTexCoord2f(g->left,  g->top   ); glVertex2f(gx,        gy);
            glTexCoord2f(g->right, g->top   ); glVertex2f(gx + g->w, gy);
            glTexCoord2f(g->right, g->bottom); glVertex2f(gx + g->w, gy + g->h);
            glTexCoord2f(g->left,  g->bottom); glVertex2f(gx,        gy + g->h);

            xtraverts += 4;
            return g->advance;
        }
    }
    return 0;
}

vector<threeint> igraphbatch;

void queue_igraph(int c, int x, int y)
{
    threeint v = { c, x, y };
    igraphbatch.add(v);
}

VARP(igraphsize, 80, 120, 300);
VARP(igraphsizehardcoded, 80, 106, 160);
VARP(igraphanimate, 0, 1, 1);

void render_igraphs()
{
    igraphbatch.sort(cmpintasc);
    int last = 0, w[2] = { (FONTH * igraphsize) / 100, (FONTH * igraphsizehardcoded) / 100 }, offs[2] = { (FONTH - w[0]) / 2, (FONTH - w[1]) / 2 }, ishc = 0;
    igraph *ig = NULL;
    glColor4ub(255, 255, 255, 255);
    loopv(igraphbatch)
    {
        if(last != igraphbatch[i].key) ig = getusedigraph((last = igraphbatch[i].key)), ishc = last > 0 && last < 10 ? 1 : 0;
        if(ig && ig->tex)
        {
            int &x = igraphbatch[i].val1, &y = igraphbatch[i].val2, fpos = ((totalmillis + x * x + VIRTW * y) % ig->frames.last() + ig->frames.last()) % ig->frames.last(), frame = 0;
            float fw = min(1.0f, float(ig->tex->ys) / ig->tex->xs);
            if(igraphanimate) loopvj(ig->frames) if(ig->frames[j] < fpos) frame++;
            quad(ig->tex->id, x + offs[ishc], y + offs[ishc], w[ishc], (frame % max(1, ig->tex->xs / ig->tex->ys)) * fw, 0, fw, 1);
        }
    }
}

//stack[sp] is current color index
static void text_color(char c, char *stack, int size, int &sp, bvec color, int a)
{
    if(c=='s') // save color
    {
        c = stack[sp];
        if(sp<size-1) stack[++sp] = c;
    }
    else
    {
        if(c=='r') c = stack[(sp > 0) ? --sp : sp]; // restore color
        else if(c == 'b') { if(allowblinkingtext && !ignoreblinkingbit) stack[sp] *= -1; } // blinking text - only if allowed
        else stack[sp] = c;
        switch(iabs(stack[sp]))
        {
            case '0': color = bvec( 2,  255,  128 ); break;   // green: player talk
            case '1': color = bvec( 96,  160, 255 ); break;   // blue: team chat
            case '2': color = bvec( 255, 192,  64 ); break;   // yellow: gameplay action messages, only actions done by players - 230 230 20 too bright
            case '3': color = bvec( 255,  64,  64 ); break;   // red: important errors and notes
            case '4': color = bvec( 128, 128, 128 ); break;   // gray
            case '5': color = bvec( 255, 255, 255 ); break;   // white
            case '6': color = bvec(  96,  48,   0 ); break;   // dark brown
            case '7': color = bvec( 153,  51,  51 ); break;   // dark red: dead admin
            case '8': color = bvec( 192,  64, 192 ); break;   // magenta
            case '9': color = bvec( 255, 102,   0 ); break;   // orange

            case 'A':color = bvec( 0xff, 0xb7, 0xb7); break;   // red set
            case 'B':color = bvec( 0xCC, 0x33, 0x33); break;   //
            case 'C':color = bvec( 0x66, 0x33, 0x33); break;   //
            case 'D':color = bvec( 0xF8, 0x98, 0x4E); break;   //

            case 'E':color = bvec( 0xFF, 0xFF, 0xB7); break;   // yellow set
            case 'F':color = bvec( 0xCC, 0xCC, 0x33); break;   //
            case 'G':color = bvec( 0x66, 0x66, 0x33); break;   //
            case 'H':color = bvec( 0xCC, 0xFC, 0x58); break;   //

            case 'I':color = bvec( 0xB7, 0xFF, 0xB7); break;   // green set
            case 'J':color = bvec( 0x33, 0xCC, 0x33); break;   //
            case 'K':color = bvec( 0x33, 0x66, 0x33); break;   //
            case 'L':color = bvec( 0x3F, 0xFF, 0x98); break;   //

            case 'M':color = bvec( 0xB7, 0xFF, 0xFF); break;   // cyan set
            case 'N':color = bvec( 0x33, 0xCC, 0xCC); break;   //
            case 'O':color = bvec( 0x33, 0x66, 0x66); break;   //
            case 'P':color = bvec( 0x4F, 0xCC, 0xF8); break;   //

            case 'Q':color = bvec( 0xB7, 0xB7, 0xFF); break;   // blue set
            case 'R':color = bvec( 0x33, 0x33, 0xCC); break;   //
            case 'S':color = bvec( 0x33, 0x33, 0x66); break;   //
            case 'T':color = bvec( 0xA0, 0x49, 0xFF); break;   //

            case 'U':color = bvec( 0xFF, 0xB7, 0xFF); break;   // magenta set
            case 'V':color = bvec( 0xCC, 0x33, 0xCC); break;   //
            case 'W':color = bvec( 0x66, 0x33, 0x66); break;   //
            case 'X':color = bvec( 0xFF, 0x01, 0xD5); break;   //

            case 'Y':color = bvec( 0xC7, 0xD1, 0xE2); break;   // lt gray
            case 'Z':color = bvec( 0x32, 0x32, 0x32); break;   // dark gray

            case 'u': color = bvec(120, 240, 120); break;   // stats: green
            case 'v': color = bvec(120, 120, 240); break;   // stats: blue
            case 'w': color = bvec(230, 230, 110); break;   // stats: yellow
            case 'x': color = bvec(250, 100, 100); break;   // stats: red
        }
        int b = (int) (sinf(lastmillis / 200.0f) * 115.0f);
        b = stack[sp] > 0 ? 100 : min(iabs(b), 100);
        glColor4ub(color.x, color.y, color.z, (a * b) / 100);
    }
}

static vector<int> *columns = NULL;

void text_startcolumns()
{
    if(!columns) columns = new vector<int>;
}

void text_endcolumns()
{
    DELETEP(columns);
}

#define TABALIGN(x) ((((x)+PIXELTAB)/PIXELTAB)*PIXELTAB)

#define TEXTGETCOLUMN \
    if(columns && col<columns->length()) \
    { \
        colx += (*columns)[col++]; \
        x = colx; \
    } \
    else x = TABALIGN(x);

#define TEXTSETCOLUMN \
    if(columns) \
    { \
        while(col>=columns->length()) columns->add(0); \
        int w = TABALIGN(x) - colx; \
        w = max(w, (*columns)[col]); \
        (*columns)[col] = w; \
        col++; \
        colx += w; \
        x = colx; \
    } \
    else x = TABALIGN(x);


// Note: the loop walks byte offsets but only ever stops on codepoint starts, so
// TEXTINDEX/TEXTWHITE/TEXTLINE keep handing out byte positions (what the edit
// buffer and hit-testing use) while multi-byte characters stay intact.
#define TEXTSKELETON \
    int y = 0, x = 0, col = 0, colx = 0;\
    int i;\
    for(i = 0; str[i]; )\
    {\
        int clen = 1;\
        int c = utf8_decode(str + i, clen);\
        TEXTINDEX(i)\
        if(c=='\t')      { TEXTTAB(i); TEXTWHITE(i) }\
        else if(c==' ')  { x += curfont->defaultw; TEXTWHITE(i) }\
        else if(c=='\n') { TEXTLINE(i) x = 0; y += FONTH; }\
        else if(c=='\f') { if(str[i+1]) { i++; TEXTCOLOR(i) }}\
        else if(c=='\1') { if(str[i+1]) { i++; TEXTIGRAPH(i) }}\
        else if(c=='\a') { if(str[i+1]) { i++; }}\
        else if(text_renderable(c))\
        {\
            if(maxwidth != -1)\
            {\
                int j = i;\
                int w = text_advance(c);\
                int p = i + clen;\
                int last = i;\
                bool single = c >= 128;\
                for(; str[p] && !single; )\
                {\
                    int plen = 1;\
                    int pc = utf8_decode(str + p, plen);\
                    if(pc=='\f') { if(str[p+1]) p += 2; continue; }\
                    if(pc=='\1') { if(!str[p+1]) break; if(w + FONTH + 1 >= maxwidth) break; w += FONTH + 1; last = p; p += 2; continue; }\
                    if(!text_renderable(pc)) break;\
                    if(pc >= 128) break;\
                    if(p - j > 16) break;\
                    int cw = text_advance(pc) + 1;\
                    if(w + cw >= maxwidth) break;\
                    w += cw;\
                    last = p;\
                    p += plen;\
                }\
                i = last;\
                if(x + w >= maxwidth && j!=0) { TEXTLINE(j-1) x = 0; y += FONTH; }\
                TEXTWORD\
            }\
            else\
            { TEXTCHAR(i) }\
        }\
        i += utf8_charlen(str + i);\
    }

//all the chars are guaranteed to be either drawable or color commands
#define TEXTWORDSKELETON \
                for(; j <= i; )\
                {\
                    int wlen = 1;\
                    int c = utf8_decode(str + j, wlen);\
                    TEXTINDEX(j)\
                    if(c=='\f') { if(str[j+1]) { j++; TEXTCOLOR(j) }}\
                    else if(c=='\1') { if(str[j+1]) { j++; TEXTIGRAPH(j) }}\
                    else { TEXTCHAR(j) }\
                    j += wlen;\
                }

int text_visible(const char *str, int hitx, int hity, int maxwidth)
{
    #define TEXTINDEX(idx)
    #define TEXTTAB(idx) TEXTGETCOLUMN
    #define TEXTWHITE(idx) if(y+FONTH > hity && x >= hitx) return idx;
    #define TEXTLINE(idx) if(y+FONTH > hity) return idx;
    #define TEXTCOLOR(idx)
    #define TEXTIGRAPH(idx) x += FONTH; TEXTWHITE(idx)
    #define TEXTCHAR(idx) x += text_advance(c)+1; TEXTWHITE(idx)
    #define TEXTWORD TEXTWORDSKELETON
    TEXTSKELETON
    #undef TEXTINDEX
    #undef TEXTTAB
    #undef TEXTWHITE
    #undef TEXTLINE
    #undef TEXTCOLOR
    #undef TEXTIGRAPH
    #undef TEXTCHAR
    #undef TEXTWORD
    return i;
}

//inverse of text_visible
void text_pos(const char *str, int cursor, int &cx, int &cy, int maxwidth)
{
    #define TEXTINDEX(idx) if(idx == cursor) { cx = x; cy = y; break; }
    #define TEXTTAB(idx) TEXTGETCOLUMN
    #define TEXTWHITE(idx)
    #define TEXTLINE(idx)
    #define TEXTCOLOR(idx)
    #define TEXTIGRAPH(idx) x += FONTH;
    #define TEXTCHAR(idx) x += text_advance(c) + 1;
    #define TEXTWORD TEXTWORDSKELETON if(i >= cursor) break;
    cx = INT_MIN;
    cy = 0;
    TEXTSKELETON
    if(cx == INT_MIN) { cx = x; cy = y; }
    #undef TEXTINDEX
    #undef TEXTTAB
    #undef TEXTWHITE
    #undef TEXTLINE
    #undef TEXTCOLOR
    #undef TEXTIGRAPH
    #undef TEXTCHAR
    #undef TEXTWORD
}

void text_bounds(const char *str, int &width, int &height, int maxwidth)
{
    str = tr(str); // must match what draw_text will layout
    #define TEXTINDEX(idx)
    #define TEXTTAB(idx) TEXTSETCOLUMN
    #define TEXTWHITE(idx)
    #define TEXTLINE(idx) if(x > width) width = x;
    #define TEXTCOLOR(idx)
    #define TEXTIGRAPH(idx) x += FONTH + 1;
    #define TEXTCHAR(idx) x += text_advance(c) + 1;
    #define TEXTWORD x += w + 1;
    width = 0;
    TEXTSKELETON
    height = y + FONTH;
    TEXTLINE(_)
    #undef TEXTINDEX
    #undef TEXTTAB
    #undef TEXTWHITE
    #undef TEXTLINE
    #undef TEXTCOLOR
    #undef TEXTIGRAPH
    #undef TEXTCHAR
    #undef TEXTWORD
}

void draw_text(const char *str, int left, int top, int r, int g, int b, int a, int cursor, int maxwidth)
{
    str = tr(str);
#define TEXTINDEX(idx) if(idx == cursor) { cx = x; cy = y; cc = c; }
#define TEXTTAB(idx) TEXTGETCOLUMN
#define TEXTWHITE(idx)
#define TEXTLINE(idx)
#define TEXTCOLOR(idx) text_color(str[idx], colorstack, sizeof(colorstack), colorpos, color, a);
#define TEXTIGRAPH(idx) queue_igraph((uchar)str[idx], left+x, top+y), x += FONTH + 1;
#define TEXTCHAR(idx) x += draw_char(c, left+x, top+y) + 1;
#define TEXTWORD TEXTWORDSKELETON
    char colorstack[10];
    bvec color(r, g, b);
    int colorpos = 0, cx = INT_MIN, cy = 0, cc = ' ';
    colorstack[0] = 'c'; //indicate user color
    igraphbatch.setsize(0);
    // rasterize CJK glyphs before the batch opens: texture creation and upload
    // are not allowed between glBegin/glEnd
    cjk_prepare(str, FONTH);
    glBlendFunc(GL_SRC_ALPHA, curfont->tex->bpp==32 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
    glBindTexture(GL_TEXTURE_2D, curfont->tex->id);
    text_lasttex = curfont->tex->id;
    glBegin(GL_QUADS);
    glColor4ub(color.x, color.y, color.z, a);
    TEXTSKELETON
    glEnd();
    if(cursor >= 0)
    {
        if(cx == INT_MIN) { cx = x; cy = y; }
        if(maxwidth != -1 && cx >= maxwidth) { cx = 0; cy += FONTH; }
        int cw = text_advance(cc) + 1;
        if(cw <= 1) cw = curfont->defaultw;
        rendercursor(left+cx, top+cy, cw);
    }
    render_igraphs();
#undef TEXTINDEX
#undef TEXTTAB
#undef TEXTWHITE
#undef TEXTLINE
#undef TEXTCOLOR
#undef TEXTIGRAPH
#undef TEXTCHAR
#undef TEXTWORD
}

void reloadfonts()
{
    enumerate(fonts, font, f,
        if(!reloadtexture(*f.tex)) fatal("failed to reload font texture");
    );
    cjk_reload();
}

void cutcolorstring(char *text, int maxlen)
{ // limit string length, ignore color codes
    if(!curfont) return;
    int len = 0;
    maxlen *= curfont->defaultw;
    for(int i = 0; text[i]; )
    {
        int start = i;
        int clen = 1;
        int c = utf8_decode(text + i, clen);
        if(c == '\f' && text[i+1]) i += 2;
        else if(c == '\1' && text[i+1]) { i += 2; len += FONTH + 1; }
        else
        {
            if(c == '\t') len = TABALIGN(len);
            else { int w = text_advance(c); len += w ? w : curfont->defaultw; }
            i += clen;
        }
        if(len > maxlen) { text[start] = '\0'; break; } // cut on a codepoint boundary
    }
}

bool filterunrenderables(char *s)
{
    bool res = false;
    char *d = s;
    for(int i = 0; s[i]; )
    {
        int clen = 1;
        int c = utf8_decode(s + i, clen);
        bool keep;
        if(c == ' ') keep = true;
        else if(c >= 128) keep = clen > 1 && cjk_hasglyph(c); // keep whole sequences, drop invalid bytes
        else keep = curfont->chars.inrange(c - curfont->skip);
        if(keep) loopj(clen) *d++ = s[i + j];
        else res = true;
        i += clen;
    }
    *d = '\0';
    return res;
}

// animated inlined graphics

VAR(igraphdefaultframetime, 5, 200, 2000);
VARP(hideigraphs, 0, 0, 1);
#define IGRAPHPATH "packages" PATHDIVS "misc" PATHDIVS "igraph" PATHDIVS
hashtable<const char *, igraph> igraphs;    // keyed by shorthand
hashtable<const char *, char> igraphsi;     // known filenames
vector<igraph *> usedigraphs;               // char codes ('\1' + n)

void addigraph(char *fname)
{
    char *b, *s = newstring(fname), *mnem = strtok_r(s, "_", &b), *r;
    if(mnem && *mnem && !igraphs.access(mnem))
    { // new mnem
        defformatstring(filename)("%s%s.png", IGRAPHPATH, fname);
        Texture *tex = textureload(filename);
        if(tex != notexture && tex->xs && tex->ys)
        {
            igraph &ig = igraphs[newstring(mnem)];
            ig.fname = fname;
            ig.tex = tex;
            ig.used = !mnem[1] && *mnem >= '1' && *mnem <= '9' ? *mnem - '0' : 0;
            if(ig.used && usedigraphs.inrange(ig.used)) usedigraphs[ig.used] = &ig;     // hardcode "1".."9"
            while((r = strtok_r(NULL, "_", &b))) ig.frames.add(max(5, (int)ATOI(r)));
            if(ig.frames.empty()) ig.frames.add(igraphdefaultframetime);
            while(ig.frames.length() < tex->xs / tex->ys) ig.frames.add(ig.frames.last());
            loopv(ig.frames) if(i) ig.frames[i] += ig.frames[i - 1];
#ifdef _DEBUG
            clientlogf(" loaded igraph \"%s\", short \"%s\", %dx%d, %d frame%s", fname, mnem, tex->xs, tex->ys, ig.frames.length(), ig.frames.length() > 1 ? "s" : "");
#endif
        }
    }
    igraphsi[fname] = 0;
    delstring(s);
}

void updateigraphs()    // read all filenames and parse new ones (also preloads the textures)
{
    vector<char *> files;
    while(usedigraphs.length() <= (int)'\n') usedigraphs.add(NULL); // allocate hardcoded slots and skip slot 10 ('\n')
    listfiles(IGRAPHPATH, "png", files);
    loopvrev(files)
    {
        if(igraphsi.access(files[i])) delstring(files[i]);
        else addigraph(files[i]);
    }
}
COMMAND(updateigraphs, "");

igraph *getusedigraph(int i)
{
    if(usedigraphs.inrange(i)) return usedigraphs[i];
    return NULL;
}

int getigraph(const char *mnem)
{
    igraph *ig = igraphs.access(mnem);
    if(ig)
    {
        if(!ig->used && usedigraphs.length() < 255)
        {
            ig->used = usedigraphs.length();
            usedigraphs.add(ig);
        }
        return ig->used;
    }
    return 0;
}

void _getigraph(char *s)
{
    uchar res[3] = { '\1', (uchar)getigraph(s), '\0' };
    result(res[1] ? (char*)res : "");
}
COMMANDN(getigraph, _getigraph, "s");

void encodeigraphs(char *d, const char *s, int len) // find known igraphs in a string and substitute with rendercodes, only used for console messages
{
    if(hideigraphs) copystring(d, s, len);
    else
    {
        len--;
        string mnem;
        loopi(len)
        {
            if(*s == ':' && (!i || isspace(s[-1])) && i < len - 1)
            {
                int l = strcspn(s + 1, " \t\n."), u;
                if(l && l < MAXSTRLEN)
                {
                    copystring(mnem, s + 1, l + 1);
                    if((u = getigraph(mnem)))
                    { // found one: encode
                        *d++ = '\1';
                        *d++ = u;
                        s += l + 1;
                        i++;
                        continue;
                    }
                }
            }
            *d++ = *s++;
        }
        *d = '\0';
    }
}

void enumigraphs(vector<const char *> &igs, const char *s, int len)
{
    enumeratek(igraphs, const char *, mnem, if(!strncasecmp(mnem, s, len)) igs.add(mnem));
}
