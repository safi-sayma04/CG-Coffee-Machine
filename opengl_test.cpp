#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

using namespace std;

static const float PI = 3.14159265358979f;
static const int WIN_W = 900, WIN_H = 840, CY = 112;   // canvas = 900x600, coffee machine scene in the centre

struct P { float x, y; };
typedef vector<P> Poly;
struct C { float r, g, b, a; };
struct Stop { float t; C c; };
typedef vector<Stop> SS;

static float gA = 1.0f;   // globalAlpha
static C rgba(float r, float g, float b, float a = 1) { return { r / 255, g / 255, b / 255, a }; }
static C hex(unsigned h, float a = 1) { return { ((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f, a }; }
static void setc(C c) { glColor4f(c.r, c.g, c.b, c.a * gA); }
static C lerpC(C a, C b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float ease(float t) { t = max(0.f, min(1.f, t)); return t * t * (3 - 2 * t); }
static float rnd() { return rand() / (float)RAND_MAX; }

static C sampleS(const SS& s, float t) {
    if (t <= s.front().t) return s.front().c;
    if (t >= s.back().t) return s.back().c;
    for (size_t i = 0; i + 1 < s.size(); i++)
        if (t <= s[i + 1].t) { float u = (t - s[i].t) / (s[i + 1].t - s[i].t); return lerpC(s[i].c, s[i + 1].c, u); }
    return s.back().c;
}

// ============================================================ paints / shapes
struct Paint { bool grad = false; int axis = 0; float a0 = 0, a1 = 1; C c{ 0, 0, 0, 1 }; SS s; };
static Paint S(C c) { Paint p; p.c = c; return p; }
static Paint G(int axis, float a0, float a1, SS s) { Paint p; p.grad = true; p.axis = axis; p.a0 = a0; p.a1 = a1; p.s = s; return p; }
static Paint GX(float a0, float a1, SS s) { return G(0, a0, a1, s); }
static Paint GY(float a0, float a1, SS s) { return G(1, a0, a1, s); }
static Paint chrome(float x0, float w) { return GX(x0, x0 + w, SS{ {0, hex(0x8d949e)}, {.5f, hex(0xf2f4f7)}, {1, hex(0x8d949e)} }); }

static Poly clipAxis(const Poly& in, int axis, float v, bool keepGE) {
    Poly out; size_t n = in.size();
    for (size_t i = 0; i < n; i++) {
        P a = in[i], b = in[(i + 1) % n];
        float av = axis ? a.y : a.x, bv = axis ? b.y : b.x;
        bool ain = keepGE ? av >= v : av <= v, bin = keepGE ? bv >= v : bv <= v;
        if (ain) out.push_back(a);
        if (ain != bin) { float t = (v - av) / (bv - av); out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }); }
    }
    return out;
}

template <class F> static void fan(const Poly& p, F col) {
    if (p.size() < 3) return;
    P c{ 0, 0 };
    for (auto& q : p) { c.x += q.x; c.y += q.y; }
    c.x /= p.size(); c.y /= p.size();
    glBegin(GL_TRIANGLE_FAN);
    setc(col(c)); glVertex2f(c.x, c.y);
    for (auto& q : p) { setc(col(q)); glVertex2f(q.x, q.y); }
    setc(col(p[0])); glVertex2f(p[0].x, p[0].y);
    glEnd();
}

static void fill(const Poly& p, const Paint& pt) {
    if (!pt.grad) { C c = pt.c; fan(p, [&](P) { return c; }); return; }
    const int N = 32; float span = pt.a1 - pt.a0;
    for (int i = 0; i < N; i++) {
        Poly q = p;
        if (i > 0) q = clipAxis(q, pt.axis, pt.a0 + span * i / N, true);
        if (i < N - 1) q = clipAxis(q, pt.axis, pt.a0 + span * (i + 1) / N, false);
        if (q.size() < 3) continue;
        fan(q, [&](P v) { float t = ((pt.axis ? v.y : v.x) - pt.a0) / span; return sampleS(pt.s, t); });
    }
}

static Poly rrect(float x, float y, float w, float h, float r) {
    r = min(r, min(w, h) / 2); Poly p; const int n = 8;
    auto arc = [&](float cx, float cy, float a0) { for (int i = 0; i <= n; i++) { float a = a0 + PI / 2 * i / n; p.push_back({ cx + r * cosf(a), cy + r * sinf(a) }); } };
    arc(x + w - r, y + r, -PI / 2); arc(x + w - r, y + h - r, 0); arc(x + r, y + h - r, PI / 2); arc(x + r, y + r, PI);
    return p;
}
static void rr(float x, float y, float w, float h, float r, const Paint& pt) { fill(rrect(x, y, w, h, r), pt); }
static void rect(float x, float y, float w, float h, const Paint& pt) { fill(Poly{ {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h} }, pt); }

// ellipse centred (cx,cy), radii rx,ry, rotated by rot about the origin, then translated by (ox,oy)
static Poly ellT(float cx, float cy, float rx, float ry, float rot, float ox, float oy, int n = 40) {
    Poly p; float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < n; i++) {
        float a = 2 * PI * i / n; float lx = cx + rx * cosf(a), ly = cy + ry * sinf(a);
        p.push_back({ ox + lx * cr - ly * sr, oy + lx * sr + ly * cr });
    }
    return p;
}
static Poly ell(float cx, float cy, float rx, float ry, int n = 40) { return ellT(cx, cy, rx, ry, 0, 0, 0, n); }
static void circle(float cx, float cy, float r, C c) { fill(ell(cx, cy, r, r, 36), S(c)); }

static vector<P> arcP(float cx, float cy, float r, float a0, float a1, int n = 24) {
    vector<P> v; for (int i = 0; i <= n; i++) { float a = a0 + (a1 - a0) * i / n; v.push_back({ cx + r * cosf(a), cy + r * sinf(a) }); } return v;
}
static vector<P> quadP(P a, P c, P b, int n = 16) {
    vector<P> v; for (int i = 0; i <= n; i++) { float t = (float)i / n, u = 1 - t; v.push_back({ u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y }); } return v;
}
static vector<P> cubicP(P a, P c1, P c2, P b, int n = 20) {
    vector<P> v; for (int i = 0; i <= n; i++) { float t = (float)i / n, u = 1 - t;
        v.push_back({ u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x,
                      u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y }); } return v;
}

static void stroke(const vector<P>& pts, float w, C c, bool closed = false) {
    size_t n = pts.size(); if (n < 2) return;
    size_t segs = closed ? n : n - 1;
    setc(c);
    glBegin(GL_QUADS);
    for (size_t i = 0; i < segs; i++) {
        P a = pts[i], b = pts[(i + 1) % n];
        float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-6f) continue;
        float nx = -dy / len * w / 2, ny = dx / len * w / 2;
        glVertex2f(a.x + nx, a.y + ny); glVertex2f(b.x + nx, b.y + ny);
        glVertex2f(b.x - nx, b.y - ny); glVertex2f(a.x - nx, a.y - ny);
    }
    glEnd();
    if (w > 2) for (auto& q : pts) fill(ell(q.x, q.y, w / 2, w / 2, 12), S(c));
}
static void line(float x0, float y0, float x1, float y1, float w, C c) { stroke(vector<P>{ {x0, y0}, {x1, y1} }, w, c); }
static void ring(float cx, float cy, float r, float w, C c) { stroke(arcP(cx, cy, r, 0, 2 * PI, 48), w, c, true); }

// radial gradient disc/ellipse; stops' t are fractions of R
static void radial(float cx, float cy, float R, const SS& s, float ys = 1, bool inner = true) {
    const int n = 40;
    if (inner && s[0].t > 0) {
        float r0 = s[0].t * R;
        fan(ell(cx, cy, r0, r0 * ys, n), [&](P) { return s[0].c; });
    }
    for (size_t k = 0; k + 1 < s.size(); k++) {
        float r0 = s[k].t * R, r1 = s[k + 1].t * R;
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= n; i++) {
            float a = 2 * PI * i / n, ca = cosf(a), sa = sinf(a);
            setc(s[k].c);     glVertex2f(cx + ca * r0, cy + sa * r0 * ys);
            setc(s[k + 1].c); glVertex2f(cx + ca * r1, cy + sa * r1 * ys);
        }
        glEnd();
    }
}

// ============================================================ text
static int tw(void* f, const string& s) { int w = 0; for (unsigned char ch : s) w += glutBitmapWidth(f, ch); return w; }
static void text(const string& s, float x, float y, void* f, C c, bool center = true) {
    setc(c);
    float w = center ? tw(f, s) / 2.f : 0;
    glRasterPos2f(x - w, y);
    for (unsigned char ch : s) glutBitmapCharacter(f, ch);
}

// ============================================================ coffee types / state
enum { ART_NONE, ART_HEART, ART_FOAM, ART_WHIP };
struct Type {
    string name; bool step, water; int base[3], col[3]; float ck, mk, foam, cap; int art; unsigned i0, i1;
    string btn8, msg8, chip, done;
};
static Type TYPES[5] = {
    { "Espresso", false, false, {59,29,12}, {59,29,12}, 0, 0, 0, 0, ART_NONE, 0xd99a55, 0x5a2c10, "", "", "Serve", "Your espresso is ready! Notice the golden crema!" },
    { "Americano", true, true, {59,29,12}, {150,100,55}, .25f, .55f, 0, 1, ART_NONE, 0xb98048, 0x7a4520, "Add hot water", "Shot pulled! Top it up with hot water for an Americano.", "Add water", "Your Americano is ready! Smooth and long." },
    { "Latte", true, false, {59,29,12}, {207,169,130}, .85f, 1, 3, 1, ART_HEART, 0xecd7b6, 0xc9a37a, "Steam milk & pour", "Golden crema! Steam some milk for your latte.", "Steam & pour", "Your heart-art latte is ready! Enjoy!" },
    { "Cappuccino", true, false, {59,29,12}, {207,169,130}, .9f, 1, 12, .75f, ART_FOAM, 0xecd7b6, 0xc9a37a, "Steam milk & foam", "A cappuccino is shot, milk and thick foam. Froth the milk!", "Froth & pour", "Your cappuccino with cocoa-dusted foam is ready!" },
    { "Mocha", true, false, {48,22,12}, {170,115,80}, .85f, 1, 0, .85f, ART_WHIP, 0xb98a62, 0x7a4a2c, "Milk & top", "Chocolate espresso is ready. Add steamed milk, then whipped cream.", "Milk & top", "Your mocha with whipped cream and chocolate drizzle is ready!" },
};
static const char* SPEC[5] = {
    "full cup of rich espresso, 9 bar",
    "espresso topped with hot water",
    "espresso with silky steamed milk",
    "espresso, milk and thick foam",
    "espresso, chocolate, milk and cream"
};
static Type* T = &TYPES[2];
static int typeIdx = 2;

static const float CX = 310, CT = 418, CB = 474, GXp = 95, GYp = 420, LX = 310, LY = 305;
static int stage, tm; static float gr, shot, press, milkF, art, px, py; static bool tamped;
static float uiProg = 0;
struct Grain { float x, y, v; };
struct Puff { float x, y, vx, vy, a, r; };
struct Pit { float x, y, r; };
static vector<Grain> grains; static vector<Puff> puffs; static Pit pit;
static string msg;
static bool darkTheme = true;
static int MX = -100, MY = -100;   // mouse position (window coordinates) for hover effects

static int animT = 0;                             // general frame counter
static float cupT = 0, cupDX = 0, cupDY = 0, cupSC = 1;   // cup travelling from the machine to the serving plate
static const float SCENE_DX = 190;                // the whole machine scene is centred in the window
static const float SPX = 310, SPY = 552;          // serving plate centre on the counter

static void reset() {
    stage = 0; tm = 0; gr = 0; tamped = false; shot = 0; press = 0; milkF = 0; art = 0;
    grains.clear(); puffs.clear(); px = GXp; py = GYp; pit = { 455, 473, 0 };
    cupT = 0; cupDX = 0; cupDY = 0; cupSC = 1;
    msg = "Start by grinding fresh beans.";
}
static void setType(int k) {
    if (stage > 0 && stage < 10) return;
    typeIdx = k; T = &TYPES[k]; reset();
    msg = T->name + ": " + SPEC[k] + ". Start by grinding fresh beans.";
}
static void finishDrink() { stage = 10; msg = T->done + " Serving it on the plate..."; }
// espresso (no extra step) now fills the cup completely; other drinks reach their level through the milk/water step
static float fillAmt() { return shot * (T->step ? .55f : .95f) + milkF * .4f; }
static float surfY() { return CB - fillAmt() * (CB - CT - 4); }
static void addPuff(float x, float y, float vx, float vy, float r) { puffs.push_back({ x, y, vx, vy, .55f, r }); }

// ============================================================ background (compiled once) - modern cafe
static GLuint bgList;
static void drawBackground() {
    // graphite wall with vertical acoustic slats
    rect(0, 0, WIN_W, 600, GY(0, 600, SS{ {0, hex(0x232830)}, {1, hex(0x0d0f13)} }));
    for (int x = 0; x < WIN_W; x += 45) {
        rect((float)x, 0, 43, 495, S(rgba(255, 255, 255, ((x / 45) % 2) ? .025f : .05f)));
        rect((float)x + 43, 0, 2, 495, S(rgba(0, 0, 0, .35f)));
    }
    radial(300, 250, 340, SS{ {10 / 340.f, rgba(255, 170, 80, .26f)}, {1, rgba(255, 170, 80, 0)} });
    radial(690, 270, 300, SS{ {10 / 300.f, rgba(255, 190, 110, .18f)}, {1, rgba(255, 190, 110, 0)} });

    // dark stone counter with soft veins and a copper edge
    rect(0, 495, WIN_W, 105, GY(495, 600, SS{ {0, hex(0x30353e)}, {1, hex(0x111317)} }));
    for (int i = 0; i < 40; i++) {
        float y = 498.f + ((i * 37) % 100);
        vector<P> pts; for (int x = 0; x <= WIN_W; x += 20) pts.push_back({ (float)x, y + sinf(x * .015f + i) * 3 });
        stroke(pts, 1, i % 2 ? rgba(255, 255, 255, .05f) : rgba(255, 255, 255, .025f));
    }
    rect(0, 495, WIN_W, 50, GY(495, 545, SS{ {0, rgba(255, 255, 255, .14f)}, {1, rgba(255, 255, 255, 0)} }));
    rect(0, 495, WIN_W, 2, S(rgba(255, 150, 90, .85f)));
    for (int i = 0; i < 10; i++) circle((float)((i * 97) % WIN_W), (float)((i * 53) % 200 + 20), 8.f + (i % 4) * 6, rgba(255, 190, 100, .05f));

    // LED strip along the ceiling
    rect(0, 0, WIN_W, 4, S(hex(0xff9a5a)));
    rect(0, 4, WIN_W, 44, GY(4, 48, SS{ {0, rgba(255, 154, 90, .30f)}, {1, rgba(255, 154, 90, 0)} }));

    // pendant lamp (above the machine)
    glPushMatrix(); glTranslatef(50, 0, 0);
    line(435, 0, 435, 38, 2, hex(0x111111));
    fill(Poly{ {420,62}, {450,62}, {560,495}, {310,495} }, GY(62, 495, SS{ {0, rgba(255, 210, 140, .14f)}, {1, rgba(255, 210, 140, 0)} }));
    fill(Poly{ {414,62}, {456,62}, {446,38}, {424,38} }, S(hex(0x1f1f1f)));
    radial(435, 64, 45, SS{ {1 / 45.f, rgba(255, 240, 200, 1)}, {1, rgba(255, 200, 120, 0)} });
    glPopMatrix();

    // menu panel
    rr(184, 22, 164, 42, 8, S(rgba(255, 255, 255, .12f)));
    rr(187, 25, 158, 36, 6, S(hex(0x14171c)));
    rr(187, 25, 158, 12, 6, S(rgba(255, 255, 255, .05f)));
    text("TODAY'S MENU", 266, 38, GLUT_BITMAP_HELVETICA_12, hex(0xff9a5a));
    text("Espresso \xB7 Latte \xB7 Cappuccino", 266, 52, GLUT_BITMAP_TIMES_ROMAN_10, rgba(245, 245, 232, .92f));

    // wall clock
    circle(385, 42, 16, hex(0xf2f3f5)); ring(385, 42, 16, 3, hex(0x2a2d34));
    for (int i = 0; i < 12; i++) { float a = i * PI / 6; line(385 + sinf(a) * 12, 42 - cosf(a) * 12, 385 + sinf(a) * 14, 42 - cosf(a) * 14, 1, hex(0x333333)); }
    line(385, 42, 379, 38, 2, hex(0x333333)); line(385, 42, 393, 33, 1.2f, hex(0xff6b35));

    // floating shelves with decor (right side)
    glPushMatrix(); glTranslatef(310, 0, 0);
    for (int y : { 205, 335 }) {
        rect(420, (float)y, 60, 7, GY((float)y, (float)y + 7, SS{ {0, hex(0x8a6a48)}, {1, hex(0x4a3320)} }));
        rect(420, (float)y + 7, 60, 3, S(rgba(0, 0, 0, .35f)));
        rect(426, (float)y + 7, 4, 12, S(hex(0x2a2a2e)));
    }
    // mug
    rr(424, 185, 22, 20, 3, S(hex(0xf4efe6)));
    stroke(arcP(447, 195, 5, -1.5f, 1.5f), 3, hex(0xf4efe6));
    rect(424, 198, 22, 7, S(rgba(0, 0, 0, .12f)));
    // coffee bag
    rect(456, 150, 22, 55, S(hex(0x2b2f38))); rect(456, 150, 22, 7, S(hex(0xff7a3d)));
    rect(460, 168, 14, 18, S(hex(0xf6f0e4)));
    rect(463, 174, 8, 2, S(hex(0x3a2414))); rect(463, 178, 8, 2, S(hex(0x3a2414)));
    // plant
    fill(Poly{ {428,335}, {452,335}, {449,311}, {431,311} }, S(hex(0xe9e6df)));
    rect(427, 306, 26, 6, S(hex(0xf5f2ec)));
    for (int i = 0; i < 5; i++) fill(ellT(0, -14, 5, 15, -1.2f + i * .6f, 440, 306, 20), S(i % 2 ? hex(0x2f7d3a) : hex(0x3c9448)));
    // bean jar
    rect(459, 316, 19, 19, S(hex(0x4a2a14)));
    for (int i = 0; i < 10; i++) fill(ellT(0, 0, 3, 2, (float)i, 462.f + (i % 3) * 6, 319.f + (i / 3) * 5, 12), S(hex(0x6b3d1e)));
    rect(459, 300, 19, 35, S(rgba(200, 220, 235, .25f)));
    stroke(Poly{ {459,300}, {478,300}, {478,335}, {459,335} }, 1.2f, rgba(220, 235, 245, .6f), true);
    rect(457, 295, 23, 6, S(hex(0x222222)));
    glPopMatrix();
}

// ============================================================ scene objects
static void shadow(float x, float y, float rx, float ry, float a) {
    radial(x, y, rx, SS{ {0, rgba(0, 0, 0, a)}, {1, rgba(0, 0, 0, 0)} }, ry / rx);
}

struct Bean { float k, x, y, r; };
static vector<Bean> beanPos;
static void initBeans() {
    for (int i = 0; i < 36; i++) {
        float y = 250.f - (i / 6) * 17, l = 35 + (y - 150) / 110 * 35, rt = 155 - (y - 150) / 110 * 35;
        beanPos.push_back({ i / 36.f, 0, y, 0 });   // fields: k, x, y, r (y was wrongly 0 before, so beans were drawn off-screen)
        beanPos.back().x = l + 12 + (i % 6) / 5.f * (rt - l - 24) + ((i * 7) % 5 - 2);
        beanPos.back().r = i * 1.3f;
    }
}

static void grinder() {
    float j = stage == 1 ? (rnd() - .5f) * 1.6f : 0;
    glPushMatrix(); glTranslatef(j, 0, 0);
    rr(118, 372, 30, 115, 4, chrome(118, 30));
    rr(28, 486, 140, 10, 4, S(hex(0x2a2a2e)));
    rr(36, 446, 104, 8, 2, S(hex(0x1b1b1e)));
    for (float x = 42; x < 136; x += 8) rect(x, 447, 3, 6, S(hex(0x4a4a50)));
    Poly hop{ {35,150}, {155,150}, {120,260}, {70,260} };
    fill(hop, GX(35, 155, SS{ {0, rgba(215, 185, 150, .38f)}, {.5f, rgba(245, 228, 205, .30f)}, {1, rgba(215, 185, 150, .38f)} }));
    stroke(hop, 2, rgba(210, 225, 240, .55f), true);
    float keep = 1 - gr * .75f;
    for (auto& b : beanPos) if (b.k <= keep) {
        float cr = cosf(b.r), sr = sinf(b.r);
        fill(ellT(0, 0, 6, 4, b.r, b.x, b.y, 16), S(hex(0x8a5230)));
        auto tf = [&](float lx, float ly) { return P{ b.x + lx * cr - ly * sr, b.y + lx * sr + ly * cr }; };
        stroke(quadP(tf(-5, 0), tf(0, -2), tf(5, 0), 8), 1, hex(0x3a1d0c));
        fill(ellT(-2, -2, 2.5f, 1.2f, b.r, b.x, b.y, 10), S(rgba(255, 235, 200, .55f)));
    }
    rr(30, 138, 130, 14, 6, S(hex(0x15151a)));
    rr(30, 138, 130, 14, 6, GY(138, 152, SS{ {0, rgba(255, 255, 255, .35f)}, {.4f, rgba(255, 255, 255, 0)} }));
    rr(82, 128, 26, 12, 5, S(hex(0x111111)));
    fill(Poly{ {48,156}, {62,156}, {84,254}, {74,254} }, GX(40, 80, SS{ {0, rgba(255, 255, 255, 0)}, {.5f, rgba(255, 255, 255, .35f)}, {1, rgba(255, 255, 255, 0)} }));
    rr(45, 262, 100, 112, 12, GX(45, 145, SS{ {0, hex(0x0e0e11)}, {.25f, hex(0x3a3a42)}, {.5f, hex(0x1b1b20)}, {1, hex(0x08080a)} }));
    rr(40, 254, 110, 12, 5, chrome(40, 110));
    rr(62, 242, 66, 14, 4, chrome(62, 66));
    rr(58, 282, 74, 64, 8, GY(282, 346, SS{ {0, hex(0xcfd3d9)}, {1, hex(0x8d949e)} }));
    for (int y = 285; y < 344; y += 3) line(60, (float)y, 130, (float)y, 1, rgba(255, 255, 255, .25f));
    rr(66, 288, 58, 17, 3, S(hex(0x06150c)));
    { char b[16]; snprintf(b, sizeof b, "%.1f g", gr * 18); text(b, 95, 300, GLUT_BITMAP_8_BY_13, hex(0x7CFF9B)); }
    radial(95, 325, 14, SS{ {0, hex(0x5a5a62)}, {1, hex(0x101012)} });
    ring(95, 325, 14, 2, hex(0xff9a5a));
    for (int a = 0; a < 12; a++) line(95 + cosf(a * .5236f) * 10, 325 + sinf(a * .5236f) * 10, 95 + cosf(a * .5236f) * 13, 325 + sinf(a * .5236f) * 13, 1, hex(0x999999));
    line(95, 325, 95, 314, 2, hex(0xffffff));
    circle(72, 335, 4, stage == 1 ? hex(0x34c759) : hex(0x3a2a24));
    circle(118, 335, 4, hex(0xff3b30));
    fill(Poly{ {78,374}, {112,374}, {101,394}, {89,394} }, chrome(78, 34));
    rr(90, 392, 10, 4, 2, S(hex(0x111111)));
    glPopMatrix();
}

static void machine() {
    for (int i = 0; i < 3; i++) rr(200.f + i * 45, 64, 28, 16, 4, S(hex(0xffffff)));
    rr(175, 78, 240, 20, 8, chrome(175, 240));
    // matte graphite body (modern)
    rr(175, 95, 240, 383, 18, GX(175, 415, SS{ {0, hex(0x12141a)}, {.35f, hex(0x3b414d)}, {1, hex(0x12141a)} }));
    rr(175, 95, 240, 383, 18, GX(175, 415, SS{ {0, rgba(255,255,255,0)}, {.18f, rgba(255,255,255,.22f)}, {.26f, rgba(255,255,255,0)}, {.8f, rgba(255,255,255,.07f)}, {1, rgba(0,0,0,.28f)} }));
    rr(175, 95, 240, 383, 18, GY(95, 478, SS{ {0, rgba(255,255,255,.16f)}, {.25f, rgba(255,255,255,0)}, {1, rgba(0,0,0,.4f)} }));
    for (P p : { P{186,106}, P{404,106}, P{186,467}, P{404,467} }) circle(p.x, p.y, 3, hex(0xc9ced6));
    rect(190, 253, 210, 2, S(hex(0xff7a3d)));    // copper accent line
    text("E S P R E S S O", 295, 112, GLUT_BITMAP_HELVETICA_12, hex(0xff9a5a));
    rr(195, 120, 200, 126, 12, S(hex(0x0b0d11)));

    // pressure gauge
    circle(245, 183, 36, hex(0xf3efe6)); ring(245, 183, 36, 4, hex(0xc0c4cc));
    const float a0 = -2.356f, sp = 4.712f;
    stroke(arcP(245, 183, 27, a0 + sp * 8 / 12, a0 + sp * 10 / 12, 16), 5, hex(0x2ca24a));
    for (int i = 0; i <= 12; i++) { float a = a0 + sp * i / 12 - PI / 2; line(245 + cosf(a) * 30, 183 + sinf(a) * 30, 245 + cosf(a) * 34, 183 + sinf(a) * 34, 1.5f, hex(0x222222)); }
    { float na = a0 + sp * press / 12 - PI / 2; line(245, 183, 245 + cosf(na) * 28, 183 + sinf(na) * 28, 2.5f, hex(0xc4161c)); }
    circle(245, 183, 3.5f, hex(0x222222));
    text("BAR", 245, 205, GLUT_BITMAP_HELVETICA_10, hex(0x222222));
    { // glass highlight on the dial
      Poly seg; for (P p : arcP(245, 183, 34, PI, PI * 1.7f, 16)) seg.push_back(p);
      fan(seg, [&](P v) { float dx = 55, dy = 65, t = ((v.x - 215) * dx + (v.y - 150) * dy) / (dx * dx + dy * dy); t = max(0.f, min(1.f, t)); return rgba(255, 255, 255, .6f * (1 - min(1.f, t / .5f))); }); }

    // timer screen + LEDs + knobs
    rr(300, 134, 84, 38, 6, S(hex(0x06150c)));
    { float s = stage == 7 ? shot * 28 : stage >= 8 ? 28 : 0; char b[16]; snprintf(b, sizeof b, "0:%02d", (int)floorf(s)); text(b, 342, 162, GLUT_BITMAP_9_BY_15, hex(0x7CFF9B)); }
    { bool on[3] = { true, stage >= 6 && stage < 9, stage == 9 && tm < 140 }; unsigned col[3] = { 0xff3b30, 0x34c759, 0xff9f0a };
      for (int i = 0; i < 3; i++) circle(318.f + i * 26, 208, 6, on[i] ? hex(col[i]) : hex(0x2a2d34)); }
    circle(320, 232, 6, hex(0x8a8f98)); circle(360, 232, 6, hex(0x8a8f98));

    // group head + drip tray
    rr(272, 262, 76, 32, 8, chrome(272, 76));
    rr(280, 292, 60, 8, 3, S(hex(0x444444)));
    rr(255, 340, 120, 138, 8, S(hex(0x0a0c10)));
    rr(175, 478, 240, 14, 4, chrome(175, 240));
    for (float x = 190; x < 405; x += 14) rect(x, 483, 8, 3, S(hex(0x333333)));

    // steam wand
    stroke(vector<P>{ {395,252}, {395,345}, {389,354} }, 7, hex(0xd5dae0));
    circle(395, 250, 9, hex(0x222222));
}

static Poly cupPoly() {
    Poly p{ {CX - 32, CT}, {CX + 32, CT}, {CX + 22, CB - 10} };
    for (P q : quadP({ CX + 22, CB - 10 }, { CX + 22, CB }, { CX + 12, CB }, 8)) p.push_back(q);
    p.push_back({ CX - 12, CB });
    for (P q : quadP({ CX - 12, CB }, { CX - 22, CB }, { CX - 22, CB - 10 }, 8)) p.push_back(q);
    return p;
}
static C mixCol(const int* a, const int* b, float t) { return rgba(lerpf((float)a[0], (float)b[0], t), lerpf((float)a[1], (float)b[1], t), lerpf((float)a[2], (float)b[2], t)); }

static void cup() {
    fill(ell(CX, CB + 2, 52, 6), S(hex(0xf1f1f1)));
    stroke(arcP(CX + 33, CT + 20, 14, -1.5f, 1.6f), 6, hex(0xf1f1f1));
    Poly body = cupPoly();
    fill(body, GX(CX - 32, CX + 32, SS{ {0, hex(0xcfcfd2)}, {.3f, hex(0xffffff)}, {1, hex(0xbdbdc2)} }));
    float f = fillAmt();
    if (f > 0) {
        float sy = surfY();
        Poly liq = clipAxis(body, 1, sy, true);
        fill(liq, S(mixCol(T->base, T->col, milkF)));
        float ca = 1 - milkF * T->ck;
        fill(clipAxis(liq, 1, sy + 7, false), GY(sy, sy + 7, SS{ {0, rgba(214, 150, 80, ca)}, {1, rgba(120, 60, 25, ca)} }));
        if (T->foam > 0 && milkF > .05f)
            fill(clipAxis(liq, 1, sy + T->foam * milkF, false), S(rgba(250, 242, 228, min(1.f, milkF * 1.5f))));
        fill(liq, GX(CX - 32, CX + 32, SS{ {0, rgba(0,0,0,.35f)}, {.3f, rgba(255,255,255,.12f)}, {1, rgba(0,0,0,.4f)} }));
    }
    stroke(body, 2.5f, hex(0xd0d0d0), true);
    if (T->art == ART_WHIP && art > 0) {
        gA = art;
        circle(CX - 14, CT - 2, 13, hex(0xfffaf0)); circle(CX + 14, CT - 2, 13, hex(0xfffaf0));
        circle(CX, CT - 8, 15, hex(0xfffaf0)); circle(CX, CT - 18, 9, hex(0xfffaf0));
        vector<P> d = quadP({ CX - 20, CT - 6 }, { CX - 8, CT - 22 }, { CX, CT - 12 }, 12);
        for (P q : quadP({ CX, CT - 12 }, { CX + 8, CT - 2 }, { CX + 20, CT - 14 }, 12)) d.push_back(q);
        stroke(d, 1.6f, hex(0x3b1d0c));
        gA = 1;
    }
    line(CX - 26, CT + 6, CX - 20, CB - 14, 2, rgba(255, 255, 255, .8f));
}

static void portafilter() {
    rr(px - 86, py - 5, 52, 12, 6, S(hex(0x5a606d)));          // graphite-steel handle
    rr(px - 80, py - 3, 40, 3, 1.5f, S(rgba(255, 255, 255, .35f)));
    rr(px - 90, py - 4, 10, 10, 3, S(hex(0xff7a3d)));
    rr(px - 40, py - 3, 16, 8, 2, S(hex(0x8d949e)));
    rr(px - 26, py - 10, 52, 22, 6, chrome(px - 26, 52));
    if (stage <= 5) {
        float ry = max(gr * (tamped ? 2.f : 9.f), .1f);
        Poly dome = arcP(px, py - 9, 22, PI, 2 * PI, 24);
        for (auto& q : dome) q.y = (py - 9) + (q.y - (py - 9)) * ry / 22.f;
        fill(dome, S(hex(0x7a4a28)));
        rect(px - 22, py - 9, 44, 3, S(hex(0x7a4a28)));
        if (gr > .05f) stroke(dome, 1.5f, rgba(210, 150, 90, .55f), true);
    }
    rr(px - 12, py + 12, 6, 10, 2, S(hex(0xc9ced6)));
    rr(px + 6, py + 12, 6, 10, 2, S(hex(0xc9ced6)));
}

static void tamper() {
    float e = tm < 40 ? ease(tm / 40.f) : ease(1 - (tm - 40) / 28.f);
    float ty = py - 60 + 46 * e;
    rr(px - 20, ty, 40, 6, 3, S(hex(0xc9ced6)));
    rr(px - 8, ty - 34, 16, 34, 7, S(hex(0xd9622b)));
    rr(px - 5, ty - 32, 4, 28, 2, S(rgba(255, 255, 255, .35f)));
    circle(px, ty - 36, 12, hex(0xff8a4c));
    circle(px - 3, ty - 39, 4, rgba(255, 255, 255, .45f));
    ring(px, ty - 36, 12, 1.5f, hex(0x8a3a14));
}

static void pitcher() {
    glPushMatrix(); glTranslatef(pit.x, pit.y, 0); glRotatef(pit.r * 180 / PI, 0, 0, 1);
    stroke(arcP(20, -2, 11, -1.5f, 1.5f), 5, hex(0xaeb4bd));
    Paint g = GX(-18, 18, SS{ {0, hex(0x8d949e)}, {.5f, hex(0xf2f4f7)}, {1, hex(0x8d949e)} });
    fill(Poly{ {-14,-22}, {18,-22}, {14,22}, {-14,22}, {-18,-14} }, g);
    fill(Poly{ {-22,-26}, {-14,-22}, {-18,-14} }, g);
    glPopMatrix();
}

static void insetView() {
    float x = 80, y = 66;
    circle(x, y, 56, rgba(15, 8, 5, .8f)); ring(x, y, 56, 2.5f, hex(0xff9a5a));
    circle(x, y, 48, hex(0xf5f5f5)); rr(x + 36, y - 5, 16, 10, 4, S(hex(0xffffff)));
    circle(x, y, 38, hex(0xffffff)); ring(x, y, 38, 2, hex(0xdddddd));
    float a = min(1.f, fillAmt() * 5);
    if (a > 0) {
        gA = a;
        radial(x, y, 32, SS{ {2 / 32.f, hex(0xd99a55)}, {(2 + 21) / 32.f, hex(0xa8622b)}, {1, hex(0x5a2c10)} });
        gA = a * milkF * T->mk;
        radial(x, y, 32, SS{ {2 / 32.f, hex(T->i0)}, {1, hex(T->i1)} });
        gA = art;
        if (T->art == ART_HEART) {
            vector<P> h = cubicP({ x, y + 19 }, { x - 30, y - 2 }, { x - 14, y - 24 }, { x, y - 9 });
            for (P q : cubicP({ x, y - 9 }, { x + 14, y - 24 }, { x + 30, y - 2 }, { x, y + 19 })) h.push_back(q);
            fill(h, S(hex(0xfff6e6)));
            line(x, y - 8, x, y + 17, 1.2f, hex(0x8a5a30));
        } else if (T->art == ART_FOAM) {
            circle(x, y, 29, hex(0xf6ead6));
            for (int i = 0; i < 70; i++) { float an = i * 2.4f, rd = sqrtf(i / 70.f) * 27; rect(x + cosf(an) * rd, y + sinf(an) * rd, 1.6f, 1.6f, S(hex(0x5a3418))); }
        } else if (T->art == ART_WHIP) {
            for (int k = 0; k < 4; k++) { circle(x, y, 28.f - k * 6, hex(0xfffaf0)); ring(x, y, 28.f - k * 6, 1, rgba(180, 150, 120, .5f)); }
            vector<P> sp; for (int i = 0; i < 60; i++) { float an = i * .35f, rd = i * .42f; sp.push_back({ x + cosf(an) * rd, y + sinf(an) * rd }); }
            stroke(sp, 2, hex(0x3b1d0c));
        }
        gA = 1;
    }
    text("TOP VIEW", x, y + 70, GLUT_BITMAP_HELVETICA_10, hex(0xff9a5a));
}

// ---------------- serving plate (the finished coffee is brought here)
static void plate() {
    shadow(SPX, SPY + 12, 134, 14, .5f);
    fill(ell(SPX, SPY + 3, 122, 20), S(hex(0xc9c9ce)));
    fill(ell(SPX, SPY, 122, 20), S(hex(0xf6f6f6)));
    fill(ell(SPX, SPY, 102, 14), S(hex(0xe6e6ea)));
    vector<P> hl; for (int i = 0; i <= 12; i++) { float a = PI * (1.05f + .4f * i / 12); hl.push_back({ SPX + 114 * cosf(a), SPY + 18 * sinf(a) }); }
    stroke(hl, 1.5f, rgba(255, 255, 255, .9f));
}

static void drawServedCup() {
    glPushMatrix();
    glTranslatef(cupDX, cupDY, 0);
    glTranslatef(CX, CB, 0); glScalef(cupSC, cupSC, 1); glTranslatef(-CX, -CB, 0);
    cup();
    glPopMatrix();
}

static void drawCanvas() {
    glCallList(bgList);
    glPushMatrix(); glTranslatef(SCENE_DX, 0, 0);
    shadow(95, 500, 85, 10, .6f); shadow(295, 500, 160, 14, .65f); if (cupT < .01f) shadow(CX, CB + 8, 60, 7, .5f);
    shadow(pit.x, 497, 30, 5, pit.y > 465 ? .5f : .12f);
    grinder(); machine(); plate();
    if (cupT > .01f) shadow(CX + cupDX, CB + 6 + cupDY, 56 * cupSC, 6 * cupSC, .3f);
    drawServedCup();

    if (stage == 7 && shot > .01f && shot < .98f)
        for (float o : { -9.f, 9.f }) {
            line(px + o + 3, py + 22, CX + o * .4f + 3, surfY(), 2.6f, hex(0x5a2c10));
            line(px + o + 2.5f, py + 22, CX + o * .4f + 2.5f, surfY(), .8f, rgba(255, 200, 120, .65f));
        }
    if (stage == 9 && tm > 180 && tm < 340) {
        float sx = pit.x - 31, sy = pit.y - 2;
        stroke(quadP({ sx, sy }, { sx - 4, sy + 20 }, { CX + 4, surfY() }, 14), 4, T->water ? hex(0xdff1fb) : hex(0xf6efe3));
    }
    for (auto& g : grains) rect(g.x, g.y, 2.8f, 2.8f, S(hex(0x9a6234)));
    for (auto& p : puffs) {
        float al = max(p.a, 0.f);
        radial(p.x, p.y, p.r * 1.7f, SS{ {0, rgba(245, 245, 245, al * .8f)}, {1, rgba(245, 245, 245, 0)} });
    }
    pitcher(); portafilter(); if (stage == 3) tamper();

    glPopMatrix();

    // vignette
    radial(450, 300, 560, SS{ {280 / 560.f, rgba(0, 0, 0, 0)}, {1, rgba(0, 0, 0, .28f)} }, 1, false);
    if (stage >= 7) insetView();
}

// ============================================================ logic
static void update() {
    animT++;
    if (stage == 10) cupT = min(1.f, cupT + 1.f / 70.f);
    { float e = ease(cupT);
      cupDX = (SPX - CX) * e;
      cupDY = (SPY - 1 - CB) * e - sinf(PI * e) * 22.f;
      cupSC = lerpf(1.f, .8f, e); }
    press += ((stage == 7 && shot < .97f ? 9.f : 0.f) - press) * .06f;
    uiProg += (stage / 10.f - uiProg) * .08f;
    if (stage == 1) {
        tm++; gr = min(tm / 150.f, 1.f);
        grains.push_back({ GXp + (rnd() - .5f) * 6, 392, 2 + rnd() });
        if (tm >= 150) { stage = 2; msg = "Grounds ready. Level and tamp them."; }
    } else if (stage == 3) {
        tm++; if (tm >= 40) tamped = true;
        if (tm >= 70) { stage = 4; msg = "Perfectly tamped puck. Lock it in."; }
    } else if (stage == 5) {
        tm++; float e = ease(tm / 60.f); px = lerpf(GXp, LX, e); py = lerpf(GYp, LY, e);
        if (tm >= 60) { stage = 6; msg = "Ready: 9 bar, about 28 seconds."; }
    } else if (stage == 7) {
        tm++; shot = min(tm / 300.f, 1.f);
        if (tm >= 300) {
            if (T->step) { stage = 8; msg = T->msg8; }
            else finishDrink();
        }
    } else if (stage == 9) {
        tm++;
        if (tm < 40) { float e = ease(tm / 40.f); pit.x = lerpf(455, 392, e); pit.y = lerpf(473, 378, e); }
        else if (tm < 130) { pit.x = 392; pit.y = 378 + sinf(tm * .3f) * 1.5f; if (tm % 2 == 0) addPuff(391, 352, 1 + rnd(), -1.2f, 5); }
        else if (tm < 180) { float e = ease((tm - 130) / 50.f); pit.x = lerpf(392, 350, e); pit.y = lerpf(378, 385, e); pit.r = lerpf(0, -.85f, e); }
        else if (tm < 340) { milkF = min((tm - 180) / 160.f, 1.f) * T->cap; }
        else { float e = ease((tm - 340) / 40.f); pit.x = lerpf(350, 455, e); pit.y = lerpf(385, 473, e); pit.r = lerpf(-.85f, 0, e); }
        if (tm > 300) art = min((tm - 300) / 80.f, 1.f);
        if (tm >= 420) finishDrink();
    }
    if (fillAmt() > .1f && rnd() < .25f) addPuff(CX + cupDX + (rnd() - .5f) * 40 * cupSC, CB + cupDY + (surfY() - CB) * cupSC, 0, -.8f, 6);

    for (auto& g : grains) { g.v += .15f; g.y += g.v; }
    grains.erase(remove_if(grains.begin(), grains.end(), [](const Grain& g) { return g.y >= 412; }), grains.end());
    for (auto& p : puffs) { p.x += p.vx * .3f + sinf(p.y * .06f) * .3f; p.y += p.vy; p.a -= .006f; p.r += .08f; }
    puffs.erase(remove_if(puffs.begin(), puffs.end(), [](const Puff& p) { return p.a <= 0; }), puffs.end());
}

// ============================================================ page UI (modern: pill buttons, hover, cards, progress)
struct Btn { float x, y, w, h; };
static const float LEFT_W = 900;   // controls are centred in the whole window
static Btn typeBtn[5], goBtn = { 275, 724, 240, 42 }, rsBtn = { 525, 724, 100, 42 };
static void* FT = GLUT_BITMAP_HELVETICA_12;

static bool goEnabled() { return stage == 0 || stage == 2 || stage == 4 || stage == 6 || stage == 8; }
static string goLabel() {
    switch (stage) {
    case 0: return "Grind beans"; case 2: return "Tamp the grounds"; case 4: return "Lock into group head";
    case 6: return "Pull the shot"; case 8: return T->btn8; case 10: return "Enjoy"; default: return "Working...";
    }
}
static void clickGo() {
    if (!goEnabled()) return;
    stage++; tm = 0;
    switch (stage) {
    case 1: msg = "Grinding fresh beans..."; break;
    case 3: msg = "Tamping with firm, level pressure..."; break;
    case 5: msg = "Locking the portafilter in..."; break;
    case 7: msg = "Extracting at 9 bar..."; break;
    case 9: msg = T->water ? "Heating and pouring hot water..." : "Steaming and pouring milk..."; break;
    default: msg = "";
    }
}

static void layoutTypes() {
    float tot = -6; for (int k = 0; k < 5; k++) tot += tw(FT, TYPES[k].name) + 24 + 6;
    float x = (LEFT_W - tot) / 2;
    for (int k = 0; k < 5; k++) { float w = tw(FT, TYPES[k].name) + 24.f; typeBtn[k] = { x, 44, w, 26 }; x += w + 6; }
}
static bool hit(const Btn& b, int mx, int my) { return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h; }

struct Theme { C bg, fg, btn, chip, acc; };
static Theme theme() {
    if (darkTheme) return { hex(0x0f1115), hex(0xf2f3f5), hex(0xff6b35), hex(0x1b1f26), hex(0xff9a5a) };
    return { hex(0xf2f3f6), hex(0x14161a), hex(0xe8531f), hex(0xe2e5ea), hex(0xe8531f) };
}

// pill-shaped button with soft shadow, gloss and hover lightening
static void pill(const Btn& b, C fillc, const string& label, void* font, C txt, float alpha = 1.f, bool allowHover = true) {
    bool h = allowHover && alpha > .9f && hit(b, MX, MY);
    float r = b.h / 2;
    float saved = gA; gA = alpha;
    rr(b.x, b.y + 3, b.w, b.h, r, S(rgba(0, 0, 0, darkTheme ? .45f : .18f)));
    C f = h ? lerpC(fillc, C{ 1, 1, 1, fillc.a }, .14f) : fillc;
    rr(b.x, b.y, b.w, b.h, r, S(f));
    rr(b.x + 3, b.y + 2, b.w - 6, b.h / 2 - 2, r, S(rgba(255, 255, 255, .10f)));
    text(label, b.x + b.w / 2, b.y + b.h / 2 + (font == GLUT_BITMAP_HELVETICA_18 ? 6.f : 4.f), font, txt);
    gA = saved;
}

static void drawPage() {
    Theme th = theme();
    // header: accent dot + spaced title + subtitle
    string title = "ESPRESSO BAR"; float tot = 0;
    for (unsigned char ch : title) tot += glutBitmapWidth(GLUT_BITMAP_TIMES_ROMAN_24, ch) + 4;
    float x = (WIN_W - tot) / 2 + 12;
    circle(x - 22, 22, 6, th.acc); ring(x - 22, 22, 10, 1.5f, rgba(255, 154, 90, .6f));
    for (unsigned char ch : title) { string s(1, ch); text(s, x, 30, GLUT_BITMAP_TIMES_ROMAN_24, th.fg, false); x += glutBitmapWidth(GLUT_BITMAP_TIMES_ROMAN_24, ch) + 4; }

    layoutTypes();
    for (int k = 0; k < 5; k++) {
        Btn b = typeBtn[k]; bool sel = (k == typeIdx);
        pill(b, sel ? th.acc : th.chip, TYPES[k].name, FT, sel ? hex(0x14161a) : th.fg, (stage > 0 && stage < 10 && !sel) ? .5f : 1.f, !(stage > 0 && stage < 10));
    }

    // step chips with done / active / upcoming states
    string names[5] = { "Grind", "Tamp", "Lock in", "Pull shot", T->chip };
    int idxTab[11] = { -1, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4 };
    int active = idxTab[stage];
    float widths[5], tot2 = -6;
    for (int i = 0; i < 5; i++) { widths[i] = tw(FT, names[i]) + 20.f; tot2 += widths[i] + 6; }
    float cx0 = (LEFT_W - tot2) / 2;
    for (int i = 0; i < 5; i++) {
        bool on = i == active, done = (stage == 10) || (i < active);
        float savedA = gA;
        if (on) { rr(cx0, 80, widths[i], 22, 11, S(th.acc)); text(names[i], cx0 + widths[i] / 2, 95, FT, hex(0x14161a)); }
        else if (done) { rr(cx0, 80, widths[i], 22, 11, S(rgba(255, 154, 90, darkTheme ? .30f : .35f))); text(names[i], cx0 + widths[i] / 2, 95, FT, th.fg); }
        else { gA = .6f; rr(cx0, 80, widths[i], 22, 11, S(th.chip)); text(names[i], cx0 + widths[i] / 2, 95, FT, th.fg); }
        gA = savedA; cx0 += widths[i] + 6;
    }
}

static vector<string> wrapText(const string& m, int maxw) {
    vector<string> lines; string cur, word;
    auto flushWord = [&]() { if (word.empty()) return; string t = cur.empty() ? word : cur + " " + word; if (tw(FT, t) > maxw && !cur.empty()) { lines.push_back(cur); cur = word; } else cur = t; word.clear(); };
    for (char ch : m) { if (ch == ' ') flushWord(); else word += ch; }
    flushWord(); if (!cur.empty()) lines.push_back(cur);
    return lines;
}

static void card(float x, float y, float w, float h) {
    Theme th = theme();
    rr(x, y + 4, w, h, 20, S(rgba(0, 0, 0, darkTheme ? .45f : .12f)));
    rr(x, y, w, h, 20, S(th.chip));
    rr(x, y, w, 3, 2, S(rgba(255, 154, 90, .55f)));
}

static void drawBottom() {
    Theme th = theme();
    card(150, 712, 600, 116);
    pill(goBtn, th.btn, goLabel(), GLUT_BITMAP_HELVETICA_18, hex(0xffffff), goEnabled() ? 1.f : .45f);
    pill(rsBtn, rgba(255, 255, 255, darkTheme ? .10f : .55f), "Reset", GLUT_BITMAP_HELVETICA_18, th.fg);
    rr(180, 774, 540, 5, 2.5f, S(rgba(128, 128, 128, .25f)));
    if (uiProg > .01f) rr(180, 774, max(5.f, 540.f * uiProg), 5, 2.5f, S(th.acc));
    { vector<string> L = wrapText(msg, 540); for (size_t i = 0; i < L.size() && i < 2; i++) text(L[i], WIN_W / 2.f, 797.f + i * 16, FT, th.fg); }
}

// ============================================================ GLUT callbacks
static void display() {
    Theme th = theme();
    glClearColor(th.bg.r, th.bg.g, th.bg.b, 1); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    drawPage();
    glEnable(GL_SCISSOR_TEST); glScissor(0, WIN_H - (CY + 600), WIN_W, 600);
    glPushMatrix(); glTranslatef(0, (float)CY, 0); drawCanvas(); glPopMatrix();
    glDisable(GL_SCISSOR_TEST);
    line(0, (float)CY, WIN_W, (float)CY, 1.5f, rgba(255, 154, 90, .5f));
    drawBottom();
    glutSwapBuffers();
}
static void reshape(int w, int h) {
    if (w != WIN_W || h != WIN_H) { glutReshapeWindow(WIN_W, WIN_H); return; }
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, WIN_W, WIN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
}
static void tick(int) { update(); glutPostRedisplay(); glutTimerFunc(16, tick, 0); }
static void motion(int x, int y) { MX = x; MY = y; }
static void mouse(int button, int state, int x, int y) {
    MX = x; MY = y;
    if (button != GLUT_LEFT_BUTTON || state != GLUT_DOWN) return;
    for (int k = 0; k < 5; k++) if (hit(typeBtn[k], x, y)) { setType(k); return; }      // coffee side
    if (hit(goBtn, x, y)) clickGo();
    else if (hit(rsBtn, x, y)) reset();
}
static void keyboard(unsigned char k, int, int) {
    if (k == 27) exit(0);
    if (k == ' ' || k == '\r') clickGo();
    else if (k == 'r' || k == 'R') reset();
    else if (k == 'd' || k == 'D') darkTheme = !darkTheme;
    else if (k >= '1' && k <= '5') setType(k - '1');
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_MULTISAMPLE);   // remove GLUT_MULTISAMPLE if your driver complains
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("Espresso Bar 2D - Modern Edition");
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    initBeans(); reset();
    bgList = glGenLists(1); glNewList(bgList, GL_COMPILE); drawBackground(); glEndList();
    glutDisplayFunc(display); glutReshapeFunc(reshape); glutMouseFunc(mouse); glutKeyboardFunc(keyboard);
    glutPassiveMotionFunc(motion); glutMotionFunc(motion);
    glutTimerFunc(16, tick, 0);
    glutMainLoop();
    return 0;
}
