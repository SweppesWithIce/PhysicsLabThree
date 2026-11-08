#pragma once
// Мир: тела, параметры и шаг физики.
//
// Дискретный шаг (stepDiscrete) устроен как в Box2D Lite (E. Catto, GDC 2006):
//   1) запомнить позиции для интерполяции;
//   2) скорости += g·dt                       (полунеявный Эйлер, часть 1);
//   3) найти пересечения в текущих позициях   (дискретная проверка);
//   4) решить контакты импульсами: несколько итераций Гаусса — Зейделя
//      по всем контактам с накоплением и ограничением импульса;
//   5) позиции += v·dt                        (полунеявный Эйлер, часть 2);
//   6) позиционная коррекция: вытолкнуть тела, проникшие глубже допуска slop.
//
// Маска групп позволяет шагать только частью тел — это понадобится для multi-rate.
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "core/body.h"
#include "core/contact.h"

namespace phys {

enum GroupMask : uint8_t {
    kMaskNormal = 1 << 0,
    kMaskFast   = 1 << 1,
    kMaskAll    = kMaskNormal | kMaskFast,
};

inline uint8_t maskOf(BodyGroup g) {
    return g == BodyGroup::Normal ? kMaskNormal : g == BodyGroup::Fast ? kMaskFast : 0;
}

struct WorldParams {
    // 980 px/с² — это g = 9,8 м/с² при масштабе 100 px = 1 м.
    Vec2 gravity{0, 980};
    real restitution = 0.5;       // e: v_n после удара = −e·v_n до удара
    real friction = 0.4;          // μ: |J_t| ≤ μ·J_n
    // Ниже этой скорости сближения отскок не применяется: иначе лежащее тело
    // «подпрыгивает» на g·dt каждый шаг и стек дрожит.
    real restitutionThreshold = 60; // px/с (≈ 4·g·dt при 60 Гц)
    int velocityIterations = 10;
    int positionIterations = 4;
    real slop = 0.5;              // допустимое проникновение, px: оставляем контакт «живым»
    real positionPercent = 0.4;   // доля оставшегося проникновения, убираемая за итерацию
    bool warmStart = true;        // начинать решатель с импульсов прошлого шага
    int maxToiIterations = 8;     // CCD: сколько ударов внутри одного шага обрабатывается точно
    // CCD: после исчерпания лимита ещё столько ударов ищется только для «опасных»
    // пар — тех, что за остаток шага смещаются друг относительно друга больше
    // суммы полуразмеров и иначе могли бы проскочить.
    int maxCriticalToi = 32;
    int fastSubsteps = 4;         // multi-rate: шагов быстрых тел на один шаг обычных (240/60)
};

// Режим шага физики.
enum class StepMode : uint8_t {
    Discrete, // проверка пересечений только в моменты t_n
    CCD,      // поиск момента удара t_c внутри шага (swept AABB)
    MultiRate // обычные тела — шаг dt, быстрые — fastSubsteps шагов по dt/fastSubsteps
};

// Удар, найденный CCD внутри шага.
struct ToiEvent {
    int a, b;          // пара тел (a < b)
    real t;            // момент удара от начала шага, с
    Vec2 n;            // нормаль от a к b
    Vec2 posA, posB;   // положения центров в момент удара
    real vnBefore;     // скорость сближения до удара (< 0)
};

struct StepStats {
    int contacts = 0;
    real maxDepth = 0;            // наибольшее проникновение после коррекции, px
    int fastSubsteps = 0;         // multi-rate: сколько шагов быстрых тел сделано в этом шаге
    int toiIterations = 0;        // CCD: сколько раз искали ближайший удар
    bool toiExhausted = false;    // CCD: исчерпан лимит maxToiIterations
};

class World {
public:
    WorldParams params;
    std::vector<Body> bodies;
    std::vector<Contact> contacts; // контакты последнего шага (для отрисовки и отладки)
    std::vector<ToiEvent> toiEvents; // удары, найденные CCD в последнем шаге
    // Все контакты последнего шага, включая подшаги multi-rate (в дискретном
    // и CCD-режимах совпадает с contacts).
    std::vector<Contact> contactLog;
    // Multi-rate: положения быстрых тел после каждого подшага (для отрисовки «следа»).
    std::vector<Vec2> fastTrail;
    StepStats stats;

    int add(const Body& b);
    void clear() { bodies.clear(); contacts.clear(); toiEvents.clear(); for (auto& c : cache_) c.clear(); }

    // ---- полный шаг ----
    void stepDiscrete(real dt, uint8_t mask = kMaskAll);
    // CCD: как дискретный, но позиции продвигаются с поиском ударов внутри шага.
    void stepCCD(real dt, uint8_t mask = kMaskAll);
    // Multi-rate: один шаг обычных тел dt и n шагов быстрых тел по dt/n.
    void stepMultiRate(real dt, int nFast);
    void step(real dt, StepMode mode, uint8_t mask = kMaskAll) {
        if (mode == StepMode::CCD) stepCCD(dt, mask);
        else if (mode == StepMode::MultiRate) stepMultiRate(dt, params.fastSubsteps);
        else stepDiscrete(dt, mask);
    }

    // ---- составные части шага (публичные: из них собираются CCD и multi-rate) ----
    void savePrevious(uint8_t mask = kMaskAll);
    void integrateVelocities(real dt, uint8_t mask = kMaskAll);
    void integratePositions(real dt, uint8_t mask = kMaskAll);
    void findContacts(uint8_t mask = kMaskAll);
    void solveVelocities(uint8_t mask = kMaskAll);
    // Дискретный шаг без сохранения prevPos (для подшагов multi-rate).
    void stepDiscreteNoSave(real dt, uint8_t mask);
    void correctPositions(uint8_t mask = kMaskAll);
    // Продвинуть позиции на dt, останавливаясь в каждом найденном ударе (CCD).
    void advanceWithTOI(real dt, uint8_t mask = kMaskAll);
    // Мгновенный удар пары с нормалью n: импульс с восстановлением e и трением μ.
    void applyImpact(int a, int b, Vec2 n);

    // Совместимость с шагом 0: только гравитация, без столкновений.
    void integrate(real dt) { integrateVelocities(dt); integratePositions(dt); }

    uint64_t stateHash() const;
    real kineticEnergy() const;

private:
    bool moves(const Body& b, uint8_t mask) const { return !b.isStatic() && (maskOf(b.group) & mask); }
    static uint64_t pairKey(int a, int b) { return (uint64_t(uint32_t(a)) << 32) | uint32_t(b); }

    struct Cached { Vec2 n; real jn, jt; };
    // Импульсы прошлого шага для warm start — отдельно для каждой маски групп:
    // в multi-rate шаги обычных и быстрых тел чередуются и не должны стирать
    // накопленные импульсы друг друга.
    std::unordered_map<uint64_t, Cached> cache_[4];
};

} // namespace phys
