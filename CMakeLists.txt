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

add_executable(kusozako
  src/main.cpp
)

target_include_directories(kusozako PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)

target_link_libraries(kusozako PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image)

if(APPLE)
  target_compile_definitions(kusozako PRIVATE SDL_HINT_VIDEO_HIGHDPI=1)
endif()
