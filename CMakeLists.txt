cmake_minimum_required( VERSION 3.21 )
set( CMAKE_CXX_STANDARD 17 )
project( diskiller VERSION 0.3.1 )

add_compile_definitions( __RELEASE=$<IF:$<CONFIG:Release>,1,0> )
add_compile_definitions( __ANDROID=$<IF:$<PLATFORM_ID:Android>,1,0> )
add_compile_definitions( __LINUX=$<IF:$<PLATFORM_ID:Linux>,1,0> )
add_compile_definitions( __WEB=$<IF:$<PLATFORM_ID:Emscripten>,1,0> )
add_compile_definitions( __WINDOWS=$<IF:$<PLATFORM_ID:Windows>,1,0> )
add_compile_definitions( BUILD_VERSION="${PROJECT_VERSION}" )

set( BUILD_STATIC_LIBS ON )
add_subdirectory( ext/raylib EXCLUDE_FROM_ALL )
add_subdirectory( ext/glm EXCLUDE_FROM_ALL )
add_subdirectory( ext/spdlog EXCLUDE_FROM_ALL )
add_subdirectory( ext/json EXCLUDE_FROM_ALL )
add_subdirectory( ext/gflags EXCLUDE_FROM_ALL )

add_library( tileson INTERFACE )
target_include_directories( tileson INTERFACE "${CMAKE_SOURCE_DIR}/ext/tileson/include" )

add_executable( diskiller src/main.cpp )
target_link_libraries( diskiller PUBLIC raylib glm spdlog nlohmann_json tileson gflags::gflags )
target_precompile_headers( diskiller PUBLIC <raylib.h> <glm/glm.hpp> <spdlog/spdlog.h> <nlohmann/json.hpp> <tileson.h> <gflags/gflags.h> )

install( TARGETS diskiller RUNTIME DESTINATION "." )
install( DIRECTORY "${CMAKE_SOURCE_DIR}/build/" DESTINATION "." )
include( CPack )
