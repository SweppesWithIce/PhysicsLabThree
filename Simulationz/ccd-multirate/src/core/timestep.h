#pragma once
// Фиксированный шаг физики с аккумулятором времени
// (G. Fiedler, «Fix Your Timestep!», gafferongames.com, 2004).
//
// Реальное время кадра копится в аккумуляторе; физика делает столько шагов
// фиксированной длины dt, сколько в нём помещается. Остаток (alpha = acc/dt)
// используется для интерполяции отрисовки между двумя последними состояниями.
// Так результат физики не зависит от FPS — это основа детерминированности.
#include "core/vec2.h"

namespace phys {

class FixedStepAccumulator {
public:
    // maxFrame — защита от «спирали смерти»: если кадр завис (отладчик,
    // перетаскивание окна), не пытаемся догонять больше maxFrame секунд.
    explicit FixedStepAccumulator(real dt, real maxFrame = 0.25)
        : dt_(dt), maxFrame_(maxFrame) {}

    void setDt(real dt) { dt_ = dt; acc_ = 0; }
    real dt() const { return dt_; }

    // Добавляет время кадра и вызывает step(dt) нужное число раз.
    // Возвращает число выполненных шагов.
    template <class StepFn>
    int advance(real frameTime, StepFn&& step) {
        if (frameTime > maxFrame_) frameTime = maxFrame_;
        acc_ += frameTime;
        int n = 0;
        while (acc_ >= dt_) {
            step(dt_);
            acc_ -= dt_;
            ++n;
        }
        return n;
    }

    // Доля следующего шага, уже «накопленная» в реальном времени, [0, 1).
    real alpha() const { return acc_ / dt_; }

private:
    real dt_;
    real maxFrame_;
    real acc_ = 0;
};

} // namespace phys
