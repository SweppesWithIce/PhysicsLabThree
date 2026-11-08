#include "core/world.h"
#include <algorithm>
#include <cmath>

namespace phys {

int World::add(const Body& b) {
    bodies.push_back(b);
    return static_cast<int>(bodies.size()) - 1;
}

void World::savePrevious(uint8_t mask) {
    for (Body& b : bodies)
        if (moves(b, mask)) b.prevPos = b.pos;
}

void World::integrateVelocities(real dt, uint8_t mask) {
    for (Body& b : bodies)
        if (moves(b, mask)) b.vel += params.gravity * (b.gravityScale * dt);
}

void World::integratePositions(real dt, uint8_t mask) {
    for (Body& b : bodies)
        if (moves(b, mask)) b.pos += b.vel * dt;
}

// Широкая фаза — полный перебор пар O(n²). При n ≤ ~120 это ~7000 дешёвых
// проверок за шаг; сетка или sweep-and-prune усложнили бы код без заметного выигрыша.
// Пара проверяется, если хотя бы одно тело из неё движется в этом шаге (mask).
void World::findContacts(uint8_t mask) {
    contacts.clear();
    const int n = static_cast<int>(bodies.size());
    if (mask == kMaskAll) {
        for (int i = 0; i < n; ++i) {
            const Body& A = bodies[i];
            for (int j = i + 1; j < n; ++j) {
                const Body& B = bodies[j];
                if (A.isStatic() && B.isStatic()) continue;
                Contact c;
                if (!overlapAABB(A, B, c.n, c.depth)) continue;
                c.a = i;
                c.b = j;
                c.pa = A.pos;
                c.pb = B.pos;
                contacts.push_back(c);
            }
        }
    } else {
        // Шаг только части тел (multi-rate): перебираем «движущиеся × все» —
        // O(n_группы · n) вместо O(n²). Так подшаги быстрых тел стоят дёшево.
        for (int i = 0; i < n; ++i) {
            if (!moves(bodies[i], mask)) continue;
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                if (moves(bodies[j], mask) && j < i) continue; // пара уже учтена
                int a = std::min(i, j), b = std::max(i, j);
                Contact c;
                if (!overlapAABB(bodies[a], bodies[b], c.n, c.depth)) continue;
                c.a = a;
                c.b = b;
                c.pa = bodies[a].pos;
                c.pb = bodies[b].pos;
                contacts.push_back(c);
            }
        }
        // Тот же порядок пар, что и в полном переборе, — решатель Гаусса — Зейделя
        // чувствителен к порядку, а результат должен быть воспроизводимым.
        std::sort(contacts.begin(), contacts.end(), [](const Contact& x, const Contact& y) {
            return x.a != y.a ? x.a < y.a : x.b < y.b;
        });
    }
    stats.contacts = static_cast<int>(contacts.size());
}

void World::solveVelocities(uint8_t mask) {
    auto& cache = cache_[mask & 3];
    const real e = params.restitution;
    const real mu = params.friction;

    // Подготовка, проход 1: эффективная масса и цель отскока — по скоростям
    // ДО любых импульсов. (Если считать её после warm start соседних контактов,
    // скорость сближения искажается, появляется ложный «удар» и стек взрывается.)
    for (Contact& c : contacts) {
        Body& A = bodies[c.a];
        Body& B = bodies[c.b];
        real invSum = A.invMass + B.invMass;
        c.massN = invSum > 0 ? 1.0 / invSum : 0;

        real vn = dot(B.vel - A.vel, c.n);
        // Отскок задаётся один раз по скорости до удара: решатель затем
        // добивается v_n' = −e·v_n. Медленные касания отскока не получают.
        c.bias = (vn < -params.restitutionThreshold) ? -e * vn : 0;
    }

    // Проход 2: warm start — сразу прикладываем импульсы прошлого шага.
    for (Contact& c : contacts) {
        Body& A = bodies[c.a];
        Body& B = bodies[c.b];
        c.jn = c.jt = 0;
        if (params.warmStart) {
            auto it = cache.find(pairKey(c.a, c.b));
            if (it != cache.end() && dot(it->second.n, c.n) > 0.99) {
                c.jn = it->second.jn;
                c.jt = it->second.jt;
                Vec2 t{-c.n.y, c.n.x};
                Vec2 P = c.n * c.jn + t * c.jt;
                A.vel -= P * A.invMass;
                B.vel += P * B.invMass;
            }
        }
    }

    // Итерации Гаусса — Зейделя: каждый контакт по очереди исправляет
    // относительную скорость с учётом уже обновлённых соседей.
    for (int it = 0; it < params.velocityIterations; ++it) {
        for (Contact& c : contacts) {
            Body& A = bodies[c.a];
            Body& B = bodies[c.b];

            // Нормаль: тела не должны сближаться (и должны отскочить на bias).
            real vn = dot(B.vel - A.vel, c.n);
            real dj = c.massN * (-vn + c.bias);
            real jnOld = c.jn;
            c.jn = std::max(jnOld + dj, 0.0); // ограничиваем НАКОПЛЕННЫЙ импульс: тела можно только расталкивать
            dj = c.jn - jnOld;
            Vec2 P = c.n * dj;
            A.vel -= P * A.invMass;
            B.vel += P * B.invMass;

            // Трение Кулона: гасим касательную скорость, но не сильнее μ·J_n.
            Vec2 t{-c.n.y, c.n.x};
            real vt = dot(B.vel - A.vel, t);
            real djt = c.massN * (-vt);
            real maxF = mu * c.jn;
            real jtOld = c.jt;
            c.jt = std::clamp(jtOld + djt, -maxF, maxF);
            djt = c.jt - jtOld;
            Vec2 Pt = t * djt;
            A.vel -= Pt * A.invMass;
            B.vel += Pt * B.invMass;
        }
    }

    // Запоминаем импульсы для следующего шага.
    cache.clear();
    for (const Contact& c : contacts)
        cache[pairKey(c.a, c.b)] = {c.n, c.jn, c.jt};
}

// Позиционная коррекция («проекция»): после шага по скоростям тела всё ещё
// немного пересекаются (гравитация за шаг вдавливает на g·dt²). Убираем часть
// проникновения сверх slop, распределяя сдвиг обратно пропорционально массе.
// Скорость при этом НЕ меняется — в отличие от метода Баумгарте, коррекция
// не добавляет энергии, и стек не «подбрасывает».
void World::correctPositions(uint8_t mask) {
    stats.maxDepth = 0;
    for (int it = 0; it < params.positionIterations; ++it) {
        for (const Contact& c : contacts) {
            Body& A = bodies[c.a];
            Body& B = bodies[c.b];
            Vec2 n; real depth;
            if (!overlapAABB(A, B, n, depth)) continue;
            real C = depth - params.slop;
            if (C <= 0) continue;
            real wA = moves(A, mask) ? A.invMass : 0;
            real wB = moves(B, mask) ? B.invMass : 0;
            real w = wA + wB;
            if (w <= 0) continue;
            real corr = params.positionPercent * C / w;
            A.pos -= n * (corr * wA);
            B.pos += n * (corr * wB);
        }
    }
    for (const Contact& c : contacts) {
        Vec2 n; real depth;
        if (overlapAABB(bodies[c.a], bodies[c.b], n, depth))
            stats.maxDepth = std::max(stats.maxDepth, depth);
    }
}

void World::stepDiscreteNoSave(real dt, uint8_t mask) {
    integrateVelocities(dt, mask);
    findContacts(mask);
    solveVelocities(mask);
    integratePositions(dt, mask);
    correctPositions(mask);
}

void World::stepDiscrete(real dt, uint8_t mask) {
    toiEvents.clear();
    stats.fastSubsteps = 0;
    savePrevious(mask);
    stepDiscreteNoSave(dt, mask);
    contactLog = contacts;
}

// ---------------------------------------------------------------------------
// Multi-rate. Один «медленный» тик длиной dt:
//   1) запомнить позиции ВСЕХ тел (интерполяция отрисовки идёт по тику целиком);
//   2) шаг обычных тел (Normal) длиной dt; быстрые тела в нём неподвижны,
//      но участвуют в контактах в своих текущих позициях;
//   3) nFast шагов быстрых тел (Fast) по dt/nFast; обычные тела в них стоят
//      в положении на КОНЕЦ тика — это и есть рассинхронизация групп.
// Каждый подшаг — обычный дискретный шаг (без CCD): быстрые тела меньше
// проходят за шаг, поэтому реже проскакивают тонкие препятствия.
// ---------------------------------------------------------------------------
void World::stepMultiRate(real dt, int nFast) {
    toiEvents.clear();
    if (nFast < 1) nFast = 1;
    savePrevious(kMaskAll);
    stepDiscreteNoSave(dt, kMaskNormal);
    contactLog = contacts;
    int nContacts = stats.contacts;
    real maxDepth = stats.maxDepth;
    const real h = dt / nFast;
    fastTrail.clear();
    for (int k = 0; k < nFast; ++k) {
        stepDiscreteNoSave(h, kMaskFast);
        for (const Body& b : bodies)
            if (b.group == BodyGroup::Fast) fastTrail.push_back(b.pos);
        for (Contact& c : contacts) c.substep = k;
        contactLog.insert(contactLog.end(), contacts.begin(), contacts.end());
        nContacts += stats.contacts;
        maxDepth = std::max(maxDepth, stats.maxDepth);
    }
    // Статистика — по всему тику, а не только по последнему подшагу.
    stats.contacts = nContacts;
    stats.maxDepth = maxDepth;
    stats.fastSubsteps = nFast;
}

// ---------------------------------------------------------------------------
// CCD. Алгоритм на шаг (как в задании):
//   1) начало шага: положения и скорости известны (скорости уже с гравитацией
//      и после решения покоящихся контактов);
//   2) для всех пар найти самый ранний момент касания t_c на оставшемся отрезке;
//   3) сдвинуть ВСЕ движущиеся тела к t_c (движение линейное — так же, как
//      в полунеявном Эйлере x += v·dt);
//   4) ударить пару импульсом;
//   5) продолжить с остатком шага; повторные удары ищутся заново,
//      но не больше maxToiIterations раз, затем тела просто досдвигаются.
// ---------------------------------------------------------------------------
void World::applyImpact(int ia, int ib, Vec2 n) {
    Body& A = bodies[ia];
    Body& B = bodies[ib];
    real invSum = A.invMass + B.invMass;
    if (invSum <= 0) return;
    real vn = dot(B.vel - A.vel, n);
    if (vn >= 0) return; // уже расходятся
    // Тот же порог, что в дискретном решателе: медленное касание — без отскока.
    real e = (vn < -params.restitutionThreshold) ? params.restitution : 0;
    real jn = -(1 + e) * vn / invSum;
    Vec2 t{-n.y, n.x};
    real vt = dot(B.vel - A.vel, t);
    real jt = std::clamp(-vt / invSum, -params.friction * jn, params.friction * jn);
    Vec2 P = n * jn + t * jt;
    A.vel -= P * A.invMass;
    B.vel += P * B.invMass;
}

void World::advanceWithTOI(real dt, uint8_t mask) {
    toiEvents.clear();
    stats.toiIterations = 0;
    stats.toiExhausted = false;
    real done = 0;
    const int n = static_cast<int>(bodies.size());
    while (done < dt) {
        if (stats.toiIterations >= params.maxToiIterations + params.maxCriticalToi) {
            stats.toiExhausted = true;
            break;
        }
        // Обычный лимит исчерпан — дальше только пары, способные проскочить.
        const bool criticalOnly = stats.toiIterations >= params.maxToiIterations;
        ++stats.toiIterations;
        const real remaining = dt - done;
        real best = remaining;
        int bi = -1, bj = -1;
        Vec2 bn;
        for (int i = 0; i < n; ++i) {
            const Body& A = bodies[i];
            const bool ma = moves(A, mask);
            for (int j = i + 1; j < n; ++j) {
                const Body& B = bodies[j];
                const bool mb = moves(B, mask);
                if (!ma && !mb) continue;
                // Тела, не входящие в маску, в этом шаге стоят на месте.
                Vec2 vrel = (ma ? A.vel : Vec2{}) - (mb ? B.vel : Vec2{});
                if (vrel.x == 0 && vrel.y == 0) continue;
                if (criticalOnly &&
                    std::fabs(vrel.x) * remaining <= A.half.x + B.half.x &&
                    std::fabs(vrel.y) * remaining <= A.half.y + B.half.y)
                    continue; // смещение меньше полуразмеров — дискретная проверка справится
                real t; Vec2 nn;
                if (!sweptAABB(A, B, vrel, best, t, nn)) continue;
                // Покоящееся касание: тела уже касаются (t = 0) и за остаток шага сблизятся
                // меньше чем на slop. Такие пары оставляем дискретному решателю — иначе они
                // каждый шаг тратят лимит ударов («удары» с нулевым временем).
                if (t <= 0 && std::fabs(dot(vrel, nn)) * remaining < params.slop) continue;
                // Строго меньше: при равных t побеждает первая пара — порог детерминирован.
                if (t < best || bi < 0) {
                    best = t; bi = i; bj = j; bn = nn;
                }
            }
        }
        // Сдвигаем всех к моменту удара (или до конца шага, если ударов нет).
        for (Body& b : bodies)
            if (moves(b, mask)) b.pos += b.vel * best;
        done += best;
        if (bi < 0) return;
        Body& A = bodies[bi];
        Body& B = bodies[bj];
        ToiEvent ev{bi, bj, done, bn, A.pos, B.pos, dot(B.vel - A.vel, bn)};
        applyImpact(bi, bj, bn);
        toiEvents.push_back(ev);
    }
    // Лимит исчерпан: досдвигаем без поиска ударов (возможны пересечения —
    // их увидит дискретный решатель на следующем шаге).
    if (done < dt)
        for (Body& b : bodies)
            if (moves(b, mask)) b.pos += b.vel * (dt - done);
}

void World::stepCCD(real dt, uint8_t mask) {
    savePrevious(mask);
    integrateVelocities(dt, mask);
    findContacts(mask);        // покоящиеся контакты (тела уже пересекаются)
    solveVelocities(mask);
    advanceWithTOI(dt, mask);  // вместо integratePositions
    correctPositions(mask);
    contactLog = contacts;
}

namespace {
inline void fnvMix(uint64_t& h, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
}
} // namespace

uint64_t World::stateHash() const {
    uint64_t h = 1469598103934665603ull;
    for (const Body& b : bodies) {
        fnvMix(h, &b.pos, sizeof(b.pos));
        fnvMix(h, &b.vel, sizeof(b.vel));
    }
    return h;
}

real World::kineticEnergy() const {
    real e = 0;
    for (const Body& b : bodies)
        if (!b.isStatic()) e += 0.5 * dot(b.vel, b.vel) / b.invMass;
    return e;
}

} // namespace phys
