#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GLFW/glfw3.h>
#include <mujoco/mujoco.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "levels.h"

// ---------------------------------------------------------------------------
// 모델 생성: assets/balance.xml 템플릿에 난이도 값을 넣어서 로드
// ---------------------------------------------------------------------------
static std::string g_template;

static void replaceAll(std::string& s, const std::string& key, double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", value);
    for (size_t pos = s.find(key); pos != std::string::npos; pos = s.find(key, pos)) {
        s.replace(pos, key.size(), buf);
        pos += std::strlen(buf);
    }
}

static double maxTorque(const Level& lv) {
    return lv.authority * lv.mass * lv.g * lv.L / 2.0;
}

static double pivotHeight(const Level& lv) { return lv.L + 0.2; }

static mjModel* buildModel(const Level& lv) {
    std::string xml = g_template;
    replaceAll(xml, "{GRAVITY}", lv.g);
    replaceAll(xml, "{DAMPING}", lv.damping);
    replaceAll(xml, "{PIVOT}", pivotHeight(lv));
    replaceAll(xml, "{PEDESTAL_HALF}", pivotHeight(lv) / 2.0);
    replaceAll(xml, "{LENGTH}", lv.L);
    replaceAll(xml, "{RADIUS}", std::max(0.012, lv.L * 0.025));
    replaceAll(xml, "{MASS}", lv.mass);
    replaceAll(xml, "{TORQUE}", maxTorque(lv));

    mjVFS vfs;
    mj_defaultVFS(&vfs);
    mj_addBufferVFS(&vfs, "level.xml", xml.data(), (int)xml.size());
    char error[1000] = "";
    mjModel* m = mj_loadXML("level.xml", &vfs, error, sizeof(error));
    mj_deleteVFS(&vfs);
    if (!m) std::fprintf(stderr, "XML load error: %s\n", error);
    return m;
}

// ---------------------------------------------------------------------------
// 게임 상태 (렌더링과 분리 → --selftest 에서 창 없이 돌릴 수 있음)
// ---------------------------------------------------------------------------
enum class State { Playing, Fallen };

struct Game {
    int level = 0;
    mjModel* m = nullptr;
    mjData* d = nullptr;
    State state = State::Playing;
    double survive = 0.0;          // 이번 판 버틴 시간
    double best[kNumLevels] = {};  // 레벨별 최고 기록
    bool clearedNow = false;       // 이번 판에서 클리어 했는지
    bool autopilot = false;        // P: 자동 PD 제어 (사람 vs 기계 비교)
    bool paused = false;
    std::deque<double> delayBuf;   // 입력 지연 버퍼
    double nextKick = 0.0;
    double kickFlash = -1.0;       // 외란 표시용 시각
    std::mt19937 rng{12345};

    const Level& lv() const { return kLevels[level]; }

    // 회복 가능 각도: k >= 1 이면 (정적으로는) 어떤 각도든 회복 가능
    double recoverAngle() const {
        return lv().authority >= 1.0 ? kFailAngle : std::asin(lv().authority);
    }
    double lambda() const { return std::sqrt(3.0 * lv().g / (2.0 * lv().L)); }

    bool load(int idx) {
        idx = std::clamp(idx, 0, kNumLevels - 1);
        mjModel* nm = buildModel(kLevels[idx]);
        if (!nm) return false;
        if (d) mj_deleteData(d);
        if (m) mj_deleteModel(m);
        m = nm;
        d = mj_makeData(m);
        level = idx;
        reset();
        return true;
    }

    void reset() {
        mj_resetData(m, d);
        std::uniform_int_distribution<int> coin(0, 1);
        d->qpos[0] = coin(rng) ? lv().tilt0 : -lv().tilt0;
        mj_forward(m, d);
        state = State::Playing;
        survive = 0.0;
        clearedNow = false;
        delayBuf.clear();
        scheduleKick();
        kickFlash = -1.0;
    }

    void scheduleKick() {
        if (lv().kickPeriod <= 0) { nextKick = 1e30; return; }
        std::uniform_real_distribution<double> u(0.6, 1.4);
        nextKick = d->time + lv().kickPeriod * u(rng);
    }

    // 선형화 모델 기준 PD 게인: 극점을 대략 -2λ 근처에 둠
    double pdTorque() const {
        double I = lv().mass * lv().L * lv().L / 3.0;
        double lam = lambda();
        // 지연이 길수록 게인을 낮춰야 안정 (λ·delay 기준)
        double ld = lam * lv().delay;
        double kp = (3.0 - 1.5 * std::min(ld, 1.0)) * I * lam * lam;
        double kd = (1.6 - 0.4 * std::min(ld, 1.0)) * I * lam;
        return -kp * d->qpos[0] - kd * d->qvel[0];
    }

    // input: 플레이어 입력 -1..1
    void step(double input) {
        const double tmax = maxTorque(lv());
        double cmd = 0.0;
        if (state == State::Playing) {
            if (autopilot) {
                cmd = pdTorque();
            } else {
                cmd = input * tmax + lv().assist * pdTorque();
            }
        }
        cmd = std::clamp(cmd, -tmax, tmax);

        // 입력 지연
        int delaySteps = (int)std::lround(lv().delay / m->opt.timestep);
        delayBuf.push_back(cmd);
        double applied = 0.0;
        if ((int)delayBuf.size() > delaySteps) {
            applied = delayBuf.front();
            delayBuf.pop_front();
        }
        d->ctrl[0] = applied;

        // 외란
        if (state == State::Playing && d->time >= nextKick) {
            std::uniform_int_distribution<int> coin(0, 1);
            d->qvel[0] += coin(rng) ? lv().kick : -lv().kick;
            kickFlash = d->time;
            scheduleKick();
        }

        mj_step(m, d);

        if (state == State::Playing) {
            survive += m->opt.timestep;
            if (survive >= kClearTime) clearedNow = true;
            if (std::fabs(d->qpos[0]) > kFailAngle) {
                state = State::Fallen;
                best[level] = std::max(best[level], survive);
            }
        } else {
            best[level] = std::max(best[level], survive);
        }
    }
};

// ---------------------------------------------------------------------------
// 진행 상황 저장 (레벨별 최고 기록)
// ---------------------------------------------------------------------------
static const char* kProgressFile = "progress.txt";

static void loadProgress(Game& g) {
    std::ifstream in(kProgressFile);
    int i; double t;
    while (in >> i >> t)
        if (i >= 0 && i < kNumLevels) g.best[i] = t;
}

static void saveProgress(const Game& g) {
    std::ofstream out(kProgressFile);
    for (int i = 0; i < kNumLevels; ++i) out << i << " " << g.best[i] << "\n";
}

static bool loadTemplate() {
    std::ifstream in("assets/balance.xml");
    if (!in) {
        std::fprintf(stderr, "assets/balance.xml 을 찾을 수 없습니다.\n");
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    g_template = ss.str();
    return true;
}

static void printLevels() {
    std::printf("\n%-3s %-14s %6s %6s %7s %7s %8s %7s %6s\n",
                "Lv", "Name", "L[m]", "g", "1/lam", "tmax", "recover", "delay", "kick");
    for (int i = 0; i < kNumLevels; ++i) {
        const Level& lv = kLevels[i];
        double lam = std::sqrt(3.0 * lv.g / (2.0 * lv.L));
        double rec = lv.authority >= 1.0 ? 90.0 : std::asin(lv.authority) * 180.0 / mjPI;
        std::printf("%-3d %-14s %6.2f %6.2f %6.3fs %6.2f %7.1f° %6.0fms %6.1f   %s\n",
                    i + 1, lv.en, lv.L, lv.g, 1.0 / lam, maxTorque(lv), rec,
                    lv.delay * 1000, lv.kick, lv.ko);
    }
    std::printf("\n");
}

// 창 없이 자동 PD 로 각 레벨을 돌려 보는 테스트: balance_game --selftest
static int selfTest() {
    printLevels();
    std::printf("Autopilot(PD) survival, %d seeds x %.0fs:\n", 5, kClearTime);
    Game g;
    g.autopilot = true;
    for (int i = 0; i < kNumLevels; ++i) {
        if (!g.load(i)) return 1;
        int ok = 0;
        double maxTilt = 0;
        for (int s = 0; s < 5; ++s) {
            g.rng.seed(100 + s);
            g.reset();
            while (g.state == State::Playing && g.survive < kClearTime) {
                g.step(0.0);
                maxTilt = std::max(maxTilt, std::fabs(g.d->qpos[0]));
            }
            if (g.state == State::Playing) ++ok;
        }
        std::printf("  Lv%-2d %-13s %d/5 cleared, max tilt %5.1f deg (recoverable %5.1f)\n",
                    i + 1, kLevels[i].en, ok, maxTilt * 180 / mjPI,
                    g.recoverAngle() * 180 / mjPI);
    }
    // 제어 없음: 넘어지는 시간 vs 해석해 t = acosh(θf/θ0)/λ (작은 각 근사)
    std::printf("\nNo control fall time (sim vs linear theory to 0.3 rad):\n");
    g.autopilot = false;
    for (int i = 0; i < kNumLevels; ++i) {
        Level lv = kLevels[i];
        lv.damping = 0; lv.assist = 0; lv.kick = 0; lv.kickPeriod = 0;
        mjModel* m = buildModel(lv);
        mjData* d = mj_makeData(m);
        d->qpos[0] = 0.01;
        while (std::fabs(d->qpos[0]) < 0.3) mj_step(m, d);
        double lam = std::sqrt(3.0 * lv.g / (2.0 * lv.L));
        std::printf("  Lv%-2d sim %.3fs  theory %.3fs\n", i + 1, d->time,
                    std::acosh(0.3 / 0.01) / lam);
        mj_deleteData(d);
        mj_deleteModel(m);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------
static Game g_game;
static bool g_levelChanged = true;

static void keyCallback(GLFWwindow* window, int key, int, int act, int) {
    if (act != GLFW_PRESS) return;
    Game& g = g_game;
    int target = -1;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, 1); return;
        case GLFW_KEY_R: g.reset(); return;
        case GLFW_KEY_SPACE: g.paused = !g.paused; return;
        case GLFW_KEY_P: g.autopilot = !g.autopilot; g.reset(); return;
        case GLFW_KEY_UP:
        case GLFW_KEY_PAGE_UP: target = g.level + 1; break;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_PAGE_DOWN: target = g.level - 1; break;
        case GLFW_KEY_0: target = 9; break;
        case GLFW_KEY_MINUS: target = 10; break;
        case GLFW_KEY_EQUAL: target = 11; break;
        default:
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) target = key - GLFW_KEY_1;
    }
    if (target >= 0 && target < kNumLevels && target != g.level) {
        g.load(target);
        g_levelChanged = true;
    }
}

static double readInput(GLFWwindow* w) {
    double in = 0.0;
    if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) in -= 1.0;
    if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) in += 1.0;
    bool fine = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    return fine ? in * 0.3 : in;
}

// 회복 불가 경계선을 3D 장면에 추가
static void addLine(mjvScene& scn, double angle, double len, double pivot, const float rgba[4]) {
    if (scn.ngeom >= scn.maxgeom) return;
    mjvGeom* geom = scn.geoms + scn.ngeom++;
    mjv_initGeom(geom, mjGEOM_NONE, nullptr, nullptr, nullptr, rgba);
    mjtNum from[3] = {0, -0.05, pivot};
    mjtNum to[3] = {len * std::sin(angle), -0.05, pivot + len * std::cos(angle)};
    mjv_connector(geom, mjGEOM_CAPSULE, 0.006, from, to);
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (!loadTemplate()) return 1;
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selfTest();

    Game& game = g_game;
    loadProgress(game);
    int startLevel = argc > 1 ? std::atoi(argv[1]) - 1 : 0;
    if (!game.load(startLevel)) return 1;

    printLevels();
    std::printf("조작: ←/→ (또는 A/D) 토크, Shift 미세 조작, ↑/↓ 또는 1~9,0,-,= 레벨 선택\n"
                "      R 재시작, P 자동 PD 제어(사람 vs 기계), Space 일시정지, Esc 종료\n"
                "      %.0f초 버티면 클리어!\n\n", kClearTime);

    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Balance Lab", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, keyCallback);

    mjvCamera cam;   mjv_defaultCamera(&cam);
    mjvOption opt;   mjv_defaultOption(&opt);
    mjvScene scn;    mjv_defaultScene(&scn);
    mjrContext con;  mjr_defaultContext(&con);

    double lastTime = glfwGetTime();
    double budget = 0.0;
    int lastBestSaved = -1;

    while (!glfwWindowShouldClose(window)) {
        Game& g = game;
        const Level& lv = g.lv();

        if (g_levelChanged) {
            g_levelChanged = false;
            char title[256];
            std::snprintf(title, sizeof(title), "Balance Lab - Lv.%d %s (%s)",
                          g.level + 1, lv.ko, lv.en);
            glfwSetWindowTitle(window, title);
            std::printf("▶ Lv.%d %s\n", g.level + 1, lv.ko);
            mjv_freeScene(&scn);
            mjv_makeScene(g.m, &scn, 2000);
            mjr_freeContext(&con);
            mjr_makeContext(g.m, &con, mjFONTSCALE_150);
            cam.type = mjCAMERA_FREE;
            cam.lookat[0] = 0;
            cam.lookat[1] = 0;
            cam.lookat[2] = pivotHeight(lv) + 0.3 * lv.L;
            cam.distance = 1.0 + 2.6 * lv.L;
            cam.azimuth = 90;
            cam.elevation = -8;
            budget = 0.0;
        }

        // 실시간에 맞춰 물리 스텝
        double now = glfwGetTime();
        double frameDt = std::min(now - lastTime, 0.05);
        lastTime = now;
        double input = readInput(window);
        if (!g.paused) {
            budget += frameDt;
            while (budget >= g.m->opt.timestep) {
                g.step(input);
                budget -= g.m->opt.timestep;
            }
        }

        // 기록 저장 (넘어진 순간 한 번)
        if (g.state == State::Fallen && lastBestSaved != g.level) {
            saveProgress(g);
            lastBestSaved = g.level;
        } else if (g.state == State::Playing) {
            lastBestSaved = -1;
        }

        // 막대 색: 회복 가능(주황) / 회복 불가 영역(빨강) / 넘어짐(회색)
        double tilt = std::fabs(g.d->qpos[0]);
        int pole = mj_name2id(g.m, mjOBJ_GEOM, "pole");
        float* rgba = g.m->geom_rgba + 4 * pole;
        if (g.state == State::Fallen)          { rgba[0] = 0.5f; rgba[1] = 0.5f; rgba[2] = 0.5f; }
        else if (tilt > g.recoverAngle())      { rgba[0] = 0.9f; rgba[1] = 0.1f; rgba[2] = 0.1f; }
        else if (g.clearedNow)                 { rgba[0] = 0.2f; rgba[1] = 0.8f; rgba[2] = 0.3f; }
        else                                   { rgba[0] = 0.9f; rgba[1] = 0.4f; rgba[2] = 0.2f; }

        // 렌더링
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        mjrRect viewport = {0, 0, w, h};
        mjv_updateScene(g.m, g.d, &opt, nullptr, &cam, mjCAT_ALL, &scn);
        if (lv.authority < 1.0) {
            const float red[4] = {1.0f, 0.1f, 0.1f, 0.6f};
            addLine(scn, g.recoverAngle(), lv.L * 1.1, pivotHeight(lv), red);
            addLine(scn, -g.recoverAngle(), lv.L * 1.1, pivotHeight(lv), red);
        }
        const float green[4] = {0.1f, 0.8f, 0.2f, 0.4f};
        addLine(scn, 0.0, lv.L * 1.1, pivotHeight(lv), green);
        mjr_render(viewport, &scn, &con);

        // HUD
        char left[1024], right[1024];
        std::snprintf(left, sizeof(left),
                      "Level\nLength L\nGravity g\n1/lambda\nTorque max\nRecoverable\n"
                      "Input delay\nPush\nAssist\nControl\n\nTime\nBest");
        char recText[32];
        if (lv.authority >= 1.0) std::snprintf(recText, sizeof(recText), "any angle");
        else std::snprintf(recText, sizeof(recText), "%.0f deg", g.recoverAngle() * 180 / mjPI);
        std::snprintf(right, sizeof(right),
                      "%d/%d %s%s\n%.2f m\n%.2f m/s2\n%.3f s\n%.2f Nm\n%s\n"
                      "%.0f ms\n%.1f rad/s\n%.0f %%\n%s\n\n%.2f / %.0f s\n%.2f s",
                      g.level + 1, kNumLevels, lv.en, g.best[g.level] >= kClearTime ? " *" : "",
                      lv.L, lv.g, 1.0 / g.lambda(), maxTorque(lv), recText,
                      lv.delay * 1000, lv.kick, lv.assist * 100,
                      g.autopilot ? "AUTOPILOT (PD)" : "HUMAN",
                      g.survive, kClearTime, g.best[g.level]);
        mjr_overlay(mjFONT_NORMAL, mjGRID_TOPLEFT, viewport, left, right, &con);

        mjr_overlay(mjFONT_NORMAL, mjGRID_BOTTOMLEFT, viewport,
                    "Left/Right (A/D)\nShift\nUp/Down, 1-9 0 - =\nR\nP\nSpace\nEsc",
                    "torque\nfine control\nchange level\nrestart\nautopilot on/off\npause\nquit",
                    &con);

        const char* status = nullptr;
        if (g.paused) status = "PAUSED";
        else if (g.state == State::Fallen)
            status = g.clearedNow ? "CLEAR!  Fell after clearing - R: retry, Up: next level"
                                  : "FALLEN - press R to retry";
        else if (g.clearedNow) status = "CLEAR!  Keep going or press Up for next level";
        else if (g.kickFlash >= 0 && g.d->time - g.kickFlash < 0.4) status = "PUSH!";
        if (status) mjr_overlay(mjFONT_BIG, mjGRID_TOP, viewport, status, nullptr, &con);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    saveProgress(game);
    mjr_freeContext(&con);
    mjv_freeScene(&scn);
    mj_deleteData(game.d);
    mj_deleteModel(game.m);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
