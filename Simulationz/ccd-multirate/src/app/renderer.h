#pragma once
// Отрисовка мира через SDL_Renderer. Физика ничего не знает о графике:
// рендерер только читает тела и рисует их в интерполированных позициях.
#include <SDL.h>
#include <vector>
#include "core/world.h"

struct RenderOptions {
    bool interpolate = true;     // рисовать lerp(prevPos, pos, alpha)
    bool showStepDots = false;   // точки в позициях, где реально считалась физика
    bool showContacts = false;   // нормали контактов последнего шага
};

class Renderer {
public:
    explicit Renderer(SDL_Renderer* r) : r_(r) {}

    void drawWorld(const phys::World& w, double alpha, const RenderOptions& opt);

    // След из «физических» позиций: сюда добавляем позицию тела после каждого шага.
    void pushStepDot(phys::Vec2 p) { dots_.push_back(p); if (dots_.size() > 4000) dots_.erase(dots_.begin(), dots_.begin() + 1000); }
    void clearDots() { dots_.clear(); toi_.clear(); }
    // Точка удара, найденная CCD (центр тела в момент t_c) — рисуется красным крестиком.
    void pushToiMark(phys::Vec2 p) { toi_.push_back(p); if (toi_.size() > 2000) toi_.erase(toi_.begin(), toi_.begin() + 500); }

private:
    void fillRect(phys::Vec2 center, phys::Vec2 half, uint32_t rgba);
    SDL_Renderer* r_;
    std::vector<phys::Vec2> dots_;
    std::vector<phys::Vec2> toi_;
};
