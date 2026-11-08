#pragma once
// Сцены — общие для интерактивного приложения и для physbench, чтобы
// эксперимент без окна и демонстрация на экране шли на одинаковых входных данных.
#include <cstdint>
#include "core/world.h"

namespace scenes {

constexpr double kWidth = 1280;  // размер «мира» в px совпадает с окном
constexpr double kHeight = 720;
constexpr double kFloorTop = 680; // верхняя грань пола, px

// Пол, левая и правая стены (неподвижные тела).
void addRoom(phys::World& w);

// Шаг 0: ящики, брошенные под разными углами; теперь они сталкиваются с полом и стенами.
void buildBallistics(phys::World& w);

// Стек из n одинаковых блоков, стоящих вплотную друг на друге на полу.
void buildStack(phys::World& w, int n = 10, double boxW = 60, double boxH = 30);
inline int stackTopIndex(const phys::World& w) { return int(w.bodies.size()) - 1; }

// Ящик 30×30, нижняя грань которого на высоте h над полом, скорость 0.
// Для экрана — три ящика с высот 100, 200 и 300 px.
void buildBounce(phys::World& w, double h);
void buildBounceDemo(phys::World& w);

// ---- Туннелирование (шаг 2) ----
constexpr double kTunnelWallX = 760;  // центр тонкой стенки
constexpr double kTunnelWallW = 10;   // толщина w, px
constexpr double kTunnelWallH = 560;  // высота, px
constexpr double kProjSize = 8;       // размер снаряда s, px

phys::Body makeProjectile(double x, double y, double v);
int addTunnelWall(phys::World& w);
// Начальная x такая, что после целого числа шагов центр снаряда окажется ровно
// в wallX + u (u отсчитывается от центра стенки).
double startXForPhase(double u, double v, double hz, double distance = 500);
// Демонстрация: три снаряда 2000 px/с с разной фазой (пропуск, выталкивание
// вперёд, отскок) и один медленный 500 px/с. Фазы подобраны для частоты hz.
void buildTunnelDemo(phys::World& w, double v, double hz);
// Один снаряд со скоростью v и фазой u — для серии запусков в physbench.
void buildTunnel(phys::World& w, double v, double hz, double u);

// Смешанная сцена для multi-rate и замеров времени: комната, тонкая стенка,
// nNormal обычных тел (падают слева от стенки) и nFast снарядов, летящих в стенку.
// hzFast — частота, под которую подбираются фазы снарядов (как в демо туннелирования).
void buildMixed(phys::World& w, int nNormal, int nFast, double v, double hzFast, uint64_t seed);

// Детерминированный генератор (LCG) — std::rand и std::mt19937 + distribution
// не гарантируют одинаковую последовательность на разных стандартных
// библиотеках, а нам нужны одинаковые входы на любой машине.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return uint32_t(s >> 33); }
    double uniform(double a, double b) { return a + (b - a) * (next() / double(1u << 31)); }
};

// Случайная (но воспроизводимая по seed) сцена из n тел — для проверки детерминированности.
// withRoom = true — тела падают в комнату и сталкиваются.
void buildRandomCloud(phys::World& w, int n, uint64_t seed, bool withRoom = false);

} // namespace scenes
