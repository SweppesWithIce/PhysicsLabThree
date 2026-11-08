#include "app/renderer.h"
#include <cmath>

using namespace phys;

namespace {
inline void setColor(SDL_Renderer* r, uint32_t rgba, int dim = 0) {
    auto ch = [&](int shift) { int c = int((rgba >> shift) & 0xFF) - dim; return Uint8(c < 0 ? 0 : c); };
    SDL_SetRenderDrawColor(r, ch(24), ch(16), ch(8), Uint8(rgba & 0xFF));
}
} // namespace

void Renderer::fillRect(Vec2 c, Vec2 h, uint32_t rgba) {
    SDL_FRect rect{float(c.x - h.x), float(c.y - h.y), float(2 * h.x), float(2 * h.y)};
    setColor(r_, rgba);
    SDL_RenderFillRectF(r_, &rect);
    setColor(r_, rgba, 70); // тёмная обводка, чтобы соседние блоки различались
    SDL_RenderDrawRectF(r_, &rect);
}

void Renderer::drawWorld(const World& w, double alpha, const RenderOptions& opt) {
    SDL_SetRenderDrawColor(r_, 24, 26, 32, 255);
    SDL_RenderClear(r_);

    for (const Body& b : w.bodies) {
        Vec2 p = opt.interpolate ? lerp(b.prevPos, b.pos, alpha) : b.pos;
        fillRect(p, b.half, b.color);
    }

    // Точки «физических» позиций рисуются поверх тел: так видно и положение
    // снаряда внутри тонкой стенки на том шаге, когда пересечение обнаружено.
    if (opt.showStepDots) {
        SDL_SetRenderDrawColor(r_, 255, 210, 80, 255);
        for (const Vec2& p : dots_) {
            SDL_FRect d{float(p.x - 1.5), float(p.y - 1.5), 3.f, 3.f};
            SDL_RenderFillRectF(r_, &d);
        }
    }

    if (opt.showStepDots) {
        SDL_SetRenderDrawColor(r_, 255, 70, 70, 255);
        for (const Vec2& p : toi_)
            for (float o = -0.5f; o <= 0.5f; o += 1.0f) { // толщина 2 px
                SDL_RenderDrawLineF(r_, float(p.x - 7 + o), float(p.y - 7), float(p.x + 7 + o), float(p.y + 7));
                SDL_RenderDrawLineF(r_, float(p.x - 7 + o), float(p.y + 7), float(p.x + 7 + o), float(p.y - 7));
            }
    }

    if (opt.showContacts) {
        // Нормаль контакта рисуется из точки посередине между центрами (без интерполяции).
        SDL_SetRenderDrawColor(r_, 255, 80, 80, 255);
        for (const Contact& c : w.contacts) {
            Vec2 m = (w.bodies[c.a].pos + w.bodies[c.b].pos) * 0.5;
            Vec2 q = m + c.n * 14.0;
            SDL_RenderDrawLineF(r_, float(m.x), float(m.y), float(q.x), float(q.y));
            SDL_FRect d{float(m.x - 2), float(m.y - 2), 4.f, 4.f};
            SDL_RenderFillRectF(r_, &d);
        }
    }
}
