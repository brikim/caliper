#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "ui_manager.h"
#include <stdio.h>
#include <string>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char **);

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
                   int nShowCmd) {
  return main(__argc, __argv);
}
#endif

int main(int, char **) {
#ifdef _WIN32
  // Set working directory to the exe's folder so relative asset paths work
  char exePath[MAX_PATH];
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
    std::string exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
      exeDir = exeDir.substr(0, lastSlash);
      SetCurrentDirectoryA(exeDir.c_str());
    }
  }
#endif

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return 1;

  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  // Create window with graphics context
  GLFWwindow *window = glfwCreateWindow(
      1280, 1000, "Caliper - FFmpeg Benchmark", nullptr, nullptr);
  if (window == nullptr)
    return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

#ifdef _WIN32
  HWND hwnd = glfwGetWin32Window(window);
  HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
  if (hIcon) {
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
  }
#endif

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  // Load Arial (Standard Windows font)
  float fontSize = 16.0f;
  ImFont *font =
      io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", fontSize);
  if (font) {
    // Use Arial Bold for the medium/bold slot
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", fontSize);
  } else {
    io.Fonts->AddFontDefault();
  }

  // Modern dark theme
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  style.WindowRounding = 8.0f;
  style.ChildRounding = 8.0f;
  style.FrameRounding = 5.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.TabRounding = 5.0f;
  style.WindowBorderSize = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.WindowPadding = ImVec2(12, 12);
  style.FramePadding = ImVec2(8, 5);
  style.CellPadding = ImVec2(8, 5);
  style.ItemSpacing = ImVec2(8, 6);
  style.ItemInnerSpacing = ImVec2(6, 4);
  style.ScrollbarSize = 10.0f;
  style.GrabMinSize = 8.0f;
  style.IndentSpacing = 20.0f;

  ImVec4 *c = style.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
  c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
  c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
  c[ImGuiCol_Border] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
  c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.27f, 1.00f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
  c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.15f, 0.22f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.26f, 0.33f, 1.00f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.34f, 0.43f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
  c[ImGuiCol_CheckMark] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
  c[ImGuiCol_SliderGrab] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
  c[ImGuiCol_SliderGrabActive] = ImVec4(0.22f, 0.80f, 0.90f, 1.00f);
  c[ImGuiCol_Button] = ImVec4(0.14f, 0.40f, 0.52f, 1.00f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.52f, 0.67f, 1.00f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.32f, 0.44f, 1.00f);
  c[ImGuiCol_Header] = ImVec4(0.14f, 0.40f, 0.52f, 0.80f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.52f, 0.67f, 0.90f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
  c[ImGuiCol_Separator] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
  c[ImGuiCol_SeparatorHovered] = ImVec4(0.18f, 0.65f, 0.75f, 0.80f);
  c[ImGuiCol_SeparatorActive] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
  c[ImGuiCol_ResizeGrip] = ImVec4(0.18f, 0.65f, 0.75f, 0.30f);
  c[ImGuiCol_ResizeGripHovered] = ImVec4(0.18f, 0.75f, 0.85f, 0.60f);
  c[ImGuiCol_ResizeGripActive] = ImVec4(0.18f, 0.80f, 0.90f, 0.90f);
  c[ImGuiCol_Tab] = ImVec4(0.10f, 0.28f, 0.38f, 1.00f);
  c[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.52f, 0.67f, 1.00f);
  c[ImGuiCol_TabActive] = ImVec4(0.14f, 0.42f, 0.56f, 1.00f);
  c[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.18f, 0.25f, 1.00f);
  c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.10f, 0.30f, 0.40f, 1.00f);
  c[ImGuiCol_DockingPreview] = ImVec4(0.18f, 0.65f, 0.75f, 0.70f);
  c[ImGuiCol_DockingEmptyBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
  c[ImGuiCol_PlotLines] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
  c[ImGuiCol_PlotLinesHovered] = ImVec4(0.22f, 0.90f, 1.00f, 1.00f);
  c[ImGuiCol_PlotHistogram] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
  c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.22f, 0.80f, 0.90f, 1.00f);
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.09f, 0.20f, 0.28f, 1.00f);
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.25f, 0.33f, 1.00f);
  c[ImGuiCol_TableBorderLight] = ImVec4(0.13f, 0.18f, 0.24f, 1.00f);
  c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
  c[ImGuiCol_TextSelectedBg] = ImVec4(0.18f, 0.65f, 0.75f, 0.35f);
  c[ImGuiCol_DragDropTarget] = ImVec4(0.18f, 0.80f, 0.90f, 0.90f);
  c[ImGuiCol_NavHighlight] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
  c[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
  c[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.45f, 0.55f, 1.00f);

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 8.0f;
    c[ImGuiCol_WindowBg].w = 1.0f;
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  UIManager uiManager;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

    uiManager.Draw();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      GLFWwindow *backup_current_context = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(backup_current_context);
    }

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
