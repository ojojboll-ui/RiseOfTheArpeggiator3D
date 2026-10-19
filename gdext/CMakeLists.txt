cmake_minimum_required(VERSION 3.17)
project(arpeggiator LANGUAGES CXX)

# The band, the rules and the autopilot from the 2D game, as a GDExtension.
# Built as template_debug: the editor and a project run from it load the debug
# library. It targets the 4.5 API, so any Godot from 4.5 on loads it.
set(GODOTCPP_TARGET "template_debug" CACHE STRING "" FORCE)
set(GODOTCPP_API_VERSION "4.5" CACHE STRING "" FORCE)
add_subdirectory(godot-cpp)

add_library(arpeggiator SHARED
    src/register_types.cpp
    src/band_stream.cpp
    src/arp_game.cpp
    src/game/World.cpp
)
target_link_libraries(arpeggiator PRIVATE godot-cpp)
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(OUT "${CMAKE_CURRENT_SOURCE_DIR}/../bin/")
set_target_properties(arpeggiator PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
    LIBRARY_OUTPUT_DIRECTORY "$<1:${OUT}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${OUT}>"
    PDB_OUTPUT_DIRECTORY "$<1:${OUT}>"
    OUTPUT_NAME "arpeggiator${GODOTCPP_SUFFIX}"
)
if(MSVC)
    # godot-cpp headers warn about float conversions at /W3; ours are clean
    target_compile_options(arpeggiator PRIVATE /utf-8 /fp:precise /W3 /wd4244 /wd4267 /D_CRT_SECURE_NO_WARNINGS)
endif()
