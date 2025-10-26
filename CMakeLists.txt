cmake_minimum_required(VERSION 3.24)
project(kusozako-mvp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /WX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

set(WORLD_SYSTEM_SOURCES
  src/world/systems/BehaviorSystem.cpp
  src/world/systems/CommanderInputSystem.cpp
  src/world/systems/CombatSystem.cpp
  src/world/systems/FormationSystem.cpp
  src/world/systems/JobAbilitySystem.cpp
  src/world/systems/MoraleSystem.cpp
  src/world/systems/MovementSystem.cpp
  src/world/systems/RenderingPrepSystem.cpp
)

add_executable(kusozako
  src/main.cpp
  src/app/GameApplication.cpp
  src/app/UiPresenter.cpp
  src/assets/AssetManager.cpp
  src/events/EventBus.cpp
  src/input/ActionBuffer.cpp
  src/input/InputMapper.cpp
  src/config/AppConfig.cpp
  src/config/AppConfigLoader.cpp
  src/services/ServiceLocator.cpp
  src/scenes/SceneStack.cpp
  src/telemetry/FileTelemetrySink.cpp
  src/telemetry/TelemetrySink.cpp
  src/telemetry/ConsoleTelemetrySink.cpp
  src/world/LegacySimulation.cpp
  src/world/spawn/Spawner.cpp
  src/world/spawn/WaveController.cpp
  ${WORLD_SYSTEM_SOURCES}
)

target_include_directories(kusozako PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

target_link_libraries(kusozako PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

if(APPLE)
  target_compile_definitions(kusozako PRIVATE SDL_HINT_VIDEO_HIGHDPI=1)
endif()

enable_testing()

set(WORLD_STATE_CORE_SOURCES
  src/main.cpp
  src/app/GameApplication.cpp
  src/app/UiPresenter.cpp
  src/assets/AssetManager.cpp
  src/config/AppConfig.cpp
  src/config/AppConfigLoader.cpp
  src/events/EventBus.cpp
  src/input/ActionBuffer.cpp
  src/input/InputMapper.cpp
  src/services/ServiceLocator.cpp
  src/scenes/SceneStack.cpp
  src/telemetry/FileTelemetrySink.cpp
  src/telemetry/TelemetrySink.cpp
  src/telemetry/ConsoleTelemetrySink.cpp
  src/world/LegacySimulation.cpp
  src/world/spawn/Spawner.cpp
  src/world/spawn/WaveController.cpp
  ${WORLD_SYSTEM_SOURCES}
)

add_executable(config_schema_test
  tests/ConfigSchemaTest.cpp
  src/assets/AssetManager.cpp
  src/config/AppConfig.cpp
  src/config/AppConfigLoader.cpp
)

target_include_directories(config_schema_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(config_schema_test PRIVATE PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(config_schema_test PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

add_test(NAME config_schema COMMAND config_schema_test)

add_executable(world_state_step_order_test
  tests/WorldStateStepOrderTest.cpp
  ${WORLD_STATE_CORE_SOURCES}
)

target_include_directories(world_state_step_order_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_compile_definitions(world_state_step_order_test PRIVATE KUSOZAKO_SKIP_APP_MAIN=1)

target_link_libraries(world_state_step_order_test PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

add_test(NAME world_state_step_order COMMAND world_state_step_order_test)

add_executable(systems_behavior_test
  tests/SystemsBehaviorTest.cpp
  ${WORLD_STATE_CORE_SOURCES}
)

target_include_directories(systems_behavior_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_compile_definitions(systems_behavior_test PRIVATE KUSOZAKO_SKIP_APP_MAIN=1)

target_link_libraries(systems_behavior_test PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

add_test(NAME systems_behavior COMMAND systems_behavior_test)

add_executable(job_ability_system_test
  tests/JobAbilitySystemTest.cpp
  ${WORLD_STATE_CORE_SOURCES}
)

target_include_directories(job_ability_system_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_compile_definitions(job_ability_system_test PRIVATE KUSOZAKO_SKIP_APP_MAIN=1)

target_link_libraries(job_ability_system_test PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

add_test(NAME job_ability_system COMMAND job_ability_system_test)
