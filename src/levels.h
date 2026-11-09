#pragma once
// 난이도 표. 아래로 갈수록 물리적으로 세우기 어렵다.
//
//  - 막대가 짧을수록, 중력이 클수록 λ = sqrt(3g / 2L) 이 커져서 빨리 넘어진다.
//  - authority k = τmax / (m g L / 2). k < 1 이면 회복 가능 각도 θmax = asin(k) 가 생기고,
//    그보다 더 기울면 무엇을 해도 넘어진다.
//  - delay: 입력이 실제 토크로 들어가기까지의 지연(초). λ·delay 가 커질수록 사실상 불가능해진다.
//  - kick: 주기적으로 막대를 쳐서 각속도를 바꾸는 외란(rad/s).
//  - assist: 자동 PD 보조 비율 (초보용).

struct Level {
    const char* ko;      // 한국어 이름 (창 제목/콘솔)
    const char* en;      // 영어 이름 (HUD: MuJoCo 폰트는 ASCII만 지원)
    double L;            // 막대 길이 [m]
    double g;            // 중력 가속도 [m/s^2]
    double mass;         // 막대 질량 [kg]
    double authority;    // k = τmax / (m g L / 2)
    double damping;      // 관절 감쇠 [N·m·s/rad]
    double assist;       // PD 보조 비율 0..1
    double delay;        // 입력 지연 [s]
    double kick;         // 외란 크기 [rad/s]
    double kickPeriod;   // 외란 평균 주기 [s] (0 = 없음)
    double tilt0;        // 시작 기울기 [rad]
};

static const Level kLevels[] = {
    //  ko              en              L     g      m    k     damp  assist delay  kick kickT  tilt0
    {"기초",          "Basic",        1.20,  5.00, 1.0, 1.60, 0.80, 0.60, 0.000, 0.0, 0.0, 0.03},
    {"초급",          "Beginner",     1.00,  9.81, 1.0, 1.40, 0.40, 0.30, 0.000, 0.0, 0.0, 0.05},
    {"중급",          "Intermediate", 0.80,  9.81, 1.0, 1.20, 0.15, 0.00, 0.000, 0.3, 4.0, 0.05},
    {"상급",          "Upper",        0.65,  9.81, 1.0, 1.00, 0.05, 0.00, 0.020, 0.5, 3.5, 0.06},
    {"고급",          "Advanced",     0.50,  9.81, 1.0, 0.85, 0.00, 0.00, 0.030, 0.7, 3.0, 0.06},
    {"심화",          "Deep",         0.45, 11.00, 1.0, 0.75, 0.00, 0.00, 0.040, 0.9, 3.0, 0.07},
    {"최상위",        "Elite",        0.40, 13.00, 1.0, 0.65, 0.00, 0.00, 0.050, 1.0, 2.5, 0.07},
    {"전문가",        "Expert",       0.35, 15.00, 1.0, 0.55, 0.00, 0.00, 0.045, 1.2, 2.5, 0.08},
    // ---- 전문가보다 더 어려운 단계 ----
    {"마스터",        "Master",       0.30, 18.00, 1.0, 0.50, 0.00, 0.00, 0.040, 1.3, 2.0, 0.08},
    {"그랜드마스터",  "Grandmaster",  0.25, 24.79, 1.0, 0.45, 0.00, 0.00, 0.035, 1.4, 2.0, 0.08},  // 목성 중력
    {"전설",          "Legend",       0.20, 30.00, 1.0, 0.40, 0.00, 0.00, 0.030, 1.5, 1.8, 0.09},
    {"신",            "God",          0.15, 40.00, 1.0, 0.35, 0.00, 0.00, 0.022, 1.5, 1.5, 0.10},
};

static const int kNumLevels = sizeof(kLevels) / sizeof(kLevels[0]);
static const double kClearTime = 20.0;   // 이 시간 버티면 클리어 [s]
static const double kFailAngle = 1.4;    // 이 각도 넘으면 넘어짐 판정 [rad] (~80°)
