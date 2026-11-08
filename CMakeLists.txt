cmake_minimum_required(VERSION 3.16)
project(BalanceGame LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# GLFW (창 / 입력)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# MuJoCo (물리 엔진)
# 3.x 계열 최신 릴리스로 고정 (4.0.0은 메이저 업데이트라 API 변경 가능성 있음)
FetchContent_Declare(mujoco
  GIT_REPOSITORY https://github.com/google-deepmind/mujoco.git
  GIT_TAG 3.9.0)
FetchContent_MakeAvailable(mujoco)

add_executable(balance_game src/main.cpp)
target_link_libraries(balance_game PRIVATE mujoco glfw)

# 빌드 후 assets 폴더를 실행 파일 옆으로 복사
add_custom_command(TARGET balance_game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:balance_game>/assets)
