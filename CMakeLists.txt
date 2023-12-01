cmake_minimum_required( VERSION 3.21 )
set( CMAKE_CXX_STANDARD 17 )
project( diskiller )

set( BUILD_STATIC_LIBS ON )
add_subdirectory( ext/raylib )
add_subdirectory( ext/glm )
add_subdirectory( ext/spdlog )
add_subdirectory( ext/json )

add_library( tileson INTERFACE )
target_include_directories( tileson INTERFACE "${CMAKE_SOURCE_DIR}/ext/tileson/include" )

add_executable( diskiller src/main.cpp )
target_link_libraries( diskiller PUBLIC raylib glm spdlog nlohmann_json tileson )
target_precompile_headers( diskiller PUBLIC <raylib.h> <glm/glm.hpp> <spdlog/spdlog.h> <nlohmann/json.hpp> <tileson.h> )