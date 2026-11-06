/* Hoppy Road - voxel road-crossing hopper for TrimUI Brick (SDL2).
 * Software-rendered 3D boxes. Run on a PC with:  ./hoppyroad -w
 */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef HEADLESS
#include <SDL2/SDL.h>
#endif

#define SW 640
#define SH 480
#define PI_F 3.14159265f
#define NR 64
#define RIDX(r) ((((r) % NR) + NR) % NR)
#define MAXP 160
#define MAXITEMS 2400
#define TRAIN_LEN 15.0f
#define EAGLE_ROWS 4.6f

/* gamepad button numbers (SDL joystick API) - edit if your firmware differs */
#define JB_SELECT 6
#define JB_START  7
#define JB_MENU   8

typedef struct { float r, g, b; } Col;
typedef struct { float x, y; } Pt;

static uint32_t fb[SW * SH], bg[SW * SH];

/* ================= raster helpers ================= */
static uint32_t rgb(float r, float g, float b) {
    int R = (int)(r < 0 ? 0 : r > 255 ? 255 : r);
    int G = (int)(g < 0 ? 0 : g > 255 ? 255 : g);
    int B = (int)(b < 0 ? 0 : b > 255 ? 255 : b);
    return 0xFF000000u | (R << 16) | (G << 8) | B;
}
static uint32_t shade(Col c, float k) { return rgb(c.r * k, c.g * k, c.b * k); }

static inline void blend_px(int x, int y, uint32_t c, int a) {
    if ((unsigned)x >= SW || (unsigned)y >= SH) return;
    uint32_t *p = &fb[y * SW + x];
    if (a >= 255) { *p = c; return; }
    if (a <= 0) return;
    uint32_t d = *p;
    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    int sr = (c >> 16) & 255, sg = (c >> 8) & 255, sb = c & 255;
    dr += ((sr - dr) * a) / 255;
    dg += ((sg - dg) * a) / 255;
    db += ((sb - db) * a) / 255;
    *p = 0xFF000000u | (dr << 16) | (dg << 8) | db;
}

static void fill_poly(const Pt *v, int n, uint32_t c, int a) {
    float miny = v[0].y, maxy = v[0].y;
    for (int i = 1; i < n; i++) {
        if (v[i].y < miny) miny = v[i].y;
        if (v[i].y > maxy) maxy = v[i].y;
    }
    int y0 = (int)ceilf(miny - 0.5f), y1 = (int)floorf(maxy - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 > SH - 1) y1 = SH - 1;
    for (int y = y0; y <= y1; y++) {
        float yc = y + 0.5f, xl = 1e9f, xr = -1e9f;
        for (int i = 0; i < n; i++) {
            Pt a0 = v[i], b0 = v[(i + 1) % n];
            if ((yc >= a0.y && yc < b0.y) || (yc >= b0.y && yc < a0.y)) {
                float t = (yc - a0.y) / (b0.y - a0.y);
                float x = a0.x + t * (b0.x - a0.x);
                if (x < xl) xl = x;
                if (x > xr) xr = x;
            }
        }
        if (xr < xl) continue;
        int xs = (int)ceilf(xl - 0.5f), xe = (int)floorf(xr - 0.5f);
        if (xs < 0) xs = 0;
        if (xe > SW - 1) xe = SW - 1;
        for (int x = xs; x <= xe; x++) blend_px(x, y, c, a);
    }
}

static void rect(int x, int y, int w, int h, uint32_t c, int a) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) blend_px(x + i, y + j, c, a);
}

/* ================= tiny 5x7 font ================= */
static const char CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-!";
static const uint8_t FONT[][7] = {
    {0,0,0,0,0,0,0},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}
};
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc - sc; }
static void draw_text(int x, int y, int sc, const char *s, uint32_t c) {
    for (; *s; s++) {
        const char *q = strchr(CH, toupper((unsigned char)*s));
        int gi = q ? (int)(q - CH) : 0;
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (FONT[gi][r] & (0x10 >> col)) rect(x + col * sc, y + r * sc, sc, sc, c, 255);
        x += 6 * sc;
    }
}
static void text_shadow(int x, int y, int sc, const char *s, uint32_t c) {
    int o = sc / 3 > 1 ? sc / 3 : 1;
    draw_text(x + o, y + o, sc, s, rgb(0, 0, 0));
    draw_text(x, y, sc, s, c);
}
static void text_center(int y, int sc, const char *s, uint32_t c) {
    text_shadow((SW - text_w(s, sc)) / 2, y, sc, s, c);
}

/* ================= camera (near-orthographic, angled) ================= */
static float cpx, cpy, cpz, vr[3], vu[3], vf[3];
static float cam_ox = SW / 2.0f, cam_oy = SH * 0.62f, cam_F, shk_x = 0, shk_y = 0;
#define CAM_D 70.0f
#define CAM_SCALE 42.0f

static void cam_update(float fx, float fz) {
    float a = 46.0f * PI_F / 180.0f, yaw = 0.14f;
    float dir[3] = { sinf(yaw) * cosf(a), sinf(a), cosf(yaw) * cosf(a) };
    cpx = fx + CAM_D * dir[0]; cpy = CAM_D * dir[1]; cpz = fz + CAM_D * dir[2];
    for (int i = 0; i < 3; i++) vf[i] = -dir[i];
    /* right = normalize(cross(f, up)) with up=(0,1,0) */
    float rx = -vf[2], rz = vf[0], l = sqrtf(rx * rx + rz * rz);
    vr[0] = rx / l; vr[1] = 0; vr[2] = rz / l;
    vu[0] = vr[1] * vf[2] - vr[2] * vf[1];
    vu[1] = vr[2] * vf[0] - vr[0] * vf[2];
    vu[2] = vr[0] * vf[1] - vr[1] * vf[0];
    cam_F = CAM_SCALE * CAM_D;
}
static void proj(float x, float y, float z, Pt *o, float *dep) {
    float qx = x - cpx, qy = y - cpy, qz = z - cpz;
    float xc = qx * vr[0] + qy * vr[1] + qz * vr[2];
    float yc = qx * vu[0] + qy * vu[1] + qz * vu[2];
    float zc = qx * vf[0] + qy * vf[1] + qz * vf[2];
    if (dep) *dep = zc;
    o->x = cam_ox + shk_x + cam_F * xc / zc;
    o->y = cam_oy + shk_y - cam_F * yc / zc;
}

/* ================= boxes ================= */
static void draw_box_ex(float cx, float cz, float hx, float hz, float y0, float y1, Col c) {
    float x0 = cx - hx, x1 = cx + hx, z0 = cz - hz, z1 = cz + hz, ym = (y0 + y1) / 2;
    Pt p[8];
    for (int i = 0; i < 8; i++)
        proj((i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0, &p[i], NULL);
    static const int F[5][4] = { {2,3,7,6}, {4,5,7,6}, {0,4,6,2}, {1,5,7,3}, {0,1,3,2} };
    static const float K[5] = { 1.0f, 0.78f, 0.88f, 0.60f, 0.50f };
    float nrm[5][3] = { {0,1,0}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,0,-1} };
    float ctr[5][3] = { {cx,y1,cz}, {cx,ym,z1}, {x0,ym,cz}, {x1,ym,cz}, {cx,ym,z0} };
    for (int f = 0; f < 5; f++) {
        float d = nrm[f][0] * (cpx - ctr[f][0]) + nrm[f][1] * (cpy - ctr[f][1]) +
                  nrm[f][2] * (cpz - ctr[f][2]);
        if (d <= 0) continue;
        Pt q[4];
        for (int i = 0; i < 4; i++) q[i] = p[F[f][i]];
        fill_poly(q, 4, shade(c, K[f]), 255);
    }
}
static void flat_quad(float x0, float z0, float x1, float z1, float y, uint32_t c, int a) {
    Pt q[4];
    proj(x0, y, z0, &q[0], NULL); proj(x1, y, z0, &q[1], NULL);
    proj(x1, y, z1, &q[2], NULL); proj(x0, y, z1, &q[3], NULL);
    fill_poly(q, 4, c, a);
}

typedef struct { float d, cx, cz, hx, hz, y0, y1; Col c; } Item;
static Item items[MAXITEMS];
static int nitems = 0;
static int cmp_item(const void *a, const void *b) {
    float da = ((const Item *)a)->d, db = ((const Item *)b)->d;
    return da < db ? 1 : (da > db ? -1 : 0);
}
static void add_item(float cx, float cz, float hx, float hz, float y0, float y1, Col c) {
    if (nitems >= MAXITEMS) return;
    Item *it = &items[nitems];
    Pt p; float d;
    proj(cx, (y0 + y1) * 0.5f, cz, &p, &d);
    if (p.x < -120 || p.x > SW + 120 || p.y < -150 || p.y > SH + 100) return;
    it->d = d; it->cx = cx; it->cz = cz; it->hx = hx; it->hz = hz; it->y0 = y0; it->y1 = y1; it->c = c;
    nitems++;
}
/* box oriented along facing (fx,fz): af = half-size along facing, ap = across */
static void add_oriented(float cx, float cz, int fx, int fz, float af, float ap, float y0, float y1, Col c) {
    if (fx != 0) add_item(cx, cz, af, ap, y0, y1, c);
    else add_item(cx, cz, ap, af, y0, y1, c);
}

/* ================= world ================= */
enum { R_GRASS, R_ROAD, R_RIVER, R_RAIL };
typedef struct { float x, len; int kind; Col c; } Obj;
typedef struct {
    int id, valid, type, dir, nobj, state;
    float speed, timer, trainx;
    Obj o[6];
    unsigned char tree[25], th[25], coin[25];
} Row;
static Row rows[NR];
static int gen_next, run_left, run_type, since_grass;

static float rf(void) { return (float)rand() / (float)RAND_MAX; }
static Row *getrow(int r) {
    Row *R = &rows[RIDX(r)];
    return (R->valid && R->id == r) ? R : NULL;
}

static const Col CAR_COL[7] = {
    {230,70,60}, {60,120,230}, {240,200,60}, {80,200,120}, {240,140,50}, {190,80,200}, {235,235,240}
};

static void gen_row(int r) {
    Row *R = &rows[RIDX(r)];
    memset(R, 0, sizeof *R);
    R->id = r; R->valid = 1;
    int type = R_GRASS;
    if (r > 2) {
        if (run_left <= 0) {
            int t;
            do {
                int roll = rand() % 100;
                t = roll < 28 ? R_GRASS : roll < 66 ? R_ROAD : roll < 86 ? R_RIVER : R_RAIL;
                if (since_grass >= 5) t = R_GRASS;
            } while (t == run_type && t != R_ROAD && since_grass < 5);
            run_type = t;
            run_left = (t == R_ROAD) ? 1 + rand() % 3 : 1 + rand() % 2;
        }
        type = run_type; run_left--;
    }
    R->type = type;
    if (type == R_GRASS) since_grass = 0; else since_grass++;
    R->dir = (rand() & 1) ? 1 : -1;
    Row *P = getrow(r - 1);
    if (P && P->type == type && type != R_GRASS) R->dir = -P->dir;

    if (type == R_GRASS) {
        for (int c = -12; c <= 12; c++) {
            int playable = abs(c) <= 6;
            int prob = playable ? (r <= 2 ? 0 : 20) : 70;
            if (rand() % 100 < prob) { R->tree[c + 12] = 1; R->th[c + 12] = rand() % 3; }
            else if (playable && r > 2 && rand() % 100 < 7) R->coin[c + 12] = 1;
        }
        if (P && P->type == R_GRASS) {
            int common = 0;
            for (int c = -6; c <= 6; c++) if (!P->tree[c + 12] && !R->tree[c + 12]) common++;
            if (!common) {
                int cands[13], n = 0;
                for (int c = -6; c <= 6; c++) if (!P->tree[c + 12]) cands[n++] = c;
                if (n) R->tree[cands[rand() % n] + 12] = 0;
            }
        }
    } else if (type == R_ROAD) {
        int n = 2 + rand() % 3;
        float phase = rf() * 26.0f;
        R->speed = 1.6f + rf() * 2.6f + fminf(r * 0.015f, 1.5f);
        R->nobj = n;
        for (int i = 0; i < n; i++) {
            Obj *o = &R->o[i];
            float x = fmodf(phase + i * 26.0f / n, 26.0f) - 13.0f;
            o->kind = (rand() % 100 < 30) ? 1 : 0;
            o->len = o->kind ? 3.0f : 1.5f;
            o->x = x;
            o->c = CAR_COL[rand() % 7];
        }
    } else if (type == R_RIVER) {
        int n = 4 + rand() % 2;
        float phase = rf() * 26.0f;
        R->speed = 0.9f + rf() * 1.5f;
        R->nobj = n;
        for (int i = 0; i < n; i++) {
            Obj *o = &R->o[i];
            o->len = 2.0f + rf() * 1.2f;
            o->x = fmodf(phase + i * 26.0f / n, 26.0f) - 13.0f;
            o->c = (Col){140, 95, 50};
        }
    } else {
        R->state = 0; R->timer = 1.0f + rf() * 3.0f;
    }
}
static void ensure_rows(int upto) { while (gen_next <= upto) gen_row(gen_next++); }

static Obj *log_at(Row *R, float x) {
    for (int i = 0; i < R->nobj; i++)
        if (fabsf(x - R->o[i].x) <= R->o[i].len * 0.5f + 0.15f) return &R->o[i];
    return NULL;
}

static void update_rows(float dt, int lo, int hi) {
    for (int r = lo; r <= hi; r++) {
        Row *R = getrow(r);
        if (!R) continue;
        if (R->type == R_ROAD || R->type == R_RIVER) {
            for (int i = 0; i < R->nobj; i++) {
                Obj *o = &R->o[i];
                o->x += R->dir * R->speed * dt;
                while (o->x >= 13.0f) o->x -= 26.0f;
                while (o->x < -13.0f) o->x += 26.0f;
            }
        } else if (R->type == R_RAIL) {
            R->timer -= dt;
            if (R->state == 0) { if (R->timer <= 0) { R->state = 1; R->timer = 1.4f; } }
            else if (R->state == 1) { if (R->timer <= 0) { R->state = 2; R->trainx = -R->dir * 14.0f; } }
            else {
                R->trainx += R->dir * 28.0f * dt;
                float tail = R->trainx - R->dir * TRAIN_LEN;
                if (tail * R->dir > 14.0f) { R->state = 0; R->timer = 2.0f + rf() * 4.0f; }
            }
        }
    }
}

/* ================= game state ================= */
enum { S_TITLE, S_PLAY, S_PAUSE, S_DEAD, S_OVER };
enum { DIE_CAR, DIE_WATER, DIE_TRAIN, DIE_EAGLE };
static int state = S_TITLE;
static float px; static int prow;
static int hopping, hr0, hr1, fdx = 0, fdz = -1;
static float hopt, hx0, hx1, hop_cd;
static int maxrow, coins, score, best, started, death_type, god = 0;
static float camRow, autoRow, cfx, death_t, shake;
static int pend_ok, pend_dx, pend_dr, held_dx, held_dr;
static float pend_t;

typedef struct { float x, y, z, vx, vy, vz, life; Col c; } Part;
static Part parts[MAXP];
static int np = 0;

static void burst(float x, float y, float z, Col c, int n) {
    for (int i = 0; i < n && np < MAXP; i++) {
        Part *p = &parts[np++];
        float a = rf() * 2 * PI_F, sp = 1.0f + rf() * 4.0f;
        p->x = x; p->y = y; p->z = z;
        p->vx = cosf(a) * sp; p->vz = sinf(a) * sp; p->vy = 2.5f + rf() * 4.5f;
        p->life = 0.6f + rf() * 0.6f; p->c = c;
    }
}

static const char *SAVE_FILE = "hoppy.sav";
static void load_best(void) {
    FILE *f = fopen(SAVE_FILE, "r");
    if (f) { if (fscanf(f, "%d", &best) != 1) best = 0; fclose(f); }
}
static void save_best(void) {
    FILE *f = fopen(SAVE_FILE, "w");
    if (f) { fprintf(f, "%d\n", best); fclose(f); }
}

static float topy(int r, float x) {
    Row *R = getrow(r);
    if (!R) return 0;
    switch (R->type) {
    case R_GRASS: return 0.0f;
    case R_ROAD: case R_RAIL: return -0.12f;
    default: return log_at(R, x) ? 0.12f : -0.35f;
    }
}
static void pose(float *x, float *z, float *y, float *rowf) {
    if (hopping) {
        float a = hopt;
        *x = hx0 + (hx1 - hx0) * a;
        *rowf = hr0 + (hr1 - hr0) * a;
        float t0 = topy(hr0, hx0), t1 = topy(hr1, hx1);
        *y = t0 + (t1 - t0) * a + sinf(PI_F * a) * 0.55f;
    } else {
        *x = px; *rowf = (float)prow; *y = topy(prow, px);
    }
    *z = -*rowf;
}

static void new_game(void) {
    memset(rows, 0, sizeof rows);
    gen_next = -8; run_left = 0; run_type = R_GRASS; since_grass = 0;
    ensure_rows(18);
    px = 0; prow = 0; hopping = 0; fdx = 0; fdz = -1; hop_cd = 0;
    maxrow = 0; coins = 0; score = 0; started = 0;
    camRow = autoRow = 0; cfx = 0; death_t = 0; shake = 0; np = 0;
    pend_ok = 0; held_dx = held_dr = 0;
    state = S_PLAY;
}

static void die(int type) {
    if (god || state != S_PLAY) return;
    float x, z, y, rowf;
    pose(&x, &z, &y, &rowf);
    state = S_DEAD; death_type = type; death_t = 0; hopping = 0;
    if (type == DIE_WATER) burst(x, -0.3f, z, (Col){150, 210, 255}, 36);
    else if (type == DIE_CAR || type == DIE_TRAIN) {
        burst(x, 0.5f, z, (Col){250, 250, 250}, 30);
        shake = 0.3f;
    }
    if (score > best) { best = score; save_best(); }
}

static int try_hop(int dx, int dr) {
    fdx = dx; fdz = -dr;
    int tx = (int)lroundf(px) + dx, tr = prow + dr;
    if (tr < -3 || tx < -6 || tx > 6) return 0;
    Row *R = getrow(tr);
    if (!R) return 0;
    if (R->type == R_GRASS && R->tree[tx + 12]) return 0;
    hopping = 1; hopt = 0; hx0 = px; hx1 = (float)tx; hr0 = prow; hr1 = tr;
    return 1;
}

static void finish_hop(void) {
    px = hx1; prow = hr1; hopping = 0; hop_cd = 0.06f;
    if (prow > maxrow) { maxrow = prow; score = maxrow; if (maxrow >= 1) started = 1; }
    Row *R = getrow(prow);
    if (!R) return;
    if (R->type == R_RIVER && !log_at(R, px)) { die(DIE_WATER); return; }
    if (R->type == R_GRASS) {
        int c = (int)lroundf(px) + 12;
        if (R->coin[c]) { R->coin[c] = 0; coins++; burst(px, 0.5f, -(float)prow, (Col){255, 215, 60}, 10); }
    }
}

static void request_hop(int dx, int dr) {
    if (state != S_PLAY) return;
    pend_dx = dx; pend_dr = dr; pend_ok = 1; pend_t = 0.2f;
}

static void check_collide(void) {
    int r = hopping ? (hopt > 0.5f ? hr1 : hr0) : prow;
    float x = hopping ? hx0 + (hx1 - hx0) * hopt : px;
    Row *R = getrow(r);
    if (!R) return;
    if (R->type == R_ROAD) {
        for (int i = 0; i < R->nobj; i++)
            if (fabsf(x - R->o[i].x) < R->o[i].len * 0.5f + 0.3f) { die(DIE_CAR); return; }
    } else if (R->type == R_RAIL && R->state == 2) {
        float tail = R->trainx - R->dir * TRAIN_LEN;
        float lo = fminf(R->trainx, tail), hi = fmaxf(R->trainx, tail);
        if (x > lo - 0.3f && x < hi + 0.3f) die(DIE_TRAIN);
    }
}

static void update(float dt) {
    if (state == S_PAUSE) return;
    int fl = (int)floorf(camRow);
    ensure_rows(fl + 19);
    if (state == S_PLAY) update_rows(dt, fl - 9, fl + 18);

    if (state == S_PLAY || state == S_DEAD) {
        if (hopping) { hopt += dt / 0.12f; if (hopt >= 1.0f) finish_hop(); }
    }
    if (state == S_PLAY) {
        if (hop_cd > 0) hop_cd -= dt;
        if (!hopping) {
            Row *R = getrow(prow);
            if (R && R->type == R_RIVER) {
                if (log_at(R, px)) px += R->dir * R->speed * dt;
                if (fabsf(px) > 7.5f) die(DIE_EAGLE);
            }
            if (state == S_PLAY) {
                if (pend_ok) { pend_t -= dt; if (pend_t <= 0) pend_ok = 0; }
                if (pend_ok) { pend_ok = 0; try_hop(pend_dx, pend_dr); }
                else if ((held_dx || held_dr) && hop_cd <= 0) try_hop(held_dx, held_dr);
            }
        }
        if (state == S_PLAY) check_collide();
        float x, z, y, rowf; pose(&x, &z, &y, &rowf);
        if (state == S_PLAY) {
            if (started) {
                float creep = 0.45f + 0.012f * maxrow; if (creep > 1.0f) creep = 1.0f;
                autoRow += creep * dt;
            }
            if (rowf > autoRow) autoRow = rowf;
            if (started && rowf < camRow - EAGLE_ROWS) die(DIE_EAGLE);
        }
    }
    if (state == S_DEAD) {
        death_t += dt;
        if (death_t > (death_type == DIE_EAGLE ? 1.1f : 0.9f)) state = S_OVER;
    }
    if (state == S_PLAY || state == S_DEAD) {
        camRow += (autoRow - camRow) * fminf(1.0f, dt * 6.0f);
        cfx += (px * 0.5f - cfx) * fminf(1.0f, dt * 4.0f);
    }
    for (int i = 0; i < np;) {
        Part *p = &parts[i];
        p->life -= dt; p->vy -= 14.0f * dt;
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        if (p->y < 0.05f) { p->y = 0.05f; p->vy *= -0.4f; p->vx *= 0.7f; p->vz *= 0.7f; }
        if (p->life <= 0) parts[i] = parts[--np]; else i++;
    }
    if (shake > 0) { shake -= dt; if (shake < 0) shake = 0; }
}

/* ================= rendering ================= */
static void build_bg(void) {
    for (int y = 0; y < SH; y++) {
        float t = (float)y / SH;
        uint32_t c = rgb(120 - 30 * t, 205 - 25 * t, 225 - 15 * t);
        for (int x = 0; x < SW; x++) bg[y * SW + x] = c;
    }
}

static void draw_ground(Row *R, float tm) {
    int r = R->id; float z = -(float)r;
    if (R->type == R_GRASS) {
        Col c = (r & 1) ? (Col){126, 204, 76} : (Col){110, 188, 66};
        draw_box_ex(0, z, 14, 0.5f, -0.8f, 0.0f, c);
    } else if (R->type == R_ROAD) {
        draw_box_ex(0, z, 14, 0.5f, -0.8f, -0.12f, (Col){74, 78, 92});
        Row *N = getrow(r + 1);
        if (N && N->type == R_ROAD)
            for (int x = -13; x < 13; x += 2)
                flat_quad((float)x, z - 0.5f - 0.05f, x + 1.0f, z - 0.5f + 0.05f, -0.115f, rgb(235, 235, 235), 255);
    } else if (R->type == R_RIVER) {
        draw_box_ex(0, z, 14, 0.5f, -0.9f, -0.35f, (Col){64, 152, 232});
        for (int k = 0; k < 5; k++) {
            float x = fmodf((float)((r * 7 + k * 5) % 26) + tm * 0.7f * R->dir + 26.0f * 4, 26.0f) - 13.0f;
            float zz = z + (((k * 37 + r * 13) % 5) - 2) * 0.16f;
            flat_quad(x, zz - 0.05f, x + 1.4f, zz + 0.05f, -0.349f, rgb(150, 205, 250), 190);
        }
    } else {
        draw_box_ex(0, z, 14, 0.5f, -0.8f, -0.12f, (Col){128, 118, 112});
        for (int x = -13; x < 13; x++)
            flat_quad(x + 0.35f, z - 0.4f, x + 0.65f, z + 0.4f, -0.118f, rgb(110, 76, 46), 255);
        flat_quad(-14, z - 0.3f, 14, z - 0.18f, -0.116f, rgb(200, 204, 214), 255);
        flat_quad(-14, z + 0.18f, 14, z + 0.3f, -0.116f, rgb(200, 204, 214), 255);
    }
}

static void row_objects(Row *R, float tm) {
    float z = -(float)R->id;
    if (R->type == R_GRASS) {
        for (int c = -12; c <= 12; c++) {
            if (R->tree[c + 12]) {
                float th = 0.18f * R->th[c + 12];
                flat_quad(c - 0.3f + 0.2f, z - 0.4f - 0.1f, c + 0.5f + 0.2f, z + 0.4f - 0.1f, 0.004f, rgb(20, 60, 20), 70);
                add_item((float)c, z, 0.14f, 0.14f, 0.0f, 0.5f, (Col){110, 75, 40});
                add_item((float)c, z, 0.40f, 0.40f, 0.5f, 1.15f + th, (Col){56, 158, 62});
                add_item((float)c, z, 0.26f, 0.26f, 1.15f + th, 1.6f + th, (Col){82, 186, 72});
            } else if (R->coin[c + 12]) {
                float th = tm * 4.0f + c;
                float hx = 0.05f + 0.17f * fabsf(cosf(th)), hz = 0.05f + 0.17f * fabsf(sinf(th));
                float b = 0.06f * sinf(tm * 3.0f + c);
                add_item((float)c, z, hx, hz, 0.35f + b, 0.62f + b, (Col){255, 205, 40});
            }
        }
    } else if (R->type == R_ROAD) {
        for (int i = 0; i < R->nobj; i++) {
            Obj *o = &R->o[i];
            float L = o->len, d = (float)R->dir;
            flat_quad(o->x - L / 2 + 0.15f, z - 0.4f - 0.08f, o->x + L / 2 + 0.15f, z + 0.4f - 0.08f, -0.115f, rgb(0, 0, 10), 80);
            if (o->kind == 0) {
                add_item(o->x, z, L / 2, 0.36f, -0.02f, 0.5f, o->c);
                Col lc = { fminf(o->c.r + 40, 255), fminf(o->c.g + 40, 255), fminf(o->c.b + 40, 255) };
                add_item(o->x - d * L * 0.08f, z, L * 0.28f, 0.30f, 0.5f, 0.88f, lc);
                add_item(o->x + d * L * 0.5f, z - 0.2f, 0.03f, 0.08f, 0.15f, 0.3f, (Col){255, 240, 150});
                add_item(o->x + d * L * 0.5f, z + 0.2f, 0.03f, 0.08f, 0.15f, 0.3f, (Col){255, 240, 150});
            } else {
                float cab = 1.0f;
                add_item(o->x + d * (L / 2 - cab / 2), z, cab / 2, 0.38f, -0.02f, 1.0f, o->c);
                add_item(o->x + d * (L / 2 - cab * 0.7f), z, cab * 0.3f, 0.32f, 0.7f, 1.05f, (Col){200, 230, 245});
                add_item(o->x - d * cab / 2, z, (L - cab) / 2 - 0.04f, 0.43f, 0.05f, 1.3f, (Col){232, 232, 238});
            }
            for (int w = -1; w <= 1; w += 2)
                for (int s = -1; s <= 1; s += 2)
                    add_item(o->x + w * L * 0.3f, z + s * 0.36f, 0.14f, 0.06f, -0.1f, 0.2f, (Col){30, 30, 36});
        }
    } else if (R->type == R_RIVER) {
        for (int i = 0; i < R->nobj; i++) {
            Obj *o = &R->o[i];
            flat_quad(o->x - o->len / 2 + 0.1f, z - 0.4f, o->x + o->len / 2 + 0.1f, z + 0.4f, -0.349f, rgb(10, 40, 90), 90);
            add_item(o->x, z, o->len / 2, 0.42f, -0.45f, 0.12f, o->c);
        }
    } else {
        float x = -7.9f;
        int on = (R->state == 2) || (R->state == 1 && (((int)(tm * 6)) & 1));
        add_item(x, z + 0.55f, 0.06f, 0.06f, -0.12f, 1.3f, (Col){110, 110, 120});
        add_item(x, z + 0.55f, 0.14f, 0.12f, 1.1f, 1.45f, on ? (Col){255, 60, 50} : (Col){100, 40, 40});
        if (R->state == 2) {
            static const float LEN[4] = { 3.4f, 3.8f, 3.8f, 3.8f };
            static const Col COL[4] = { {210,60,50}, {70,90,170}, {84,112,184}, {70,90,170} };
            float d = (float)R->dir, off = 0;
            for (int s = 0; s < 4; s++) {
                float cx = R->trainx - d * (off + LEN[s] / 2);
                add_item(cx, z, LEN[s] / 2 - 0.06f, 0.46f, -0.05f, 1.35f, COL[s]);
                add_item(cx, z, LEN[s] / 2 - 0.4f, 0.34f, 1.35f, 1.6f, (Col){60, 64, 78});
                off += LEN[s];
            }
        }
    }
}

static void player_items(float tm) {
    float x, z, y, rowf;
    pose(&x, &z, &y, &rowf);
    if (state == S_DEAD) {
        if (death_type == DIE_WATER) { y -= death_t * 1.4f; if (death_t > 0.35f) return; }
        if (death_type == DIE_EAGLE && death_t > 0.5f) return;
    }
    float sq = (state == S_DEAD && (death_type == DIE_CAR || death_type == DIE_TRAIN)) ? 0.18f : 1.0f;
    float gy = topy(hopping ? (hopt > 0.5f ? hr1 : hr0) : prow, x);
    if (!(state == S_DEAD && death_type == DIE_WATER)) {
        float s = 0.38f * (1.0f - 0.25f * fminf(1.0f, (y - gy) / 0.55f));
        flat_quad(x - s + 0.1f, z - s - 0.06f, x + s + 0.1f, z + s - 0.06f, gy + 0.003f, rgb(0, 0, 10), 90);
    }
    int fx = fdx, fz = fdz;
    int px_ = -fz, pz_ = fx;
    (void)tm;
    float b = y;
    add_item(x, z, 0.33f, 0.33f, b, b + 0.62f * sq, (Col){246, 246, 246});
    for (int s = -1; s <= 1; s += 2) {
        add_oriented(x + px_ * 0.36f * s, z + pz_ * 0.36f * s, fx, fz, 0.2f, 0.04f, b + 0.12f * sq, b + 0.45f * sq, (Col){222, 222, 230});
        add_oriented(x + fx * 0.25f + px_ * 0.34f * s, z + fz * 0.25f + pz_ * 0.34f * s, fx, fz, 0.06f, 0.03f, b + 0.42f * sq, b + 0.54f * sq, (Col){20, 20, 28});
    }
    add_oriented(x + fx * 0.1f, z + fz * 0.1f, fx, fz, 0.1f, 0.06f, b + 0.62f * sq, b + 0.8f * sq, (Col){232, 52, 52});
    add_oriented(x + fx * 0.4f, z + fz * 0.4f, fx, fz, 0.09f, 0.12f, b + 0.34f * sq, b + 0.46f * sq, (Col){255, 160, 30});
    add_oriented(x + fx * 0.36f, z + fz * 0.36f, fx, fz, 0.05f, 0.05f, b + 0.24f * sq, b + 0.34f * sq, (Col){232, 52, 52});
    add_oriented(x - fx * 0.36f, z - fz * 0.36f, fx, fz, 0.06f, 0.14f, b + 0.3f * sq, b + 0.5f * sq, (Col){240, 240, 240});

    if (state == S_DEAD && death_type == DIE_EAGLE) {
        float e = death_t < 0.5f ? 9.0f + (0.2f - 9.0f) * (death_t / 0.5f)
                                 : 0.2f + (10.0f - 0.2f) * ((death_t - 0.5f) / 0.6f);
        add_item(x, z, 0.35f, 0.6f, e, e + 0.5f, (Col){110, 72, 42});
        add_item(x, z, 1.3f, 0.3f, e + 0.25f, e + 0.4f, (Col){150, 108, 66});
        add_item(x, z - 0.7f, 0.22f, 0.2f, e + 0.3f, e + 0.65f, (Col){245, 245, 245});
        add_item(x, z - 0.95f, 0.08f, 0.08f, e + 0.35f, e + 0.45f, (Col){255, 200, 40});
    }
}

static void render(float tm) {
    memcpy(fb, bg, sizeof(fb));
    if (shake > 0) { shk_x = (rf() - 0.5f) * shake * 24; shk_y = (rf() - 0.5f) * shake * 24; }
    else shk_x = shk_y = 0;
    cam_update(cfx, -camRow);
    int lo = (int)floorf(camRow) - 9, hi = (int)floorf(camRow) + 16;
    nitems = 0;
    for (int r = hi; r >= lo; r--) { Row *R = getrow(r); if (R) draw_ground(R, tm); }
    for (int r = hi; r >= lo; r--) { Row *R = getrow(r); if (R) row_objects(R, tm); }
    player_items(tm);
    qsort(items, nitems, sizeof(Item), cmp_item);
    for (int i = 0; i < nitems; i++) {
        Item *it = &items[i];
        draw_box_ex(it->cx, it->cz, it->hx, it->hz, it->y0, it->y1, it->c);
    }
    for (int i = 0; i < np; i++) {
        Part *p = &parts[i];
        Pt s; float d; proj(p->x, p->y, p->z, &s, &d);
        float sz = 0.2f * cam_F / d; if (sz < 2) sz = 2;
        int a = (int)(p->life * 420); if (a > 255) a = 255;
        rect((int)(s.x - sz / 2), (int)(s.y - sz / 2), (int)sz, (int)sz, rgb(p->c.r, p->c.g, p->c.b), a);
    }

    char buf[48];
    snprintf(buf, sizeof buf, "%d", score);
    text_shadow(14, 10, 4, buf, rgb(255, 255, 255));
    snprintf(buf, sizeof buf, "COINS %d", coins);
    text_shadow((SW - text_w(buf, 3)) / 2, 14, 3, buf, rgb(255, 225, 90));
    snprintf(buf, sizeof buf, "BEST %d", best);
    text_shadow(SW - 14 - text_w(buf, 3), 14, 3, buf, rgb(255, 255, 255));

    if (state == S_TITLE) {
        rect(0, 0, SW, SH, rgb(0, 0, 0), 90);
        text_center(110, 8, "HOPPY ROAD", rgb(255, 235, 110));
        if (((int)(tm * 2)) & 1) text_center(230, 3, "PRESS A TO START", rgb(255, 255, 255));
        text_center(395, 2, "DPAD HOP  A HOP FORWARD", rgb(255, 255, 255));
        text_center(420, 2, "START PAUSE  MENU QUIT", rgb(255, 255, 255));
    } else if (state == S_PAUSE) {
        rect(0, 0, SW, SH, rgb(0, 0, 0), 110);
        text_center(190, 7, "PAUSED", rgb(255, 255, 255));
        text_center(280, 3, "PRESS START", rgb(230, 235, 255));
    } else if (state == S_OVER) {
        rect(0, 0, SW, SH, rgb(0, 0, 0), 110);
        text_center(140, 7, "GAME OVER", rgb(255, 100, 90));
        snprintf(buf, sizeof buf, "SCORE %d", score);
        text_center(230, 4, buf, rgb(255, 255, 255));
        if (score > 0 && score >= best) text_center(275, 3, "NEW BEST!", rgb(255, 225, 90));
        if (((int)(tm * 2)) & 1) text_center(330, 3, "PRESS A TO RETRY", rgb(230, 235, 255));
    }
}

static void action(void) {
    if (state == S_TITLE || state == S_OVER) new_game();
    else if (state == S_PAUSE) state = S_PLAY;
    else if (state == S_PLAY) request_hop(0, 1);
}
#ifndef HEADLESS
static void toggle_pause(void) {
    if (state == S_PLAY) state = S_PAUSE;
    else if (state == S_PAUSE) state = S_PLAY;
    else action();
}
static void set_dir(int dx, int dr, int press) {
    if (press) { held_dx = dx; held_dr = dr; request_hop(dx, dr); }
    else if (held_dx == dx && held_dr == dr) { held_dx = held_dr = 0; }
}
#endif

/* ================= platform ================= */
#ifdef HEADLESS
static void dump(const char *fn) {
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SW, SH);
    for (int i = 0; i < SW * SH; i++) { putc((fb[i] >> 16) & 255, f); putc((fb[i] >> 8) & 255, f); putc(fb[i] & 255, f); }
    fclose(f);
}
int main(int argc, char **argv) {
    int seed = argc > 1 ? atoi(argv[1]) : 5;
    srand(seed); build_bg(); load_best();
    new_game(); state = S_TITLE; render(0.3f); dump("h_title.ppm");
    state = S_PLAY; god = 1;
    float tm = 0; int shots = 0;
    for (int tick = 0; tick < 60 * 90 && shots < 3; tick++) {
        float dt = 1.0f / 60; tm += dt;
        if (!hopping) {
            if (!try_hop(0, 1)) { if (!try_hop((rand() & 1) ? 1 : -1, 0)) try_hop(0, 1); }
        }
        update(dt);
        if (tick == 60 * 6 || tick == 60 * 14 || tick == 60 * 24) {
            char fn[32]; snprintf(fn, sizeof fn, "h_play%d.ppm", shots++);
            render(tm); dump(fn);
        }
    }
    god = 0; state = S_PLAY; death_type = DIE_CAR;
    state = S_DEAD; death_t = 0.2f; burst(px, 0.5f, -prow, (Col){250,250,250}, 20);
    render(tm); dump("h_dead.ppm");
    state = S_OVER; render(tm); dump("h_over.ppm");
    printf("row %d camRow %.1f\n", prow, camRow);
    return 0;
}
#else
int main(int argc, char **argv) {
    int windowed = argc > 1 && !strcmp(argv[1], "-w");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("Hoppy Road", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1024, 768, windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, SW, SH);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);

    srand((unsigned)SDL_GetTicks());
    build_bg(); load_best();
    new_game(); state = S_TITLE;

    int running = 1, axst[2] = {0, 0};
    float tm = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / freq); last = now;
        if (dt > 0.05f) dt = 0.05f;
        tm += dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_KEYDOWN:
                if (e.key.repeat) break;
                switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_w: set_dir(0, 1, 1); break;
                case SDLK_DOWN: case SDLK_s: set_dir(0, -1, 1); break;
                case SDLK_LEFT: case SDLK_a: set_dir(-1, 0, 1); break;
                case SDLK_RIGHT: case SDLK_d: set_dir(1, 0, 1); break;
                case SDLK_RETURN: case SDLK_SPACE: action(); break;
                case SDLK_p: toggle_pause(); break;
                case SDLK_ESCAPE: running = 0; break;
                }
                break;
            case SDL_KEYUP:
                switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_w: set_dir(0, 1, 0); break;
                case SDLK_DOWN: case SDLK_s: set_dir(0, -1, 0); break;
                case SDLK_LEFT: case SDLK_a: set_dir(-1, 0, 0); break;
                case SDLK_RIGHT: case SDLK_d: set_dir(1, 0, 0); break;
                }
                break;
            case SDL_JOYHATMOTION:
                if (e.jhat.value & SDL_HAT_UP) set_dir(0, 1, 1);
                else if (e.jhat.value & SDL_HAT_DOWN) set_dir(0, -1, 1);
                else if (e.jhat.value & SDL_HAT_LEFT) set_dir(-1, 0, 1);
                else if (e.jhat.value & SDL_HAT_RIGHT) set_dir(1, 0, 1);
                else held_dx = held_dr = 0;
                break;
            case SDL_JOYAXISMOTION:
                if (e.jaxis.axis < 2) {
                    int v = e.jaxis.value > 16000 ? 1 : (e.jaxis.value < -16000 ? -1 : 0);
                    if (v != axst[e.jaxis.axis]) {
                        axst[e.jaxis.axis] = v;
                        if (!v) held_dx = held_dr = 0;
                        else if (e.jaxis.axis == 0) set_dir(v, 0, 1);
                        else set_dir(0, -v, 1);
                    }
                }
                break;
            case SDL_JOYBUTTONDOWN: {
                int b = e.jbutton.button;
                if (b == JB_MENU || b == JB_SELECT) running = 0;
                else if (b == JB_START) toggle_pause();
                else if (b < 4) action();
                break; }
            }
        }
        update(dt);
        render(tm);
        SDL_UpdateTexture(tex, NULL, fb, SW * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(1);
    }
    if (score > best) { best = score; save_best(); }
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif
