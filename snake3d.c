/* Snake 3D - for TrimUI Brick (1024x768, SDL2).
 * Software-rendered perspective 3D: shaded cubes, sphere apple, shadows, particles.
 * Build: see Makefile.  Run windowed on a PC with:  ./snake3d -w
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

#define SW 640            /* internal render size, scaled up to the screen */
#define SH 480
#define GW 20
#define GH 15
#define MAXLEN (GW * GH)
#define WALL_H 0.7f
#define PI_F 3.14159265f
#define MAXP 128

/* ---- gamepad button numbers (SDL joystick API). Tweak if your firmware differs ---- */
#define JB_SELECT 6
#define JB_START  7
#define JB_MENU   8

typedef struct { float r, g, b; } Col;
typedef struct { float x, y; } Pt;
typedef struct { int x, y; } Cell;

static uint32_t fb[SW * SH], bg[SW * SH];

/* ================= pixel + raster helpers ================= */
static uint32_t rgb(float r, float g, float b) {
    int R = (int)(r < 0 ? 0 : r > 255 ? 255 : r);
    int G = (int)(g < 0 ? 0 : g > 255 ? 255 : g);
    int B = (int)(b < 0 ? 0 : b > 255 ? 255 : b);
    return 0xFF000000u | (R << 16) | (G << 8) | B;
}
static uint32_t shade(Col c, float k) { return rgb(c.r * k, c.g * k, c.b * k); }
static Col mix(Col a, Col b, float t) {
    Col c = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    return c;
}

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

/* filled convex polygon, pixel-center sampling */
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

static void draw_line(Pt a, Pt b, uint32_t c, int al) {
    float dx = b.x - a.x, dy = b.y - a.y;
    int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
    if (n > 2000) return;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        blend_px((int)(a.x + dx * t), (int)(a.y + dy * t), c, al);
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

/* ================= camera ================= */
static float camx = 0, camy, camz, camD, sinA, cosA;
static float cam_f = 1, cam_ox = 0, cam_oy = 0, shk_x = 0, shk_y = 0;

static void proj(float x, float y, float z, Pt *o, float *dep) {
    float qx = x - camx, qy = y - camy, qz = z - camz;
    float yc = qy * cosA - qz * sinA;
    float zc = -qy * sinA - qz * cosA;
    if (dep) *dep = zc;
    o->x = cam_ox + shk_x + cam_f * (qx / zc + camx / camD);
    o->y = cam_oy + shk_y - cam_f * yc / zc;
}

static void cam_init(void) {
    float A = 60.0f * PI_F / 180.0f;
    camD = 34.0f;
    sinA = sinf(A); cosA = cosf(A);
    camx = 0; camy = camD * sinA; camz = camD * cosA;
    cam_f = 1; cam_ox = 0; cam_oy = 0; shk_x = shk_y = 0;
    float ex = GW / 2 + 1.0f, ez = GH / 2 + 1.0f;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < 8; i++) {
        float x = (i & 1) ? ex : -ex, z = (i & 2) ? ez : -ez;
        float y = (i & 4) ? WALL_H : 0.0f;
        Pt p; proj(x, y, z, &p, NULL);
        if (p.x < minx) minx = p.x;
        if (p.x > maxx) maxx = p.x;
        if (p.y < miny) miny = p.y;
        if (p.y > maxy) maxy = p.y;
    }
    { Pt p; proj(ex, -0.8f, ez, &p, NULL); if (p.y > maxy) maxy = p.y; }
    float availW = SW - 24, top = 46, availH = SH - 46 - 10;
    float f1 = availW / (maxx - minx), f2 = availH / (maxy - miny);
    cam_f = f1 < f2 ? f1 : f2;
    cam_ox = SW / 2.0f - cam_f * (minx + maxx) / 2.0f;
    cam_oy = (top + availH / 2.0f) - cam_f * (miny + maxy) / 2.0f;
}

/* ================= game state ================= */
enum { S_TITLE, S_PLAY, S_PAUSE, S_OVER };
static int state = S_TITLE;
static Cell body[MAXLEN + 2], prevb[MAXLEN + 2];
static int len, dirx, diry, qn, qdx[2], qdy[2];
static Cell apple;
static float apple_age = 0, acc = 0, shake = 0;
static int score = 0, best = 0;

typedef struct { float x, y, z, vx, vy, vz, life; Col c; } Part;
static Part parts[MAXP];
static int np = 0;

static float rf(void) { return (float)rand() / (float)RAND_MAX; }
static float cell_x(float cx) { return cx - GW / 2.0f + 0.5f; }
static float cell_z(float cy) { return cy - GH / 2.0f + 0.5f; }

static void burst(float x, float y, float z, Col c, int n) {
    for (int i = 0; i < n && np < MAXP; i++) {
        Part *p = &parts[np++];
        float a = rf() * 2 * PI_F, sp = 1.5f + rf() * 4.5f;
        p->x = x; p->y = y; p->z = z;
        p->vx = cosf(a) * sp; p->vz = sinf(a) * sp; p->vy = 3.0f + rf() * 5.0f;
        p->life = 0.6f + rf() * 0.6f;
        p->c = c;
    }
}

static const char *SAVE_FILE = "snake3d.sav";
static void load_best(void) {
    FILE *f = fopen(SAVE_FILE, "r");
    if (f) { if (fscanf(f, "%d", &best) != 1) best = 0; fclose(f); }
}
static void save_best(void) {
    FILE *f = fopen(SAVE_FILE, "w");
    if (f) { fprintf(f, "%d\n", best); fclose(f); }
}

static int occupied(int x, int y) {
    for (int i = 0; i < len; i++) if (body[i].x == x && body[i].y == y) return 1;
    return 0;
}

static void spawn_apple(void) {
    int free_n = 0;
    static Cell fr[GW * GH];
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            if (!occupied(x, y)) { fr[free_n].x = x; fr[free_n].y = y; free_n++; }
    if (!free_n) { state = S_OVER; return; }
    apple = fr[rand() % free_n];
    apple_age = 0;
}

static void new_game(void) {
    len = 4;
    for (int i = 0; i < len; i++) { body[i].x = GW / 2 - i; body[i].y = GH / 2; prevb[i] = body[i]; }
    dirx = 1; diry = 0; qn = 0; score = 0; acc = 0; np = 0; shake = 0;
    spawn_apple();
    state = S_PLAY;
}

static void queue_dir(int dx, int dy) {
    if (state != S_PLAY) return;
    int rx = qn ? qdx[qn - 1] : dirx, ry = qn ? qdy[qn - 1] : diry;
    if ((dx == rx && dy == ry) || (dx == -rx && dy == -ry)) return;
    if (qn < 2) { qdx[qn] = dx; qdy[qn] = dy; qn++; }
}

static float base_interval(void) {
    float iv = 0.17f - 0.004f * (score / 10);
    return iv < 0.075f ? 0.075f : iv;
}

static void die(void) {
    state = S_OVER;
    shake = 0.5f;
    burst(cell_x((float)body[0].x), 0.6f, cell_z((float)body[0].y), (Col){120, 255, 90}, 40);
    if (score > best) { best = score; save_best(); }
}

static void do_step(void) {
    if (qn) { dirx = qdx[0]; diry = qdy[0]; qdx[0] = qdx[1]; qdy[0] = qdy[1]; qn--; }
    Cell nh = { body[0].x + dirx, body[0].y + diry };
    int eat = (nh.x == apple.x && nh.y == apple.y);
    if (nh.x < 0 || nh.x >= GW || nh.y < 0 || nh.y >= GH) { die(); return; }
    int check = eat ? len : len - 1;
    for (int i = 0; i < check; i++)
        if (body[i].x == nh.x && body[i].y == nh.y) { die(); return; }
    for (int i = 0; i < len; i++) prevb[i] = body[i];
    for (int i = len - 1; i > 0; i--) body[i] = body[i - 1];
    body[0] = nh;
    if (eat) {
        body[len] = prevb[len - 1];
        prevb[len] = prevb[len - 1];
        len++;
        score += 10;
        burst(cell_x((float)apple.x), 0.5f, cell_z((float)apple.y), (Col){255, 70, 70}, 28);
        spawn_apple();
    }
}

static void update(float dt, int boost) {
    if (state == S_PLAY) {
        acc += dt * (boost ? 2.0f : 1.0f);
        float iv = base_interval();
        while (acc >= iv && state == S_PLAY) { acc -= iv; do_step(); }
        apple_age += dt;
    }
    if (state != S_PAUSE) {
        for (int i = 0; i < np;) {
            Part *p = &parts[i];
            p->life -= dt;
            p->vy -= 14.0f * dt;
            p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
            if (p->y < 0.05f) { p->y = 0.05f; p->vy *= -0.4f; p->vx *= 0.7f; p->vz *= 0.7f; }
            if (p->life <= 0) parts[i] = parts[--np]; else i++;
        }
        if (shake > 0) { shake -= dt; if (shake < 0) shake = 0; }
    }
}

/* ================= 3D drawing ================= */
static void draw_box(float cx, float cz, float hs, float y0, float y1, Col c) {
    float x0 = cx - hs, x1 = cx + hs, z0 = cz - hs, z1 = cz + hs, ym = (y0 + y1) / 2;
    Pt p[8];
    for (int i = 0; i < 8; i++)
        proj((i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0, &p[i], NULL);
    static const int F[5][4] = { {2,3,7,6}, {4,5,7,6}, {0,4,6,2}, {1,5,7,3}, {0,1,3,2} };
    static const float K[5] = { 1.0f, 0.66f, 0.82f, 0.48f, 0.42f };
    float nrm[5][3] = { {0,1,0}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,0,-1} };
    float ctr[5][3] = { {cx,y1,cz}, {cx,ym,z1}, {x0,ym,cz}, {x1,ym,cz}, {cx,ym,z0} };
    for (int f = 0; f < 5; f++) {
        float d = nrm[f][0] * (camx - ctr[f][0]) + nrm[f][1] * (camy - ctr[f][1]) +
                  nrm[f][2] * (camz - ctr[f][2]);
        if (d <= 0) continue;
        Pt q[4];
        for (int i = 0; i < 4; i++) q[i] = p[F[f][i]];
        fill_poly(q, 4, shade(c, K[f]), 255);
        uint32_t ec = shade(c, K[f] * (f == 0 ? 1.35f : 0.7f));
        for (int i = 0; i < 4; i++) draw_line(q[i], q[(i + 1) % 4], ec, 255);
    }
}

static void floor_quad(float x0, float z0, float x1, float z1, float y, uint32_t c, int a) {
    Pt q[4];
    proj(x0, y, z0, &q[0], NULL); proj(x1, y, z0, &q[1], NULL);
    proj(x1, y, z1, &q[2], NULL); proj(x0, y, z1, &q[3], NULL);
    fill_poly(q, 4, c, a);
}

static void draw_apple(float wx, float wz, float tm) {
    float pop = apple_age < 0.25f ? (apple_age / 0.25f) * (1.2f - 0.2f * (apple_age / 0.25f)) : 1.0f;
    float R = 0.47f * pop, bob = 0.07f * sinf(tm * 4.0f);
    Pt s; float d;
    proj(wx, R + 0.06f + bob, wz, &s, &d);
    float rp = R * cam_f / d;
    if (rp < 1) return;
    const float Lx = -0.45f, Ly = -0.6f, Lz = 0.66f;
    const float Hx = -0.247f, Hy = -0.329f, Hz = 0.911f;
    int xa = (int)floorf(s.x - rp - 1), xb = (int)ceilf(s.x + rp + 1);
    int ya = (int)floorf(s.y - rp - 1), yb = (int)ceilf(s.y + rp + 1);
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float nx = (x + 0.5f - s.x) / rp, ny = (y + 0.5f - s.y) / rp;
            float d2 = nx * nx + ny * ny;
            if (d2 >= 1.0f) continue;
            float nz = sqrtf(1.0f - d2), dist = sqrtf(d2);
            float diff = nx * Lx + ny * Ly + nz * Lz; if (diff < 0) diff = 0;
            float sp = nx * Hx + ny * Hy + nz * Hz; if (sp < 0) sp = 0;
            sp = powf(sp, 28.0f);
            float k = 0.30f + 0.80f * diff;
            float cov = (1.0f - dist) * rp; if (cov > 1) cov = 1;
            blend_px(x, y, rgb(235 * k + 255 * sp * 0.9f, 35 * k + 255 * sp * 0.9f,
                               50 * k + 255 * sp * 0.9f), (int)(cov * 255));
        }
    Pt a = { s.x, s.y - rp * 0.9f }, b = { s.x + rp * 0.18f, s.y - rp * 1.4f };
    draw_line(a, b, rgb(110, 70, 30), 255);
    a.x += 1; b.x += 1;
    draw_line(a, b, rgb(110, 70, 30), 255);
    Pt leaf[3] = { { b.x, b.y + 1 }, { b.x + rp * 0.65f, b.y - rp * 0.2f }, { b.x + rp * 0.4f, b.y + rp * 0.25f } };
    fill_poly(leaf, 3, rgb(90, 210, 80), 255);
}

typedef struct { float d; int kind; float x, z, y1, hs; Col c; } Item;
static int cmp_item(const void *a, const void *b) {
    float da = ((const Item *)a)->d, db = ((const Item *)b)->d;
    return da < db ? 1 : (da > db ? -1 : 0);
}

static void build_bg(void) {
    for (int y = 0; y < SH; y++) {
        float t = (float)y / SH;
        uint32_t c = rgb(12 + 34 * t, 14 + 12 * t, 38 + 36 * t);
        for (int x = 0; x < SW; x++) bg[y * SW + x] = c;
    }
    unsigned s = 12345;
    for (int i = 0; i < 70; i++) {
        s = s * 1103515245u + 12345u; int x = (s >> 8) % SW;
        s = s * 1103515245u + 12345u; int y = (s >> 8) % (SH / 2);
        bg[y * SW + x] = rgb(160, 170, 220);
    }
}

static void render(float tm) {
    memcpy(fb, bg, sizeof(fb));
    camx = 0.8f * sinf(tm * 0.5f);
    if (shake > 0) { shk_x = (rf() - 0.5f) * shake * 30; shk_y = (rf() - 0.5f) * shake * 30; }
    else shk_x = shk_y = 0;

    float t = (state == S_OVER || state == S_TITLE) ? 1.0f : fminf(acc / base_interval(), 1.0f);
    float ex = GW / 2 + 1.0f, ez = GH / 2 + 1.0f;

    /* board slab front */
    { Pt q[4];
      proj(-ex, 0, ez, &q[0], NULL); proj(ex, 0, ez, &q[1], NULL);
      proj(ex, -0.8f, ez, &q[2], NULL); proj(-ex, -0.8f, ez, &q[3], NULL);
      fill_poly(q, 4, rgb(28, 22, 60), 255); }

    /* floor */
    for (int gy = 0; gy < GH; gy++)
        for (int gx = 0; gx < GW; gx++) {
            Col c = ((gx + gy) & 1) ? (Col){30, 50, 84} : (Col){38, 62, 100};
            floor_quad(gx - GW / 2.0f, gy - GH / 2.0f, gx - GW / 2.0f + 1, gy - GH / 2.0f + 1,
                       0, shade(c, 1.0f), 255);
        }

    /* snake positions (interpolated) */
    static float sx[MAXLEN + 2], sz[MAXLEN + 2];
    for (int i = 0; i < len; i++) {
        float gx = prevb[i].x + (body[i].x - prevb[i].x) * t;
        float gy = prevb[i].y + (body[i].y - prevb[i].y) * t;
        sx[i] = cell_x(gx); sz[i] = cell_z(gy);
    }

    /* shadows */
    for (int i = 0; i < len; i++) {
        float hs = i == 0 ? 0.46f : 0.41f;
        floor_quad(sx[i] - hs + 0.16f, sz[i] - hs - 0.12f, sx[i] + hs + 0.16f, sz[i] + hs - 0.12f,
                   0.004f, rgb(0, 0, 10), 110);
    }
    { float ax = cell_x((float)apple.x), az = cell_z((float)apple.y);
      Pt q[12];
      for (int i = 0; i < 12; i++) {
          float a = i * 2 * PI_F / 12;
          proj(ax + 0.14f + cosf(a) * 0.36f, 0.004f, az - 0.1f + sinf(a) * 0.36f, &q[i], NULL);
      }
      fill_poly(q, 12, rgb(0, 0, 10), 110); }

    /* collect + sort solid items */
    static Item items[MAXLEN + 128];
    int n = 0;
    for (int gy = -1; gy <= GH; gy++)
        for (int gx = -1; gx <= GW; gx++) {
            if (gx >= 0 && gx < GW && gy >= 0 && gy < GH) continue;
            Item *it = &items[n++];
            it->kind = 0; it->x = cell_x((float)gx); it->z = cell_z((float)gy);
            it->y1 = WALL_H; it->hs = 0.5f;
            it->c = ((gx + gy) & 1) ? (Col){96, 78, 170} : (Col){110, 90, 190};
            Pt p; proj(it->x, WALL_H / 2, it->z, &p, &it->d);
        }
    for (int i = len - 1; i >= 0; i--) {
        Item *it = &items[n++];
        float k = (float)i / (float)(len > 1 ? len : 1);
        it->kind = i == 0 ? 2 : 0;
        it->x = sx[i]; it->z = sz[i];
        it->hs = i == 0 ? 0.46f : 0.41f;
        it->y1 = (i == 0 ? 0.95f : 0.80f) + 0.05f * sinf(tm * 7.0f - i * 0.6f);
        Col c = i == 0 ? (Col){150, 255, 100} : mix((Col){70, 230, 110}, (Col){30, 150, 215}, k);
        if (i > 0 && (i & 1)) c = (Col){c.r * 0.9f, c.g * 0.9f, c.b * 0.9f};
        if (state == S_OVER) c = mix(c, (Col){255, 60, 60}, 0.45f);
        it->c = c;
        Pt p; proj(it->x, it->y1 / 2, it->z, &p, &it->d);
    }
    { Item *it = &items[n++]; it->kind = 1;
      it->x = cell_x((float)apple.x); it->z = cell_z((float)apple.y);
      Pt p; proj(it->x, 0.4f, it->z, &p, &it->d); }
    qsort(items, n, sizeof(Item), cmp_item);

    for (int i = 0; i < n; i++) {
        Item *it = &items[i];
        if (it->kind == 1) { draw_apple(it->x, it->z, tm); continue; }
        draw_box(it->x, it->z, it->hs, 0, it->y1, it->c);
        if (it->kind == 2) {
            float dx = (float)dirx, dz = (float)diry, px = -dz, pz = dx;
            for (int s = -1; s <= 1; s += 2) {
                float ecx = it->x + dx * 0.22f + px * 0.24f * s;
                float ecz = it->z + dz * 0.22f + pz * 0.24f * s;
                float yy = it->y1 + 0.004f;
                floor_quad(ecx - 0.14f, ecz - 0.14f, ecx + 0.14f, ecz + 0.14f, yy, rgb(250, 250, 250), 255);
                floor_quad(ecx + dx * 0.05f - 0.07f, ecz + dz * 0.05f - 0.07f,
                           ecx + dx * 0.05f + 0.07f, ecz + dz * 0.05f + 0.07f, yy + 0.002f, rgb(10, 10, 20), 255);
            }
        }
    }

    /* particles */
    for (int i = 0; i < np; i++) {
        Part *p = &parts[i];
        Pt s; float d; proj(p->x, p->y, p->z, &s, &d);
        float sz2 = 0.2f * cam_f / d; if (sz2 < 2) sz2 = 2;
        int a = (int)(p->life * 420); if (a > 255) a = 255;
        rect((int)(s.x - sz2 / 2), (int)(s.y - sz2 / 2), (int)sz2, (int)sz2, rgb(p->c.r, p->c.g, p->c.b), a);
    }

    /* HUD */
    { char buf[32];
      draw_text(14, 12, 3, "SCORE", rgb(150, 170, 255));
      snprintf(buf, sizeof buf, "%d", score);
      text_shadow(14 + 6 * 3 * 6, 12, 3, buf, rgb(255, 255, 255));
      char b2[32]; snprintf(b2, sizeof b2, "BEST %d", best);
      int w = text_w(b2, 3);
      text_shadow(SW - 14 - w, 12, 3, b2, rgb(255, 215, 90)); }

    /* overlays */
    if (state == S_TITLE) {
        rect(0, 0, SW, SH, rgb(0, 0, 0), 120);
        text_center(150, 9, "SNAKE 3D", rgb(120, 255, 130));
        if (((int)(tm * 2)) & 1) text_center(270, 3, "PRESS A TO START", rgb(255, 255, 255));
        text_center(400, 2, "DPAD MOVE  A HOLD BOOST", rgb(170, 180, 230));
        text_center(425, 2, "START PAUSE  MENU QUIT", rgb(170, 180, 230));
    } else if (state == S_PAUSE) {
        rect(0, 0, SW, SH, rgb(0, 0, 0), 130);
        text_center(200, 7, "PAUSED", rgb(255, 255, 255));
        text_center(290, 3, "PRESS START", rgb(170, 180, 230));
    } else if (state == S_OVER) {
        rect(0, 0, SW, SH, rgb(0, 0, 0), 110);
        text_center(160, 7, "GAME OVER", rgb(255, 90, 90));
        char b[32]; snprintf(b, sizeof b, "SCORE %d", score);
        text_center(250, 4, b, rgb(255, 255, 255));
        if (score > 0 && score >= best) text_center(295, 3, "NEW BEST!", rgb(255, 215, 90));
        if (((int)(tm * 2)) & 1) text_center(340, 3, "PRESS A TO RETRY", rgb(200, 210, 255));
    }
}

static void action(void) {
    if (state == S_TITLE || state == S_OVER) new_game();
    else if (state == S_PAUSE) state = S_PLAY;
}
#ifndef HEADLESS
static void toggle_pause(void) {
    if (state == S_PLAY) state = S_PAUSE;
    else action();
}
#endif

/* ================= platform ================= */
#ifdef HEADLESS
/* Test harness: renders screenshots without SDL. */
static void dump(const char *fn) {
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SW, SH);
    for (int i = 0; i < SW * SH; i++) { putc((fb[i] >> 16) & 255, f); putc((fb[i] >> 8) & 255, f); putc(fb[i] & 255, f); }
    fclose(f);
}
static int blocked(int x, int y) { return x < 0 || y < 0 || x >= GW || y >= GH || occupied(x, y); }
int main(void) {
    srand(7); build_bg(); cam_init(); load_best();
    new_game(); state = S_TITLE; render(0.3f); dump("title.ppm");
    state = S_PLAY;
    int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
    for (int k = 0; k < 600 && len < 16; k++) {
        int bi = -1, bd = 1 << 30;
        for (int d = 0; d < 4; d++) {
            if (dx[d] == -dirx && dy[d] == -diry) continue;
            int nx = body[0].x + dx[d], ny = body[0].y + dy[d];
            if (blocked(nx, ny)) continue;
            int dist = abs(nx - apple.x) + abs(ny - apple.y);
            if (dist < bd) { bd = dist; bi = d; }
        }
        if (bi >= 0) queue_dir(dx[bi], dy[bi]);
        do_step(); if (state != S_PLAY) break;
    }
    acc = base_interval() * 0.4f; apple_age = 1; np = 0; render(2.0f); dump("play.ppm");
    burst(0, 1, 0, (Col){255, 70, 70}, 30); update(0.1f, 0); render(2.1f); dump("burst.ppm");
    state = S_OVER; render(2.2f); dump("over.ppm");
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
    SDL_Joystick *joys[8]; int nj = 0;
    for (int i = 0; i < SDL_NumJoysticks() && nj < 8; i++) {
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (j) joys[nj++] = j;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("Snake 3D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1024, 768, windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, SW, SH);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);

    srand((unsigned)SDL_GetTicks());
    build_bg(); cam_init(); load_best();
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
                case SDLK_UP: case SDLK_w: queue_dir(0, -1); break;
                case SDLK_DOWN: case SDLK_s: queue_dir(0, 1); break;
                case SDLK_LEFT: case SDLK_a: queue_dir(-1, 0); break;
                case SDLK_RIGHT: case SDLK_d: queue_dir(1, 0); break;
                case SDLK_RETURN: case SDLK_SPACE: action(); break;
                case SDLK_p: toggle_pause(); break;
                case SDLK_ESCAPE: running = 0; break;
                }
                break;
            case SDL_JOYHATMOTION:
                if (e.jhat.value & SDL_HAT_UP) queue_dir(0, -1);
                else if (e.jhat.value & SDL_HAT_DOWN) queue_dir(0, 1);
                else if (e.jhat.value & SDL_HAT_LEFT) queue_dir(-1, 0);
                else if (e.jhat.value & SDL_HAT_RIGHT) queue_dir(1, 0);
                break;
            case SDL_JOYAXISMOTION:
                if (e.jaxis.axis < 2) {
                    int v = e.jaxis.value > 16000 ? 1 : (e.jaxis.value < -16000 ? -1 : 0);
                    if (v != axst[e.jaxis.axis]) {
                        axst[e.jaxis.axis] = v;
                        if (v) { if (e.jaxis.axis == 0) queue_dir(v, 0); else queue_dir(0, v); }
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

        int boost = 0;
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_SPACE]) boost = 1;
        for (int i = 0; i < nj; i++)
            for (int b = 0; b < 4; b++) if (SDL_JoystickGetButton(joys[i], b)) boost = 1;

        update(dt, boost);
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
