#include <GLFW/glfw3.h>
#include <mujoco/mujoco.h>
#include <cstdio>

int main() {
    // 모델 로드
    char error[1000] = "";
    mjModel* m = mj_loadXML("assets/balance.xml", nullptr, error, sizeof(error));
    if (!m) {
        std::fprintf(stderr, "XML load error: %s\n", error);
        return 1;
    }
    mjData* d = mj_makeData(m);
    d->qpos[0] = 0.05;  // 살짝 기울여서 시작

    // 창 생성
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Balance Game", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // 렌더링 준비
    mjvCamera cam;   mjv_defaultCamera(&cam);
    mjvOption opt;   mjv_defaultOption(&opt);
    mjvScene scn;    mjv_defaultScene(&scn);
    mjrContext con;  mjr_defaultContext(&con);
    mjv_makeScene(m, &scn, 2000);
    mjr_makeContext(m, &con, mjFONTSCALE_150);

    const double kp = 60.0;  // 각도 복원 게인 (튜닝 대상)
    const double kd = 8.0;   // 각속도 감쇠 게인 (튜닝 대상)

    while (!glfwWindowShouldClose(window)) {
        // PD 제어: 막대를 똑바로 세우는 토크
        d->ctrl[0] = -kp * d->qpos[0] - kd * d->qvel[0];

        // 서브스테핑: 프레임당 여러 번 물리 스텝
        for (int i = 0; i < 4; ++i) {
            mj_step(m, d);
        }

        // 렌더링
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        mjrRect viewport = {0, 0, w, h};
        mjv_updateScene(m, d, &opt, nullptr, &cam, mjCAT_ALL, &scn);
        mjr_render(viewport, &scn, &con);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 정리
    mjr_freeContext(&con);
    mjv_freeScene(&scn);
    mj_deleteData(d);
    mj_deleteModel(m);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
