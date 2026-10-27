# gmcl_sdlpad: SDL3 gamepads for Garry's Mod (x86-64 client). SDL is linked statically, so the module is one DLL.
cmake_minimum_required(VERSION 3.24)
project(sdlpad C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_CXX_STANDARD 17)

# Only the joystick, gamepad and HIDAPI parts of SDL (PS4/PS5/Switch pads are read over HIDAPI).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO VIDEO GPU RENDER CAMERA HAPTIC POWER DIALOG TRAY)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

add_library(sdlpad SHARED src/sdlpad.cpp)
target_link_libraries(sdlpad PRIVATE SDL3::SDL3-static)
set_target_properties(sdlpad PROPERTIES OUTPUT_NAME "gmcl_sdlpad_win64" PREFIX "")

add_executable(sdlpad_harness src/harness.cpp)
