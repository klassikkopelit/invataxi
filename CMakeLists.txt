cmake_minimum_required(VERSION 3.10)
project(invataxi C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(invataxi WIN32
    src/main.c
    src/game.c
    src/platform.c
    src/modplayer.c
    src/files.c
    src/pklite.c)

if(TARGET SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(invataxi PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(invataxi PRIVATE SDL2::SDL2)
else()
    target_include_directories(invataxi PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(invataxi PRIVATE ${SDL2_LIBRARIES})
endif()

if(MSVC)
    target_compile_definitions(invataxi PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(invataxi PRIVATE /W3 /utf-8)
else()
    target_compile_options(invataxi PRIVATE -Wall -Wextra -O2)
endif()

# Windows: copy SDL2.dll next to the exe when SDL2 was found as a shared lib
if(WIN32 AND TARGET SDL2::SDL2)
  get_target_property(_sdl_type SDL2::SDL2 TYPE)
  if(_sdl_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET invataxi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:invataxi>)
  endif()
endif()
