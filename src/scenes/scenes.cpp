#include "scenes/scenes.h"
#include <cmath>

using namespace phys;

namespace scenes {

void addRoom(World& w) {
    // Видимая толщина стен — 40 px, но сами тела толщиной 200 px уходят за край
    // экрана. Тонкую стенку быстрый снаряд может проскочить; толстые стены
    // комнаты удерживают его до v·Δt < (200 + 8)/2 px, т.е. до ~6200 px/с при 60 Гц.
    const double t = 200, vis = 40;
    w.add(makeStatic({kWidth * 0.5, kFloorTop + t * 0.5}, {kWidth + 2 * t, t}));       // пол
    w.add(makeStatic({vis - t * 0.5, kHeight * 0.5}, {t, kHeight * 2}));                // левая стена
    w.add(makeStatic({kWidth - vis + t * 0.5, kHeight * 0.5}, {t, kHeight * 2}));       // правая стена
}

void buildBallistics(World& w) {
    w.clear();
    addRoom(w);
    const double speed = 900;  // px/с
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < 8; ++i) {
        double angle = (30 + i * 7.5) * pi / 180.0; // 30°…82,5°
        Vec2 v{speed * std::cos(angle), -speed * std::sin(angle)}; // y вниз
        uint32_t color = 0xD9A441FF - uint32_t(i) * 0x0C0C0000;
        w.add(makeDynamic({80 + i * 30.0, kFloorTop - 20}, {24, 24}, v, BodyGroup::Normal, 0.001, color));
    }
}

void buildStack(World& w, int n, double boxW, double boxH) {
    w.clear();
    addRoom(w);
    for (int i = 0; i < n; ++i) {
        double y = kFloorTop - boxH * (i + 0.5); // блоки касаются гранями, без зазора
        uint32_t color = (i % 2) ? 0xD9A441FF : 0xC08A2EFF;
        w.add(makeDynamic({kWidth * 0.5, y}, {boxW, boxH}, {0, 0}, BodyGroup::Normal, 0.001, color));
    }
}

void buildBounce(World& w, double h) {
    w.clear();
    addRoom(w);
    w.add(makeDynamic({kWidth * 0.5, kFloorTop - h - 15}, {30, 30}, {0, 0}, BodyGroup::Normal));
}

void buildBounceDemo(World& w) {
    w.clear();
    addRoom(w);
    const double hs[3] = {100, 200, 300};
    for (int i = 0; i < 3; ++i)
        w.add(makeDynamic({kWidth * 0.5 + (i - 1) * 200.0, kFloorTop - hs[i] - 15}, {30, 30}, {0, 0},
                          BodyGroup::Normal));
}

Body makeProjectile(double x, double y, double v) {
    Body b = makeDynamic({x, y}, {kProjSize, kProjSize}, {v, 0}, BodyGroup::Fast, 0.001, 0x5AB0FFFF);
    b.gravityScale = 0; // летит по прямой: так условие туннелирования проверяется точно
    return b;
}

int addTunnelWall(World& w) {
    return w.add(makeStatic({kTunnelWallX, kFloorTop - kTunnelWallH * 0.5}, {kTunnelWallW, kTunnelWallH}, 0x9AA6B8FF));
}

double startXForPhase(double u, double v, double hz, double distance) {
    const double L = v / hz;               // путь за шаг
    const int m = int(distance / L) + 1;   // стартуем примерно на distance px левее стенки
    return kTunnelWallX + u - m * L;    // через m шагов снаряд окажется в точке wallX + u
}

void buildTunnelDemo(World& w, double v, double hz) {
    w.clear();
    addRoom(w);
    addTunnelWall(w);
    // u — положение центра снаряда относительно центра стенки в первый момент,
    // когда он оказался не левее −(w+s)/2 = −9 px. Подобраны для 2000 px/с при 60 Гц:
    //   u = −15: следующее положение +18,3 — пересечения не было вовсе (пропуск);
    //   u = +4 : пересечение найдено, но центр уже за серединой — вытолкнет вперёд;
    //   u = −5 : пересечение найдено до середины — отскок.
    const double us[3] = {-15, 4, -5};
    const double ys[3] = {200, 300, 400};
    for (int i = 0; i < 3; ++i) w.add(makeProjectile(startXForPhase(us[i], v, hz), ys[i], v));
    // Медленный снаряд 500 px/с ловится всегда; стартует ближе, чтобы долетел одновременно с быстрыми.
    w.add(makeProjectile(startXForPhase(-5, 500, hz, 130), 500, 500));
}

void buildTunnel(World& w, double v, double hz, double u) {
    w.clear();
    addRoom(w);
    addTunnelWall(w);
    w.add(makeProjectile(startXForPhase(u, v, hz), 300, v));
}

void buildMixed(World& w, int nNormal, int nFast, double v, double hzFast, uint64_t seed) {
    w.clear();
    addRoom(w);
    addTunnelWall(w);
    Lcg rng(seed);
    // Обычные тела: сетка из 8 столбцов у левой стены, падают и укладываются на пол.
    // Снаряды стартуют правее (≈ 300 px от стенки) и в начале их не задевают.
    for (int i = 0; i < nNormal; ++i) {
        double sz = rng.uniform(16, 36);
        double x = 80 + (i % 8) * 45 + rng.uniform(-4, 4);
        double y = 120 + (i / 8) * 45;
        w.add(makeDynamic({x, y}, {sz, sz}, {rng.uniform(-60, 60), 0}, BodyGroup::Normal));
    }
    // Снаряды: дорожки над кучей тел, фазы как в демо (пропуск / выталкивание / отскок).
    const double us[3] = {-15, 4, -5};
    for (int k = 0; k < nFast; ++k) {
        double y = 140 + k * 60.0;
        w.add(makeProjectile(startXForPhase(us[k % 3], v, hzFast, 300), y, v));
    }
}

void buildRandomCloud(World& w, int n, uint64_t seed, bool withRoom) {
    w.clear();
    if (withRoom) addRoom(w);
    Lcg rng(seed);
    for (int i = 0; i < n; ++i) {
        Vec2 p{rng.uniform(100, kWidth - 100), rng.uniform(100, kHeight - 100)};
        Vec2 s{rng.uniform(10, 40), rng.uniform(10, 40)};
        Vec2 v{rng.uniform(-300, 300), rng.uniform(-600, 0)};
        w.add(makeDynamic(p, s, v, BodyGroup::Normal));
    }
}

} // namespace scenes
