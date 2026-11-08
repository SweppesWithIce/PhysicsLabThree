#pragma once
// Контакт двух AABB и дискретный тест пересечения.
#include <algorithm>
#include <cmath>
#include "core/body.h"

namespace phys {

struct Contact {
    int a = -1, b = -1;   // индексы тел, a < b
    Vec2 n;               // нормаль от a к b (единичная, вдоль x или y)
    real depth = 0;       // глубина проникновения, px
    Vec2 pa, pb;          // положения центров в момент обнаружения (для отладки и HUD)
    int substep = -1;     // multi-rate: номер подшага быстрых тел (0…N−1), −1 — шаг обычных тел

    // Состояние решателя (sequential impulses)
    real massN = 0;       // 1 / (1/m_a + 1/m_b) — эффективная масса (без вращения одна на обе оси)
    real bias = 0;        // целевая скорость отскока по нормали: −e·v_n до удара
    real jn = 0;          // накопленный нормальный импульс (≥ 0)
    real jt = 0;          // накопленный касательный импульс (|jt| ≤ μ·jn)
};

// Дискретный тест: пересекаются ли тела в ТЕКУЩИХ позициях.
// Нормаль выбирается по оси наименьшего перекрытия — по ней тела
// выталкиваются на минимальное расстояние.
// Это и есть «проверка в момент t_n», которая пропускает тонкие стенки.
inline bool overlapAABB(const Body& A, const Body& B, Vec2& n, real& depth) {
    Vec2 d = B.pos - A.pos;
    real ox = (A.half.x + B.half.x) - std::fabs(d.x);
    if (ox <= 0) return false;
    real oy = (A.half.y + B.half.y) - std::fabs(d.y);
    if (oy <= 0) return false;
    if (ox < oy) {
        n = {d.x < 0 ? -1.0 : 1.0, 0};
        depth = ox;
    } else {
        n = {0, d.y < 0 ? -1.0 : 1.0};
        depth = oy;
    }
    return true;
}

// Непрерывный тест (swept AABB, метод slab; C. Ericson, «Real-Time Collision
// Detection», 5.5.8). Тело A движется относительно B со скоростью vrel на
// интервале [0, tMax]. Сумма Минковского: B расширяется на размеры A, A
// превращается в точку-центр, и ищется пересечение луча x_A + vrel·t с
// прямоугольником. Для каждой оси k:
//   t_вход,k = (d_k − sign(v_k)·H_k) / v_k,  t_выход,k = (d_k + sign(v_k)·H_k) / v_k,
// где d = x_B − x_A, H = h_A + h_B. Касание начинается в t_c = max_k t_вход,k
// и есть, если t_c ≤ min_k t_выход,k и 0 ≤ t_c ≤ tMax.
// Тела, уже пересекающиеся в t = 0 (покоящиеся контакты), этот тест не
// возвращает — их обрабатывает дискретный решатель.
// n — нормаль от A к B (по оси, на которой вход наступил последним).
inline bool sweptAABB(const Body& A, const Body& B, Vec2 vrel, real tMax, real& t, Vec2& n) {
    const Vec2 d = B.pos - A.pos;
    const Vec2 H = A.half + B.half;
    const real inf = 1e300;
    real enter[2], exit[2];
    const real dv[2] = {d.x, d.y}, hv[2] = {H.x, H.y}, vv[2] = {vrel.x, vrel.y};
    for (int k = 0; k < 2; ++k) {
        if (vv[k] == 0) {
            if (std::fabs(dv[k]) >= hv[k]) return false; // по этой оси разнесены и не сближаются
            enter[k] = -inf;
            exit[k] = inf;
        } else {
            real s = vv[k] > 0 ? 1.0 : -1.0;
            enter[k] = (dv[k] - s * hv[k]) / vv[k];
            exit[k] = (dv[k] + s * hv[k]) / vv[k];
        }
    }
    const int axis = enter[0] > enter[1] ? 0 : 1;
    const real tIn = enter[axis];
    const real tOut = std::min(exit[0], exit[1]);
    if (tIn > tOut || tIn < 0 || tIn > tMax) return false;
    t = tIn;
    n = axis == 0 ? Vec2{vrel.x > 0 ? 1.0 : -1.0, 0} : Vec2{0, vrel.y > 0 ? 1.0 : -1.0};
    return true;
}

} // namespace phys
