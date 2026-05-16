cmake_minimum_required(VERSION 3.20)
project(Caliper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# ------------------------------------------------------------------------------
# Fetch GLFW
# ------------------------------------------------------------------------------
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ------------------------------------------------------------------------------
# Fetch ImGui
# ------------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4-docking # Use docking branch for modern UI features
)
FetchContent_MakeAvailable(imgui)

# ------------------------------------------------------------------------------
# Fetch ImPlot
# ------------------------------------------------------------------------------
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
)
FetchContent_MakeAvailable(implot)

# ------------------------------------------------------------------------------
# Fetch nlohmann_json
# ------------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ------------------------------------------------------------------------------
# Setup ImGui Sources
# ------------------------------------------------------------------------------
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

set(IMPLOT_DIR ${implot_SOURCE_DIR})
set(IMPLOT_SOURCES
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
    ${IMPLOT_DIR}/implot_demo.cpp
)

# ------------------------------------------------------------------------------
# Executable and Linking
# ------------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/ui_manager.cpp
    src/ffmpeg_runner.cpp
    src/job_manager.cpp
    src/window_manager.cpp
)

if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${IMGUI_SOURCES} ${IMPLOT_SOURCES})

# OpenGL dependency
find_package(OpenGL REQUIRED)

target_include_directories(Caliper PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMPLOT_DIR}
)

target_link_libraries(Caliper PRIVATE
    glfw
    OpenGL::GL
    nlohmann_json::nlohmann_json
)

if (MSVC)
    target_compile_definitions(Caliper PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_custom_command(TARGET Caliper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Caliper>/assets"
    COMMENT "Copying assets to build directory..."
)
