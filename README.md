# Balance Game (MuJoCo + C++)

막대를 똑바로 세우는 3D 균형 게임의 시작 골격입니다.

## 폴더 구조
```
BalanceGame/
├─ CMakeLists.txt
├─ README.md
├─ assets/
│  └─ balance.xml
└─ src/
   └─ main.cpp
```

## 사전 준비 (Windows)
- Visual Studio 2022 + "C++를 사용한 데스크톱 개발" 워크로드
- CMake (VS에 포함된 버전 사용 가능)
- 선택: "C++ Clang 도구" (MuJoCo 문서에 따르면 Windows에서는 Clang이 성능이 더 좋았다고 함)

## 빌드
MuJoCo는 `CMakeLists.txt`에서 `3.9.0` 태그로 고정되어 있으며, 첫 configure 시 자동으로 내려받습니다.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Clang을 쓰려면 다음과 같이 configure 합니다.
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
```

## 실행
```
build\Release\balance_game.exe
```
빌드 시 `assets` 폴더가 실행 파일 옆으로 복사됩니다.

## 현재 상태
- MuJoCo 모델 로드와 시뮬레이션 루프
- OpenGL 기반 3D 렌더링
- PD 제어로 막대 세우기 (게인 kp, kd는 `src/main.cpp`에서 튜닝)

## 다음 작업 (로드맵)
1. 키보드 입력(←/→)으로 수동 토크 추가
2. 토크 예산 시스템과 HUD 구현
3. 랜덤 외란(바람, 충격) 추가
4. 난이도 설정, 최고 기록 저장
5. 고스트 리플레이
