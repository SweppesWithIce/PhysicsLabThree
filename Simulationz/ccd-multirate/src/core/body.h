#pragma once
// Твёрдое тело в форме AABB (axis-aligned bounding box) без вращения.
// Система координат экранная: x вправо, y вниз, единицы — пиксели и секунды.
#include <cstdint>
#include "core/vec2.h"

namespace phys {

// Три группы тел из задания. Группа определяет, с какой частотой тело
// обновляется в режиме multi-rate.
enum class BodyGroup : uint8_t {
    Static, // неподвижные препятствия (бесконечная масса)
    Normal, // обычные тела, 60 Гц
    Fast    // быстрые тела (снаряды), 240 Гц
};

struct Body {
    Vec2 pos;          // центр AABB, px
    Vec2 half;         // половинные размеры, px
    Vec2 vel;          // скорость, px/с
    real invMass = 0;  // 1/m; 0 у неподвижных тел
    real gravityScale = 1; // 0 — тело летит без гравитации (снаряды в опыте с туннелированием)
    BodyGroup group = BodyGroup::Normal;

    Vec2 prevPos;      // позиция на предыдущем шаге — для интерполяции отрисовки
    uint32_t color = 0xC8C8C8FF; // RGBA, только для отрисовки

    bool isStatic() const { return invMass == 0; }
    Vec2 min() const { return pos - half; }
    Vec2 max() const { return pos + half; }
};

// Фабрики, чтобы сцены читались коротко.
inline Body makeStatic(Vec2 center, Vec2 size, uint32_t color = 0x6E7B8BFF) {
    Body b;
    b.pos = b.prevPos = center;
    b.half = size * 0.5;
    b.invMass = 0;
    b.group = BodyGroup::Static;
    b.color = color;
    return b;
}

// Масса пропорциональна площади (плотность density, «масса на px²»).
inline Body makeDynamic(Vec2 center, Vec2 size, Vec2 vel, BodyGroup group,
                        real density = 0.001, uint32_t color = 0xD9A441FF) {
    Body b;
    b.pos = b.prevPos = center;
    b.half = size * 0.5;
    b.vel = vel;
    b.invMass = 1.0 / (density * size.x * size.y);
    b.group = group;
    b.color = color;
    return b;
}

} // namespace phys
