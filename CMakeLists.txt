cmake_minimum_required( VERSION 3.21 )
set( CMAKE_CXX_STANDARD 17 )
project( diskiller )

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

enable_testing()
add_test( NAME quit COMMAND diskiller -seed=349843924 -play_automation=../test/quit.txt "-check_log=Quit detected" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build" )
add_test( NAME play_simple COMMAND diskiller -seed=349843924 -play_automation=../test/play_simple.txt "-check_log=Finished session with best score 10" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build" )
add_test( NAME survival1 COMMAND diskiller -seed=349843924 -play_automation=../test/survival1.txt "-check_log=Finished session with best score 4" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build" )
