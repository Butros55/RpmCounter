cmake_minimum_required(VERSION 3.24)

project(RpmCounterSimulator LANGUAGES C CXX)

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/include/lv_conf.h" RPMCOUNTER_LV_CONF)
set(LV_CONF_PATH "${RPMCOUNTER_LV_CONF}" CACHE STRING "LVGL configuration file" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)

set(RPMCOUNTER_LOCAL_LVGL "${CMAKE_SOURCE_DIR}/.pio/libdeps/esp32s3/lvgl")
if(EXISTS "${RPMCOUNTER_LOCAL_LVGL}/CMakeLists.txt")
    add_subdirectory("${RPMCOUNTER_LOCAL_LVGL}" "${CMAKE_BINARY_DIR}/lvgl")
else()
    FetchContent_Declare(
        lvgl
        URL https://github.com/lvgl/lvgl/archive/refs/tags/v8.4.0.zip
    )
    FetchContent_MakeAvailable(lvgl)
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        rpmcounter_sdl2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG SDL2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(rpmcounter_sdl2)
endif()

if(TARGET SDL2::SDL2-static)
    set(RPMCOUNTER_SDL_TARGET SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(RPMCOUNTER_SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2-static)
    set(RPMCOUNTER_SDL_TARGET SDL2-static)
else()
    message(FATAL_ERROR "Unable to locate an SDL2 target for the simulator build")
endif()

add_executable(rpmcounter_simulator
    simulator/main.cpp
    simulator/rpm_simulator.cpp
    simulator/sdl_lvgl_window.cpp
    simulator/simulator_app.cpp
    simulator/simulator_web_server.cpp
    simulator/virtual_led_bar.cpp
    src/ui/ui_s3_main.cpp
    telemetry/simhub_http_listener.cpp
    telemetry/simhub_udp_listener.cpp
    telemetry/telemetry_service.cpp
)

target_include_directories(rpmcounter_simulator
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/simulator"
)

target_compile_definitions(rpmcounter_simulator
    PRIVATE
        SDL_MAIN_HANDLED
)

target_link_libraries(rpmcounter_simulator
    PRIVATE
        lvgl::lvgl
        ${RPMCOUNTER_SDL_TARGET}
)

if(WIN32)
    target_link_libraries(rpmcounter_simulator PRIVATE ws2_32)
endif()

add_executable(rpmcounter_simulator_tests
    simulator/rpm_simulator.cpp
    simulator/simulator_app.cpp
    simulator/virtual_led_bar.cpp
    simulator/tests/simulator_app_tests.cpp
    telemetry/simhub_http_listener.cpp
    telemetry/simhub_udp_listener.cpp
    telemetry/telemetry_service.cpp
)

target_include_directories(rpmcounter_simulator_tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/simulator"
)

if(WIN32)
    target_link_libraries(rpmcounter_simulator_tests PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME rpmcounter_simulator_tests COMMAND rpmcounter_simulator_tests)
