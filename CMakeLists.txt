cmake_minimum_required(VERSION 3.15)
project(earth_generator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CPU raytracer: default to an optimized build (-O0 is ~10x slower).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenMP)

add_library(core STATIC
    src/render.cpp
    src/post.cpp
    src/earth.cpp
    src/image_io.cpp
    src/grid.cpp
    src/terrain.cpp
    src/erosion.cpp
    src/surface.cpp
    src/climate.cpp
    src/clouds.cpp
    src/view.cpp
    src/shade.cpp
    src/earth_data.cpp
    src/maps.cpp src/cities.cpp
    src/physics.cpp
    src/craters.cpp
)
target_include_directories(core PUBLIC src third_party)
if(OpenMP_CXX_FOUND)
    target_link_libraries(core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(earthgen src/main.cpp)
target_link_libraries(earthgen PRIVATE core)

add_executable(stylize_ref src/stylize_ref.cpp)
target_link_libraries(stylize_ref PRIVATE core)
