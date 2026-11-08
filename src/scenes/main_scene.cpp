#include "scenes/main_scene.h"
#include <cmath>
#include "scenes/scenes.h"

using namespace phys;

namespace scenes {

void MainScene::build(World& w) {
    w.clear();
    fastIdx.clear();
    obstacleIdx.clear();
    fastStart_.clear();
    tunnelEvents = 0;
    shots = 0;
    sinceFire_ = 0;

    addRoom(w); // пол и две стены — 3 неподвижных тела

    // 6 препятствий 60×60: три мишени в столбце справа и три полки посередине.
    const uint32_t obstacleColor = 0x7D8AA0FF;
    const Vec2 obstacles[6] = {
        {1000, 230}, {1000, 350}, {1000, 470},   // мишени для снарядов
        {420, 520}, {600, 400}, {780, 560},      // полки, на которые падают обычные тела
        // (полки не пересекают дорожки снарядов: иначе снаряд, задевший полку у края,
        //  дискретный решатель выталкивает вдоль грани — «срезание угла»)
    };
    for (const Vec2& c : obstacles)
        obstacleIdx.push_back(w.add(makeStatic(c, {60, 60}, obstacleColor)));

    // Стек блоков у левой стены.
    stackFirst = int(w.bodies.size());
    for (int i = 0; i < cfg.stackBlocks; ++i) {
        double y = kFloorTop - kStackBlockH * (i + 0.5);
        uint32_t color = (i % 2) ? 0xD9A441FF : 0xC08A2EFF;
        w.add(makeDynamic({kStackX, y}, {50, kStackBlockH}, {0, 0}, BodyGroup::Normal, 0.001, color));
    }
    stackTop = int(w.bodies.size()) - 1;
    stackTopY0 = w.bodies[stackTop].pos.y;

    // Обычные тела разного размера падают сверху на полки и пол.
    Lcg rng(cfg.seed);
    for (int i = 0; i < cfg.nNormal; ++i) {
        double sx = rng.uniform(18, 38), sy = rng.uniform(18, 38);
        double x = 320 + (i % 10) * 52 + rng.uniform(-8, 8);
        double y = 40 + (i / 10) * 44 + rng.uniform(-5, 5);
        uint32_t color = 0xE0A93BFF - uint32_t(rng.next() % 4) * 0x0A0A0000;
        w.add(makeDynamic({x, y}, {sx, sy}, {rng.uniform(-40, 40), 0}, BodyGroup::Normal, 0.001, color));
    }

    // Снаряды: дорожки на высоте мишеней, старт у левой стены (разные x — разные фазы).
    const double lanes[5] = {215, 245, 350, 455, 485};
    for (int k = 0; k < cfg.nFast; ++k) {
        Vec2 p{60, lanes[k % 5]};
        fastStart_.push_back(p);
        fastIdx.push_back(w.add(makeProjectile(p.x, p.y, cfg.projSpeed)));
    }
    armed_.assign(fastIdx.size(), true);
    fireRng_ = cfg.seed * 7919 + 17;
    shots = cfg.nFast;
}

// Убрать снаряды за пределы комнаты (выше потолка, далеко друг от друга): без гравитации
// и со скоростью 0 они ни с чем не сталкиваются до следующего залпа.
void MainScene::park(World& w) {
    for (size_t k = 0; k < fastIdx.size(); ++k) {
        Body& b = w.bodies[fastIdx[k]];
        b.pos = b.prevPos = {200.0 + 100.0 * k, -2000.0};
        b.vel = {0, 0};
        armed_[k] = false;
    }
}

void MainScene::fire(World& w) {
    Lcg rng(fireRng_);
    for (size_t k = 0; k < fastIdx.size(); ++k) {
        Body& b = w.bodies[fastIdx[k]];
        b.pos = b.prevPos = fastStart_[k] + Vec2{rng.uniform(0, 80), 0};
        b.vel = {cfg.projSpeed, 0};
        armed_[k] = true;
    }
    fireRng_ = rng.s;
    sinceShot_ = 0;
    shots += int(fastIdx.size());
    sinceFire_ = 0;
}

void MainScene::afterStep(World& w, double dt) {
    // Проскок: центр снаряда перешёл через центр препятствия по x за один шаг,
    // при этом снаряд перекрывался с препятствием по y до и после шага.
    for (size_t k = 0; k < fastIdx.size(); ++k) {
        if (!armed_[k]) continue;
        const Body& b = w.bodies[fastIdx[k]];
        for (int oi : obstacleIdx) {
            const Body& o = w.bodies[oi];
            double hy = o.half.y + b.half.y;
            if (std::fabs(b.prevPos.y - o.pos.y) >= hy || std::fabs(b.pos.y - o.pos.y) >= hy) continue;
            double s0 = b.prevPos.x - o.pos.x, s1 = b.pos.x - o.pos.x;
            if (s0 < 0 && s1 > 0) { ++tunnelEvents; armed_[k] = false; break; }
        }
        // Первый отскок (скорость развернулась) — дальше этот выстрел не считаем.
        if (b.vel.x <= 0) armed_[k] = false;
    }
    sinceFire_ += dt;
    const double before = sinceShot_;
    sinceShot_ += dt;
    if (cfg.projLifetime > 0 && before < cfg.projLifetime && sinceShot_ >= cfg.projLifetime - 1e-9) park(w);
    firedThisStep = false;
    if (cfg.fireEvery > 0 && sinceFire_ >= cfg.fireEvery - 1e-9) {
        fire(w);
        firedThisStep = true;
    }
}

} // namespace scenes
