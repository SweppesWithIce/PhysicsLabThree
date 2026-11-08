// Эксперименты шага 6 на основной сцене (scenes::MainScene).
//
//   physbench main-compare [--time 60] [--out data/main_compare.csv]
//       режимы: дискретный 60, CCD 60, multi-rate 60/240 (+ справочно: дискретный 240 для всех,
//       multi-rate 60/120); метрики: проскоки, глубина и отношение скоростей при ударе о мишень,
//       устойчивость стека, детерминированность, рассинхронизация групп (multi-rate).
//   physbench main-timing  [--time 20] [--reps 9] [--out data/main_timing.csv]
//       ms/frame и время одного шага при 20, 50, 70, 100 обычных телах (включая стек).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core/timestep.h"
#include "core/world.h"
#include "scenes/main_scene.h"
#include "scenes/scenes.h"

using namespace phys;

namespace mainexp {

struct Case { const char* key; const char* name; StepMode mode; double hz; int n; };

const Case kCases[] = {
    {"discrete60", "discrete 60 Hz", StepMode::Discrete, 60, 1},
    {"ccd60", "CCD 60 Hz", StepMode::CCD, 60, 1},
    {"multirate240", "multi-rate 60/240 Hz", StepMode::MultiRate, 60, 4},
    {"multirate120", "multi-rate 60/120 Hz", StepMode::MultiRate, 60, 2},
    {"discrete240", "discrete 240 Hz (all)", StepMode::Discrete, 240, 1},
};

struct Result {
    int shots = 0, tunnels = 0;
    int hits = 0;                  // первых ударов снаряда о мишень
    double depthSum = 0, depthMax = 0;   // проникновение в мишень в момент обнаружения, px
    double ratioErrMax = 0;        // max |v_после/v_до + e| по x среди отскочивших
    int bounced = 0;               // удары, после которых снаряд полетел назад
    double stackDxMax = 0, stackSink = 0, stackKe = 0, stackVmax = 0;
    uint64_t hash = 0;
    double desyncSum = 0, desyncMax = 0, desyncPred = 0; int desyncN = 0;
    double toiPerStep = 0; double exhaustedShare = 0;
};

// Один прогон основной сцены длительностью T с; frameRng ≠ 0 — шаги через
// аккумулятор со случайной длиной кадра (проверка независимости от FPS).
Result run(const Case& c, double T, int nNormal, uint64_t frameSeed = 0) {
    World w;
    scenes::MainScene ms;
    ms.cfg.nNormal = nNormal;
    ms.build(w);
    w.params.fastSubsteps = c.n;
    const double dt = 1.0 / c.hz, e = w.params.restitution;
    const int steps = int(std::lround(T * c.hz));
    Result r;
    std::vector<bool> hitDone(w.bodies.size(), false);
    std::vector<int> shotOf(w.bodies.size(), 0);
    int lastShots = ms.shots;
    long long toi = 0, exhausted = 0;
    const int nTargets = 3;
    const Body& top0 = w.bodies[ms.stackTop];
    const double topX0 = top0.pos.x, topY0 = top0.pos.y;

    auto oneStep = [&](double h) {
        std::vector<Vec2> velBefore(w.bodies.size());
        for (int fi : ms.fastIdx) velBefore[fi] = w.bodies[fi].vel;
        w.step(h, c.mode);
        toi += (long long)w.toiEvents.size();
        exhausted += w.stats.toiExhausted ? 1 : 0;
        // Первый удар о мишень после каждого выстрела.
        for (int fi : ms.fastIdx) {
            if (hitDone[fi]) continue;
            bool hit = false; double depth = 0;
            for (const Contact& ct : w.contactLog)
                for (int t = 0; t < nTargets; ++t) {
                    int oi = ms.obstacleIdx[t];
                    if ((ct.a == oi && ct.b == fi) || (ct.b == oi && ct.a == fi)) { hit = true; depth = std::max(depth, ct.depth); }
                }
            for (const ToiEvent& ev : w.toiEvents)
                for (int t = 0; t < nTargets; ++t) {
                    int oi = ms.obstacleIdx[t];
                    if ((ev.a == oi && ev.b == fi) || (ev.b == oi && ev.a == fi)) hit = true;
                }
            if (!hit) continue;
            hitDone[fi] = true;
            ++r.hits;
            r.depthSum += depth;
            r.depthMax = std::max(r.depthMax, depth);
            double vb = velBefore[fi].x, va = w.bodies[fi].vel.x;
            if (vb > 0 && va < 0) {
                ++r.bounced;
                r.ratioErrMax = std::max(r.ratioErrMax, std::fabs(va / vb + e));
            }
        }
        // Рассинхронизация (multi-rate): обычное тело в контакте с быстрым на подшаге k
        // стоит в положении конца тика, а «истинное» положение в конце подшага —
        // lerp(prevPos, pos, (k+1)/N). Смещение = (1 − (k+1)/N)·|Δx тика|.
        if (c.mode == StepMode::MultiRate) {
            for (const Contact& ct : w.contactLog) {
                if (ct.substep < 0) continue;
                for (int idx : {ct.a, ct.b}) {
                    const Body& b = w.bodies[idx];
                    if (b.group != BodyGroup::Normal) continue;
                    Vec2 truePos = lerp(b.prevPos, b.pos, double(ct.substep + 1) / c.n);
                    double d = length(b.pos - truePos);
                    double pred = (1.0 - double(ct.substep + 1) / c.n) * length(b.vel) * h;
                    r.desyncSum += d; r.desyncMax = std::max(r.desyncMax, d);
                    r.desyncPred = std::max(r.desyncPred, pred);
                    ++r.desyncN;
                }
            }
        }
        ms.afterStep(w, h);
        if (ms.shots != lastShots) { std::fill(hitDone.begin(), hitDone.end(), false); lastShots = ms.shots; }
    };

    int done = 0;
    if (frameSeed == 0) {
        for (; done < steps; ++done) {
            oneStep(dt);
            double t = (done + 1) * dt;
            const Body& top = w.bodies[ms.stackTop];
            if (t > 1.0) {
                r.stackDxMax = std::max(r.stackDxMax, std::fabs(top.pos.x - topX0));
                for (int i = ms.stackFirst; i <= ms.stackTop; ++i)
                    r.stackVmax = std::max(r.stackVmax, length(w.bodies[i].vel));
            }
            if (t > T - 10.0) {
                double ke = 0;
                for (int i = ms.stackFirst; i <= ms.stackTop; ++i)
                    ke += 0.5 * dot(w.bodies[i].vel, w.bodies[i].vel) / w.bodies[i].invMass;
                r.stackKe += ke / (10.0 * c.hz);
            }
        }
    } else {
        FixedStepAccumulator acc(dt);
        scenes::Lcg fr(frameSeed);
        while (done < steps)
            acc.advance(fr.uniform(0.004, 0.040), [&](double h) { if (done < steps) { oneStep(h); ++done; } });
    }
    r.stackSink = w.bodies[ms.stackTop].pos.y - topY0;
    r.shots = ms.shots;
    r.tunnels = ms.tunnelEvents;
    r.hash = w.stateHash();
    r.toiPerStep = double(toi) / steps;
    r.exhaustedShare = double(exhausted) / steps;
    return r;
}

int compare(int argc, char** argv) {
    double T = 60;
    const char* out = nullptr;
    for (int i = 2; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--time")) T = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
    }
    FILE* f = out ? std::fopen(out, "w") : nullptr;
    if (f) std::fprintf(f, "case,name,shots,tunnels,tunnel_share,hits,bounced,depth_mean,depth_max,ratio_err_max,"
                           "stack_dx_max,stack_sink,stack_ke,stack_vmax,hash1,hash2,hash_fps,deterministic,"
                           "desync_mean,desync_max,desync_pred_max,toi_per_step,toi_exhausted_share\n");
    std::printf("main-compare: T=%.0f s, 40 normal + 10-block stack + 5 projectiles @ 4000 px/s\n", T);
    for (const Case& c : kCases) {
        Result a = run(c, T, 40), b = run(c, T, 40), fps = run(c, T, 40, 12345);
        bool det = a.hash == b.hash && a.hash == fps.hash;
        double dm = a.hits ? a.depthSum / a.hits : 0;
        double desMean = a.desyncN ? a.desyncSum / a.desyncN : 0;
        std::printf("  %-24s tunnels %3d/%3d (%.3f)  hits %3d bounced %3d depth mean %.2f max %.2f  |v'/v+e| %.1e\n"
                    "  %-24s stack dx %.3f sink %.3f KE %.2e vmax %.3f  deterministic=%s  desync mean %.2f max %.2f (pred %.2f)\n",
                    c.name, a.tunnels, a.shots, double(a.tunnels) / a.shots, a.hits, a.bounced, dm, a.depthMax, a.ratioErrMax,
                    "", a.stackDxMax, a.stackSink, a.stackKe, a.stackVmax, det ? "yes" : "NO", desMean, a.desyncMax, a.desyncPred);
        if (f) std::fprintf(f, "%s,%s,%d,%d,%.6f,%d,%d,%.6f,%.6f,%.6e,%.6f,%.6f,%.6e,%.6f,%016llx,%016llx,%016llx,%d,%.6f,%.6f,%.6f,%.4f,%.4f\n",
                            c.key, c.name, a.shots, a.tunnels, double(a.tunnels) / a.shots, a.hits, a.bounced, dm, a.depthMax,
                            a.ratioErrMax, a.stackDxMax, a.stackSink, a.stackKe, a.stackVmax,
                            (unsigned long long)a.hash, (unsigned long long)b.hash, (unsigned long long)fps.hash, det ? 1 : 0,
                            desMean, a.desyncMax, a.desyncPred, a.toiPerStep, a.exhaustedShare);
    }
    if (f) std::fclose(f);
    return 0;
}

// Время: ms на кадр (1/60 с) и ms на один шаг модели, минимум из reps повторов.
int timing(int argc, char** argv) {
    double T = 20; int reps = 9;
    const char* out = nullptr;
    for (int i = 2; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--time")) T = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--reps")) reps = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
    }
    FILE* f = out ? std::fopen(out, "w") : nullptr;
    if (f) std::fprintf(f, "case,name,n_normal,n_bodies,ms_per_frame,ms_per_frame_median,ms_per_step,steps_per_frame,fast_steps_per_frame\n");
    std::printf("main-timing: T=%.0f s, min of %d runs\n", T, reps);
    const int counts[] = {10, 40, 60, 90}; // + 10 блоков стека = 20, 50 (основная сцена), 70, 100 обычных тел
    for (int nn : counts) {
        for (const Case& c : kCases) {
            std::vector<double> ts;
            int nb = 0;
            for (int r = 0; r < reps; ++r) {
                World w;
                scenes::MainScene ms;
                ms.cfg.nNormal = nn;
                ms.build(w);
                nb = int(w.bodies.size());
                w.params.fastSubsteps = c.n;
                const double dt = 1.0 / c.hz;
                const int steps = int(std::lround(T * c.hz));
                auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < steps; ++i) { w.step(dt, c.mode); ms.afterStep(w, dt); }
                ts.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            }
            std::sort(ts.begin(), ts.end());
            const double frames = T * 60.0;
            double msf = ts.front() * 1000 / frames, msMed = ts[reps / 2] * 1000 / frames;
            double stepsPerFrame = c.hz / 60.0;
            double fastPerFrame = c.mode == StepMode::MultiRate ? stepsPerFrame * c.n : stepsPerFrame;
            std::printf("  n=%3d (%3d bodies) %-24s %8.4f ms/frame  %8.4f ms/step  fast steps/frame %.0f\n",
                        nn + 10, nb, c.name, msf, msf / stepsPerFrame, fastPerFrame);
            if (f) std::fprintf(f, "%s,%s,%d,%d,%.6f,%.6f,%.6f,%.2f,%.2f\n", c.key, c.name, nn + 10, nb, msf, msMed,
                                msf / stepsPerFrame, stepsPerFrame, fastPerFrame);
        }
    }
    if (f) std::fclose(f);
    return 0;
}

} // namespace mainexp

int mainCompare(int argc, char** argv) { return mainexp::compare(argc, argv); }
int mainTiming(int argc, char** argv) { return mainexp::timing(argc, argv); }
