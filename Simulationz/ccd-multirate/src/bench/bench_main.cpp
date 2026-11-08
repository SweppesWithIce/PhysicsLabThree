// physbench — эксперименты без окна. Каждый эксперимент печатает сводку
// в консоль и (если задан --out) пишет сырые данные в CSV для графиков.
//
//   physbench freefall     [--hz 60] [--time 1.0] [--out f.csv]
//   physbench determinism  [--bodies 50] [--steps 600] [--seed 1] [--room 0|1]
//   physbench bounce       [--hz 60] [--e 0.5] [--h 300] [--out f.csv]
//   physbench bounce-sweep [--hz 60] [--h 300] [--out f.csv]       e = 0; 0,1; …; 1
//   physbench stack        [--hz 60] [--boxes 10] [--iters 10] [--warm 1] [--time 10] [--out f.csv]
//   physbench stack-sweep  [--hz 60] [--boxes 10] [--time 10] [--out f.csv]  iters × warm
//   physbench tunnel       [--hz 60] [--v 2000] [--u -15]   один снаряд, печать положений у стенки
//   physbench tunnel-sweep [--runs 1000] [--seed 1] [--out f.csv]  v = 250…6000, 60/120/240 Гц
//   physbench ccd-check    [--runs 2000] [--hz 60]   точность момента и точки удара CCD
//   physbench multirate-sweep [--runs 1000]   снаряд на 60·N Гц, обычные тела 60 Гц, N = 1…4
//   physbench timing [--normal 40] [--fast 5] [--time 10] [--reps 7] [--out f.csv]  ms/frame по режимам
//   ключ --mode discrete|ccd|multirate принимают determinism, bounce*, stack, tunnel
//   (для multirate ещё --fast-hz 240; --hz — частота обычных тел)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "core/timestep.h"
#include "core/world.h"
#include "scenes/scenes.h"

using namespace phys;

namespace {

struct Args {
    int argc; char** argv;
    const char* get(const char* key, const char* def) const {
        for (int i = 2; i + 1 < argc; ++i)
            if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
        return def;
    }
    double num(const char* key, double def) const {
        const char* v = get(key, nullptr);
        return v ? std::atof(v) : def;
    }
};

StepMode parseMode(const Args& a) {
    const char* m = a.get("--mode", "discrete");
    if (std::strcmp(m, "ccd") == 0) return StepMode::CCD;
    if (std::strcmp(m, "multirate") == 0) return StepMode::MultiRate;
    return StepMode::Discrete;
}
const char* modeName(StepMode m) {
    return m == StepMode::CCD ? "ccd" : m == StepMode::MultiRate ? "multirate" : "discrete";
}

FILE* openOut(const Args& a) {
    const char* out = a.get("--out", nullptr);
    return out ? std::fopen(out, "w") : nullptr;
}

// ---------------------------------------------------------------------------
// Свободное падение из покоя (шаг 0). Ошибка полунеявного Эйлера = g·dt·t/2.
// ---------------------------------------------------------------------------
int runFreefall(const Args& a) {
    const double hz = a.num("--hz", 60);
    const double T = a.num("--time", 1.0);
    const double dt = 1.0 / hz;

    World w;
    w.add(makeDynamic({0, 0}, {10, 10}, {0, 0}, BodyGroup::Normal));
    const double g = w.params.gravity.y;

    FILE* f = openOut(a);
    if (f) std::fprintf(f, "t,y_num,y_exact,err,err_pred\n");
    const int steps = int(std::lround(T * hz));
    double t = 0;
    for (int n = 1; n <= steps; ++n) {
        w.integrate(dt);
        t = n * dt;
        double yNum = w.bodies[0].pos.y, yEx = 0.5 * g * t * t;
        if (f) std::fprintf(f, "%.6f,%.9f,%.9f,%.9f,%.9f\n", t, yNum, yEx, yNum - yEx, 0.5 * g * dt * t);
    }
    if (f) std::fclose(f);
    double yNum = w.bodies[0].pos.y, yEx = 0.5 * g * t * t;
    std::printf("freefall: hz=%.0f t=%.4f s  y_num=%.6f  y_exact=%.6f  err=%.6f  pred g*dt*t/2=%.6f\n",
                hz, t, yNum, yEx, yNum - yEx, 0.5 * g * dt * t);
    return 0;
}

// ---------------------------------------------------------------------------
// Детерминированность: два прогона фиксированным циклом и прогон через
// аккумулятор со случайным временем кадра 4…40 мс. --room 1 — со столкновениями.
// ---------------------------------------------------------------------------
uint64_t runFixed(int bodies, uint64_t seed, int steps, double dt, bool room, StepMode mode) {
    World w;
    scenes::buildRandomCloud(w, bodies, seed, room);
    for (int i = 0; i < steps; ++i) w.step(dt, mode);
    return w.stateHash();
}

uint64_t runAccumulated(int bodies, uint64_t seed, int steps, double dt, bool room, StepMode mode,
                        uint64_t fpsSeed, int* framesOut) {
    World w;
    scenes::buildRandomCloud(w, bodies, seed, room);
    FixedStepAccumulator acc(dt);
    scenes::Lcg frameRng(fpsSeed);
    int done = 0, frames = 0;
    while (done < steps) {
        acc.advance(frameRng.uniform(0.004, 0.040), [&](double h) {
            if (done < steps) { w.step(h, mode); ++done; }
        });
        ++frames;
    }
    *framesOut = frames;
    return w.stateHash();
}

int runDeterminism(const Args& a) {
    const int bodies = int(a.num("--bodies", 50));
    const int steps = int(a.num("--steps", 600));
    const uint64_t seed = uint64_t(a.num("--seed", 1));
    const bool room = a.num("--room", 0) != 0;
    const StepMode mode = parseMode(a);
    const double dt = 1.0 / 60.0;

    uint64_t h1 = runFixed(bodies, seed, steps, dt, room, mode);
    uint64_t h2 = runFixed(bodies, seed, steps, dt, room, mode);
    int frames = 0;
    uint64_t h3 = runAccumulated(bodies, seed, steps, dt, room, mode, 777, &frames);

    std::printf("determinism: bodies=%d steps=%d room=%d mode=%s\n", bodies, steps, room ? 1 : 0, modeName(mode));
    std::printf("  fixed run #1      hash=%016llx\n", (unsigned long long)h1);
    std::printf("  fixed run #2      hash=%016llx\n", (unsigned long long)h2);
    std::printf("  accumulator run   hash=%016llx  (%d frames of random length 4..40 ms)\n",
                (unsigned long long)h3, frames);
    bool ok = (h1 == h2) && (h1 == h3);
    std::printf("  result: %s\n", ok ? "IDENTICAL" : "DIFFERENT");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Отскок. Ящик падает из покоя с высоты h (нижняя грань над полом).
// Теория: скорость удара √(2gh); после удара v' = −e·v; высота отскока h' = e²·h.
// Измеряем: скорость на шаге перед ударом, скорость сразу после,
// наибольшую высоту нижней грани после первого удара.
// ---------------------------------------------------------------------------
// vIn — скорость на шаге перед ударом; vSolve = vIn + g·dt — скорость сближения,
// которую видит решатель (гравитация этого шага уже добавлена) — именно её он
// разворачивает: vOut = e·vSolve. depth0 — проникновение в момент обнаружения.
struct BounceResult { double vIn, vSolve, vOut, hApex, tImpact, depth0; };

BounceResult simulateBounce(double hz, double e, double h, FILE* f, StepMode mode = StepMode::Discrete) {
    World w;
    w.params.restitution = e;
    scenes::buildBounce(w, h);
    const int box = int(w.bodies.size()) - 1;
    const double dt = 1.0 / hz;
    const double half = w.bodies[box].half.y;

    BounceResult r{0, 0, 0, 0, 0, 0};
    bool impacted = false, apexDone = false;
    double prevVy = 0;
    const int steps = int(4.0 * hz);
    for (int n = 1; n <= steps && !apexDone; ++n) {
        w.step(dt, mode);
        const Body& b = w.bodies[box];
        double height = scenes::kFloorTop - (b.pos.y + half);
        if (f) std::fprintf(f, "%.6f,%.6f,%.6f\n", n * dt, height, b.vel.y);
        if (!impacted && b.vel.y <= 0 && prevVy > 0) {
            impacted = true;
            r.vIn = prevVy;       // скорость падения перед ударом (вниз, > 0)
            r.vSolve = prevVy + w.params.gravity.y * dt;
            r.vOut = -b.vel.y;    // скорость отскока (вверх, > 0)
            r.depth0 = w.contacts.empty() ? 0 : w.contacts[0].depth;
            if (!w.toiEvents.empty()) r.vSolve = -w.toiEvents[0].vnBefore; // CCD: скорость в момент удара
            r.tImpact = n * dt;
            r.hApex = height;
        } else if (impacted) {
            r.hApex = std::max(r.hApex, height);
            if (b.vel.y > 0) apexDone = true; // начал падать — вершина пройдена
        }
        prevVy = b.vel.y;
    }
    return r;
}

int runBounce(const Args& a) {
    const double hz = a.num("--hz", 60), e = a.num("--e", 0.5), h = a.num("--h", 300);
    const StepMode mode = parseMode(a);
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "t,height,vy\n");
    BounceResult r = simulateBounce(hz, e, h, f, mode);
    if (f) std::fclose(f);
    const double g = 980;
    std::printf("bounce: hz=%.0f e=%.2f h=%.1f mode=%s\n", hz, e, h, modeName(mode));
    const double dt = 1.0 / hz;
    std::printf("  v_in (step before) = %.3f px/s   (theory sqrt(2gh) = %.3f)\n", r.vIn, std::sqrt(2 * g * h));
    std::printf("  v_solve = v_in + g*dt = %.3f px/s, depth at detection = %.3f px\n", r.vSolve, r.depth0);
    std::printf("  v_out = %.3f px/s   v_out/v_solve = %.6f (theory e = %.4f)\n", r.vOut, r.vOut / r.vSolve, e);
    std::printf("  apex  = %.3f px  (continuous e^2*h = %.3f; semi-implicit from v_out: v^2/2g - v*dt/2 = %.3f)\n",
                r.hApex, e * e * h, r.vOut * r.vOut / (2 * g) - r.vOut * dt / 2);
    return 0;
}

int runBounceSweep(const Args& a) {
    const double hz = a.num("--hz", 60), h = a.num("--h", 300);
    const StepMode mode = parseMode(a);
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "e,v_in,v_solve,v_out,ratio,apex,apex_over_h,theory_e2\n");
    std::printf("bounce-sweep: hz=%.0f h=%.0f mode=%s\n   e    ratio   h'/h   e^2\n", hz, h, modeName(mode));
    for (int i = 0; i <= 10; ++i) {
        double e = i / 10.0;
        BounceResult r = simulateBounce(hz, e, h, nullptr, mode);
        double ratio = r.vSolve > 0 ? r.vOut / r.vSolve : 0;
        if (f) std::fprintf(f, "%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", e, r.vIn, r.vSolve, r.vOut, ratio, r.hApex, r.hApex / h, e * e);
        std::printf("  %.1f  %.4f  %.4f  %.4f\n", e, ratio, r.hApex / h, e * e);
    }
    if (f) std::fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// Устойчивость стека. n блоков стоят вплотную на полу; T секунд моделирования.
// Метрики: просадка верхнего блока (насколько он опустился относительно
// идеального положения), наибольшее горизонтальное смещение любого блока,
// наибольшее проникновение, средняя кинетическая энергия за последнюю секунду
// («дрожание»: у покоящегося стека она должна быть ≈ 0).
// ---------------------------------------------------------------------------
struct StackResult { double sinkTop, maxDx, maxDepth, keLast, sinkTop1s, toiPerStep, exhaustedShare; };

StackResult simulateStack(double hz, int boxes, int iters, bool warm, double T, FILE* f,
                          StepMode mode = StepMode::Discrete) {
    World w;
    w.params.velocityIterations = iters;
    w.params.warmStart = warm;
    scenes::buildStack(w, boxes);
    const int first = int(w.bodies.size()) - boxes, top = int(w.bodies.size()) - 1;
    std::vector<Vec2> start;
    for (int i = first; i <= top; ++i) start.push_back(w.bodies[i].pos);

    const double dt = 1.0 / hz;
    const int steps = int(std::lround(T * hz));
    StackResult r{0, 0, 0, 0, 0, 0, 0};
    double keSum = 0; int keN = 0;
    long long toiTotal = 0, exhausted = 0;
    for (int n = 1; n <= steps; ++n) {
        w.step(dt, mode);
        toiTotal += (long long)w.toiEvents.size();
        exhausted += w.stats.toiExhausted ? 1 : 0;
        double sink = w.bodies[top].pos.y - start.back().y;
        for (int i = first; i <= top; ++i)
            r.maxDx = std::max(r.maxDx, std::fabs(w.bodies[i].pos.x - start[i - first].x));
        r.maxDepth = std::max(r.maxDepth, w.stats.maxDepth);
        double ke = w.kineticEnergy();
        if (n * dt > T - 1.0) { keSum += ke; ++keN; }
        if (n == int(hz)) r.sinkTop1s = sink;
        if (f) std::fprintf(f, "%.6f,%.6f,%.6f,%.9f\n", n * dt, sink, w.stats.maxDepth, ke);
    }
    r.sinkTop = w.bodies[top].pos.y - start.back().y;
    r.keLast = keN ? keSum / keN : 0;
    r.toiPerStep = double(toiTotal) / steps;
    r.exhaustedShare = double(exhausted) / steps;
    return r;
}

int runStack(const Args& a) {
    const double hz = a.num("--hz", 60), T = a.num("--time", 10);
    const int boxes = int(a.num("--boxes", 10)), iters = int(a.num("--iters", 10));
    const bool warm = a.num("--warm", 1) != 0;
    const StepMode mode = parseMode(a);
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "t,sink_top,max_depth,ke\n");
    StackResult r = simulateStack(hz, boxes, iters, warm, T, f, mode);
    if (f) std::fclose(f);
    std::printf("stack: hz=%.0f boxes=%d iters=%d warm=%d T=%.0f s mode=%s\n", hz, boxes, iters, warm, T, modeName(mode));
    std::printf("  top sink after 1 s = %.4f px, after T = %.4f px\n", r.sinkTop1s, r.sinkTop);
    std::printf("  max |dx| = %.6f px, max depth = %.4f px, mean KE last 1 s = %.6g\n", r.maxDx, r.maxDepth, r.keLast);
    std::printf("  CCD impacts per step = %.2f, steps with TOI limit exhausted = %.1f%%\n", r.toiPerStep, 100 * r.exhaustedShare);
    return 0;
}

int runStackSweep(const Args& a) {
    const double hz = a.num("--hz", 60), T = a.num("--time", 10);
    const int boxes = int(a.num("--boxes", 10));
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "iters,warm,sink_top,sink_top_1s,max_dx,max_depth,ke_last\n");
    std::printf("stack-sweep: hz=%.0f boxes=%d T=%.0f s\n  iters warm  sink(T)   sink(1s)  maxDepth  KE_last\n", hz, boxes, T);
    const int itersList[] = {1, 2, 4, 8, 16};
    for (int warm = 0; warm <= 1; ++warm)
        for (int iters : itersList) {
            StackResult r = simulateStack(hz, boxes, iters, warm, T, nullptr);
            if (f) std::fprintf(f, "%d,%d,%.6f,%.6f,%.6f,%.6f,%.9g\n", iters, warm, r.sinkTop, r.sinkTop1s, r.maxDx, r.maxDepth, r.keLast);
            std::printf("  %5d %4d  %8.4f  %8.4f  %8.4f  %.4g\n", iters, warm, r.sinkTop, r.sinkTop1s, r.maxDepth, r.keLast);
        }
    if (f) std::fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// Туннелирование (дискретный режим). Снаряд s = 8 px летит без гравитации на
// стенку w = 10 px. u — положение центра снаряда относительно центра стенки в
// первый момент, когда u ≥ −(w+s)/2. Исход:
//   miss   — пересечение ни разу не обнаружено (проскок «между кадрами»);
//   pushed — обнаружено, но центр уже за серединой стенки: решатель выталкивает
//            снаряд по оси наименьшего перекрытия — вперёд, и он пролетает;
//   bounce — обнаружено до середины: отскок.
// Теория при равномерной фазе, L = v·Δt:
//   P_miss = max(0, 1 − (w+s)/L),   P_pass = P_miss + P_pushed = max(0, 1 − (w+s)/(2L)).
// ---------------------------------------------------------------------------
enum class TunnelOutcome { Miss, Pushed, Bounce, Undecided };

// hz — частота шага; в multi-rate это частота обычных тел, а снаряд шагает
// с fastHz (по ней же отсчитывается фаза u).
TunnelOutcome runTunnelOnce(double v, double hz, double u, std::vector<double>* samples = nullptr,
                            StepMode mode = StepMode::Discrete, double fastHz = 0) {
    World w;
    if (fastHz <= 0) fastHz = hz;
    scenes::buildTunnel(w, v, mode == StepMode::MultiRate ? fastHz : hz, u);
    w.params.fastSubsteps = int(std::lround(fastHz / hz));
    const int wall = 3, proj = 4;
    const double dt = 1.0 / hz, xw = scenes::kTunnelWallX;
    const double halfSum = (scenes::kTunnelWallW + scenes::kProjSize) / 2;
    bool detected = false;
    for (int n = 0; n < 100000; ++n) {
        w.step(dt, mode);
        for (const Contact& c : w.contacts)
            if (c.a == wall && c.b == proj) detected = true;
        for (const ToiEvent& ev : w.toiEvents)
            if (ev.a == wall && ev.b == proj) detected = true;
        const Body& p = w.bodies[proj];
        if (samples && std::fabs(p.pos.x - xw) < 60) samples->push_back(p.pos.x - xw);
        if (p.vel.x < 0) return TunnelOutcome::Bounce;
        if (p.pos.x - xw >= halfSum) return detected ? TunnelOutcome::Pushed : TunnelOutcome::Miss;
    }
    return TunnelOutcome::Undecided;
}

const char* outcomeName(TunnelOutcome o) {
    switch (o) {
    case TunnelOutcome::Miss: return "MISS (no overlap ever detected) -> passed through";
    case TunnelOutcome::Pushed: return "PUSHED (overlap detected past the middle) -> passed through";
    case TunnelOutcome::Bounce: return "BOUNCE";
    default: return "undecided";
    }
}

int runTunnel(const Args& a) {
    const double hz = a.num("--hz", 60), v = a.num("--v", 2000), u = a.num("--u", -15);
    const StepMode mode = parseMode(a);
    const double fastHz = mode == StepMode::MultiRate ? a.num("--fast-hz", 240) : hz;
    std::vector<double> xs;
    TunnelOutcome o = runTunnelOnce(v, hz, u, &xs, mode, fastHz);
    std::printf("tunnel: hz=%.0f v=%.0f L=v*dt=%.3f px mode=%s fast_hz=%.0f, |x - x_wall| < %.1f means overlap\n",
                hz, v, v / fastHz, modeName(mode), fastHz, (scenes::kTunnelWallW + scenes::kProjSize) / 2);
    std::printf("  positions relative to wall center:");
    for (double x : xs) std::printf(" %.2f", x);
    std::printf("\n  outcome: %s\n", outcomeName(o));

    // Траектория для рисунка: (t, x − x_wall) в концах шагов и в моментах удара.
    if (const char* out = a.get("--trace", nullptr)) {
        World w;
        scenes::buildTunnel(w, v, hz, u);
        const int proj = 4;
        const double dt = 1.0 / hz, xw = scenes::kTunnelWallX;
        FILE* f = std::fopen(out, "w");
        std::fprintf(f, "t,x,kind\n");
        double t = 0;
        std::fprintf(f, "%.9f,%.9f,step\n", t, w.bodies[proj].pos.x - xw);
        const int nSteps = int(700.0 / (v * dt)) + 2; // ~500 px подлёта и ~200 px после стенки
        for (int n = 0; n < nSteps; ++n) {
            w.step(dt, mode);
            for (const ToiEvent& ev : w.toiEvents)
                std::fprintf(f, "%.9f,%.9f,impact\n", t + ev.t, (ev.b == proj ? ev.posB.x : ev.posA.x) - xw);
            t += dt;
            double x = w.bodies[proj].pos.x - xw;
            std::fprintf(f, "%.9f,%.9f,step\n", t, x);
        }
        std::fclose(f);
    }
    return 0;
}

int runTunnelSweep(const Args& a) {
    const int runs = int(a.num("--runs", 1000));
    const uint64_t seed = uint64_t(a.num("--seed", 1));
    const StepMode mode = parseMode(a);
    const double halfSum = (scenes::kTunnelWallW + scenes::kProjSize) / 2;
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "hz,v,L,p_miss,p_pushed,p_pass,p_bounce,th_miss,th_pass\n");
    std::printf("tunnel-sweep: runs=%d per point, mode=%s\n   hz     v      L    P_miss(th)      P_pass(th)\n", runs, modeName(mode));
    const double hzs[3] = {60, 120, 240};
    for (double hz : hzs) {
        scenes::Lcg rng(seed);
        for (int vi = 1; vi <= 24; ++vi) {
            double v = 250.0 * vi, L = v / hz;
            int miss = 0, pushed = 0, bounce = 0;
            for (int r = 0; r < runs; ++r) {
                double u = -halfSum + L * rng.uniform(0, 1);
                TunnelOutcome o = runTunnelOnce(v, hz, u, nullptr, mode);
                if (o == TunnelOutcome::Miss) ++miss;
                else if (o == TunnelOutcome::Pushed) ++pushed;
                else if (o == TunnelOutcome::Bounce) ++bounce;
            }
            double pm = double(miss) / runs, pp = double(pushed) / runs, pb = double(bounce) / runs;
            double thm = std::max(0.0, 1 - 2 * halfSum / L), thp = std::max(0.0, 1 - halfSum / L);
            if (f) std::fprintf(f, "%.0f,%.0f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", hz, v, L, pm, pp, pm + pp, pb, thm, thp);
            std::printf("  %4.0f %5.0f %6.2f  %.3f (%.3f)   %.3f (%.3f)\n", hz, v, L, pm, thm, pm + pp, thp);
        }
    }
    if (f) std::fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// Точность CCD. Снаряд со случайной скоростью 250…6000 px/с и случайной фазой
// летит на стенку (60 Гц). На шаге удара сравниваем с аналитикой:
//   t_c = (x_w − (w+s)/2 − x_0) / v   — момент касания от начала шага;
//   x_c = x_w − (w+s)/2               — центр снаряда в момент касания;
//   v'  = −e·v                        — скорость после удара;
//   x_1 = x_c − e·v·(Δt − t_c)        — положение в конце шага.
// ---------------------------------------------------------------------------
int runCcdCheck(const Args& a) {
    const int runs = int(a.num("--runs", 2000));
    const double hz = a.num("--hz", 60), dt = 1.0 / hz;
    const uint64_t seed = uint64_t(a.num("--seed", 3));
    const double halfSum = (scenes::kTunnelWallW + scenes::kProjSize) / 2, xw = scenes::kTunnelWallX;
    scenes::Lcg rng(seed);
    double maxErrT = 0, maxErrX = 0, maxErrV = 0, maxErrEnd = 0;
    int bounced = 0, noEvent = 0;
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "v,u,t_c,t_c_exact,x_c_rel,x_end,x_end_exact,v_after\n");
    for (int r = 0; r < runs; ++r) {
        double v = rng.uniform(250, 6000), L = v / hz;
        double u = -halfSum + L * rng.uniform(0, 1);
        World w;
        scenes::buildTunnel(w, v, hz, u);
        const int wall = 3, proj = 4;
        const double e = w.params.restitution;
        bool found = false;
        for (int n = 0; n < 10000 && !found; ++n) {
            double x0 = w.bodies[proj].pos.x;
            w.stepCCD(dt);
            for (const ToiEvent& ev : w.toiEvents) {
                if (!(ev.a == wall && ev.b == proj)) continue;
                found = true;
                double tc = (xw - halfSum - x0) / v;
                double xEndExact = (xw - halfSum) - e * v * (dt - tc);
                const Body& p = w.bodies[proj];
                maxErrT = std::max(maxErrT, std::fabs(ev.t - tc));
                maxErrX = std::max(maxErrX, std::fabs(ev.posB.x - (xw - halfSum)));
                maxErrV = std::max(maxErrV, std::fabs(p.vel.x - (-e * v)));
                maxErrEnd = std::max(maxErrEnd, std::fabs(p.pos.x - xEndExact));
                if (p.vel.x < 0) ++bounced;
                if (f) std::fprintf(f, "%.6f,%.6f,%.12f,%.12f,%.12f,%.9f,%.9f,%.9f\n", v, u, ev.t, tc,
                                    ev.posB.x - xw, p.pos.x, xEndExact, p.vel.x);
                break;
            }
            if (w.bodies[proj].pos.x > xw + 100) break;
        }
        if (!found) ++noEvent;
    }
    if (f) std::fclose(f);
    std::printf("ccd-check: hz=%.0f runs=%d (v = 250..6000 px/s, random phase)\n", hz, runs);
    std::printf("  impacts found: %d, bounced: %d, no impact: %d\n", runs - noEvent, bounced, noEvent);
    std::printf("  max |t_c - exact|      = %.3e s\n", maxErrT);
    std::printf("  max |x_c - x_face|     = %.3e px\n", maxErrX);
    std::printf("  max |v_after + e*v|    = %.3e px/s\n", maxErrV);
    std::printf("  max |x_end - exact|    = %.3e px\n", maxErrEnd);
    return 0;
}

// ---------------------------------------------------------------------------
// Multi-rate, серия: обычные тела 60 Гц, снаряд — fast = 60·N Гц, N = 1…4.
// Теория та же, что для дискретного режима, но с шагом снаряда L = v/fast.
// ---------------------------------------------------------------------------
int runMultirateSweep(const Args& a) {
    const int runs = int(a.num("--runs", 1000));
    const uint64_t seed = uint64_t(a.num("--seed", 1));
    const double slowHz = a.num("--hz", 60);
    const double halfSum = (scenes::kTunnelWallW + scenes::kProjSize) / 2;
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "slow_hz,fast_hz,v,L,p_miss,p_pass,th_miss,th_pass\n");
    std::printf("multirate-sweep: slow=%.0f Hz, runs=%d\n  fast     v      L    P_pass (theory)\n", slowHz, runs);
    for (int N = 1; N <= 4; ++N) {
        const double fastHz = slowHz * N;
        scenes::Lcg rng(seed);
        for (int vi = 1; vi <= 24; ++vi) {
            double v = 250.0 * vi, L = v / fastHz;
            int miss = 0, pass = 0;
            for (int r = 0; r < runs; ++r) {
                double u = -halfSum + L * rng.uniform(0, 1);
                TunnelOutcome o = runTunnelOnce(v, slowHz, u, nullptr, StepMode::MultiRate, fastHz);
                if (o == TunnelOutcome::Miss) { ++miss; ++pass; }
                else if (o == TunnelOutcome::Pushed) ++pass;
            }
            double thm = std::max(0.0, 1 - 2 * halfSum / L), thp = std::max(0.0, 1 - halfSum / L);
            if (f) std::fprintf(f, "%.0f,%.0f,%.0f,%.6f,%.6f,%.6f,%.6f,%.6f\n", slowHz, fastHz, v, L,
                                double(miss) / runs, double(pass) / runs, thm, thp);
            if (v == 2000) std::printf("  %4.0f  %5.0f  %6.2f  %.3f (%.3f)\n", fastHz, v, L, double(pass) / runs, thp);
        }
    }
    if (f) std::fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// Время расчёта по режимам. Сцена buildMixed: nNormal обычных тел + nFast
// снарядов; T секунд модельного времени. Для каждого режима — медиана из reps
// повторов; выводится время физики на кадр 1/60 с (ms/frame) и число шагов
// быстрых тел на кадр. «Все на 240 Гц» — дискретный режим с Δt = 1/240 для всех.
// ---------------------------------------------------------------------------
struct TimingCase { const char* name; StepMode mode; double hz; int nFast; };

double timeCase(const TimingCase& c, int nNormal, int nFast, double v, double T, uint64_t seed,
                int* passedOut, long long* fastStepsOut) {
    World w;
    double fastHzForPhase = c.mode == StepMode::MultiRate ? c.hz * c.nFast : c.hz;
    scenes::buildMixed(w, nNormal, nFast, v, fastHzForPhase, seed);
    w.params.fastSubsteps = c.nFast;
    const double dt = 1.0 / c.hz;
    const int steps = int(std::lround(T * c.hz));
    long long fastSteps = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < steps; ++i) {
        w.step(dt, c.mode);
        fastSteps += c.mode == StepMode::MultiRate ? c.nFast : 1;
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    *fastStepsOut = fastSteps;
    // Отдельный (не замеряемый) прогон: сколько снарядов хоть раз оказались за стенкой.
    World w2;
    scenes::buildMixed(w2, nNormal, nFast, v, fastHzForPhase, seed);
    w2.params.fastSubsteps = c.nFast;
    std::vector<bool> behind(w2.bodies.size(), false);
    const double half = (scenes::kTunnelWallW + scenes::kProjSize) / 2;
    for (int i = 0; i < steps; ++i) {
        w2.step(dt, c.mode);
        for (size_t k = 0; k < w2.bodies.size(); ++k)
            if (w2.bodies[k].group == BodyGroup::Fast && w2.bodies[k].pos.x >= scenes::kTunnelWallX + half)
                behind[k] = true;
    }
    int passed = 0;
    for (bool b : behind) passed += b ? 1 : 0;
    *passedOut = passed;
    return sec;
}

int runTiming(const Args& a) {
    const int nNormal = int(a.num("--normal", 40)), nFast = int(a.num("--fast", 5));
    const double v = a.num("--v", 2000), T = a.num("--time", 10);
    const int reps = int(a.num("--reps", 7));
    const uint64_t seed = uint64_t(a.num("--seed", 5));
    const TimingCase cases[] = {
        {"discrete 60 Hz", StepMode::Discrete, 60, 1},
        {"discrete 240 Hz (all bodies)", StepMode::Discrete, 240, 1},
        {"CCD 60 Hz", StepMode::CCD, 60, 1},
        {"multi-rate 60/60 Hz", StepMode::MultiRate, 60, 1},
        {"multi-rate 60/120 Hz", StepMode::MultiRate, 60, 2},
        {"multi-rate 60/240 Hz", StepMode::MultiRate, 60, 4},
    };
    FILE* f = openOut(a);
    if (f) std::fprintf(f, "case,ms_per_frame,ms_per_frame_median,fast_steps_per_frame,projectiles_behind_wall\n");
    std::printf("timing: normal=%d fast=%d v=%.0f T=%.0f s, %d runs (min and median)\n", nNormal, nFast, v, T, reps);
    std::printf("  %-30s %10s %10s %16s %10s\n", "case", "ms/fr min", "ms/fr med", "fast steps/frame", "passed");
    const double frames = T * 60.0;
    for (const TimingCase& c : cases) {
        std::vector<double> ts;
        int passed = 0; long long fs = 0;
        for (int r = 0; r < reps; ++r) ts.push_back(timeCase(c, nNormal, nFast, v, T, seed, &passed, &fs));
        std::sort(ts.begin(), ts.end());
        // Минимум по повторам — оценка «чистого» времени без помех от других
        // процессов; медиана — для контроля разброса.
        double ms = ts.front() * 1000.0 / frames;
        double msMed = ts[reps / 2] * 1000.0 / frames;
        std::printf("  %-30s %10.4f %10.4f %16.1f %7d/%d\n", c.name, ms, msMed, fs / frames, passed, nFast);
        if (f) std::fprintf(f, "%s,%.6f,%.6f,%.3f,%d\n", c.name, ms, msMed, fs / frames, passed);
    }
    if (f) std::fclose(f);
    return 0;
}

}  // namespace (временно закрываем, чтобы объявить внешние функции)
int mainCompare(int argc, char** argv);
int mainTiming(int argc, char** argv);
namespace {

void usage() {
    std::printf("usage: physbench <experiment> [options]\n"
                "  freefall | determinism | bounce | bounce-sweep | stack | stack-sweep | tunnel | tunnel-sweep | ccd-check\n"
                "  multirate-sweep | timing | main-compare | main-timing\n"
                "  most experiments accept --mode discrete|ccd|multirate\n"
                "  see the header of src/bench/bench_main.cpp for options\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    Args a{argc, argv};
    std::string cmd = argv[1];
    if (cmd == "freefall") return runFreefall(a);
    if (cmd == "determinism") return runDeterminism(a);
    if (cmd == "bounce") return runBounce(a);
    if (cmd == "bounce-sweep") return runBounceSweep(a);
    if (cmd == "stack") return runStack(a);
    if (cmd == "stack-sweep") return runStackSweep(a);
    if (cmd == "tunnel") return runTunnel(a);
    if (cmd == "tunnel-sweep") return runTunnelSweep(a);
    if (cmd == "ccd-check") return runCcdCheck(a);
    if (cmd == "multirate-sweep") return runMultirateSweep(a);
    if (cmd == "timing") return runTiming(a);
    if (cmd == "main-compare") return mainCompare(argc, argv);
    if (cmd == "main-timing") return mainTiming(argc, argv);
    usage();
    return 1;
}
