// weapon.cpp: all shooting and effects code

#include "cube.h"
#include "bot/bot.h"
#include "hudgun.h"
#include "cs2weapons.h"

VARP(autoreload, 0, 1, 1);
VARP(akimboautoswitch, 0, 1, 1);
VARP(akimboendaction, 0, 3, 3); // 0: switch to knife, 1: stay with pistol (if has ammo), 2: switch to grenade (if possible), 3: switch to primary (if has ammo) - all fallback to previous one w/o ammo for target

struct sgray {
    int ds; // damage flag: 0:outer, 1:medium, 2:center
    vec rv; // ray vector
};

sgray sgr[SGRAYS*3];

int burstshotssettings[NUMGUNS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, };

bool quicknade = false;

void updatelastaction(playerent *d, int millis = lastmillis)
{
    loopi(NUMGUNS) d->weapons[i]->updatetimers(millis);
    d->lastaction = millis;
}

void checkweaponswitch()
{
    if(!player1->weaponchanging) return;
    int timeprogress = lastmillis-player1->weaponchanging;
    if(timeprogress>weapon::weaponchangetime)
    {
        addmsg(SV_WEAPCHANGE, "ri", player1->weaponsel->type);
        player1->weaponchanging = 0;
    }
    else if(timeprogress>(weapon::weaponchangetime>>1) && player1->weaponsel != player1->nextweaponsel)
    {
        player1->prevweaponsel = player1->weaponsel;
        player1->weaponsel = player1->nextweaponsel;
        if(quicknade){
            keym *key_LMB = keyms.access(-1);
            if(key_LMB->pressed)
            {
                player1->attacking = true; 
                player1->weaponsel->attack(worldpos); 
            }
        }
    }
}

void selectweapon(weapon *w)
{
    if(!w || !player1->weaponsel->deselectable() || ispaused || intermission) return;
    if(w->selectable())
    {
        if(player1->attacking && player1->state == CS_ALIVE) attack(false);
        int i = w->type;
        // substitute akimbo
        weapon *akimbo = player1->weapons[GUN_AKIMBO];
        if(w->type==GUN_PISTOL && akimbo->selectable()) w = akimbo;

        player1->weaponswitch(w);
        exechook(HOOK_SP, "onWeaponSwitch", "%d", i);
    }
}

void requestweapon(char *ws)
{
    int w = getlistindex(ws, gunnames, true, 0);
    if(keypressed && player1->state == CS_ALIVE && w >= 0 && !ispaused)
    {
        if (player1->akimbo && w == GUN_PISTOL) w = GUN_AKIMBO;
        selectweapon(player1->weapons[w]);
    }
}
COMMANDN(weapon, requestweapon, "s");

void shiftweapon(int *s)
{
    if(keypressed && player1->state == CS_ALIVE && !ispaused)
    {
        if(!player1->weaponsel->deselectable()) return;

        weapon *curweapon = player1->weaponsel;
        weapon *akimbo = player1->weapons[GUN_AKIMBO];

        // collect available weapons
        vector<weapon *> availweapons;
        loopi(NUMGUNS)
        {
            weapon *w = player1->weapons[i];
            if(!w) continue;
            if(w->selectable() || w==curweapon || (w->type==GUN_PISTOL && player1->akimbo))
            {
                availweapons.add(w);
            }
        }

        // replace pistol by akimbo
        if(player1->akimbo)
        {
            availweapons.removeobj(akimbo); // and remove initial akimbo
            int pistolidx = availweapons.find(player1->weapons[GUN_PISTOL]);
            if(pistolidx>=0) availweapons[pistolidx] = akimbo; // insert at pistols position
            if(curweapon->type==GUN_PISTOL) curweapon = akimbo; // fix selection
        }

        // detect the next weapon
        int num = availweapons.length();
        int curidx = availweapons.find(curweapon);
        if(!num || curidx<0) return;
        int idx = (curidx + *s) % num;
        if(idx<0) idx += num;
        weapon *next = availweapons[idx];
        if(next->type!=player1->weaponsel->type) // different weapon
        {
            selectweapon(next);
        }
    }
    else if(player1->isspectating()) updatefollowplayer(*s);
}
COMMAND(shiftweapon, "i");

void quicknadethrow(bool on)
{
    if (!player1->weapons || !player1->weaponsel || !player1->nextweaponsel) return;
    if(player1->state != CS_ALIVE) return;
    if(on)
    {
        if(player1->weapons[GUN_GRENADE]->mag > 0)
        {
            if(player1->weaponsel->type != GUN_GRENADE) selectweapon(player1->weapons[GUN_GRENADE]);
            if(player1->weaponsel->type == GUN_GRENADE || player1->nextweaponsel->type == GUN_GRENADE)
            {
                if(!(player1->weaponsel->type == GUN_GRENADE && player1->nextweaponsel->type != GUN_GRENADE)) attack(true);
            }
        }
    }
    else
    {
        if(player1->weaponsel->type == GUN_GRENADE || player1->nextweaponsel->type == GUN_GRENADE)
        {
            attack(false);
            quicknade = true;
        }
    }
}
COMMAND(quicknadethrow, "d");

COMMANDF(currentprimary, "", () { intret(player1->primweap->type); });
COMMANDF(prevweapon, "", () { intret(player1->prevweaponsel->type); });
COMMANDF(curweapon, "", () { intret(player1->weaponsel->type); });

COMMANDF(magcontent, "s", (char *ws)
{
    int w = getlistindex(ws, gunnames, true, -1);
    if(w >= 0) intret(player1->weapons[w]->mag);
    else intret(-1);
});

COMMANDF(magreserve, "s", (char *ws)
{
    int w = getlistindex(ws, gunnames, true, -1);
    if(w >= 0) intret(player1->weapons[w]->ammo);
    else intret(-1);
});

void tryreload(playerent *p)
{
    if(!p || p->state!=CS_ALIVE || ispaused || p->weaponsel->reloading || p->weaponchanging) return;
    p->weaponsel->reload(false);
}

COMMANDF(reload, "", () { tryreload(player1); });

void createrays(const vec &from, const vec &to) // create random spread of rays for the shotgun
{
    vec dir = vec(from).sub(to);
    float f = dir.magnitude() / 10.0f;
    dir.normalize();
    vec spoke;
    spoke.orthogonal(dir);
    spoke.normalize();
    spoke.mul(f);
    loopk(3)
    {
        float base;
        int wrange;
        switch(k)
        {
            case 0:  base = SGCObase / 100.0f; wrange = SGCOrange; break;
            case 1:  base = SGCMbase / 100.0f; wrange = SGCMrange; break;
            case 2:
            default: base = SGCCbase / 100.0f; wrange = SGCCrange; break;
        }
        float rnddir = rndscale(PI2);
        loopi(SGRAYS)
        {
            int j = k * SGRAYS + i;
            sgr[j].ds = k;
            vec p(spoke);
            int rndmul = rnd(wrange);
            float veclen = base + rndmul/100.0f;
            p.mul(veclen);

            p.rotate(PI2 / SGRAYS * i + rnddir, dir);
            vec rray = vec(to);
            rray.add(p);
            sgr[j].rv = rray;
        }
    }
}

static inline bool intersectbox(const vec &o, const vec &rad, const vec &from, const vec &to, vec *end) // if lineseg hits entity bounding box
{
    const vec *p;
    vec v = to, w = o;
    v.sub(from);
    w.sub(from);
    float c1 = w.dot(v);

    if(c1<=0) p = &from;
    else
    {
        float c2 = v.squaredlen();
        if(c2<=c1) p = &to;
        else
        {
            float f = c1/c2;
            v.mul(f).add(from);
            p = &v;
        }
    }

    if(p->x <= o.x+rad.x
       && p->x >= o.x-rad.x
       && p->y <= o.y+rad.y
       && p->y >= o.y-rad.y
       && p->z <= o.z+rad.z
       && p->z >= o.z-rad.z)
    {
        if(end) *end = *p;
        return true;
    }
    return false;
}

static inline bool intersectsphere(const vec &from, const vec &to, vec center, float radius, float &dist)
{
    vec ray(to);
    ray.sub(from);
    center.sub(from);
    float v = center.dot(ray),
          inside = radius*radius - center.squaredlen();
    if(inside < 0 && v < 0) return false;
    float raysq = ray.squaredlen(), d = inside*raysq + v*v;
    if(d < 0) return false;
    dist = (v - sqrtf(d)) / raysq;
    return dist >= 0 && dist <= 1;
}

static inline bool intersectcylinder(const vec &from, const vec &to, const vec &start, const vec &end, float radius, float &dist)
{
    vec d(end), m(from), n(to);
    d.sub(start);
    m.sub(start);
    n.sub(from);
    float md = m.dot(d),
          nd = n.dot(d),
          dd = d.squaredlen();
    if(md < 0 && md + nd < 0) return false;
    if(md > dd && md + nd > dd) return false;
    float nn = n.squaredlen(),
          mn = m.dot(n),
          a = dd*nn - nd*nd,
          k = m.squaredlen() - radius*radius,
          c = dd*k - md*md;
    if(fabs(a) < 0.005f)
    {
        if(c > 0) return false;
        if(md < 0) dist = -mn / nn;
        else if(md > dd) dist = (nd - mn) / nn;
        else dist = 0;
        return true;
    }
    else if(c > 0)
    {
        float b = dd*mn - nd*md,
              discrim = b*b - a*c;
        if(discrim < 0) return false;
        dist = (-b - sqrtf(discrim)) / a;
    }
    else dist = 0;
    float offset = md + dist*nd;
    if(offset < 0)
    {
        if(nd <= 0) return false;
        dist = -md / nd;
        if(k + dist*(2*mn + dist*nn) > 0) return false;
    }
    else if(offset > dd)
    {
        if(nd >= 0) return false;
        dist = (dd - md) / nd;
        if(k + dd - 2*md + dist*(2*(mn-nd) + dist*nn) > 0) return false;
    }
    return dist >= 0 && dist <= 1;
}


int intersect(playerent *d, const vec &from, const vec &to, vec *end)
{
    float dist;
    if(d->head.x >= 0)
    {
        if(intersectsphere(from, to, d->head, HEADSIZE, dist))
        {
            if(end) (*end = to).sub(from).mul(dist).add(from);
            return 2;
        }
    }
    float y = d->yaw*RAD, p = (d->pitch/4+90)*RAD, c = cosf(p);
    vec bottom(d->o), top(sinf(y)*c, -cosf(y)*c, sinf(p))/*, mid(top)*/;
    bottom.z -= d->eyeheight;
    float h = d->eyeheight /*+ d->aboveeye*/; // this mod makes the shots pass over the shoulders
//     mid.mul(h*0.5).add(bottom);            // this mod divides the hitbox in 2
    top.mul(h).add(bottom);
    if( intersectcylinder(from, to, bottom, top, d->radius, dist) ) // FIXME if using 2 hitboxes
    {
        if(end) (*end = to).sub(from).mul(dist).add(from);
        return 1;
    }
    return 0;
}

bool intersect(entity *e, const vec &from, const vec &to, vec *end)
{
    mapmodelinfo *mmi = getmminfo(e->attr2);
    if(!mmi || !mmi->h) return false;

    float lo = float(S(e->x, e->y)->floor + mmi->zoff + e->attr3);
    return intersectbox(vec(e->x, e->y, lo + mmi->h / 2.0f), vec(mmi->rad, mmi->rad, mmi->h / 2.0f), from, to, end);
}

playerent *intersectclosest(const vec &from, const vec &to, const playerent *at, float &bestdistsquared, int &hitzone, bool aiming = true)
{
    playerent *best = NULL;
    bestdistsquared = 1e16f;
    int zone;
    if(at!=player1 && player1->state==CS_ALIVE && (zone = intersect(player1, from, to)))
    {
        best = player1;
        bestdistsquared = at->o.squareddist(player1->o);
        hitzone = zone;
    }
    loopv(players)
    {
        playerent *o = players[i];
        if(!o || o==at || (o->state!=CS_ALIVE && (aiming || (o->state!=CS_EDITING && o->state!=CS_LAGGED)))) continue;
        float distsquared = at->o.squareddist(o->o);
        if(distsquared < bestdistsquared && (zone = intersect(o, from, to)))
        {
            best = o;
            bestdistsquared = distsquared;
            hitzone = zone;
        }
    }
    return best;
}

playerent *playerincrosshair()
{
    if(camera1->type == ENT_PLAYER || (camera1->type == ENT_CAMERA && player1->spectatemode == SM_DEATHCAM))
    {
        float dist;
        int hitzone;
        return intersectclosest(camera1->o, worldpos, (playerent *)camera1, dist, hitzone, false);
    }
    else return NULL;
}

inline bool intersecttriangle(const vec &from, const vec &dir, const vec &v0, const vec &v1, const vec &v2, vec *end, float *_t) // precise but rather expensive, based on Moeller–Trumbore intersection algorithm
{
    const float EPSILON = 0.00001f;
    vec edge1 = v1; edge1.sub(v0);      // edge1 = v1 - v0
    vec edge2 = v2; edge2.sub(v0);      // edge2 = v2 - v0
    vec pvec; pvec.cross(dir, edge2);   // pvec = dir x edge2
    float det = edge1.dot(pvec);        // det = edge1 * pvec
    if(fabs(det) < EPSILON) return false;
    float invdet = 1.0f / det;
    vec tvec = from; tvec.sub(v0);      // tvec = from - v0
    float u = invdet * tvec.dot(pvec);  // u = tvec * pvec / det
    if(u < 0.0f || u > 1.0f) return false;
    vec qvec; qvec.cross(tvec, edge1);  // qvec = tvec x edge1
    float v = invdet * dir.dot(qvec);   // v = dir * qvec / det
    if(v < 0.0f || u + v > 1.0f) return false;
    float t = invdet * edge2.dot(qvec); // t = edge2 * qvec / det
    if(t < EPSILON) return false;
    if(_t) *_t = t;                     // 0..1 if intersection is between from and to
    if(end) *end = dir, end->mul(t).add(from); // calculate point of intersection
    return true;
}

inline bool intersecttriangle2(const vec &from, const vec &to, const vec &v0, const vec &v1, const vec &v2, vec *end, float *_t)
{
    vec dir = to; dir.sub(from);        // dir = to - from
    return intersecttriangle(from, dir, v0, v1, v2, end, _t);
}

inline bool intersectcorner(const vec &from, const vec &dir, int x, int y, int size, bool cdir, vec *end, float *_t)
{
    float fsx, fsy, dsx, dsy;
    if(cdir)
    {
        dsx = dir.x + dir.y; fsx = from.x - x + from.y - y - size;
        dsy = dir.y - dir.x; fsy = from.y - y - from.x + x + size;
    }
    else
    {
        dsx = dir.x - dir.y; fsx = from.x - x - from.y + y;
        dsy = dir.y + dir.x; fsy = from.y - y + from.x - x;
    }
    if(!dsx) return false;
    float t = -fsx / dsx;
    float dy = dsy * t + fsy;
    if(fabs(t * dsx + fsx) < NEARZERO && dy >= 0 && dy <= 2 * size)
    {
        if(_t) *_t = t;
        if(end) *end = dir, end->mul(t).add(from);
        return true;
    }
    return false;
}

void intersectgeometry(const vec &from, vec &to) // check line for contact with map geometry, shorten if necessary
{
    int x = from.x, y = from.y, hfnb[4] = { 0, 1, ssize, ssize +1 };
    if(OUTBORD(x, y) || from.z < -127.0f || from.z > 127.0f) return;
    vec d = to;                                     // d: direction
    d.sub(from);

    float distmin = 1.0f, vdelta[4];
    sqr *r[2], *s, *nb[4];
    loop(xy, 2) // first check x == const planes and then y == const planes
    {
        float dxy = xy ? d.y : d.x, fromxy = xy ? from.y : from.x;
        bool dxynz = fabs(dxy) < NEARZERO;
        if(dxy == 0.0f) dxy = 1e-20;        // hack

        int ixy = fromxy, step = dxy < 0 ? -1 : 1, steps = fabs(dxy) + 1, sqrdir = xy ? -ssize : -1;
        while(steps-- > 0)
        {
            // intersect d with plane
            float t = dxynz ? distmin : (ixy - fromxy) / dxy;
            vec p = d;
            p.mul(t).add(from);

            // check position
            if(xy) x = p.x, y = ixy;
            else x = ixy, y = p.y;
            if(t > distmin || OUTBORD(x, y)) break;

            // always check cubes on both sides of the plane
            r[0] = S(x, y);
            r[1] = r[0] + sqrdir;
            if(t > 0) loopk(2)
            {
                s = r[k];
                if(SOLID(s) || s->floor > p.z || s->ceil < p.z || s->type == CORNER)
                { // cube s is a probable hit, examine further
                    if(k)
                    { // match x|y back to s
                        if(xy) y--;
                        else x--;
                    }

                    if(s->type == CORNER)
                    {
                        sqr *ns, *h = NULL, *n;
                        int bx, by, bs;
                        int q = cornertest(x, y, bx, by, bs, ns, h);
                        vec newto; float newdist;
                        if(intersectcorner(from, d, bx, by, bs, !(q & 1), &newto, &newdist) && newdist < distmin && (!h || newto.z < ns->floor || newto.z > ns->ceil)) to = newto, distmin = newdist;
                        if(d.z)
                        { // intersect with z == const plane where the corner ends
                            bool downwards = d.z < 0.0f;
                            n = h ? h : ns;
                            float endplate = downwards ? n->floor : n->ceil;
                            float tz = (endplate - from.z) / d.z;
                            vec pz = d;
                            pz.mul(tz).add(from);
                            if(pz.x >= bx && pz.x <= bx + bs && pz.y >= by && pz.y <= by + bs && tz < distmin) to = pz, distmin = tz;
                            if(h)
                            { // intersect with the triangles where socket corners end
                                float sockplate = downwards ? ns->floor : ns->ceil;
                                tz = (sockplate - from.z) / d.z;
                                pz = d; pz.mul(tz).add(from);
                                float cx = pz.x - bx, cy = pz.y - by;
                                bool hit = false;
                                switch(q)                                   //  0XX3
                                {                                           //  XXXX
                                    case 0: hit = cx + cy >= bs; break;     //  1XX2
                                    case 1: hit = cx >= cy;      break;
                                    case 2: hit = cx + cy <= bs; break;
                                    case 3: hit = cx <= cy;      break;
                                    default: hit = true;         break;     // annoy bad mappers
                                }
                                if(hit && cx >= 0 && cy >= 0 && cx <= bs && cy <= bs && tz < distmin) to = pz, distmin = tz;
                            }
                        }
                    }
                    else
                    {
                        // finish checking walls
                        if(SOLID(s) || s->type == SPACE || (s->type == CHF && s->floor > p.z) || (s->type == FHF && s->ceil < p.z))
                        {
                            if(t < distmin) to = p, distmin = t;
                        }
                        else if(s->type == CHF || s->type == FHF)
                        {
                            loopi(4) vdelta[i] = ((nb[i] = s + hfnb[i]))->vdelta;   // 23
                            int nb2 = "\002\001\013\023"[xy + 2 * k], nb1 = nb2 >> 3; nb2 &= 3;
                            if(s->type == FHF)
                            { // FHF side surfaces
                                float a = s->floor - vdelta[nb1] / 4.0f, b = (vdelta[nb1] - vdelta[nb2]) / 4.0f, dummy, i = modff(xy ? p.x : p.y, &dummy);
                                if(a + i * b > p.z && t < distmin) to = p, distmin = t;
                            }
                            else
                            { // CHF side surfaces
                                float a = s->ceil + vdelta[nb1] / 4.0f, b = (vdelta[nb2] - vdelta[nb1]) / 4.0f, dummy, i = modff(xy ? p.x : p.y, &dummy);
                                if(a + i * b < p.z && t < distmin) to = p, distmin = t;
                            }
                        }

                        // check floor and ceiling
                        bool isflat = true, downwards = d.z < 0.0f;
                        if(s->type == CHF || s->type == FHF) loopi(3) if(vdelta[3] != vdelta[i]) isflat = false;
                        float h = downwards ? s->floor - (isflat && s->type == FHF ? vdelta[0] / 4.0f : 0) : s->ceil + (isflat && s->type == CHF ? vdelta[0] / 4.0f : 0);
                        if(isflat || (downwards && s->type == CHF) || (!downwards && s->type == FHF))
                        { // intersect with z == const plane on either SPACE or flat ends of FHF and CHF
                            float tz = d.z != 0.0f ? (h - from.z) / d.z : distmin;
                            vec pz = d;
                            pz.mul(tz).add(from);
                            if(int(pz.x) == x && int(pz.y) == y && tz < distmin) to = pz, distmin = tz;
                        }
                        if(!isflat)
                        { // sloped CHF or FHF: check two triangles, regardless of "downwards" or not (FHF slopes can be seen looking upwards)
                            if(s->type == FHF) loopi(4) vdelta[i] *= -1.0f;
                            float hh = s->type == FHF ? s->floor : s->ceil;
                            vec v0(x, y, hh + vdelta[0] / 4.0f), v1(x + 1, y, hh + vdelta[1] / 4.0f), v2(x, y + 1, hh + vdelta[2] / 4.0f), v3(x + 1, y + 1, hh + vdelta[3] / 4.0f);
                            float newdist; vec newto;
                            if(s->type == FHF)
                            {
                                if(intersecttriangle(from, d, v0, v1, v2, &newto, &newdist) && newdist < distmin) to = newto, distmin = newdist;
                                if(intersecttriangle(from, d, v3, v1, v2, &newto, &newdist) && newdist < distmin) to = newto, distmin = newdist;
                            }
                            else
                            {
                                if(intersecttriangle(from, d, v0, v1, v3, &newto, &newdist) && newdist < distmin) to = newto, distmin = newdist;
                                if(intersecttriangle(from, d, v2, v0, v3, &newto, &newdist) && newdist < distmin) to = newto, distmin = newdist;
                            }
                        }
                    }
                }
            }
            ixy += step;
        }
    }
}

void damageeffect(int damage, playerent *d)
{
    particle_splash(PART_BLOOD, damage/10, 1000, d->o);
}

struct hitweap
{
    float hits;
    int shots;
    hitweap() {hits=shots=0;}
};
hitweap accuracym[NUMGUNS];

inline void attackevent(playerent *owner, int weapon)
{
    if(owner == player1) exechook(HOOK_SP, "onAttack", "%d", weapon);
}

vector<hitmsg> hits;

void hit(int damage, playerent *d, playerent *at, const vec &vel, int gun, bool gib, int info)
{
    if(d==player1 || d->type==ENT_BOT || !m_mp(gamemode)) d->hitpush(damage, vel, at, gun);

    if(at == player1 && d != player1)
    {
        extern int hitsound;
        extern int lasthit;
        if(hitsound == 2 && lasthit != lastmillis)
        {
            defformatstring(hitsnd)("sound %d %d;", S_HITSOUND, SP_HIGHEST);
            addsleep(60, hitsnd);
            lasthit = lastmillis;
        }
    }

    if(!m_mp(gamemode)) dodamage(damage, d, at, gun, gib);
    else
    {
        hitmsg &h = hits.add();
        h.target = d->clientnum;
        h.lifesequence = d->lifesequence;
        h.info = info;
        if(d==player1)
        {
            h.dir = ivec(0, 0, 0);
            d->damageroll(damage);
            if(d != at) updatedmgindicator(player1, at->o);
            damageblend(damage, d);
            damageeffect(damage, d);
            audiomgr.playsound(S_PAIN6, SP_HIGH);
        }
        else
        {
            h.dir = ivec(int(vel.x*DNF), int(vel.y*DNF), int(vel.z*DNF));
//             damageeffect(damage, d);
//             audiomgr.playsound(S_PAIN1+rnd(5), d);
        }
    }
}

void hitpush(int damage, playerent *d, playerent *at, vec &from, vec &to, int gun, bool gib, int info)
{
    vec v(to);
    v.sub(from);
    v.normalize();
    hit(damage, d, at, v, gun, gib, info);
}

float expdist(playerent *o, vec &dir, const vec &v)
{
    vec middle = o->o;
    middle.z += (o->aboveeye-o->eyeheight)/2;
    float dist = middle.dist(v, dir);
    dir.div(dist);
    if(dist<0) dist = 0;
    return dist;
}

void radialeffect(playerent *o, vec &v, int qdam, playerent *at, int gun)
{
    if(o->state!=CS_ALIVE) return;
    vec dir;
    float dist = expdist(o, dir, v);
    if(dist<EXPDAMRAD)
    {
        if(at == player1 && o != player1 && multiplayer(NULL)) accuracym[gun].hits += 1.0f-(float)dist/EXPDAMRAD;
        int damage = (int)(qdam*(1-dist/EXPDAMRAD));
        hit(damage, o, at, dir, gun, true, int(dist*DMF));
    }
}

vector<bounceent *> bounceents;

void removebounceents(playerent *owner)
{
    loopv(bounceents) if(bounceents[i]->owner==owner) { delete bounceents[i]; bounceents.remove(i--); }
}

void movebounceents()
{
    if(ispaused) return;
    loopv(bounceents) if(bounceents[i])
    {
        bounceent *p = bounceents[i];
        if((p->bouncetype==BT_NADE || p->bouncetype==BT_GIB) && p->applyphysics()) movebounceent(p, 1, false);
        if(!p->isalive(lastmillis))
        {
            p->destroy();
            delete p;
            bounceents.remove(i--);
        }
    }
}

void clearbounceents()
{
    if(gamespeed==100);
    else if(multiplayer(NULL)) bounceents.add((bounceent *)player1);
    loopv(bounceents) if(bounceents[i]) { delete bounceents[i]; bounceents.remove(i--); }
}

void renderbounceents()
{
    loopv(bounceents)
    {
        bounceent *p = bounceents[i];
        if(!p) continue;
        string model;
        vec o(p->o);

        int anim = ANIM_MAPMODEL, basetime = 0;
        switch(p->bouncetype)
        {
            case BT_NADE:
                copystring(model, "weapons/grenade/static");
                break;
            case BT_GIB:
            default:
            {
                uint n = (((4*(uint)(size_t)p)+(uint)p->timetolive)%3)+1;
                formatstring(model)("misc/gib0%u", n);
                int t = lastmillis-p->millis;
                if(t>p->timetolive-2000)
                {
                    anim = ANIM_DECAY;
                    basetime = p->millis+p->timetolive-2000;
                    t -= p->timetolive-2000;
                    o.z -= t*t/4000000000.0f*t;
                }
                break;
            }
        }
        rendermodel(model, anim|ANIM_LOOP|ANIM_DYNALLOC, 0, 1.1f, o, 0, p->yaw+90, p->pitch, 0, basetime);
    }
}

VARP(gib, 0, 1, 1);
VARP(gibnum, 0, 6, 1000);
VARP(gibttl, 0, 7000, 60000);
VARP(gibspeed, 1, 30, 100);

void addgib(playerent *d)
{
    if(!d || !gib || !gibttl) return;
    audiomgr.playsound(S_GIB, d);
    d->nocorpse = true; // don't render regular corpse: it was gibbed

    loopi(gibnum)
    {
        bounceent *p = bounceents.add(new bounceent);
        p->owner = d;
        p->millis = lastmillis;
        p->timetolive = gibttl+rnd(10)*100;
        p->bouncetype = BT_GIB;

        p->o = d->o;
        p->o.z -= d->aboveeye;
        p->inwater = waterlevel > p->o.z;

        p->yaw = (float)rnd(360);
        p->pitch = (float)rnd(360);

        p->maxspeed = 30.0f;
        p->rotspeed = 3.0f;

        const float angle = (float)rnd(360);
        const float speed = (float)gibspeed;

        p->vel.x = sinf(RAD*angle)*rnd(1000)/1000.0f;
        p->vel.y = cosf(RAD*angle)*rnd(1000)/1000.0f;
        p->vel.z = rnd(1000)/1000.0f;
        p->vel.mul(speed/100.0f);

        p->resetinterp();
    }
}

void shorten(const vec &from, vec &target, float distsquared)
{
    target.sub(from);
    float m = target.squaredlen();
    if(m < 0.07f) target.add(vec(0.1f, 0.1f, 0.1f));   // if "from == target" just fake a target to avoid zero-length fx vectors
    else target.mul(max(0.07f, sqrtf(distsquared / m)));
    target.add(from);
}

void raydamage(vec &from, vec &to, playerent *d)
{
    int dam = d->weaponsel->info.damage;
    int hitzone = -1;
    playerent *o = NULL;
    float distsquared, hitdistsquared = 0.0f;
    bool hit = false;
    int rayscount = 0, hitscount = 0;
    if(d->weaponsel->type==GUN_SHOTGUN)
    {
        playerent *hits[3*SGRAYS];
        loopk(3)
        loopi(SGRAYS)
        {
            rayscount++;
            int h = k*SGRAYS + i;
            if((hits[h] = intersectclosest(from, sgr[h].rv, d, distsquared, hitzone)))
                shorten(from, sgr[h].rv, (hitdistsquared = distsquared));
        }
        loopk(3)
        loopi(SGRAYS)
        {
            int h = k*SGRAYS + i;
            if(hits[h])
            {
                o = hits[h];
                hits[h] = NULL;
                int numhits_o, numhits_m, numhits_c;
                numhits_o = numhits_m = numhits_c = 0;
                switch(sgr[h].ds)
                {
                    case 0: numhits_o++; break;
                    case 1: numhits_m++; break;
                    case 2: numhits_c++; break;
                    default: break;
                }
                for(int j = i+1; j < 3*SGRAYS; j++) if(hits[j] == o)
                {
                    hits[j] = NULL;
                    switch(sgr[j].ds)
                    {
                        case 0: numhits_o++; break;
                        case 1: numhits_m++; break;
                        case 2: numhits_c++; break;
                        default: break;
                    }
                }
                int numhits = numhits_o + numhits_m + numhits_c;
                int dmgreal = 0;
                float dmg4r = 0.0f;
                bool withBONUS = false;
                if(SGDMGBONUS)
                {
                    float d2o = SGDMGDISTB;
                    if(o) d2o = vec(from).sub(o->o).magnitude();
                    if(d2o <= (SGDMGDISTB/10.0f) && numhits)
                    {
                        dmg4r += SGDMGBONUS;
                        withBONUS = true;
                    }
                }
                dmg4r += (SGCOdmg / 10.0f * SGDMGTOTAL / 100.0f) * numhits_o / 21.0f;
                dmg4r += (SGCMdmg / 10.0f * SGDMGTOTAL / 100.0f) * numhits_m / 21.0f;
                dmg4r += (SGCCdmg / 10.0f * SGDMGTOTAL / 100.0f) * numhits_c / 21.0f;
                dmgreal = (int) ceil(dmg4r);
                int info = (withBONUS ? SGDMGBONUS : 0) | (numhits_c << 8) | (numhits_m << 16) | (numhits_o << 24);
                if(numhits) hitpush(dmgreal, o, d, from, to, d->weaponsel->type, numhits == SGRAYS * 3, info);

                if(d == player1) hit = true;
                hitscount+=numhits;
            }
        }
        if(hitscount) shorten(from, to, hitdistsquared);
    }
    else if((o = intersectclosest(from, to, d, distsquared, hitzone)))
    {
        bool gib = false;
        switch(d->weaponsel->type)
        {
            case GUN_KNIFE: gib = true; break;
            case GUN_SNIPER: if(d==player1 && hitzone==2) { dam *= 3; gib = true; }; break;
            default: break;
        }
        bool info = gib;
        hitpush(dam, o, d, from, to, d->weaponsel->type, gib, info ? 1 : 0);
        if(d == player1) hit = true;
        shorten(from, to, distsquared);
        hitscount++;
    }

    if(d == player1 && multiplayer(NULL))
    {
        if(!rayscount) rayscount = 1;
        if(hit) accuracym[d->weaponsel->type].hits += (float)hitscount / rayscount;
        accuracym[d->weaponsel->type].shots++;
    }
}

const char *weapstr(int i) { return valid_weapon(i) ? tr(guns[i].title) : "x"; }

VARP(accuracy,0,0,1);

void r_accuracy(int h)
{
    int i = player1->weaponsel->type;
    if(accuracy && valid_weapon(i))
    {
        int x_offset = 2 * HUDPOS_X_BOTTOMLEFT, y_offset = 2 * (h - 1.75 * FONTH);
        string line;
        float acc = accuracym[i].shots ? 100.0 * accuracym[i].hits / (float)accuracym[i].shots : 0;
        if(i == GUN_GRENADE || i == GUN_SHOTGUN)
        {
            formatstring(line)("\f5%5.1f%% (%.1f/%d): \f0%s", acc, accuracym[i].hits, accuracym[i].shots, weapstr(i));
        }
        else
        {
            formatstring(line)("\f5%5.1f%% (%d/%d): \f0%s", acc, (int)accuracym[i].hits, accuracym[i].shots, weapstr(i));
        }
        blendbox(x_offset, y_offset + FONTH, x_offset + text_width(line) + 2 * FONTH, y_offset - FONTH, false, -1);
        draw_textf("%s", x_offset + FONTH, y_offset - 0.5 * FONTH, line);
    }
}

void accuracyinfo()
{
    vector <char*>lines;
    loopi(NUMGUNS) if(accuracym[i].shots)
    {
        float acc = 100.0 * accuracym[i].hits / (float)accuracym[i].shots;
        string line;
        if(i == GUN_GRENADE || i == GUN_SHOTGUN)
        {
            formatstring(line)("\f0%-10s\t\f5 %.1f%% (%.1f/%d)", weapstr(i), acc, accuracym[i].hits, accuracym[i].shots);
        }
        else
        {
            formatstring(line)("\f0%-10s\t\f5 %.1f%% (%d/%d)", weapstr(i), acc, (int)accuracym[i].hits, accuracym[i].shots);
        }
        lines.add(newstring(line));
    }
    loopv(lines) conoutf("%s", lines[i]);
    lines.deletearrays();
}

COMMAND(accuracyinfo, "");

void accuracyreset()
{
    loopi(NUMGUNS)
    {
        accuracym[i].hits=accuracym[i].shots=0;
    }
    conoutf("Your accuracy has been reset");
}
COMMAND(accuracyreset, "");
// weapon

weapon::weapon(class playerent *owner, int type) : type(type), owner(owner), info(guns[type]),
    ammo(owner->ammo[type]), mag(owner->mag[type]), gunwait(owner->gunwait[type]), shots(0),
    reloading(0), cs2fireinacc(0.0f), cs2lasttick(0), cs2epoch(-1),
    cs2recoilindex(0.0f), cs2lastshot(0), cs2recoiltick(0)
{
}

const int weapon::weaponchangetime = 400;
const float weapon::weaponbeloweye = 0.2f;

int weapon::flashtime() const { return max((int)info.attackdelay, 120)/4; }

void weapon::sendshoot(vec &from, vec &to, int millis)
{
    if(owner!=player1) return;
    addmsg(SV_SHOOT, "ri2i3iv", millis, owner->weaponsel->type,
           (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF),
           hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
    player1->pstatshots[player1->weaponsel->type]++; //NEW
}

bool weapon::modelattacking()
{
    int animtime = min(owner->gunwait[owner->weaponsel->type], (int)owner->weaponsel->info.attackdelay);
    if(lastmillis - owner->lastaction < animtime) return true;
    else return false;
}

void weapon::attacksound()
{
    if(info.sound == S_NULL) return;
    bool local = (owner == player1);
    audiomgr.playsound(info.sound, owner, local ? SP_HIGH : SP_NORMAL);
}

bool weapon::reload(bool autoreloaded)
{
    if(mag>=info.magsize || ammo<=0) return false;
    updatelastaction(owner);
    reloading = lastmillis;
    gunwait += info.reloadtime;

    int numbullets = min(info.magsize - mag, ammo);
    mag += numbullets;
    ammo -= numbullets;

    bool local = (player1 == owner);
    if(info.reload != S_NULL) audiomgr.playsound(info.reload, owner, local ? SP_HIGH : SP_NORMAL);
    if(local)
    {
        addmsg(SV_RELOAD, "ri2", lastmillis, owner->weaponsel->type);
        exechook(HOOK_SP, "onReload", "%d", (int)autoreloaded);
    }
    return true;
}

VARP(oldfashionedgunstats, 0, 0, 1);

void weapon::renderstats()
{
    string gunstats;
    if(oldfashionedgunstats) formatstring(gunstats)("%d/%d", mag, ammo); else formatstring(gunstats)("%d", mag);
    draw_text(gunstats, HUDPOS_WEAPON + HUDPOS_NUMBERSPACING, 823);
    if(!oldfashionedgunstats)
    {
        int offset = text_width(gunstats);
        glScalef(0.5f, 0.5f, 1.0f);
        formatstring(gunstats)("%d", ammo);
        draw_text(gunstats, (HUDPOS_WEAPON + HUDPOS_NUMBERSPACING + offset)*2, 826*2);
        glLoadIdentity();
    }
}


static int recoiltest = 0;//VAR(recoiltest, 0, 0, 1); // DISABLE ON RELEASE
static int recoilincrease = 2; //VAR(recoilincrease, 1, 2, 10);
static int recoilbase = 40;//VAR(recoilbase, 0, 40, 1000);
static int maxrecoil = 1000;//VAR(maxrecoil, 0, 1000, 1000);

// CS2-style recoil: the climb goes into the bullet pattern, the crosshair is not displaced and the
// shooter is not pushed backwards. Set sv_recoilaim/sv_recoilkick to 1 to get the original AC feel.
FVARP(sv_punchscale, 0, 0.6f, 10);  // bullet climb in degrees per unit of the gun's recoil value
FVARP(sv_punchmax, 0, 45, 90);      // backstop on the accumulated climb (degrees); the CS2 model
                                    // bounds itself by decay, and the hardest kick of the nine
                                    // (the shotgun) peaks near 19, so this should never bite
VARP(sv_recoilaim, 0, 0, 1);        // 1 = recoil displaces the aim (classic AC); 0 = bullets only (CS2)
VARP(sv_recoilkick, 0, 0, 1);       // 1 = firing pushes the shooter backwards (classic AC); 0 = off (CS2)

// movement inaccuracy, in the same spread units as guninfo::spread (placeholder magnitudes)
FVARP(sv_moveinacc, 0, 400, 2000);  // extra spread at full run speed (magnitude found empirically in play)
FVARP(sv_airinacc, 0, 600, 2000);   // extra spread while airborne (scaled from moveinacc, untested)
FVARP(sv_crouchacc, 0, 0.5f, 1.0f); // spread multiplier while crouched (1.0 = no effect)
FVARP(sv_firstshotfrac, 0, 0.0f, 1.0f); // burst spread on the first shot of a burst (0 = pinpoint)

// Per-weapon constants extracted from a Counter-Strike 2 install. Field order and
// meaning are documented in cs2weapons.h; the values are CS2's own units, so they
// can be diffed against the data package directly. The [2] pairs are CS2's
// CFiringModeFloat = { primary, alternate (scoped/burst) }.
const cs2guninfo cs2guns[NUMGUNS] =
{
//   spread            inaccStand        inaccCrouch       inaccMove         inaccFire
//   inaccJump         jumpInit  apex   inaccLand         recStand  recCrouch recSFinal recCFinal trans
//   recoilMag         magVar    angleVar         seed    auto speed        bullets

    // GUN_KNIFE - "melee". No spread or recoil at all; the entry exists for its maxspeed.
    { {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, 0, 0, {0,0},
      1, 1, -1, -1, 0, 0, {0,0}, {0,0}, {0,0}, 26701, false, {250,250}, 1 },

    // GUN_PISTOL - weapon_usp_silencer_prefab. Pistols punish spamming hardest:
    // inaccFire 0.071 is the largest per-shot jump of any weapon here.
    { {0.0025,0.0015}, {0.0049,0.0049}, {0.00368,0.00368}, {0.01387,0.01387}, {0.071,0.052},
      {0.09448,0.09448}, 0.0966, 0, {0.000191,0.000198},
      0.349532, 0.291277, 0.349532, 0.291277, 3, 10, {29,23}, {0,0}, {0,0}, 5426, false, {240,240}, 1 },

    // GUN_CARBINE - weapon_famas_prefab (light 220 u/s rifle tier; galilar is the 215 tier
    // already covered 1:1 by GUN_ASSAULT as the AK).
    { {0.0006,0.0006}, {0.00759,0.00369}, {0.0055,0.00325}, {0.09934,0.09934}, {0.00605,0.00335},
      {0.11039,0.11039}, 0.09477, 0, {0.000205,0.000205},
      0.25, 0.12, 0.5, 0.48, 2, 5, {20,20}, {1,1}, {60,50}, 39623, true, {220,220}, 1 },

    // GUN_SHOTGUN - weapon_nova_prefab. m_flSpread 0.04 is the big cone CS2 gives shotguns;
    // 9 pellets, but AC keeps its own createrays() pellet model on top of this.
    { {0.04,0.04}, {0.007,0.007}, {0.00525,0.00525}, {0.03675,0.03675}, {0.00972,0.00972},
      {0.12631,0.12631}, 0.1097, 0, {0.000236,0.000236},
      0.460517, 0.328941, 0.460517, 0.328941, 2, 5, {143,143}, {22,22}, {20,20}, 7763, false, {220,220}, 9 },

    // GUN_SUBGUN - weapon_mp9_prefab
    { {0.0006,0.0006}, {0.009,0.009}, {0.008,0.008}, {0.02904,0.02904}, {0.0037,0.0037},
      {0.05,0.05}, 0.03728, 0, {5.6e-05,5.6e-05},
      0.25789, 0.184207, 0.25789, 0.184207, 2, 5, {21,21}, {1,1}, {70,70}, 50729, true, {240,240}, 1 },

    // GUN_SNIPER - weapon_awp_prefab. Mode 0 is hipfire (stand inaccuracy 0.0808: useless),
    // mode 1 is scoped (0.002: pinpoint), and scoping halves m_flMaxSpeed 200 -> 100.
    { {0.0002,0.0002}, {0.0808,0.002}, {0.0606,0.0015}, {0.17648,0.17648}, {0.05385,0.05385},
      {0.13383,0.13383}, 0.17286, 0, {0.000307,0.0001},
      0.34539, 0.24671, 0.34539, 0.24671, 2, 5, {78,25}, {15,2}, {20,20}, 4100, false, {200,100}, 1 },

    // GUN_ASSAULT - weapon_ak47_prefab. The reference weapon: recovery ramps from 0.368 s to
    // 0.506 s between shots 2 and 5, which is what makes a sustained AK spray stay inaccurate.
    { {0.0006,0.0006}, {0.00641,0.00641}, {0.00481,0.00481}, {0.17506,0.17506}, {0.0078,0.0078},
      {0.14076,0.14076}, 0.10094, 0, {0.000242,0.000242},
      0.368, 0.305257, 0.506, 0.419728, 2, 5, {30,30}, {0,0}, {70,70}, 223, true, {215,215}, 1 },

    // GUN_GRENADE - weapon_hegrenade_prefab. Like the knife: carried for its maxspeed.
    { {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, 0, 0, {0,0},
      1, 1, -1, -1, 0, 0, {0,0}, {0,0}, {0,0}, 52132, false, {245,245}, 1 },

    // GUN_AKIMBO - weapon_elite_prefab
    { {0.002,0.002}, {0.007,0.01}, {0.00525,0.0075}, {0.01785,0.01785}, {0.01116,0.01196},
      {0.15842,0.15842}, 0.09586, 0, {0.000255,0.000255},
      0.524989, 0.437491, 0.524989, 0.437491, 3, 10, {27,27}, {4,4}, {20,20}, 24563, false, {240,240}, 1 },
};

// --- per-weapon movement speed ----------------------------------------------------
// CS2 gives every weapon its own absolute top speed (m_flMaxSpeed, Source units/s) and
// its sv_accelerate_use_weapon_speed - measured "true" in the reference install - aims
// the accelerator at that instead of one shared constant. AC has a single player value
// (entity.h sets maxspeed = 16.0f), so the equivalent is to write pl->maxspeed every
// tick from the weapon in hand. The anchor is the same one the movement model uses:
// 250 u/s <-> 16 cubes/s (SVSCALE).
VARP(sv_cs2weapons, 0, 1, 1);            // master switch: 0 = classic AC weapon behaviour
VARP(sv_weaponspeed, 0, 1, 1);           // per-weapon top speed
FVARP(sv_speedscale, 0.0f, 1.0f, 2.0f);  // trim on the whole conversion, for calibration

static const float ACMAXSPEED = 16.0f;   // the player value from entity.h, for the revert path

float weapon::cs2maxspeed() const
{
    return svunits(cs2guns[type].maxspeed[cs2firemode()]) * sv_speedscale;
}

// --- CS2 accuracy model -----------------------------------------------------------
// Ported from the CS:GO logic (CWeaponCSBase::GetInaccuracy / UpdateAccuracyPenalty /
// GetRecoveryTime), which the CS2 weapon data still speaks - see cs2weapons.h.
//
// cs2fireinacc holds CS2's m_fAccuracyPenalty, in CS2 units: each shot adds the weapon's
// InaccuracyFire and it decays back towards the current stance's base over the weapon's
// recovery time. The movement penalty is deliberately NOT part of that accumulator -
// CS2 computes it fresh on every shot and it never persists, which is exactly why a
// counter-strafe snaps your accuracy back with no ramp.
VARP(sv_weaponaccuracy, 0, 1, 1);                   // 0 = the classic AC spread model
FVARP(sv_accuracyfactor, 0.0f, 1000.0f, 4000.0f);   // CS2 spread units -> AC spread units (derived)
FVARP(sv_jumpimpulse, 1.0f, 301.993377f, 1000.0f);  // CS2's sv_jump_impulse; scales the airborne curve
FVARP(sv_airspreadscale, 0.0f, 1.0f, 1.0f);         // CS2's weapon_air_spread_scale

// Movement curve constants, read from the CS:GO source rather than fitted:
// cs_shareddefs.cpp's CS_PLAYER_SPEED_DUCK_MODIFIER and weapon_csbase.cpp's
// MOVEMENT_CURVE01_EXPONENT. Below the crouch speed there is no penalty at all, it maxes
// out at 95% of run speed (so jitter at the top doesn't make it flicker), and the result
// is raised to a power that turns the low end into something close to a hard floor.
static const float CS2_SPEED_DUCK_MODIFIER = 0.34f;
static const float CS2_MOVEMENT_CURVE_EXPONENT = 0.25f;

static inline float cs2remap(float v, float lo, float hi, float dlo, float dhi)
{
    if(hi <= lo) return v < lo ? dlo : dhi;
    return dlo + clamp((v - lo)/(hi - lo), 0.0f, 1.0f)*(dhi - dlo);
}

// What this weapon recovers back to for a given stance (CS2's UpdateAccuracyPenalty floor).
// Airborne it adds the flat InaccuracyJump on top of the standing value; the velocity-
// dependent part of the air penalty is separate and applies per shot.
static float cs2basepenalty(const cs2guninfo &c, int mode, bool onfloor, bool crouching)
{
    if(!onfloor) return c.inaccStand[mode] + c.inaccJump[mode]*sv_airspreadscale;
    return crouching ? c.inaccCrouch[mode] : c.inaccStand[mode];
}

// CS2's GetRecoveryTime, in seconds. A sustained spray recovers more slowly: the time ramps
// from RecoveryTime* to RecoveryTime*Final between two bullet counts of the burst. Being
// airborne costs a flat 4x. CS2 ramps off a per-shot index that decays over time; AC's
// per-burst "shots" counter is the closest equivalent (it resets when the trigger is let go).
static float cs2recoverytime(const cs2guninfo &c, int mode, bool onfloor, bool crouching, int shots)
{
    if(!onfloor) return c.recoveryCrouch*4.0f;
    float first = crouching ? c.recoveryCrouch : c.recoveryStand;
    float final = crouching ? c.recoveryCrouchFinal : c.recoveryStandFinal;
    if(final < 0.0f || c.recoveryEndBullet <= c.recoveryStartBullet) return first; // no ramp defined
    return first + clamp((shots - c.recoveryStartBullet)/(float)(c.recoveryEndBullet - c.recoveryStartBullet), 0.0f, 1.0f)*(final - first);
}

// One step of CS2's decay: "90% of the accumulated penalty is gone after one recovery time".
// The value is never allowed to fall below the stance's base, so leaving the ground snaps it
// *up* immediately; dtms == 0 is a no-op, which makes this safe to call more than once a tick.
static float cs2decaystep(float inacc, const cs2guninfo &c, int mode, bool onfloor, bool crouching, int shots, int dtms)
{
    float base = cs2basepenalty(c, mode, onfloor, crouching);
    if(inacc <= base) return base;
    if(dtms <= 0) return inacc;
    float rt = cs2recoverytime(c, mode, onfloor, crouching, shots);
    if(rt <= 0.0f) return base;
    return base + (inacc - base)*expf(-(logf(10.0f)/rt)*(dtms/1000.0f));
}

// --- CS2 recoil model --------------------------------------------------------------
// The other half of the weapon model: CWeaponCSBase::Recoil, CCSPlayer::KickBack and
// CCSGameMovement::DecayAimPunchAngle, again read from the CS:GO logic that the CS2
// weapon data still speaks. It replaces "the recoil kicks the aim by a formula of the
// shot count" with the two-part model CS2 actually runs:
//
//   1. The pattern is generated, not authored. WeaponRecoilData::GenerateRecoilTable
//      seeds a Numerical-Recipes ran1 stream with the weapon's m_nRecoilSeed and draws
//      64 (angle, magnitude) pairs; full-auto weapons ease each draw into the previous
//      one by weapon_recoil_variance and scale the first few down by weapon_recoil_
//      suppression_*. That easing is the whole trick - independent random kicks would be
//      unlearnable, a smoothed random walk is a pattern you can memorise. It is also why
//      the AK, whose magnitude variance is 0, climbs steadily and only swings sideways.
//   2. Recoil() hands the pair to KickBack, which adds it to an angular *velocity*
//      rather than to a displacement. A burst therefore ramps up, keeps climbing past
//      its last shot, and is bled off by DecayAimPunchAngle once you let go. The bullet
//      is fired before Recoil() is called, so a shot is never bent by its own kick -
//      which is what makes the first shot of a burst leave exactly along the aim.
//
// Both halves of the pair are in degrees and degrees/second, and what accumulates is a
// raw punch angle with no scale of its own: weapon_recoil_scale (sv_recoilscale) is
// applied when the punch is turned into a bullet direction, the same place CS2 applies
// it. See also cs2weapons.h for the table these constants come from.
VARP(sv_recoil, 0, 1, 1);                    // 0 = the classic AC recoil formula
FVARP(sv_recoilscale, 0.0f, 2.0f, 10.0f);    // CS2's weapon_recoil_scale
FVARP(sv_recoilvariance, 0.0f, 0.55f, 1.0f); // weapon_recoil_variance (pattern easing)
VARP(sv_recoilsuppressshots, 0, 4, 16);      // weapon_recoil_suppression_shots
FVARP(sv_recoilsuppressfactor, 0.0f, 0.75f, 1.0f); // weapon_recoil_suppression_factor
FVARP(sv_recoilveldecay, 0.0f, 4.5f, 20.0f); // weapon_recoil_vel_decay (punch velocity)
FVARP(sv_recoildecayexp, 0.0f, 8.0f, 40.0f); // weapon_recoil_decay2_exp  (punch)
FVARP(sv_recoildecaylin, 0.0f, 18.0f, 60.0f);// weapon_recoil_decay2_lin  (punch)
FVARP(sv_recoilindexdecay, 0.0f, 2.0f, 20.0f); // weapon_recoil_decay_coefficient

// The bullets are only half of what CS2 shows you when you hold the trigger - the camera moves
// too, and that is most of what makes a spray read as a spray. Two separate terms add up to it
// (CBasePlayer::CalcPlayerView, baseplayer_shared.cpp:2076-2079, and the tail of
// CCSPlayer::KickBack):
//   - the view tracks the aim punch by `view_recoil_tracking`, so the whole screen walks upwards
//     while the crosshair stays nailed to the middle of it;
//   - every shot also adds a short punch of its own (m_viewPunchAngle) of
//     magnitude * weapon_recoil_view_punch_extra. CS2's own comment calls it "additional punch to
//     the view (screen shake) to make the kick back a bit more visceral". It decays on its own
//     clock and never touches where the bullet goes.
// Both are camera-only. The bullet direction keeps using the aim punch alone - if the shake bent
// the bullets too, the pattern would stop being reproducible and stop being learnable.
FVARP(sv_viewrecoiltracking, 0.0f, 0.45f, 4.0f); // view_recoil_tracking
FVARP(sv_viewpunchextra, 0.0f, 0.055f, 1.0f);    // weapon_recoil_view_punch_extra
FVARP(sv_viewpunchdecay, 0.0f, 18.0f, 60.0f);    // view_punch_decay (CS2 passes 0 as the linear term)

// m_viewPunchAngle. Only ever the local player's: nothing else renders it, and keeping it out of
// physent avoids a PCH change for a purely visual value. Degrees, AC's convention, same axes as
// punchpitch/punchyaw.
static float cs2viewpunchpitch = 0.0f, cs2viewpunchyaw = 0.0f;

// The extra rotation the camera should be rendered at this frame, in degrees. rendergl.cpp calls
// this once per frame and adds both to camera1's angles.
void cs2viewpunch(float &yawoffset, float &pitchoffset)
{
    if(!sv_cs2weapons || !sv_recoil || !player1)
    {
        // Clear the state rather than only masking it. cs2recuiltick is what ages the shake and it
        // only runs while sv_recoil is on, so a value left over from just before the switch would
        // otherwise reappear intact the moment the switch went back on, mid-spray.
        cs2viewpunchpitch = cs2viewpunchyaw = 0.0f;
        yawoffset = pitchoffset = 0.0f;
        return;
    }
    yawoffset = cs2viewpunchyaw + player1->punchyaw*sv_viewrecoiltracking;
    pitchoffset = cs2viewpunchpitch + player1->punchpitch*sv_viewrecoiltracking;
}

// CS2's RNG, and it has to be CS2's exactly: the pattern is nothing but the sequence
// this produces. It is Numerical Recipes' ran1 - a Park-Miller generator with the
// Bays-Durham shuffle on top, which is why the values are not merely "some random
// numbers" but a specific, reproducible walk.
static const int CS2R_IA = 16807, CS2R_IM = 2147483647, CS2R_IQ = 127773, CS2R_IR = 2836;
static const int CS2R_NTAB = 32;
static const int CS2R_NDIV = 1 + (CS2R_IM - 1)/CS2R_NTAB;
// AM is CS:GO's `1.0/IM` - a double division, so it is written as one here rather than
// letting the int widen to a float and landing on the neighbouring 2^-31.
static const float CS2R_AM = (float)(1.0/2147483647.0), CS2R_RNMX = 1.0f - 1.2e-7f;

struct cs2random
{
    int idum, iy, iv[32];
    void setseed(int s) { idum = s < 0 ? s : -s; iy = 0; }
    int generate()
    {
        int j, k;
        if(idum <= 0 || !iy)
        {   // first call (or a zero seed): scramble the shuffle table from the seed
            idum = -idum < 1 ? 1 : -idum;
            for(j = CS2R_NTAB + 7; j >= 0; --j)
            {
                k = idum/CS2R_IQ;
                idum = CS2R_IA*(idum - k*CS2R_IQ) - CS2R_IR*k;
                if(idum < 0) idum += CS2R_IM;
                if(j < CS2R_NTAB) iv[j] = idum;
            }
            iy = iv[0];
        }
        k = idum/CS2R_IQ;
        idum = CS2R_IA*(idum - k*CS2R_IQ) - CS2R_IR*k;
        if(idum < 0) idum += CS2R_IM;
        j = iy/CS2R_NDIV;
        iy = iv[j];
        iv[j] = idum;
        return iy;
    }
    float range(float lo, float hi)
    {
        float f = CS2R_AM*generate();
        if(f > CS2R_RNMX) f = CS2R_RNMX;
        return f*(hi - lo) + lo;
    }
};

// The pattern the generator fills: one row per shot before it wraps, in CS2.
enum { CS2_RECOIL_SHOTS = 64 };
struct cs2recoiloffset { float angle, magnitude; };
struct cs2recoilpattern { cs2recoiloffset off[CS2_RECOIL_SHOTS]; };

static cs2recoilpattern cs2recoiltable[NUMGUNS][2];
static bool cs2recoiltablebuilt = false;
static float cs2recoiltablevariance = -1.0f, cs2recoiltablesuppressfactor = -1.0f;
static int cs2recoiltablesuppressshots = -1;

static void cs2buildrecoiltable()
{
    cs2recoiltablebuilt = true;
    cs2recoiltablevariance = sv_recoilvariance;
    cs2recoiltablesuppressshots = sv_recoilsuppressshots;
    cs2recoiltablesuppressfactor = sv_recoilsuppressfactor;
    for(int g = 0; g < NUMGUNS; ++g)
    {
        const cs2guninfo &c = cs2guns[g];
        for(int mode = 0; mode < 2; ++mode)
        {
            cs2random r;
            r.setseed(c.recoilSeed);
            float angle = 0.0f, magnitude = 0.0f;
            for(int j = 0; j < CS2_RECOIL_SHOTS; ++j)
            {
                // m_flRecoilAngle is 0 on every weapon mapped here, so the angle the
                // pattern wobbles around is the generator's own default of 0.
                // Both draws happen on every row even when a variance is zero, because
                // the stream has to advance in step with CS2's or the walk diverges.
                float na = r.range(-c.recoilAngleVar[mode], c.recoilAngleVar[mode]);
                float nm = c.recoilMag[mode] + r.range(-c.recoilMagVar[mode], c.recoilMagVar[mode]);
                if(c.fullAuto && j > 0)
                {   // Lerp(variance, prev, new): a smoothed walk, not independent kicks
                    angle += (na - angle)*cs2recoiltablevariance;
                    magnitude += (nm - magnitude)*cs2recoiltablevariance;
                }
                else { angle = na; magnitude = nm; }
                if(c.fullAuto && j < cs2recoiltablesuppressshots)
                    magnitude *= cs2recoiltablesuppressfactor
                               + (1.0f - cs2recoiltablesuppressfactor)*(j/(float)cs2recoiltablesuppressshots);
                cs2recoiltable[g][mode].off[j].angle = angle;
                cs2recoiltable[g][mode].off[j].magnitude = magnitude;
            }
        }
    }
}

// The three constants above are read at generation time, so a pattern outlives a cvar
// change unless it is rebuilt - cheap enough to just check every shot.
static const cs2recoilpattern &cs2recoilpatternfor(int gun, int mode)
{
    if(!cs2recoiltablebuilt
       || cs2recoiltablevariance != sv_recoilvariance
       || cs2recoiltablesuppressshots != sv_recoilsuppressshots
       || cs2recoiltablesuppressfactor != sv_recoilsuppressfactor) cs2buildrecoiltable();
    return cs2recoiltable[gun][mode];
}

// Recoil(): pick this shot's row and add it to the punch velocity.
static void cs2recoilkick(weapon *w, playerent *p)
{
    const cs2guninfo &c = cs2guns[w->type];
    // Full auto walks the table by shot number, which is what makes the pattern
    // repeatable. Everything else takes a pseudo-random row, which is what CS2 gets from
    // GetPredictionRandomSeed(): the rows are already an i.i.d. sample of the same
    // distribution, so a random index and a random seed come to the same thing.
    int index = c.fullAuto ? (int)w->cs2recoilindex : (int)rndscale((float)CS2_RECOIL_SHOTS);
    index %= CS2_RECOIL_SHOTS;
    if(index < 0) index += CS2_RECOIL_SHOTS;
    const cs2recoiloffset &o = cs2recoilpatternfor(w->type, w->cs2firemode()).off[index];
    float rad = o.angle*RAD;
    // KickBack() sets an angular velocity in degrees per second. Both engines count yaw
    // counter-clockwise seen from above, but they do not share a world: Source's is right-handed,
    // AC's is its mirror image (transplayer: "move from RH to Z-up LH quake style worldspace",
    // glScalef(1, -1, 1)). The same positive yaw offset therefore turns the shot to opposite sides
    // of the crosshair, so the horizontal term is negated here - drop that sign and the whole
    // pattern comes out flipped left-for-right. Pitch needs no handedness correction; its sign
    // difference is the stored-angle convention (Source's pitch grows downwards, AC's upwards),
    // which is the flip the cos term carries.
    p->punchpitchvel += cosf(rad)*o.magnitude;
    p->punchyawvel += sinf(rad)*o.magnitude;
    // ...and the same kick, scaled down, straight onto the camera. Bots share this code, so the
    // screen shake is the local player's only.
    if(p == player1)
    {
        cs2viewpunchpitch += cosf(rad)*o.magnitude*sv_viewpunchextra;
        cs2viewpunchyaw += sinf(rad)*o.magnitude*sv_viewpunchextra;
    }
    w->cs2recoilindex += 1.0f;
    w->cs2lastshot = lastmillis;
}

// DecayAimPunchAngle, once per engine tick. The punch sheds an exponential and a linear
// term, is carried along by the velocity, and the velocity decays in turn. The two
// half-steps around the velocity update are CS2's (an implicit midpoint step, so the
// punch and the velocity meet in the middle of the tick), and the linear term is what
// guarantees the punch actually reaches zero instead of only approaching it.
static void cs2recuiltick(playerent *p, weapon *w, int dtms)
{
    if(dtms <= 0) return;
    float dt = dtms/1000.0f, decay = expf(-sv_recoildecayexp*dt);
    // m_viewPunchAngle is a plain decaying offset rather than a velocity (DecayAngles with a zero
    // linear term), and it belongs to the local player alone, so it ages exactly once per frame.
    if(p == player1)
    {
        float vpdecay = expf(-sv_viewpunchdecay*dt);
        cs2viewpunchpitch *= vpdecay;
        cs2viewpunchyaw *= vpdecay;
    }
    float pitch = p->punchpitch*decay, yaw = p->punchyaw*decay;
    float lin = sv_recoildecaylin*dt, mag = sqrtf(pitch*pitch + yaw*yaw);
    if(mag > lin) { float k = 1.0f - lin/mag; pitch *= k; yaw *= k; }
    else pitch = yaw = 0.0f;
    pitch += p->punchpitchvel*dt*0.5f;
    yaw += p->punchyawvel*dt*0.5f;
    float veldecay = expf(-sv_recoilveldecay*dt);
    p->punchpitchvel *= veldecay;
    p->punchyawvel *= veldecay;
    // sv_punchmax is only a backstop now: the decay bounds the punch on its own (the
    // hardest kick of the nine, the shotgun, peaks near 19 degrees).
    p->punchpitch = clamp(pitch + p->punchpitchvel*dt*0.5f, -sv_punchmax, sv_punchmax);
    p->punchyaw = clamp(yaw + p->punchyawvel*dt*0.5f, -sv_punchmax, sv_punchmax);
    // m_flRecoilIndex walks the pattern, so it has to stop walking when the burst does.
    // CS2 decays it only once more than a cycle time has passed since the last shot,
    // which is precisely the "trigger is not down" case here - while a full-auto weapon
    // is firing, the shot-to-shot gap is the cycle time and the index never decays.
    // CS2's slack over the cycle time is 10%, which is exactly one 64 Hz tick. AC counts gunwait
    // down in frame milliseconds, so the interval a full-auto weapon actually manages is the cycle
    // time rounded *up* to a frame boundary - up to one whole frame longer than the cycle itself.
    // Without that frame in the slack, unlucky frame rates would fail the test on every shot (a
    // 16 ms frame gives the 100 ms assault rifle 112 ms between shots) and the index would decay
    // as fast as it walked, stalling the pattern after a dozen rounds instead of walking it out.
    if(lastmillis - w->cs2lastshot > (int)(guns[w->type].attackdelay*1.10f) + dtms)
        w->cs2recoilindex *= expf(-logf(10.0f)*sv_recoilindexdecay*dt);
}

static void cs2resetweapon(weapon *w, playerent *p)
{
    w->cs2fireinacc = 0.0f;
    w->cs2lasttick = lastmillis;
    w->cs2epoch = p ? p->lifesequence : -1;
    w->cs2recoilindex = 0.0f;
    w->cs2lastshot = w->cs2recoiltick = lastmillis;
    // The punch is player state and would decay on its own within a quarter second, but
    // clearing it here is what keeps a weapon switch or a respawn from carrying a
    // half-finished spray into the next one.
    if(p) p->punchpitch = p->punchyaw = p->punchpitchvel = p->punchyawvel = 0.0f;
    if(p == player1) cs2viewpunchpitch = cs2viewpunchyaw = 0.0f;
}

static void cs2tick(weapon *w, playerent *p, int now)
{
    int dtms = now - w->cs2lasttick;
    if(dtms <= 0) return;
    w->cs2lasttick = now;
    w->cs2fireinacc = cs2decaystep(w->cs2fireinacc, cs2guns[w->type], w->cs2firemode(),
                                   p->onfloor, p->crouching, w->shots, dtms);
}

// Per-tick upkeep for one player's held weapon: age the accuracy penalty and pick the
// movement speed. maxspeed is persistent state on the entity rather than something physics
// recomputes, so writing it every tick is also what restores 16.0 the moment sv_weaponspeed
// is switched off.
static void cs2playerupdate(playerent *p)
{
    if(!p || !p->weaponsel) return;
    weapon *w = p->weaponsel;
    // The engine only resets the weapon actually in hand on death/respawn, so one that was
    // put away mid-burst would keep a stale accuracy penalty - the life stamp catches that.
    if(p->state != CS_ALIVE || w->cs2epoch != p->lifesequence) cs2resetweapon(w, p);
    else if(sv_cs2weapons)
    {
        if(sv_weaponaccuracy) cs2tick(w, p, lastmillis);
        // The punch runs on its own clock rather than sharing cs2tick's, so either half of
        // the model can be switched off without stalling the other one's dt.
        if(sv_recoil) cs2recuiltick(p, w, lastmillis - w->cs2recoiltick);
    }
    w->cs2recoiltick = lastmillis; // stamped even while the recoil is off, so re-enabling it
                                   // does not integrate one enormous step
    p->maxspeed = p->state == CS_ALIVE && sv_cs2weapons && sv_weaponspeed
                ? w->cs2maxspeed() : ACMAXSPEED;
}

// Once per tick, from updateworld(). Bots are included or a player slowed down by an AWP
// would be racing bots that never slow down.
void cs2weaponupdate()
{
    cs2playerupdate(player1);
    if(m_botmode) loopv(bots) cs2playerupdate(bots[i]);
}

// Direction vector for a view angle, using the same convention as the projectile code below
// (weapon.cpp: x = sin(yaw)*cos(pitch), y = -cos(yaw)*cos(pitch), z = sin(pitch)).
static inline vec viewdir(float yaw, float pitch)
{
    float cp = cosf(RAD*pitch);
    return vec(sinf(RAD*yaw)*cp, -cosf(RAD*yaw)*cp, sinf(RAD*pitch));
}

void weapon::attackphysics(vec &from, vec &to) // physical fx to the owner
{
    const guninfo &g = info;
    vec unitv;
    float dist = to.dist(from, unitv);   // NB: unitv is to-from and is NOT normalised (|unitv| == dist)
    vec aimdir(unitv);
    if(dist > 0.0001f) aimdir.div(dist); // proper unit vector along the shot
    float f = dist/1000;
    // Two spread models behind one switch: CS2's per-weapon one, or AC's global
    // burst/movement/crouch constants exactly as they were.
    float spread;
    if(sv_cs2weapons && sv_weaponaccuracy)
    {
        const cs2guninfo &c = cs2guns[type];
        int mode = cs2firemode();
        cs2tick(this, owner, lastmillis); // lazy: stays correct even if the tick hook hasn't run
        // The stance's base is a floor, so the very first shot after a switch or a respawn is
        // still only as accurate as CS2's standing value rather than being free.
        float inacc = max(cs2fireinacc, cs2basepenalty(c, mode, owner->onfloor, owner->crouching));
        // Movement is instantaneous and costs nothing below the crouch speed. owner->vel is
        // normalised, so its xy magnitude is already the fraction of this weapon's top speed -
        // the very quantity CS2 remaps, and equal to it because m_flMaxSpeed is what drives
        // pl->maxspeed in cs2playerspeed().
        float move = cs2remap(owner->vel.magnitudexy(), CS2_SPEED_DUCK_MODIFIER, 0.95f, 0.0f, 1.0f);
        if(move > 0.0f) inacc += powf(move, CS2_MOVEMENT_CURVE_EXPONENT)*c.inaccMove[mode];
        // Airborne: the flat part of the penalty is already in cs2fireinacc (see
        // cs2basepenalty); this adds the velocity curve, full strength just after takeoff and
        // falling away towards the apex. AC has no falling term - its vel.z is a damped
        // impulse that never goes negative - so the descent is an approximation.
        if(!owner->onfloor)
        {
            float peak = c.inaccJumpInitial*sv_airspreadscale;
            float sqmax = sqrtf(sv_jumpimpulse);
            float vspeed = acunits(fabsf(owner->vel.z)*owner->maxspeed); // Source units/second
            inacc += clamp(cs2remap(sqrtf(vspeed), sqmax*0.25f, sqmax, 0.0f, peak), 0.0f, 2.0f*peak);
        }
        inacc = min(inacc, 1.0f);                     // CS2 clamps the total at 1.0
        spread = (inacc + c.spread[mode])*sv_accuracyfactor;
        cs2fireinacc += c.inaccFire[mode];            // this shot's penalty lands on the next one
    }
    else
    {
        float basespread = dynspread();
        if(shots <= 1) basespread *= sv_firstshotfrac;
        int classic = (int)(basespread + sv_moveinacc*owner->vel.magnitudexy()
                            + (owner->onfloor ? 0.0f : sv_airinacc));
        if(owner->crouching) classic = (int)(classic*sv_crouchacc);
        spread = classic;
    }
    float recoil = dynrecoil()*-0.01f;

    // spread: a random point in a disc perpendicular to the shot, uniform by area. (The old code
    // picked an axis-aligned cube, which favoured the diagonal corners. Max radius is unchanged.)
    if(spread>1)
    {
        float maxr = spread*0.5f*f;
        float ang = rndscale(360.0f)*RAD;
        float rad = maxr*sqrtf(rndscale(1.0f));   // sqrt keeps the density uniform over the disc
        vec right, up;
        up.orthogonal(aimdir); up.normalize();
        right.cross(aimdir, up); right.normalize();
        to.add(vec(right).mul(cosf(ang)*rad));
        to.add(vec(up).mul(sinf(ang)*rad));
    }
    // kickback & recoil
    float kick;
    if(recoiltest)
        kick = min(powf(shots/(float)(recoilincrease), 2.0f)+(float)(recoilbase)/10.0f, (float)(maxrecoil)/10.0f);
    else
        kick = min(powf(shots/(float)(g.recoilincrease), 2.0f)+(float)(g.recoilbase)/10.0f, (float)(g.maxrecoil)/10.0f);

    if(sv_recoilkick)
        owner->vel.add(vec(unitv).mul(recoil/dist).mul(owner->crouching ? 0.75 : 1.0f));

    if(sv_cs2weapons && sv_recoil)
    { // CS2: the recoil climbs through the bullet pattern while the crosshair stays where
      // you aim. The bullet is fired before the kick is applied (Recoil() runs after
      // FireBullets), so this shot is bent by what the earlier shots built up and never by
      // its own kick - which is what makes the first shot of a burst land on the aim.
        cs2recuiltick(owner, this, lastmillis - cs2recoiltick); // lazy, exactly like cs2tick above
        cs2recoiltick = lastmillis;
        float bpitch = owner->punchpitch*sv_recoilscale, byaw = owner->punchyaw*sv_recoilscale;
        if(bpitch != 0.0f || byaw != 0.0f)
        {
            vec bend = viewdir(owner->yaw + byaw, owner->pitch + bpitch);
            bend.sub(viewdir(owner->yaw, owner->pitch));   // small-angle offset from the aim direction
            vec dir = aimdir;
            dir.add(bend);
            dir.normalize();
            // add only the bend: assigning "to = from + dir*dist" would wipe the spread offset
            // that was applied to "to" just above.
            to.add(vec(dir).sub(aimdir).mul(dist));
        }
        cs2recoilkick(this, owner);
    }
    else if(sv_recoilaim)
        owner->pitchvel = kick;                 // classic AC: the recoil displaces the aim
    else
    { // The interim CS2-ish model from before the data arrived: a kick of the shot count
      // accumulated straight into the punch. Kept as the behaviour of sv_cs2weapons 0.
      // shots resets when the trigger is released, so shots<=1 marks the start of a new burst.
        if(shots <= 1) owner->punchpitch = owner->punchyaw = 0.0f;
        // Bend this bullet by the recoil accumulated from the PREVIOUS shots, then add this shot's
        // own kick. That way the first shot of a burst leaves the barrel exactly where the crosshair
        // points (CS2 behaviour) instead of being pushed upwards by its own recoil.
        if(owner->punchpitch > 0.0f)
        {
            vec bend = viewdir(owner->yaw + owner->punchyaw, owner->pitch + owner->punchpitch);
            bend.sub(viewdir(owner->yaw, owner->pitch));   // small-angle offset from the aim direction
            vec dir = aimdir;
            dir.add(bend);
            dir.normalize();
            // add only the bend: assigning "to = from + dir*dist" would wipe the spread offset
            // that was applied to "to" just above.
            to.add(vec(dir).sub(aimdir).mul(dist));
        }
        owner->punchpitch = min(owner->punchpitch + kick*sv_punchscale, sv_punchmax);
    }
}

VARP(righthanded, 0, 1, 1); // flowtron 20090727

void weapon::renderhudmodel(int lastaction, int index)
{
    playerent *p = owner;
    vec unitv;
    float dist = worldpos.dist(p->o, unitv);
    unitv.div(dist);

    weaponmove wm;
    if(!intermission || !ispaused) wm.calcmove(unitv, lastaction, p);
    defformatstring(path)("weapons/%s", info.modelname);
    bool emit = (wm.anim&ANIM_INDEX)==ANIM_GUN_SHOOT && (lastmillis - lastaction) < flashtime();
    // The viewmodel is oriented from the aim, but it is drawn inside the world and therefore under
    // the shaken camera matrix - so it has to carry the same recoil offset the view does, or the
    // screen would rotate around a gun that stayed where it was and the muzzle would visibly sag
    // as a spray climbs. Only the local player's own weapon: someone else's gun in a follow camera
    // has no punch applied to the view either.
    float vy = 0.0f, vp = 0.0f;
    if(p == player1) cs2viewpunch(vy, vp);
    rendermodel(path, wm.anim|ANIM_DYNALLOC|(righthanded==index ? ANIM_MIRROR : 0)|(emit ? ANIM_PARTICLE : 0), 0, -1, wm.pos, 0, p->yaw+90+vy, p->pitch+wm.k_rot+vp, 40.0f, wm.basetime, NULL, NULL, 1.28f);
}

void weapon::updatetimers(int millis)
{
    if(gunwait) gunwait = max(gunwait - (millis-owner->lastaction), 0);
}

void weapon::onselecting(bool sound)
{
    updatelastaction(owner);
    cs2resetweapon(this, owner); // drawing a weapon clears any penalty it kept from last time
    bool local = (owner == player1);
    if(sound) audiomgr.playsound(S_GUNCHANGE, owner, local ? SP_HIGH : SP_NORMAL);
}

void weapon::renderhudmodel() { renderhudmodel(owner->lastaction); }
void weapon::renderaimhelp(bool teamwarning)
{
    if(!editmode) drawcrosshair(owner, teamwarning ? CROSSHAIR_TEAMMATE : owner->weaponsel->type);
    else drawcrosshair(owner, CROSSHAIR_EDIT);
}
int weapon::dynspread() { return info.spread; }
float weapon::dynrecoil() { return info.recoil; }
bool weapon::selectable() { return this != owner->weaponsel && owner->state == CS_ALIVE && !owner->weaponchanging; }
bool weapon::deselectable() { return !reloading; }

void weapon::equipplayer(playerent *pl)
{
    if(!pl) return;
    pl->weapons[GUN_ASSAULT] = new assaultrifle(pl);
    pl->weapons[GUN_GRENADE] = new grenades(pl);
    pl->weapons[GUN_KNIFE] = new knife(pl);
    pl->weapons[GUN_PISTOL] = new pistol(pl);
    pl->weapons[GUN_CARBINE] = new carbine(pl);
    pl->weapons[GUN_SHOTGUN] = new shotgun(pl);
    pl->weapons[GUN_SNIPER] = new sniperrifle(pl);
    pl->weapons[GUN_SUBGUN] = new subgun(pl);
    pl->weapons[GUN_AKIMBO] = new akimbo(pl);
    pl->selectweapon(GUN_ASSAULT);
    pl->setprimary(GUN_ASSAULT);
    pl->setnextprimary(GUN_ASSAULT);
}

// grenadeent

enum { NS_NONE, NS_ACTIVATED = 0, NS_THROWN, NS_EXPLODED };

grenadeent::grenadeent (playerent *owner, int millis)
{
    ASSERT(owner);
    nadestate = NS_NONE;
    local = owner==player1;
    bounceent::owner = owner;
    bounceent::millis = lastmillis;
    timetolive = 2000-millis;
    bouncetype = BT_NADE;
    maxspeed = 30.0f;
    rotspeed = 6.0f;
    distsincebounce = 0.0f;
}

grenadeent::~grenadeent()
{
    if(owner && owner->weapons[GUN_GRENADE]) owner->weapons[GUN_GRENADE]->removebounceent(this);
}

void grenadeent::explode()
{
    if(nadestate!=NS_ACTIVATED && nadestate!=NS_THROWN ) return;
    nadestate = NS_EXPLODED;
    static vec n(0,0,0);
    hits.setsize(0);
    splash();
    if(local)
        addmsg(SV_EXPLODE, "ri3iv", lastmillis, GUN_GRENADE, millis, hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
    audiomgr.playsound(S_FEXPLODE, &o);
    if(((grenades *)owner->weapons[GUN_GRENADE])->state == GST_NONE) owner->weapons[GUN_GRENADE]->reset();
}

void grenadeent::splash()
{
    particle_splash(PART_SPARK, 50, 300, o);
    particle_fireball(PART_FIREBALL, o);
    addscorchmark(o);
    adddynlight(NULL, o, 16, 200, 100, 255, 255, 224);
    adddynlight(NULL, o, 16, 600, 600, 192, 160, 128);
    if(owner == player1)
    {
        if(multiplayer(NULL)) accuracym[GUN_GRENADE].shots++;
    }
    else if(!m_botmode) return;
    int damage = guns[GUN_GRENADE].damage;

    radialeffect(owner->type == ENT_BOT ? player1 : owner, o, damage, owner, GUN_GRENADE);
    loopv(players)
    {
        playerent *p = players[i];
        if(!p) continue;
        radialeffect(p, o, damage, owner, GUN_GRENADE);
    }
}

void grenadeent::activate(const vec &from, const vec &vel)
{
    if(nadestate!=NS_NONE) return;
    nadestate = NS_ACTIVATED;

    if(local)
    {
        addmsg(SV_SHOOT, "ri2i3i", millis, owner->weaponsel->type, (int)(vel.x*DMF), (int)(vel.y*DMF), (int)(vel.z*DMF), 0); // server reads all weapons with DMF precision
        audiomgr.playsound(S_GRENADEPULL, SP_HIGH);
        player1->pstatshots[GUN_GRENADE]++;
    }
}

void grenadeent::_throw(const vec &from, const vec &vel)
{
    if(nadestate!=NS_ACTIVATED) return;
    nadestate = NS_THROWN;
    this->vel = vel;
    this->o = from;
    this->resetinterp();
    inwater = waterlevel > o.z;
    if(local)
    {
        addmsg(SV_THROWNADE, "ri7", int(o.x*DNF), int(o.y*DNF), int(o.z*DNF), int(vel.x*DNF), int(vel.y*DNF), int(vel.z*DNF), lastmillis-millis);
        audiomgr.playsound(S_GRENADETHROW, SP_HIGH);
    }
    else audiomgr.playsound(S_GRENADETHROW, owner);
}

void grenadeent::moveoutsidebbox(const vec &direction, playerent *boundingbox)
{
    vel = direction;
    o = boundingbox->o;
    inwater = waterlevel > o.z;

    boundingbox->cancollide = false;
    loopi(10) moveplayer(this, 10, true, 10);
    boundingbox->cancollide = true;
}

void grenadeent::destroy() { explode(); }
bool grenadeent::applyphysics() { return nadestate==NS_THROWN; }

void grenadeent::oncollision()
{
    if(distsincebounce>=1.5f) audiomgr.playsound(rnd(2) ? S_GRENADEBOUNCE1 : S_GRENADEBOUNCE2, &o);
    distsincebounce = 0.0f;
}

void grenadeent::onmoved(const vec &dist)
{
    distsincebounce += dist.magnitude();
}

// grenades

grenades::grenades(playerent *owner) : weapon(owner, GUN_GRENADE), inhandnade(NULL), throwwait((13*1000)/40), throwmillis(0), cookingmillis(0), state(GST_NONE) {}

int grenades::flashtime() const { return 0; }

bool grenades::busy() { return state!=GST_NONE; }

bool grenades::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction;
    vec &vel = targ;

    bool waitdone = attackmillis>=gunwait && !(m_arena && m_teammode && arenaintermission);
    if(waitdone) gunwait = reloading = 0;

    switch(state)
    {
        case GST_NONE:
            if(waitdone && owner->attacking && this==owner->weaponsel)
            {
                attackevent(owner, type);
                activatenade(vel);
            }
        break;

        case GST_INHAND:
            if(waitdone)
            {
                if(!owner->attacking || this!=owner->weaponsel) thrownade();
                else if(!inhandnade->isalive(lastmillis)) dropnade();
            }
            break;

        case GST_THROWING:
            if(attackmillis >= throwwait)
            {
                reset();
                if(!mag && this==owner->weaponsel)
                {
                    owner->weaponchanging = lastmillis-1-(weaponchangetime/2);
                    owner->nextweaponsel = owner->weaponsel = owner->primweap;
                }
                return false;
            }
            break;
    }
    return true;
}

void grenades::attackfx(const vec &from, const vec &vel, int millis) // other player's grenades
{
    throwmillis = lastmillis-millis;
    cookingmillis = millis;
    if(millis == 0 || millis == -1)
    {
        state = GST_INHAND;
        audiomgr.playsound(S_GRENADEPULL, owner);
    }
    else if(millis > 0) // throw
    {
        grenadeent *g = new grenadeent(owner, millis);
        state = GST_THROWING;
        bounceents.add(g);
        g->_throw(from, vel);
    }
}

int grenades::modelanim()
{
    if(state == GST_THROWING)
    {
        if(lastmillis - owner->lastaction >= throwwait) state = GST_NONE;
        return ANIM_GUN_THROW;
    }
    else
    {
        int animtime = min(gunwait, (int)info.attackdelay);
        if(state == GST_INHAND || lastmillis - owner->lastaction < animtime) return ANIM_GUN_SHOOT;
    }
    return ANIM_GUN_IDLE;
}

void grenades::activatenade(const vec &vel)
{
    if(!mag) return;
    throwmillis = 0;

    inhandnade = new grenadeent(owner);
    bounceents.add(inhandnade);

    updatelastaction(owner);
    mag--;
    gunwait = info.attackdelay;
    owner->lastattackweapon = this;
    state = GST_INHAND;
    inhandnade->activate(owner->o, vel);
}

void grenades::thrownade()
{
    if(!inhandnade) return;
    const float speed = cosf(RAD*owner->pitch);
    vec lvel(sinf(RAD*owner->yaw)*speed, -cosf(RAD*owner->yaw)*speed, sinf(RAD*owner->pitch));
    lvel.mul(1.5f);
    vec uvel(int(lvel.x*DNF)/DNF,int(lvel.y*DNF)/DNF,int(lvel.z*DNF)/DNF); // force universal granularity 
    thrownade(uvel);
}

void grenades::thrownade(const vec &vel)
{
    inhandnade->moveoutsidebbox(vel, owner);
    // sending with DNF precision fixes current "remote stuck on plateau"-bug, but "universal granularity"(TM) lessens discrepancies between worlds even more
    inhandnade->o.x = int(inhandnade->o.x*DNF)/DNF;
    inhandnade->o.y = int(inhandnade->o.y*DNF)/DNF;
    inhandnade->o.z = int(inhandnade->o.z*DNF)/DNF;
    inhandnade->_throw(inhandnade->o, vel);
    inhandnade = NULL;

    throwmillis = lastmillis;
    updatelastaction(owner);
    state = GST_THROWING;
    if(this==owner->weaponsel) owner->attacking = false;
    if(quicknade && owner->weaponsel->type == GUN_GRENADE) selectweapon(owner->prevweaponsel);
}

void grenades::dropnade()
{
    vec n(0,0,0);
    thrownade(n);
}

void grenades::renderstats()
{
    char gunstats[64];
    sprintf(gunstats, "%d", mag);
    draw_text(gunstats, oldfashionedgunstats ? HUDPOS_GRENADE + HUDPOS_NUMBERSPACING + (((float)screenw / (float)screenh > 1.5f) ? 75 : 25) : HUDPOS_GRENADE + HUDPOS_NUMBERSPACING, 823);
}

bool grenades::selectable() { return weapon::selectable() && state != GST_INHAND && mag; }
void grenades::reset() { throwmillis = 0; cookingmillis = 0; quicknade = false; state = GST_NONE; }

void grenades::onselecting(bool sound)
{
    reset();
    updatelastaction(owner);
    bool local = (owner == player1);
    if(sound) audiomgr.playsound(S_GUNCHANGE, owner, local ? SP_HIGH : SP_NORMAL);
}

void grenades::onownerdies()
{
    reset();
    if(owner==player1 && inhandnade) dropnade();
}

void grenades::removebounceent(bounceent *b)
{
    if(b == inhandnade) { inhandnade = NULL; reset(); }
}

// gun base class

gun::gun(playerent *owner, int type) : weapon(owner, type) {}

bool gun::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction - gunwait;
    if(attackmillis<0) return false;
    gunwait = reloading = 0;

    if(!owner->attacking)
    {
        shots = 0;
        checkautoreload();
        return false;
    }

    attackmillis = lastmillis - min(attackmillis, curtime);
    updatelastaction(owner, attackmillis);
    if(!mag)
    {
        bool local = (owner == player1);
        audiomgr.playsound(S_NOAMMO, owner, local ? SP_HIGH : SP_NORMAL);
        gunwait += 250;
        owner->lastattackweapon = NULL;
        shots = 0;
        checkautoreload();
        return false;
    }

    owner->lastattackweapon = this;
    shots++;

    if(!info.isauto) owner->attacking = false;

    if(burstshotssettings[this->type] > 0 && shots >= burstshotssettings[this->type]) owner->attacking = false;

    vec from = owner->o;
    vec to = targ;
    from.z -= weaponbeloweye;

    attackphysics(from, to);

    attackevent(owner, type);

    hits.setsize(0);
    raydamage(from, to, owner);
    attackfx(from, to, 0);

    gunwait = info.attackdelay;
    mag--;

    sendshoot(from, to, attackmillis);

    return true;
}

void gun::attackfx(const vec &from, const vec &to, int millis)
{
    if(from.squareddist(to) > 0.07f)
    {
        addbullethole(owner, from, to);
        addshotline(owner, from, to);
    }
    particle_splash(PART_SPARK, 5, 250, to);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    attacksound();
}

int gun::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT|ANIM_LOOP : ANIM_GUN_IDLE; }
void gun::checkautoreload() { if(autoreload && owner==player1 && !mag) reload(true); }
void gun::onownerdies() { shots = 0; cs2resetweapon(this, owner); }


// shotgun

shotgun::shotgun(playerent *owner) : gun(owner, GUN_SHOTGUN) {}

void shotgun::attackphysics(vec &from, vec &to)
{
    createrays(from, to);
    gun::attackphysics(from, to);
}

bool shotgun::attack(vec &targ)
{
    return gun::attack(targ);
}

void shotgun::attackfx(const vec &from, const vec &to, int millis)
{
    loopi(SGRAYS) particle_splash(PART_SPARK, 5, 200, sgr[i].rv);

    if(addbullethole(owner, from, to))
        loopk(3) loopi(3) addbullethole(owner, from, sgr[k*SGRAYS+i*SGRAYS/3].rv, 0, false);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    attacksound();
}

bool shotgun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }


// subgun

subgun::subgun(playerent *owner) : gun(owner, GUN_SUBGUN) {}
bool subgun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }
int subgun::dynspread() { return shots > 2 ? 70 : ( info.spread + ( shots > 0 ? ( shots == 1 ? 5 : 10 ) : 0 ) ); } // CHANGED: 2010nov19 was: min(info.spread + 10 * shots, 80)


// sniperrifle

sniperrifle::sniperrifle(playerent *owner) : gun(owner, GUN_SNIPER), scoped(false) {}

void sniperrifle::attackfx(const vec &from, const vec &to, int millis)
{
    if(from.squareddist(to) > 0.07f)
    {
        addbullethole(owner, from, to);
        addshotline(owner, from, to);
        particle_trail(PART_SMOKE, 500, from, to);
    }
    particle_splash(PART_SPARK, 50, 200, to);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    attacksound();
}

bool sniperrifle::reload(bool autoreloaded)
{
    bool r = weapon::reload(autoreloaded);
    if(owner==player1 && r) { scoped = false; player1->scoping = false; }
    return r;
}

#define SCOPESETTLETIME 180
int sniperrifle::dynspread()
{
    if(scoped)
    {
        int scopetime = lastmillis - scoped_since;
        if(scopetime > SCOPESETTLETIME)
            return 1;
        else
            return max((info.spread * (SCOPESETTLETIME - scopetime)) / SCOPESETTLETIME, 1);
    }
    return info.spread;
}
float sniperrifle::dynrecoil() { return scoped && lastmillis - scoped_since > SCOPESETTLETIME ? info.recoil / 3 : info.recoil; }
int sniperrifle::cs2firemode() const { return scoped ? 1 : 0; }  // CS2's AWP: mode 1 is the scoped one
bool sniperrifle::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }
void sniperrifle::onselecting(bool sound) { weapon::onselecting(sound); scoped = false; player1->scoping = false; }
void sniperrifle::ondeselecting() { scoped = false; owner->scoping = false; }
void sniperrifle::onownerdies() { shots = 0; scoped = false; owner->scoping = false; cs2resetweapon(this, owner); }
void sniperrifle::renderhudmodel() { if(!scoped) weapon::renderhudmodel(); }

void sniperrifle::renderaimhelp(bool teamwarning)
{
    if(scoped) drawscope();
    if(!editmode)
    {
        if(scoped || teamwarning) drawcrosshair(owner, teamwarning ? CROSSHAIR_TEAMMATE : CROSSHAIR_SCOPE, NULL, 24.0f);
    }
    else drawcrosshair(owner, CROSSHAIR_EDIT);
}

void sniperrifle::setscope(bool enable)
{
    if(this == owner->weaponsel && !reloading && owner->state == CS_ALIVE)
    {
        if(scoped == false && enable == true) scoped_since = lastmillis;
        if(enable != scoped) owner->scoping = enable;
        scoped = enable;
    }
}

// carbine

carbine::carbine(playerent *owner) : gun(owner, GUN_CARBINE) {}

bool carbine::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }


// assaultrifle

assaultrifle::assaultrifle(playerent *owner) : gun(owner, GUN_ASSAULT) {}

int assaultrifle::dynspread() { return shots > 2 ? 55 : ( info.spread + ( shots > 0 ? ( shots == 1 ? 5 : 15 ) : 0 ) ); }
float assaultrifle::dynrecoil() { return info.recoil + (rnd(8)*-0.01f); }
bool assaultrifle::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }

// pistol

pistol::pistol(playerent *owner) : gun(owner, GUN_PISTOL) {}
bool pistol::selectable() { return weapon::selectable() && !m_nopistol; }


// akimbo

akimbo::akimbo(playerent *owner) : gun(owner, GUN_AKIMBO), akimboside(0), akimbomillis(0)
{
    akimbolastaction[0] = akimbolastaction[1] = 0;
}

void akimbo::attackfx(const vec &from, const vec &to, int millis)
{
    akimbolastaction[akimboside] = owner->lastaction;
    akimboside = (akimboside+1)%2;
    gun::attackfx(from, to, millis);
}

void akimbo::onammopicked()
{
    akimbomillis = lastmillis + 30000;
    if(owner==player1)
    {
        if(akimboautoswitch || owner->weaponsel->type==GUN_PISTOL)
        {
            if(player1->weapons[GUN_GRENADE]->busy()) player1->attacking = false;
            player1->weaponswitch(this);
        }
        addmsg(SV_AKIMBO, "ri", lastmillis);
    }
}

void akimbo::onselecting(bool sound)
{
    gun::onselecting(sound);
    akimbolastaction[0] = akimbolastaction[1] = lastmillis;
}

bool akimbo::selectable() { return weapon::selectable() && !m_nopistol && owner->akimbo; }
void akimbo::updatetimers(int millis) { weapon::updatetimers(millis); /*loopi(2) akimbolastaction[i] = millis;*/ }
void akimbo::reset() { akimbolastaction[0] = akimbolastaction[1] = akimbomillis = akimboside = 0; }

void akimbo::renderhudmodel()
{
    weapon::renderhudmodel(akimbolastaction[0], 0);
    weapon::renderhudmodel(akimbolastaction[1], 1);
}

bool akimbo::timerout() { return akimbomillis && akimbomillis <= lastmillis; }


// knife

knife::knife(playerent *owner) : weapon(owner, GUN_KNIFE) {}

int knife::flashtime() const { return 0; }

bool knife::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction - gunwait;
    if(attackmillis<0) return false;
    gunwait = reloading = 0;

    if(!owner->attacking) return false;

    attackmillis = lastmillis - min(attackmillis, curtime);
    updatelastaction(owner, attackmillis);

    owner->lastattackweapon = this;
    owner->attacking = false;

    vec from = owner->o;
    vec to = targ;
    from.z -= weaponbeloweye;

    vec unitv;
    float dist = to.dist(from, unitv);
    unitv.div(dist);
    unitv.mul(3); // punch range
    to = from;
    to.add(unitv);
    intersectgeometry(from,to);
    if ( owner->pitch < 0 ) to.z += 2.5 * sin( owner->pitch * 0.01745329 );

    attackevent(owner, type);

    hits.setsize(0);
    raydamage(from, to, owner);
    attackfx(from, to, 0);
    sendshoot(from, to, attackmillis);
    gunwait = info.attackdelay;

    return true;
}

int knife::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT : ANIM_GUN_IDLE; }

void knife::drawstats() {}
void knife::attackfx(const vec &from, const vec &to, int millis) { attacksound(); }
void knife::renderstats() { }


void setscope(bool enable)
{
    if(player1->weaponsel->type != GUN_SNIPER) return;
    sniperrifle *sr = (sniperrifle *)player1->weaponsel;
    sr->setscope(enable);
}

COMMANDF(setscope, "i", (int *on) { setscope(*on != 0); });

void shoot(playerent *p, vec &targ)
{
    if(p->state!=CS_ALIVE) return;
    weapon *weap = p->weaponsel, *bweap = p->weapons[GUN_GRENADE];
    if(bweap->busy()) bweap->attack(targ); // continue ongoing nade action
    else bweap = NULL;
    if(weap && !p->weaponchanging && weap != bweap) weap->attack(targ);
}

void checkakimbo()
{
    if(player1->akimbo)
    {
        if (!player1->hasakimbo) {
			player1->hasakimbo = true;
			exechook(HOOK_SP_MP, "onAkimboStart", "");
		}
        akimbo &a = *((akimbo *)player1->weapons[GUN_AKIMBO]);
        bool isdeadorspect = player1->state == CS_DEAD || player1->state == CS_SPECTATE;
        if(a.timerout() || isdeadorspect)
        {
            weapon &p = *player1->weapons[GUN_PISTOL];
            player1->akimbo = false;
            a.reset();

            if(player1->weaponsel->type==GUN_AKIMBO || (player1->weaponchanging && player1->nextweaponsel->type==GUN_AKIMBO))
            {
                switch(isdeadorspect ? 1 : akimboendaction)
                {
                    case 0: player1->weaponswitch(player1->weapons[GUN_KNIFE]); break;
                    case 1:
                    {
                        if(player1->weapons[GUN_PISTOL]->ammo) player1->weaponswitch(&p);
                        else player1->weaponswitch(player1->weapons[GUN_KNIFE]);
                        break;
                    }
                    case 2:
                    {
                        if(player1->mag[GUN_GRENADE]) player1->weaponswitch(player1->weapons[GUN_GRENADE]);
                        else {
                            if(player1->weapons[GUN_PISTOL]->ammo) player1->weaponswitch(&p);
                            else player1->weaponswitch(player1->weapons[GUN_KNIFE]);
                        }
                        break;
                    }
                    case 3:
                    {
                        if(player1->ammo[player1->primary]) player1->weaponswitch(player1->weapons[player1->primary]);
                        else {
                            if(player1->mag[GUN_GRENADE]) player1->weaponswitch(player1->weapons[GUN_GRENADE]);
                            else {
                                if(player1->weapons[GUN_PISTOL]->ammo) player1->weaponswitch(&p);
                                else player1->weaponswitch(player1->weapons[GUN_KNIFE]);
                            }
                        }
                        break;
                    }
                    default: break;
                /*
                    case 0: player1->weaponswitch(&p); break;
                    case 1:
                    {
                        if( player1->ammo[player1->primary] ) player1->weaponswitch(player1->weapons[player1->primary]);
                        else player1->weaponswitch(&p);
                        break;
                    }
                    case 2:
                    {
                        if( player1->mag[GUN_GRENADE] ) player1->weaponswitch(player1->weapons[GUN_GRENADE]);
                        else
                        {
                            if( player1->ammo[player1->primary] ) player1->weaponswitch(player1->weapons[player1->primary]);
                            else player1->weaponswitch(&p);
                        }
                        break;
                    }
                    default: break;
                */
                }
            }
            if(!isdeadorspect) audiomgr.playsoundc(S_AKIMBOOUT);
            player1->hasakimbo = false;
			exechook(HOOK_SP_MP, "onAkimboEnd", "");
        }
    }
}

void checkweaponstate()
{
    checkweaponswitch();
    checkakimbo();
}
