cmake_minimum_required(VERSION 3.30)
project(MyProject)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

# Include SDL3
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG "origin/main"
)
FetchContent_MakeAvailable(SDL3)

# Include ImGui
FetchContent_Declare(imgui_fetch
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
)
FetchContent_MakeAvailable(imgui_fetch)

add_library(imgui STATIC 
    ${imgui_fetch_SOURCE_DIR}/imgui.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_widgets.cpp

    ${imgui_fetch_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_fetch_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

target_include_directories(imgui PUBLIC ${imgui_fetch_SOURCE_DIR})
target_link_libraries(imgui PRIVATE SDL3::SDL3)

# Include glm
FetchContent_Declare(glm
	GIT_REPOSITORY	https://github.com/g-truc/glm.git
)
FetchContent_MakeAvailable(glm)

# Include STB_Image (and it's friends)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
)
FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# Include asio
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include/)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
)
FetchContent_MakeAvailable(yaml-cpp)

file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_executable(Application ${SourceFiles})
target_link_libraries(Application PRIVATE SDL3::SDL3 imgui stb glm::glm asio yaml-cpp::yaml-cpp)

add_custom_command(TARGET Application POST_BUILD
    # Copy runtime deps
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Application>
        $<TARGET_FILE_DIR:Application>
        COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime dependencies to output folder "
)

add_custom_command(TARGET Application POST_BUILD
    # Copy resources 
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/"
        "$<TARGET_FILE_DIR:Application>/resources/"
    COMMENT "Copying resources to output folder"
)