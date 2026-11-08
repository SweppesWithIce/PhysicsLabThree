// Сборка (из корня репозитория):
//   g++ -O2 -std=c++17 -Isrc tools/debug_main.cpp src/core/world.cpp src/scenes/scenes.cpp src/scenes/main_scene.cpp -o debug_main
// Запуск: ./debug_main discrete|ccd|multirate — печатает каждый проскок в основной сцене
// вместе с состоянием CCD (исчерпан ли лимит ударов) и контактами снаряда.
// Отладка: где и почему снаряды проходят препятствия в основной сцене.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "core/world.h"
#include "scenes/main_scene.h"
using namespace phys;
int main(int argc, char** argv) {
    StepMode mode = StepMode::CCD;
    if (argc > 1 && !std::strcmp(argv[1], "multirate")) mode = StepMode::MultiRate;
    if (argc > 1 && !std::strcmp(argv[1], "discrete")) mode = StepMode::Discrete;
    World w; scenes::MainScene ms; ms.build(w);
    const double dt = 1.0 / 60;
    for (int n = 0; n < 600; ++n) {
        std::vector<Body> before = w.bodies;
        int ev0 = ms.tunnelEvents;
        w.step(dt, mode);
        ms.afterStep(w, dt);
        if (ms.tunnelEvents > ev0) {
            for (int fi : ms.fastIdx) {
                const Body& b = w.bodies[fi]; const Body& p = before[fi];
                std::printf("tick %d proj %d: prev (%.1f,%.1f) v(%.0f,%.0f) -> now (%.1f,%.1f) v(%.0f,%.0f) toiEvents=%zu exhausted=%d\n",
                    n, fi, p.pos.x, p.pos.y, p.vel.x, p.vel.y, b.pos.x, b.pos.y, b.vel.x, b.vel.y, w.toiEvents.size(), (int)w.stats.toiExhausted);
            }
            for (const Contact& c : w.contactLog)
                for (int fi : ms.fastIdx) if (c.a == fi || c.b == fi)
                    std::printf("   contact %d-%d n(%.0f,%.0f) depth %.2f\n", c.a, c.b, c.n.x, c.n.y, c.depth);
        }
    }
    std::printf("total tunnel events %d of %d shots\n", ms.tunnelEvents, ms.shots);
}
