// physsim — интерактивное приложение: окно SDL2, панель на Dear ImGui.
//
// Клавиши: M — режим (дискретный → CCD → multi-rate), F — залп снарядов (основная сцена),
//          Space — пауза, N — один шаг физики, R — перезапуск сцены, 1–7 — сцена,
//          I — интерполяция, D — точки шагов, C — контакты, H — скрыть панель,
//          F12 — скриншот (BMP), Esc — выход.
// Параметры: --scene K              0 стек, 1 отскок, 2 баллистика, 3 облако тел, 4 туннелирование,
//                                   5 смешанная, 6 основная (по умолчанию)
//            --mode discrete|ccd|multirate  режим шага
//            --hz 60                частота физики (в multi-rate — частота обычных тел)
//            --fast-n N             multi-rate: быстрые тела шагают в N раз чаще (по умолчанию 4)
//            --speed V              скорость снарядов, px/с (основная сцена — 4000, остальные — 2000)
//            --dots / --no-interp / --contacts / --no-hud   настройки отображения
//            --record DIR --frames N запись N кадров в DIR (кадр = ровно 1/60 с, окно
//                                    не показывается; для видео и скриншотов отчёта)
//            --script "t1:cmd;t2:cmd"  команды по времени для записи видео: cmd = mode-discrete,
//                                    mode-ccd, mode-multirate, fire, reset
//            при записи основной сцены в stdout печатается счётчик проскоков по кадрам
//            (scripts/make_media.py использует его для подписей в видео)
#include <SDL.h>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "app/renderer.h"
#include "core/timestep.h"
#include "core/world.h"
#include "scenes/main_scene.h"
#include "scenes/scenes.h"

using namespace phys;
using Clock = std::chrono::steady_clock;

namespace {

enum class SceneId : int { Stack = 0, Bounce, Ballistics, Cloud, Tunnel, Mixed, Main, Count };
const char* kSceneNames[] = {"1  Stack (10 blocks)", "2  Bounce (h = 100/200/300 px)", "3  Ballistics",
                             "4  Random cloud (60 bodies)", "5  Tunnel: projectile vs 10 px wall",
                             "6  Mixed: 40 normal + 5 projectiles", "7  MAIN: obstacles + stack + projectiles"};

struct ScriptCmd { double t; std::string cmd; };

struct Options {
    int hz = 60;
    std::string recordDir;
    int recordFrames = 0;
    int scene = int(SceneId::Main);
    double speed = -1; // -1: значение по умолчанию для сцены
    bool dots = false, noInterp = false, contacts = false, noHud = false;
    StepMode mode = StepMode::Discrete;
    int fastN = 4;
    std::vector<ScriptCmd> script;
};

StepMode parseMode(const char* s) {
    if (!std::strcmp(s, "ccd")) return StepMode::CCD;
    if (!std::strcmp(s, "multirate")) return StepMode::MultiRate;
    return StepMode::Discrete;
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--dots")) { o.dots = true; continue; }
        if (!std::strcmp(argv[i], "--no-interp")) { o.noInterp = true; continue; }
        if (!std::strcmp(argv[i], "--contacts")) { o.contacts = true; continue; }
        if (!std::strcmp(argv[i], "--no-hud")) { o.noHud = true; continue; }
        if (i + 1 >= argc) break;
        if (!std::strcmp(argv[i], "--hz")) o.hz = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--record")) o.recordDir = argv[++i];
        else if (!std::strcmp(argv[i], "--frames")) o.recordFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scene")) o.scene = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--speed")) o.speed = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--mode")) o.mode = parseMode(argv[++i]);
        else if (!std::strcmp(argv[i], "--fast-n")) o.fastN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--script")) {
            // "1.5:fire;3:mode-ccd" — время в секундах модельного времени
            std::string s = argv[++i];
            size_t pos = 0;
            while (pos < s.size()) {
                size_t end = s.find(';', pos);
                std::string item = s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                size_t c = item.find(':');
                if (c != std::string::npos) o.script.push_back({std::atof(item.substr(0, c).c_str()), item.substr(c + 1)});
                if (end == std::string::npos) break;
                pos = end + 1;
            }
        }
    }
    return o;
}

bool saveScreenshot(SDL_Renderer* r, const std::string& path) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

// Скользящее среднее для панели, чтобы числа не прыгали каждый кадр.
struct Ema {
    double v = 0; bool init = false;
    void add(double x, double k = 0.05) { v = init ? v + k * (x - v) : x; init = true; }
};

const char* modeLabel(StepMode m) {
    return m == StepMode::CCD ? "CCD" : m == StepMode::MultiRate ? "Multi-rate" : "Discrete";
}

} // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    const bool recording = !opt.recordDir.empty() && opt.recordFrames > 0;
    if (recording) SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen"); // запись без окна

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    const int W = int(scenes::kWidth), H = int(scenes::kHeight);
    SDL_Window* window = SDL_CreateWindow("CCD vs multi-rate — 2D rigid body",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
        SDL_WINDOW_RESIZABLE | (recording ? SDL_WINDOW_HIDDEN : 0));
    if (!window) { std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }

    // Сначала аппаратный рендерер с vsync, при неудаче — программный.
    SDL_Renderer* sdlr = recording ? nullptr
        : SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdlr) sdlr = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!sdlr) { std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(sdlr, W, H); // мир 1280×720 px масштабируется под окно

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // не сохранять imgui.ini рядом с бинарником
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.f;
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdlr);
    ImGui_ImplSDLRenderer2_Init(sdlr);

    // ------------------------------------------------------------------ state
    World world;
    Renderer renderer(sdlr);
    RenderOptions ropt;
    ropt.interpolate = !opt.noInterp;
    ropt.showContacts = opt.contacts;

    SceneId scene = SceneId(opt.scene >= 0 && opt.scene < int(SceneId::Count) ? opt.scene : int(SceneId::Main));
    StepMode mode = opt.mode;
    int hz = opt.hz;
    int fastN = opt.fastN;
    float projSpeed = 2000.f;
    scenes::MainScene mainScene;
    bool autoFire = true;

    FixedStepAccumulator acc(1.0 / hz);
    bool paused = false, stepOnce = false, running = true, showHud = !opt.noHud;
    long long totalSteps = 0;
    double simTime = 0;
    int toiThisFrame = 0, fastStepsThisFrame = 0;
    Ema physMs, frameMs, fastStepsEma;
    float physHistory[120] = {};
    int histPos = 0, recorded = 0, shotCounter = 0;
    size_t scriptPos = 0;

    // Частота, с которой шагают снаряды: в multi-rate — в fastN раз выше.
    auto fastHz = [&] { return mode == StepMode::MultiRate ? hz * fastN : hz; };

    // Сцена туннелирования: где был центр снаряда относительно центра стенки в момент
    // первого обнаруженного касания (индекс тела → u, px), и скорость при подлёте.
    std::map<int, double> firstContactU, launchSpeed;
    // Итог первого подлёта (PASSED/BOUNCED) запоминается: после отскока от стен комнаты
    // снаряд может вернуться к стенке, но строка в панели уже не меняется.
    std::map<int, const char*> outcome;

    auto reset = [&] {
        WorldParams keep = world.params; // параметры с панели переживают перезапуск
        firstContactU.clear();
        launchSpeed.clear();
        outcome.clear();
        switch (scene) {
        case SceneId::Stack: scenes::buildStack(world, 10); break;
        case SceneId::Bounce: scenes::buildBounceDemo(world); break;
        case SceneId::Ballistics: scenes::buildBallistics(world); break;
        case SceneId::Cloud: scenes::buildRandomCloud(world, 60, 1, true); break;
        case SceneId::Tunnel: scenes::buildTunnelDemo(world, projSpeed, fastHz()); break;
        case SceneId::Mixed: scenes::buildMixed(world, 40, 5, projSpeed, fastHz(), 5); break;
        default:
            mainScene.cfg.projSpeed = projSpeed;
            mainScene.cfg.fireEvery = autoFire ? 1.5 : 0;
            mainScene.build(world);
            break;
        }
        world.params = keep;
        world.params.fastSubsteps = fastN;
        for (int i = 0; i < int(world.bodies.size()); ++i) launchSpeed[i] = std::fabs(world.bodies[i].vel.x);
        renderer.clearDots();
        totalSteps = 0;
        simTime = 0;
    };
    // Настройки отображения «по умолчанию» для сцены — только при её ВЫБОРЕ,
    // а не при перезапуске (R), чтобы не сбрасывать то, что пользователь включил сам.
    auto selectScene = [&](SceneId s) {
        scene = s;
        if (opt.speed > 0) projSpeed = float(opt.speed);
        else projSpeed = scene == SceneId::Main ? 4000.f : 2000.f;
        ropt.showStepDots = opt.dots || scene == SceneId::Tunnel;
        reset();
    };
    selectScene(scene);

    auto physicsStep = [&](double dt) {
        world.step(dt, mode);
        ++totalSteps;
        simTime += dt;
        toiThisFrame += int(world.toiEvents.size());
        fastStepsThisFrame += mode == StepMode::MultiRate ? world.stats.fastSubsteps : 1;
        if (scene == SceneId::Main) {
            mainScene.afterStep(world, dt);
            if (mainScene.firedThisStep) renderer.clearDots(); // след — только текущего залпа
        }
        for (const ToiEvent& ev : world.toiEvents) {
            if (!world.bodies[ev.a].isStatic()) renderer.pushToiMark(ev.posA);
            if (!world.bodies[ev.b].isStatic()) renderer.pushToiMark(ev.posB);
        }
        if (scene == SceneId::Tunnel || scene == SceneId::Mixed) {
            const int wallIdx = 3; // комната (3 тела), затем стенка
            for (const Contact& c : world.contactLog) {
                int other = c.a == wallIdx ? c.b : c.b == wallIdx ? c.a : -1;
                if (other >= 0 && !firstContactU.count(other) && !outcome.count(other))
                    firstContactU[other] = (other == c.a ? c.pa.x : c.pb.x) - scenes::kTunnelWallX;
            }
            for (const ToiEvent& ev : world.toiEvents) {
                int other = ev.a == wallIdx ? ev.b : ev.b == wallIdx ? ev.a : -1;
                if (other >= 0 && !firstContactU.count(other) && !outcome.count(other))
                    firstContactU[other] = (other == ev.a ? ev.posA.x : ev.posB.x) - scenes::kTunnelWallX;
            }
        }
        if (ropt.showStepDots) {
            // В multi-rate быстрые тела рисуем по всем подшагам, обычные — по тикам.
            for (const Body& b : world.bodies)
                if (!b.isStatic() && !(mode == StepMode::MultiRate && b.group == BodyGroup::Fast))
                    renderer.pushStepDot(b.pos);
            if (mode == StepMode::MultiRate)
                for (const Vec2& p : world.fastTrail) renderer.pushStepDot(p);
        }
    };

    auto setMode = [&](StepMode m) { mode = m; }; // в рантайме, без перезапуска сцены
    auto fire = [&] {
        if (scene != SceneId::Main) return;
        mainScene.fire(world);
        renderer.clearDots();
    };
    auto runCommand = [&](const std::string& c) {
        if (c == "mode-discrete") setMode(StepMode::Discrete);
        else if (c == "mode-ccd") setMode(StepMode::CCD);
        else if (c == "mode-multirate") setMode(StepMode::MultiRate);
        else if (c == "fire") fire();
        else if (c == "reset") reset();
    };

    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && !ImGui::GetIO().WantCaptureKeyboard) {
                SDL_Keycode k = e.key.keysym.sym;
                switch (k) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE: paused = !paused; break;
                case SDLK_n: stepOnce = true; break;
                case SDLK_r: reset(); break;
                case SDLK_f: fire(); break;
                case SDLK_i: ropt.interpolate = !ropt.interpolate; break;
                case SDLK_d: ropt.showStepDots = !ropt.showStepDots; break;
                case SDLK_c: ropt.showContacts = !ropt.showContacts; break;
                case SDLK_h: showHud = !showHud; break;
                case SDLK_m:
                    setMode(mode == StepMode::Discrete ? StepMode::CCD
                          : mode == StepMode::CCD ? StepMode::MultiRate : StepMode::Discrete);
                    break;
                case SDLK_F12: {
                    std::string p = "screenshot_" + std::to_string(shotCounter++) + ".bmp";
                    saveScreenshot(sdlr, p);
                    std::printf("saved %s\n", p.c_str());
                } break;
                default:
                    if (k >= SDLK_1 && k <= SDLK_7) selectScene(SceneId(k - SDLK_1));
                    break;
                }
            }
        }

        // Сценарий для записи видео: команды по модельному времени.
        while (scriptPos < opt.script.size() && simTime >= opt.script[scriptPos].t - 1e-9)
            runCommand(opt.script[scriptPos++].cmd);

        Uint64 now = SDL_GetPerformanceCounter();
        double frameTime = recording ? 1.0 / 60.0 : (now - last) / freq;
        last = now;
        frameMs.add(frameTime * 1000.0);

        // ---- физика: столько фиксированных шагов, сколько накопилось ----
        auto t0 = Clock::now();
        int steps = 0;
        toiThisFrame = 0;
        fastStepsThisFrame = 0;
        if (!paused) {
            steps = acc.advance(frameTime, physicsStep);
        } else if (stepOnce) {
            physicsStep(acc.dt());
            steps = 1;
        }
        stepOnce = false;
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        physMs.add(ms);
        fastStepsEma.add(fastStepsThisFrame);
        physHistory[histPos] = float(ms);
        histPos = (histPos + 1) % 120;

        // ---- отрисовка ----
        double alpha = paused ? 1.0 : acc.alpha();
        renderer.drawWorld(world, alpha, ropt);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(372, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin("Simulation  [H - hide]", nullptr, ImGuiWindowFlags_NoResize);

        // --- режим: главное, что сравнивается, — всегда наверху и крупно ---
        ImGui::TextUnformatted("Mode [M]:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Discrete", mode == StepMode::Discrete)) setMode(StepMode::Discrete);
        ImGui::SameLine();
        if (ImGui::RadioButton("CCD", mode == StepMode::CCD)) setMode(StepMode::CCD);
        ImGui::SameLine();
        if (ImGui::RadioButton("Multi-rate", mode == StepMode::MultiRate)) setMode(StepMode::MultiRate);
        ImGui::SetNextItemWidth(-1);
        int sceneIdx = int(scene);
        if (ImGui::Combo("##scene", &sceneIdx, kSceneNames, int(SceneId::Count))) selectScene(SceneId(sceneIdx));

        // --- показатели ---
        ImGui::Separator();
        ImGui::Text("%s  |  %d Hz%s", modeLabel(mode), hz,
                    mode == StepMode::MultiRate ? "" : "");
        if (mode == StepMode::MultiRate) {
            ImGui::SameLine();
            ImGui::Text("/ fast %d Hz (N = %d)", hz * fastN, fastN);
        }
        ImGui::Text("Physics: %.4f ms/frame   FPS: %.0f", physMs.v, frameMs.v > 0 ? 1000.0 / frameMs.v : 0.0);
        ImGui::PlotLines("##phys", physHistory, 120, histPos, nullptr, 0.0f, FLT_MAX, ImVec2(-1, 32));
        ImGui::Text("Steps this frame: %d", steps);
        if (mode == StepMode::MultiRate) {
            ImGui::SameLine();
            ImGui::Text("| fast steps: %d (avg %.1f)", fastStepsThisFrame, fastStepsEma.v);
        }
        if (mode == StepMode::CCD) {
            ImGui::SameLine();
            ImGui::Text("| CCD impacts: %d%s", toiThisFrame, world.stats.toiExhausted ? " LIMIT!" : "");
        }
        ImGui::Text("Contacts: %d   max depth: %.2f px", world.stats.contacts, world.stats.maxDepth);

        // --- основная сцена: счётчик проскоков ---
        if (scene == SceneId::Main) {
            ImGui::Separator();
            ImGui::Text("Projectiles through obstacles: %d of %d shots", mainScene.tunnelEvents, mainScene.shots);
            if (ImGui::Button("Fire [F]")) fire();
            ImGui::SameLine();
            if (ImGui::Checkbox("auto every 1.5 s", &autoFire)) mainScene.cfg.fireEvery = autoFire ? 1.5 : 0;
            ImGui::SetNextItemWidth(170);
            if (ImGui::SliderFloat("speed, px/s", &projSpeed, 250.f, 8000.f, "%.0f")) mainScene.cfg.projSpeed = projSpeed;
            const Body& top = world.bodies[mainScene.stackTop];
            ImGui::Text("Stack top: dx %+.2f  sink %+.2f px", top.pos.x - mainScene.kStackX,
                        top.pos.y - mainScene.stackTopY0);
        }

        // --- частоты ---
        if (ImGui::CollapsingHeader("Time step", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(170);
            if (ImGui::SliderInt(mode == StepMode::MultiRate ? "Hz (normal bodies)" : "Physics Hz", &hz, 10, 480)) {
                acc.setDt(1.0 / hz);
                if (scene == SceneId::Tunnel || scene == SceneId::Mixed) reset(); // фазы снарядов зависят от частоты
            }
            ImGui::SetNextItemWidth(170);
            if (ImGui::SliderInt("multi-rate N", &fastN, 1, 8)) {
                world.params.fastSubsteps = fastN;
                if (scene == SceneId::Tunnel || scene == SceneId::Mixed) reset();
            }
        }

        // --- решатель ---
        if (ImGui::CollapsingHeader("Solver")) {
            float rest = float(world.params.restitution), mu = float(world.params.friction);
            ImGui::SetNextItemWidth(170);
            if (ImGui::SliderFloat("restitution e", &rest, 0.f, 1.f, "%.2f")) world.params.restitution = rest;
            ImGui::SetNextItemWidth(170);
            if (ImGui::SliderFloat("friction mu", &mu, 0.f, 1.f, "%.2f")) world.params.friction = mu;
            ImGui::SetNextItemWidth(170);
            ImGui::SliderInt("iterations", &world.params.velocityIterations, 1, 30);
            ImGui::Checkbox("warm start", &world.params.warmStart);
            ImGui::Text("Kinetic energy: %.1f", world.kineticEnergy());
        }

        // --- сцена туннелирования: подробности по каждому снаряду ---
        if ((scene == SceneId::Tunnel || scene == SceneId::Mixed) &&
            ImGui::CollapsingHeader("Projectiles", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(170);
            if (ImGui::SliderFloat("speed, px/s##t", &projSpeed, 250.f, 6000.f, "%.0f")) reset();
            const double xw = scenes::kTunnelWallX, halfSum = (scenes::kTunnelWallW + scenes::kProjSize) / 2;
            ImGui::TextDisabled(mode == StepMode::CCD ? "CCD: impact at the front face, u = -9 px"
                                                      : "overlap if |u| < 9 px; u > 0 -> pushed forward");
            for (int i = 0; i < int(world.bodies.size()); ++i) {
                const Body& b = world.bodies[i];
                if (b.group != BodyGroup::Fast) continue;
                double rel = b.pos.x - xw;
                const char* st = rel >= halfSum ? "PASSED" : b.vel.x < 0 ? "BOUNCED" : "flying";
                auto oc = outcome.find(i);
                if (oc != outcome.end()) st = oc->second;
                else if (st[0] != 'f') outcome[i] = st;
                char contact[48];
                auto it = firstContactU.find(i);
                if (it != firstContactU.end()) std::snprintf(contact, sizeof(contact), "u=%+5.1f", it->second);
                else std::snprintf(contact, sizeof(contact), rel >= halfSum ? "missed" : "-");
                ImGui::Text("%4.0f px/s  %4.1f px/step  %-8s %s", launchSpeed[i], launchSpeed[i] / fastHz(), contact, st);
            }
            ImGui::TextDisabled("phases are set for the current Hz: press R after changes");
        }

        // --- отображение ---
        if (ImGui::CollapsingHeader("Display")) {
            ImGui::Checkbox("interpolation [I]", &ropt.interpolate);
            ImGui::SameLine();
            ImGui::Checkbox("step dots [D]", &ropt.showStepDots);
            ImGui::Checkbox("contacts [C]", &ropt.showContacts);
        }

        ImGui::Separator();
        if (ImGui::Button(paused ? "Resume [Space]" : "Pause [Space]")) paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("Step [N]")) stepOnce = true;
        ImGui::SameLine();
        if (ImGui::Button("Reset [R]")) reset();
        ImGui::End();
        ImGui::Render();
        if (showHud) ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdlr);

        if (recording) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/frame_%05d.bmp", opt.recordDir.c_str(), recorded);
            saveScreenshot(sdlr, path);
            // Для подписей в видео: счётчик проскоков основной сцены на каждом кадре.
            if (scene == SceneId::Main)
                std::printf("frame %d t %.4f tunnels %d shots %d\n", recorded, simTime, mainScene.tunnelEvents, mainScene.shots);
            if (++recorded >= opt.recordFrames) running = false;
        }
        SDL_RenderPresent(sdlr);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(sdlr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
