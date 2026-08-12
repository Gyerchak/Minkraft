cmake_minimum_required(VERSION 3.16)
project(Minkraft CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(PNG REQUIRED libpng)

add_executable(minkraft
    src/main.cpp
    src/Chunk.cpp
    src/World.cpp
    src/Texture.cpp
    src/Player.cpp
    src/VkRenderer.cpp
    src/PngLoader.cpp
    src/Settings.cpp
)

target_include_directories(minkraft PRIVATE
    ${GLFW_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
)

target_compile_options(minkraft PRIVATE -O2 -Wall)

target_link_libraries(minkraft PRIVATE
    ${GLFW_LIBRARIES}
    ${PNG_LIBRARIES}
    vulkan
    m dl pthread
)

# Precompile the GLSL shaders to SPIR-V at build time (no runtime shaderc).
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found (install shaderc)")
endif()

set(SHADER_NAMES world.vert world.frag box.vert box.frag cross.vert cross.frag crack.vert crack.frag)
set(SHADER_OUTPUTS)
foreach(sh ${SHADER_NAMES})
    get_filename_component(stage ${sh} EXT)
    string(SUBSTRING ${stage} 1 -1 stage)
    set(spv ${CMAKE_CURRENT_BINARY_DIR}/shaders/${sh}.spv)
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLC} -fshader-stage=${stage} -O -o ${spv} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${sh}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${sh}
    )
    list(APPEND SHADER_OUTPUTS ${spv})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(minkraft shaders)

# Ship the texture pack, shaders and settings next to the binary.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/textures)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures
         DESTINATION ${CMAKE_BINARY_DIR})
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/settings.conf)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/settings.conf
         DESTINATION ${CMAKE_BINARY_DIR})
endif()
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders
     DESTINATION ${CMAKE_BINARY_DIR}
     PATTERN "*.vert" EXCLUDE
     PATTERN "*.frag" EXCLUDE)
