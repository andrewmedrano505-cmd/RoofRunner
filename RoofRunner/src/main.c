/*
 * ROOF RUNNER  -  a Mirror's Edge style first-person parkour game for PS Vita
 * Built with VitaSDK + vitaGL (fixed-function OpenGL).
 *
 * Controls
 *   Left stick   move            Right stick  look
 *   R / Cross    jump (hold in the air to grab ledges and to wall-run)
 *   L            slide (while running) / crouch / roll when landing from a height
 *   Triangle     wall-jump (while wall-running)
 *   Start        restart level   Select  respawn at checkpoint
 *   Start+Select quit
 *
 * Follow the RED objects ("runner vision") to the finish gate.
 */
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SCR_W 960
#define SCR_H 544
#define HW 0.30f          /* player half width            */
#define EPS 0.001f
#define STAND_H 1.8f
#define LOW_H 0.9f
#define GRAV 20.0f
#define MAXB 320
#define PI_F 3.14159265f
#define SIM_DT (1.0f / 120.0f)

#define HAZE_R 0.80f
#define HAZE_G 0.89f
#define HAZE_B 0.96f

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static uint32_t rs = 777;
static float frand(void) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.0f; }

/* ------------------------------------------------------------------ */
/* world                                                               */
/* ------------------------------------------------------------------ */
enum { F_SOLID = 1, F_RED = 2, F_DECOR = 4 };
typedef struct { float x0, y0, z0, x1, y1, z1, r, g, b; int flags; int chk; } Box;
static Box boxes[MAXB];
static int nbox;

typedef struct { float x, y, z; int active; } Check;
static Check cps[8];
static int ncp, curCp;

static int addBox(float x0, float y0, float z0, float x1, float y1, float z1,
                  float r, float g, float b, int flags)
{
    if (nbox >= MAXB) return -1;
    Box *o = &boxes[nbox];
    o->x0 = fminf(x0, x1); o->x1 = fmaxf(x0, x1);
    o->y0 = fminf(y0, y1); o->y1 = fmaxf(y0, y1);
    o->z0 = fminf(z0, z1); o->z1 = fmaxf(z0, z1);
    o->r = r; o->g = g; o->b = b; o->flags = flags; o->chk = 0;
    return nbox++;
}

/* rooftop slab spanning x -6..6, z0 (near) to z1 (far), surface at 'top' */
static void roof(float z0, float z1, float top, int red)
{
    float r = 0.93f, g = 0.94f, b = 0.95f;
    switch (nbox % 4) {
        case 1: r = 0.88f; g = 0.90f; b = 0.93f; break;
        case 2: r = 0.95f; g = 0.92f; b = 0.86f; break;
        case 3: r = 0.85f; g = 0.89f; b = 0.92f; break;
    }
    int fl = F_SOLID;
    if (red) { r = 0.92f; g = 0.13f; b = 0.10f; fl |= F_RED; }
    addBox(-6, -60, z1, 6, top, z0, r, g, b, fl);
    addBox(-6.0f, top, z1, -5.75f, top + 0.04f, z0, 0.42f, 0.45f, 0.50f, F_DECOR);
    addBox(5.75f, top, z1, 6.0f, top + 0.04f, z0, 0.42f, 0.45f, 0.50f, F_DECOR);
}

static void prop(float x0, float z0, float x1, float z1, float base, float h)
{
    addBox(x0, base, z0, x1, base + h, z1, 0.92f, 0.13f, 0.10f, F_SOLID | F_RED);
}

/* overhead bar that must be slid under (bottom 1.0m above 'base') */
static void slideBar(float z, float base)
{
    prop(-5.5f, z - 0.6f, 5.5f, z, base + 1.0f, 0.7f);
    prop(-6.0f, z - 0.6f, -5.5f, z, base, 1.7f);
    prop(5.5f, z - 0.6f, 6.0f, z, base, 1.7f);
}

static void addCheck(float x, float y, float z)
{
    if (ncp >= 8) return;
    Check *c = &cps[ncp++];
    c->x = x; c->y = y; c->z = z; c->active = (ncp == 1);
    int a = addBox(x - 5.6f, y, z - 0.15f, x - 5.3f, y + 3.2f, z + 0.15f, 0.2f, 0.55f, 0.95f, F_DECOR);
    int b = addBox(x + 5.3f, y, z - 0.15f, x + 5.6f, y + 3.2f, z + 0.15f, 0.2f, 0.55f, 0.95f, F_DECOR);
    if (a >= 0) boxes[a].chk = ncp;
    if (b >= 0) boxes[b].chk = ncp;
}

static void buildLevel(void)
{
    nbox = 0; ncp = 0;
    rs = 777;

    /* far-below ground haze plane */
    addBox(-3000, -400, -3000, 3000, -150, 3000, 0.66f, 0.72f, 0.78f, F_DECOR);

    /* 1. start roof: two vault barriers */
    roof(6, -40, 0, 0);
    prop(-6, -14.6f, 6, -14.0f, 0, 1.0f);
    prop(-3, -27.0f, 3, -26.4f, 0, 1.1f);
    addCheck(0, 0, 3);

    /* 2. gap jump, slide bar */
    roof(-45, -70, 0, 0);
    slideBar(-58, 0);
    addCheck(0, 0, -50);

    /* 3. wall-run across a 12m gap */
    addBox(-20, -80, -82, -3, 10, -70, 0.92f, 0.13f, 0.10f, F_SOLID | F_RED);

    /* 4. stair of vaults (1.1m each) */
    roof(-82, -92, 0, 0);
    roof(-92, -95, 1.1f, 1);
    roof(-95, -98, 2.2f, 1);
    roof(-98, -126, 3.3f, 0);
    slideBar(-112, 3.3f);

    /* 5. gap, high roof, then a big drop (roll!) */
    roof(-131, -150, 3.3f, 0);
    addCheck(0, 3.3f, -137);

    roof(-150, -200, -5.0f, 0);
    addCheck(0, -5.0f, -156);
    prop(-6, -165.6f, 6, -165.0f, -5.0f, 1.0f);
    prop(-6, -175.6f, 1, -175.0f, -5.0f, 1.0f);
    prop(-1, -183.6f, 6, -183.0f, -5.0f, 1.1f);
    slideBar(-190, -5.0f);
    addCheck(0, -5.0f, -194);

    /* 6. canyon: wall-run, wall-jump across, wall-run again */
    addBox(-20, -80, -212, -3, 12, -200, 0.92f, 0.13f, 0.10f, F_SOLID | F_RED);
    addBox(3, -80, -224, 20, 12, -208, 0.92f, 0.13f, 0.10f, F_SOLID | F_RED);

    /* 7. finish roof and gate */
    roof(-224, -252, -5.0f, 0);
    addBox(-6, -5, -244.8f, -5, 3.5f, -244.0f, 0.92f, 0.13f, 0.10f, F_DECOR | F_RED);
    addBox(5, -5, -244.8f, 6, 3.5f, -244.0f, 0.92f, 0.13f, 0.10f, F_DECOR | F_RED);
    addBox(-6, 3.5f, -244.8f, 6, 4.2f, -244.0f, 0.92f, 0.13f, 0.10f, F_DECOR | F_RED);

    /* background towers */
    static const float pal[5][3] = {
        {0.72f, 0.82f, 0.92f}, {0.90f, 0.92f, 0.94f}, {0.60f, 0.72f, 0.86f},
        {0.93f, 0.89f, 0.82f}, {0.78f, 0.84f, 0.88f}};
    for (float z = 60; z > -330; z -= 13.0f) {
        for (int s = -1; s <= 1; s += 2) {
            float w = 9 + frand() * 9, d = 9 + frand() * 8;
            float cx = s * (34 + frand() * 45), top = -25 + frand() * 95;
            const float *c = pal[(int)(frand() * 4.99f)];
            addBox(cx - w / 2, -200, z - d / 2, cx + w / 2, top, z + d / 2, c[0], c[1], c[2], F_DECOR);
        }
    }
}

/* ------------------------------------------------------------------ */
/* static mesh                                                         */
/* ------------------------------------------------------------------ */
static float mv[MAXB * 30 * 3];
static uint8_t mc[MAXB * 30 * 4];
static int nv;

static void pv(float x, float y, float z, float r, float g, float b)
{
    float k = 0.55f + 0.45f * clampf((y + 30.0f) / 30.0f, 0, 1);   /* darker toward the street */
    int i = nv++;
    mv[i * 3] = x; mv[i * 3 + 1] = y; mv[i * 3 + 2] = z;
    mc[i * 4]     = (uint8_t)(clampf(r * k, 0, 1) * 255);
    mc[i * 4 + 1] = (uint8_t)(clampf(g * k, 0, 1) * 255);
    mc[i * 4 + 2] = (uint8_t)(clampf(b * k, 0, 1) * 255);
    mc[i * 4 + 3] = 255;
}

static void quadv(const float p[4][3], float sh, float r, float g, float b)
{
    r *= sh; g *= sh; b *= sh;
    pv(p[0][0], p[0][1], p[0][2], r, g, b);
    pv(p[1][0], p[1][1], p[1][2], r, g, b);
    pv(p[2][0], p[2][1], p[2][2], r, g, b);
    pv(p[0][0], p[0][1], p[0][2], r, g, b);
    pv(p[2][0], p[2][1], p[2][2], r, g, b);
    pv(p[3][0], p[3][1], p[3][2], r, g, b);
}

static void buildMesh(void)
{
    nv = 0;
    for (int i = 0; i < nbox; i++) {
        const Box *bx = &boxes[i];
        float x0 = bx->x0, y0 = bx->y0, z0 = bx->z0, x1 = bx->x1, y1 = bx->y1, z1 = bx->z1;
        float r = bx->r, g = bx->g, b = bx->b;
        if (bx->chk && cps[bx->chk - 1].active) { r = 0.15f; g = 0.85f; b = 0.40f; }
        float fpx[4][3] = {{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}};
        float fnx[4][3] = {{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}};
        float fpz[4][3] = {{x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}};
        float fnz[4][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}};
        float fpy[4][3] = {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}};
        quadv(fpx, 0.80f, r, g, b);
        quadv(fnx, 0.72f, r, g, b);
        quadv(fpz, 0.90f, r, g, b);
        quadv(fnz, 0.68f, r, g, b);
        quadv(fpy, 1.00f, r, g, b);
    }
}

/* ------------------------------------------------------------------ */
/* player                                                              */
/* ------------------------------------------------------------------ */
static struct {
    float x, y, z, vx, vy, vz, yaw, pitch, h;
    int grounded, sliding, rolling, wallrun, mantling;
    float speedCap, coyote, jumpBuf, slideBuf, wjBuf, wallGrace, wallCool;
    float slideT, rollT, stumbleT, dipT, dipDur, wallT, contactT;
    float wnx, wnz, wdx, wdz, wspeed;
    float mfx, mfy, mfz, mtx, mty, mtz, mt, mdur, mexit, mdx, mdz;
} P;

static struct { float f, r; int jumpHeld, slideHeld; } In;

static float camEye = 1.65f, camRoll = 0, camFov = 68, bobPhase = 0, bobAmp = 0;
static float gameTime, finalTime, bestTime, deathFlash, cpFlash, finBlink;
static int running, finished, deaths, newBest;

/* collision bookkeeping for one physics step */
static int sideHit, hbox, landed, wasGround;
static float hnx, hnz, landImpact;

static int hitBox(float x, float y, float z, float h)
{
    for (int i = 0; i < nbox; i++) {
        const Box *b = &boxes[i];
        if (!(b->flags & F_SOLID)) continue;
        if (x + HW - EPS > b->x0 && x - HW + EPS < b->x1 &&
            y + h - EPS > b->y0 && y + EPS < b->y1 &&
            z + HW - EPS > b->z0 && z - HW + EPS < b->z1)
            return i;
    }
    return -1;
}

static void moveAxis(int axis, float d)
{
    if (d == 0.0f) return;
    float *p = axis == 0 ? &P.x : (axis == 1 ? &P.y : &P.z);
    *p += d;
    for (int guard = 0; guard < 4; guard++) {
        int i = hitBox(P.x, P.y, P.z, P.h);
        if (i < 0) break;
        const Box *b = &boxes[i];
        if (axis == 1) {
            if (d < 0) {
                P.y = b->y1; landed = 1;
                if (!wasGround) landImpact = -P.vy;
                P.vy = 0;
            } else {
                P.y = b->y0 - P.h;
                if (P.vy > 0) P.vy = 0;
            }
        } else if (axis == 0) {
            if (d > 0) { P.x = b->x0 - HW; hnx = -1; } else { P.x = b->x1 + HW; hnx = 1; }
            hnz = 0; sideHit = 1; hbox = i; P.vx = 0;
        } else {
            if (d > 0) { P.z = b->z0 - HW; hnz = -1; } else { P.z = b->z1 + HW; hnz = 1; }
            hnx = 0; sideHit = 1; hbox = i; P.vz = 0;
        }
    }
}

static void respawn(void)
{
    Check *c = &cps[curCp];
    memset(&P, 0, sizeof(P));
    P.x = c->x; P.y = c->y; P.z = c->z;
    P.yaw = 0; P.pitch = 0; P.h = STAND_H; P.speedCap = 4.5f;
    camRoll = 0; camEye = 1.65f;
}

static void die(void)
{
    deaths++;
    respawn();
    deathFlash = 0.7f;
}

static void resetGame(void)
{
    for (int i = 0; i < ncp; i++) cps[i].active = (i == 0);
    curCp = 0; running = 0; finished = 0; deaths = 0; gameTime = 0; newBest = 0;
    respawn();
    buildMesh();
}

/* --- parkour moves ------------------------------------------------- */
static int startMantle(const Box *b, float rel, float pre)
{
    float tx = P.x - hnx * 0.7f, tz = P.z - hnz * 0.7f;
    float lo = b->x0 + HW + 0.05f, hi = b->x1 - HW - 0.05f;
    tx = lo <= hi ? clampf(tx, lo, hi) : (b->x0 + b->x1) * 0.5f;
    lo = b->z0 + HW + 0.05f; hi = b->z1 - HW - 0.05f;
    tz = lo <= hi ? clampf(tz, lo, hi) : (b->z0 + b->z1) * 0.5f;
    float ty = b->y1;
    if (hitBox(tx, ty, tz, STAND_H) >= 0) return 0;
    P.mantling = 1; P.mt = 0;
    P.mdur = rel < 0.9f ? 0.28f : 0.42f;
    P.mfx = P.x; P.mfy = P.y; P.mfz = P.z;
    P.mtx = tx; P.mty = ty; P.mtz = tz;
    P.mexit = fmaxf(pre * 0.9f, 3.5f);
    P.mdx = -hnx; P.mdz = -hnz;
    P.sliding = 0; P.rolling = 0; P.wallrun = 0;
    P.vx = P.vy = P.vz = 0;
    return 1;
}

static void updateMantle(float dt)
{
    P.mt += dt;
    float u = clampf(P.mt / P.mdur, 0, 1);
    float e = u * u * (3 - 2 * u);
    P.x = lerpf(P.mfx, P.mtx, e);
    P.z = lerpf(P.mfz, P.mtz, e);
    P.y = lerpf(P.mfy, P.mty, e) + sinf(PI_F * u) * 0.18f;
    if (u >= 1.0f) {
        P.x = P.mtx; P.y = P.mty; P.z = P.mtz;
        P.mantling = 0; P.h = STAND_H;
        P.vx = P.mdx * P.mexit; P.vz = P.mdz * P.mexit; P.vy = 0;
        P.speedCap = clampf(fmaxf(P.speedCap, P.mexit), 4.5f, 10.0f);
    }
}

static void endWall(void)
{
    P.wallrun = 0; P.wallGrace = 0.3f; P.wallCool = 0.3f;
    P.vx = P.wdx * P.wspeed; P.vz = P.wdz * P.wspeed;
}

static void wallJump(void)
{
    float sp = P.wspeed;
    P.vx = P.wdx * sp + P.wnx * 5.0f;
    P.vz = P.wdz * sp + P.wnz * 5.0f;
    P.vy = 6.8f;
    P.wallrun = 0; P.wallGrace = 0; P.wallCool = 0.35f;
    P.jumpBuf = 0; P.wjBuf = 0; P.grounded = 0;
}

static void onSideHit(float pvx, float pvz, float pre)
{
    const Box *b = &boxes[hbox];
    float rel = b->y1 - P.y;
    float into = pre > 0.01f ? -(pvx * hnx + pvz * hnz) / pre : 0;

    if (P.wallrun) {
        if (hnx * P.wnx + hnz * P.wnz > 0.5f) P.contactT = 0;
        return;
    }
    /* vault / ledge grab */
    float maxRel = P.grounded ? 1.25f : 1.75f;
    if (rel > 0.25f && rel <= maxRel && into > 0.3f &&
        ((P.grounded && pre >= 3.0f) || In.jumpHeld)) {
        if (startMantle(b, rel, pre)) return;
    }
    /* wall-run */
    if (!P.grounded && In.jumpHeld && P.wallCool <= 0 && pre >= 4.5f &&
        fabsf(into) < 0.75f && rel > 1.9f && b->y0 < P.y) {
        float d = pvx * hnx + pvz * hnz;
        float tx = pvx - d * hnx, tz = pvz - d * hnz;
        float tl = hypotf(tx, tz);
        if (tl < 4.0f) return;
        P.wallrun = 1; P.wallT = 0; P.contactT = 0;
        P.wnx = hnx; P.wnz = hnz;
        P.wdx = tx / tl; P.wdz = tz / tl;
        P.wspeed = clampf(tl, 6.5f, 10.0f);
        P.vy = fmaxf(P.vy, 2.0f);
        P.sliding = 0;
    }
}

static int onLand(float impact)
{
    float hs = hypotf(P.vx, P.vz);
    if (impact > 9.0f && P.slideBuf > 0 && impact < 27.0f) {
        P.rolling = 1; P.rollT = 0.5f; P.slideBuf = 0; P.sliding = 0;
        P.vx *= 0.93f; P.vz *= 0.93f;
    } else if (impact > 21.0f) {
        die();
        return 1;
    } else if (impact > 11.0f) {
        P.stumbleT = 0.45f; P.dipT = 0.45f; P.dipDur = 0.45f;
        P.vx *= 0.3f; P.vz *= 0.3f; P.speedCap = 4.5f;
        return 0;
    } else if (impact > 5.0f) {
        P.dipT = 0.18f; P.dipDur = 0.18f;
    }
    P.speedCap = clampf(fmaxf(P.speedCap, hs), 4.5f, 10.0f);
    return 0;
}

static void approach2(float *vx, float *vz, float tx, float tz, float maxd)
{
    float dx = tx - *vx, dz = tz - *vz, l = sqrtf(dx * dx + dz * dz);
    if (l <= maxd || l < 1e-5f) { *vx = tx; *vz = tz; }
    else { *vx += dx / l * maxd; *vz += dz / l * maxd; }
}

/* ------------------------------------------------------------------ */
/* one fixed physics step                                              */
/* ------------------------------------------------------------------ */
static void step(float dt)
{
    if (P.mantling) { updateMantle(dt); return; }

    P.coyote -= dt; P.jumpBuf -= dt; P.slideBuf -= dt; P.wjBuf -= dt;
    P.wallGrace -= dt; P.wallCool -= dt; P.stumbleT -= dt; P.dipT -= dt;
    if (P.grounded) P.coyote = 0.12f;

    float s = sinf(P.yaw), c = cosf(P.yaw);
    float mx = -s * In.f + c * In.r, mz = -c * In.f - s * In.r;
    float mag = hypotf(mx, mz);
    if (mag > 1.0f) { mx /= mag; mz /= mag; mag = 1.0f; }
    float hs = hypotf(P.vx, P.vz);

    /* slide start */
    if (P.slideBuf > 0 && P.grounded && !P.sliding && !P.rolling && hs > 4.0f) {
        P.sliding = 1; P.slideT = 0.95f; P.slideBuf = 0;
        float k = fminf(1.1f, 10.5f / hs);
        P.vx *= k; P.vz *= k; hs *= k;
    }
    if (P.rolling) { P.rollT -= dt; if (P.rollT <= 0) P.rolling = 0; }
    if (P.sliding) {
        float nh = fmaxf(0, hs - 3.2f * dt);
        if (hs > 0.01f) { P.vx *= nh / hs; P.vz *= nh / hs; }
        P.slideT -= dt;
        if (!P.grounded || P.slideT <= 0 || hs < 1.5f || (!In.slideHeld && P.slideT < 0.65f))
            P.sliding = 0;
    }

    /* locomotion */
    if (!P.sliding && !P.rolling && !P.wallrun) {
        if (P.grounded) {
            float cap = P.speedCap;
            float tx = mx * cap, tz = mz * cap;
            if (In.slideHeld && hs < 4.0f) { tx *= 0.4f; tz *= 0.4f; }
            if (P.stumbleT > 0) { tx *= 0.6f; tz *= 0.6f; }
            approach2(&P.vx, &P.vz, tx, tz, (mag > 0.1f ? 26.0f : 34.0f) * dt);
            if (mag > 0.6f && P.stumbleT <= 0) P.speedCap = fminf(9.2f, P.speedCap + 2.4f * dt);
            else if (mag < 0.2f) P.speedCap = fmaxf(4.5f, P.speedCap - 10.0f * dt);
        } else {
            float sp0 = hypotf(P.vx, P.vz);
            P.vx += mx * 9.0f * dt; P.vz += mz * 9.0f * dt;
            float sp1 = hypotf(P.vx, P.vz), lim = fmaxf(sp0, 5.5f);
            if (sp1 > lim) { P.vx *= lim / sp1; P.vz *= lim / sp1; }
        }
    }

    /* jumping */
    if (P.jumpBuf > 0) {
        if (!P.grounded && P.wallGrace > 0 && !P.wallrun) {
            wallJump();
        } else if (P.coyote > 0 && !P.wallrun) {
            P.vy = 6.6f; P.coyote = 0; P.jumpBuf = 0; P.grounded = 0; P.sliding = 0;
        }
    }
    if (P.wallrun && P.wjBuf > 0) wallJump();

    /* vertical forces */
    if (P.wallrun) {
        P.wallT += dt;
        P.vx = P.wdx * P.wspeed - P.wnx * 1.2f;
        P.vz = P.wdz * P.wspeed - P.wnz * 1.2f;
        P.vy -= 1.8f * dt;
    } else {
        P.vy -= GRAV * dt;
        if (P.vy < -45.0f) P.vy = -45.0f;
    }

    /* body height */
    float wantH = (P.sliding || P.rolling || (In.slideHeld && P.grounded && hs < 4.0f)) ? LOW_H : STAND_H;
    if (wantH > P.h) { if (hitBox(P.x, P.y, P.z, wantH) < 0) P.h = wantH; }
    else P.h = wantH;

    /* integrate */
    float pvx = P.vx, pvz = P.vz, pre = hypotf(pvx, pvz);
    sideHit = 0; landed = 0; landImpact = 0; wasGround = P.grounded;
    moveAxis(0, P.vx * dt);
    moveAxis(2, P.vz * dt);
    moveAxis(1, P.vy * dt);
    P.grounded = landed;

    if (landed && !wasGround) {
        if (onLand(landImpact)) return;
    }
    if (sideHit) onSideHit(pvx, pvz, pre);

    if (P.wallrun) {
        if (!sideHit || hnx * P.wnx + hnz * P.wnz < 0.5f) P.contactT += dt;
        if (!In.jumpHeld || P.wallT > 2.0f || P.contactT > 0.12f || P.grounded) endWall();
    }

    if (P.y < cps[curCp].y - 28.0f) die();
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */
/* ------------------------------------------------------------------ */
static const char *glyph(char c)
{
    switch (c) {
    case '0': return "XXX" "X.X" "X.X" "X.X" "XXX";
    case '1': return ".X." "XX." ".X." ".X." "XXX";
    case '2': return "XXX" "..X" "XXX" "X.." "XXX";
    case '3': return "XXX" "..X" "XXX" "..X" "XXX";
    case '4': return "X.X" "X.X" "XXX" "..X" "..X";
    case '5': return "XXX" "X.." "XXX" "..X" "XXX";
    case '6': return "XXX" "X.." "XXX" "X.X" "XXX";
    case '7': return "XXX" "..X" "..X" "..X" "..X";
    case '8': return "XXX" "X.X" "XXX" "X.X" "XXX";
    case '9': return "XXX" "X.X" "XXX" "..X" "XXX";
    case 'A': return ".X." "X.X" "XXX" "X.X" "X.X";
    case 'B': return "XX." "X.X" "XX." "X.X" "XX.";
    case 'D': return "XX." "X.X" "X.X" "X.X" "XX.";
    case 'E': return "XXX" "X.." "XX." "X.." "XXX";
    case 'F': return "XXX" "X.." "XX." "X.." "X..";
    case 'H': return "X.X" "X.X" "XXX" "X.X" "X.X";
    case 'I': return "XXX" ".X." ".X." ".X." "XXX";
    case 'M': return "X.X" "XXX" "XXX" "X.X" "X.X";
    case 'N': return "XX." "X.X" "X.X" "X.X" "X.X";
    case 'P': return "XXX" "X.X" "XXX" "X.." "X..";
    case 'R': return "XX." "X.X" "XX." "X.X" "X.X";
    case 'S': return "XXX" "X.." "XXX" "..X" "XXX";
    case 'T': return "XXX" ".X." ".X." ".X." ".X.";
    case 'W': return "X.X" "X.X" "XXX" "XXX" "X.X";
    case ':': return "..." ".X." "..." ".X." "...";
    case '.': return "..." "..." "..." "..." ".X.";
    default:  return NULL;
    }
}

static float tbuf[960 * 12];

static void rect(float x, float y, float w, float h, float r, float g, float b, float a)
{
    GLfloat v[12] = {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h};
    glColor4f(r, g, b, a);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void drawTextRaw(float x, float y, float s, const char *str, float r, float g, float b, float a)
{
    int n = 0;
    for (; *str && n < 940; str++) {
        const char *gl = glyph(*str);
        if (gl) {
            for (int i = 0; i < 15; i++) {
                if (gl[i] != 'X') continue;
                float cx = x + (i % 3) * s, cy = y + (i / 3) * s;
                float *t = &tbuf[n * 12];
                t[0] = cx; t[1] = cy; t[2] = cx + s; t[3] = cy; t[4] = cx + s; t[5] = cy + s;
                t[6] = cx; t[7] = cy; t[8] = cx + s; t[9] = cy + s; t[10] = cx; t[11] = cy + s;
                n++;
            }
        }
        x += 4 * s;
    }
    if (!n) return;
    glColor4f(r, g, b, a);
    glVertexPointer(2, GL_FLOAT, 0, tbuf);
    glDrawArrays(GL_TRIANGLES, 0, n * 6);
}

static void drawText(float x, float y, float s, const char *str, float r, float g, float b, float a)
{
    drawTextRaw(x + s * 0.5f, y + s * 0.5f, s, str, 0, 0, 0, a * 0.35f);
    drawTextRaw(x, y, s, str, r, g, b, a);
}

static float textW(const char *s, float sc) { return (float)strlen(s) * 4 * sc - sc; }

static void fmtTime(char *out, size_t n, float t)
{
    int cs = (int)(t * 100.0f);
    snprintf(out, n, "%02d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

static void drawSky(float hy)
{
    GLfloat v[12] = {0, hy - 1200, SCR_W, hy - 1200, SCR_W, hy, 0, hy - 1200, SCR_W, hy, 0, hy};
    GLfloat c[24] = {
        0.28f, 0.54f, 0.90f, 1, 0.28f, 0.54f, 0.90f, 1, HAZE_R, HAZE_G, HAZE_B, 1,
        0.28f, 0.54f, 0.90f, 1, HAZE_R, HAZE_G, HAZE_B, 1, HAZE_R, HAZE_G, HAZE_B, 1};
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glColorPointer(4, GL_FLOAT, 0, c);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_COLOR_ARRAY);
}

static void render(void)
{
    glViewport(0, 0, SCR_W, SCR_H);
    glClearColor(HAZE_R, HAZE_G, HAZE_B, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* sky (2D) */
    glDisable(GL_DEPTH_TEST); glDisable(GL_FOG); glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrthof(0, SCR_W, SCR_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    float fovR = camFov * 0.5f * PI_F / 180.0f;
    float hy = SCR_H * 0.5f + tanf(P.pitch) / tanf(fovR) * SCR_H * 0.5f;
    drawSky(clampf(hy, -2000, 3000));

    /* camera */
    float hs = hypotf(P.vx, P.vz);
    float eye = camEye + bobAmp * sinf(bobPhase * 2.0f);
    if (P.dipT > 0 && P.dipDur > 0) eye -= 0.35f * sinf(clampf(P.dipT / P.dipDur, 0, 1) * PI_F);
    if (P.mantling) eye -= 0.25f * sinf(PI_F * clampf(P.mt / P.mdur, 0, 1));
    float pitchR = P.pitch;
    if (P.rolling) {
        float u = clampf(1.0f - P.rollT / 0.5f, 0, 1);
        pitchR -= 2.0f * PI_F * (u * u * (3 - 2 * u));
    }

    float aspect = (float)SCR_W / SCR_H, nearp = 0.1f, farp = 320.0f;
    float top = nearp * tanf(fovR), right = top * aspect;
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-right, right, -top, top, nearp, farp);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glRotatef(camRoll, 0, 0, 1);
    glRotatef(-pitchR * 180.0f / PI_F, 1, 0, 0);
    glRotatef(-P.yaw * 180.0f / PI_F, 0, 1, 0);
    glTranslatef(-P.x, -(P.y + eye), -P.z);

    GLfloat fc[4] = {HAZE_R, HAZE_G, HAZE_B, 1};
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 60.0f);
    glFogf(GL_FOG_END, 260.0f);
    glFogfv(GL_FOG_COLOR, fc);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mv);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, mc);
    glDrawArrays(GL_TRIANGLES, 0, nv);

    /* HUD (2D) */
    glDisable(GL_DEPTH_TEST); glDisable(GL_FOG);
    glDisableClientState(GL_COLOR_ARRAY);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrthof(0, SCR_W, SCR_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    char buf[48];
    fmtTime(buf, sizeof buf, finished ? finalTime : gameTime);
    drawText(24, 18, 6, buf, 1, 1, 1, 0.95f);
    snprintf(buf, sizeof buf, "DEATHS %d", deaths);
    drawText(SCR_W - 24 - textW(buf, 4), 22, 4, buf, 0.95f, 0.2f, 0.15f, 0.95f);

    float sp = clampf(hs / 10.0f, 0, 1);
    rect(380, 506, 200, 8, 1, 1, 1, 0.25f);
    rect(380, 506, 200 * sp, 8, 1.0f, 1.0f - sp * 0.8f, 1.0f - sp * 0.85f, 0.9f);
    rect(SCR_W / 2 - 2, SCR_H / 2 - 2, 4, 4, 1, 1, 1, 0.65f);

    if (deathFlash > 0) rect(0, 0, SCR_W, SCR_H, 1, 1, 1, clampf(deathFlash / 0.7f, 0, 1));
    if (cpFlash > 0) rect(0, 0, SCR_W, SCR_H, 0.2f, 1.0f, 0.5f, clampf(cpFlash, 0, 1) * 0.25f);

    if (finished) {
        rect(0, 0, SCR_W, SCR_H, 1, 1, 1, 0.55f);
        drawText((SCR_W - textW("FINISH", 14)) / 2, 90, 14, "FINISH", 0.9f, 0.12f, 0.1f, 1);
        fmtTime(buf, sizeof buf, finalTime);
        drawText((SCR_W - textW(buf, 8)) / 2, 220, 8, buf, 0.1f, 0.1f, 0.12f, 1);
        if (newBest) drawText((SCR_W - textW("NEW BEST", 6)) / 2, 290, 6, "NEW BEST", 0.1f, 0.6f, 0.25f, 1);
        else if (bestTime > 0) {
            char t2[32]; fmtTime(t2, sizeof t2, bestTime);
            snprintf(buf, sizeof buf, "BEST %s", t2);
            drawText((SCR_W - textW(buf, 5)) / 2, 292, 5, buf, 0.1f, 0.1f, 0.12f, 1);
        }
        if (((int)(finBlink * 2.0f)) % 2 == 0)
            drawText((SCR_W - textW("PRESS START", 5)) / 2, 420, 5, "PRESS START", 0.1f, 0.1f, 0.12f, 1);
    }
}

/* ------------------------------------------------------------------ */
/* camera feel, saving, input                                          */
/* ------------------------------------------------------------------ */
static void updateCamera(float dt)
{
    float hs = hypotf(P.vx, P.vz);
    float tgtEye = P.h * 0.92f;
    camEye += (tgtEye - camEye) * fminf(1.0f, 14.0f * dt);

    float tgtAmp = (P.grounded && !P.sliding && !P.rolling && !P.mantling) ? 0.04f * clampf(hs / 9.0f, 0, 1) : 0;
    bobAmp += (tgtAmp - bobAmp) * fminf(1.0f, 8.0f * dt);
    if (P.grounded) bobPhase += hs * dt * 1.3f;

    float tgtRoll = In.r * 2.5f;
    if (P.wallrun) {
        float rx = cosf(P.yaw), rz = -sinf(P.yaw);
        tgtRoll = (P.wnx * rx + P.wnz * rz > 0) ? 13.0f : -13.0f;
    }
    camRoll += (tgtRoll - camRoll) * fminf(1.0f, 6.0f * dt);

    float tgtFov = 66.0f + 16.0f * clampf(hs / 10.0f, 0, 1);
    camFov += (tgtFov - camFov) * fminf(1.0f, 4.0f * dt);
}

#define SAVE_DIR  "ux0:data/ROOFRUN"
#define SAVE_FILE "ux0:data/ROOFRUN/best.txt"

static void loadBest(void)
{
    bestTime = 0;
    FILE *f = fopen(SAVE_FILE, "r");
    if (f) { if (fscanf(f, "%f", &bestTime) != 1) bestTime = 0; fclose(f); }
}

static void saveBest(void)
{
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_FILE, "w");
    if (f) { fprintf(f, "%f\n", bestTime); fclose(f); }
}

static float stick(uint8_t a)
{
    float v = ((float)a - 128.0f) / 128.0f, dz = 0.18f;
    float m = fabsf(v);
    if (m < dz) return 0;
    m = (m - dz) / (1.0f - dz);
    return v < 0 ? -m : m;
}

static void checkTriggers(float dt)
{
    for (int i = 0; i < ncp; i++) {
        Check *c = &cps[i];
        if (c->active) continue;
        if (fabsf(P.x - c->x) < 7.0f && fabsf(P.z - c->z) < 4.0f && fabsf(P.y - c->y) < 3.0f) {
            c->active = 1; curCp = i; cpFlash = 1.0f;
            buildMesh();
        }
    }
    if (!finished && P.z < -244.0f && P.y > -8.0f) {
        finished = 1; finalTime = gameTime; finBlink = 0;
        if (bestTime <= 0 || finalTime < bestTime) { bestTime = finalTime; newBest = 1; saveBest(); }
    }
    deathFlash = fmaxf(0, deathFlash - dt);
    cpFlash = fmaxf(0, cpFlash - dt * 2.0f);
}

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    vglInitExtended(0, SCR_W, SCR_H, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);

    buildLevel();
    loadBest();
    resetGame();

    uint64_t last = sceKernelGetProcessTimeWide();
    float acc = 0;
    uint32_t prev = 0;
    int prevJump = 0, prevSlide = 0, prevTri = 0;

    for (;;) {
        uint64_t now = sceKernelGetProcessTimeWide();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t b = pad.buttons, pressed = b & ~prev;
        prev = b;

        if ((b & SCE_CTRL_START) && (b & SCE_CTRL_SELECT)) sceKernelExitProcess(0);
        if (pressed & SCE_CTRL_START) resetGame();
        else if (pressed & SCE_CTRL_SELECT) respawn();

        float lx = stick(pad.lx), ly = stick(pad.ly), rx = stick(pad.rx), ry = stick(pad.ry);
        P.yaw -= rx * fabsf(rx) * 2.9f * dt;
        P.pitch -= ry * fabsf(ry) * 2.2f * dt;
        P.pitch = clampf(P.pitch, -1.45f, 1.45f);

        In.f = finished ? 0 : -ly;
        In.r = finished ? 0 : lx;
        int jh = !finished && (b & (SCE_CTRL_CROSS | SCE_CTRL_RTRIGGER)) != 0;
        int sh = !finished && (b & SCE_CTRL_LTRIGGER) != 0;
        int th = !finished && (b & SCE_CTRL_TRIANGLE) != 0;
        if (jh && !prevJump) P.jumpBuf = 0.15f;
        if (sh && !prevSlide) P.slideBuf = 0.40f;
        if (th && !prevTri) P.wjBuf = 0.20f;
        prevJump = jh; prevSlide = sh; prevTri = th;
        In.jumpHeld = jh; In.slideHeld = sh;

        if (!running && !finished && (fabsf(In.f) > 0.3f || fabsf(In.r) > 0.3f)) running = 1;
        if (running && !finished) gameTime += dt;
        if (finished) finBlink += dt;

        if (!finished) {
            acc += dt;
            int n = 0;
            while (acc >= SIM_DT && n < 8) { step(SIM_DT); acc -= SIM_DT; n++; }
            if (n == 8) acc = 0;
        }
        checkTriggers(dt);
        updateCamera(dt);

        render();
        vglSwapBuffers(GL_FALSE);
    }
    return 0;
}
