#include "window_manager.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <filesystem>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace
{
   void glfw_error_callback(int error, const char* description)
   {
      fprintf(stderr, "GLFW Error %d: %s\n", error, description);
   }

   std::filesystem::path get_executable_directory()
   {
#if defined(_WIN32)
      wchar_t path[MAX_PATH];
      GetModuleFileNameW(NULL, path, MAX_PATH);
      return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
      char path[PATH_MAX];
      ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
      return std::filesystem::path(std::string(path, count > 0 ? count : 0)).parent_path();
#elif defined(__APPLE__)
      char path[PATH_MAX];
      uint32_t size = sizeof(path);
      if (_NSGetExecutablePath(path, &size) == 0)
         return std::filesystem::path(path).parent_path();
      return "";
#endif
   }
}

WindowManager::WindowManager(int width, int height, std::string_view title)
   : m_width(width), m_height(height), m_title(title)
{
}

WindowManager::~WindowManager()
{
   // RAII Cleanup
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();

   if (m_window)
   {
      glfwDestroyWindow(m_window);
   }
   glfwTerminate();
}

bool WindowManager::Initialize()
{
   glfwSetErrorCallback(glfw_error_callback);
   if (!glfwInit())
      return false;

   const char* glsl_version = "#version 130";
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

   m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
   if (!m_window)
      return false;

   glfwMakeContextCurrent(m_window);
   glfwSwapInterval(1); // Enable vsync

#ifdef _WIN32
   HWND hwnd = glfwGetWin32Window(m_window);
   HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
   if (hIcon)
   {
      SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
      SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
   }
#endif

   SetupImGui();
   LoadFonts();
   SetupTheme();

   ImGui_ImplGlfw_InitForOpenGL(m_window, true);
   ImGui_ImplOpenGL3_Init(glsl_version);

   return true;
}

void WindowManager::SetupImGui()
{
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();

   ImGuiIO& io = ImGui::GetIO();
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
   io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
   io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
}

void WindowManager::LoadFonts()
{
   ImGuiIO& io = ImGui::GetIO();
   std::filesystem::path exeDir = get_executable_directory();

   std::string regularFontPath = (exeDir / "assets" / "fonts" / "Roboto-Regular.ttf").string();
   std::string mediumFontPath = (exeDir / "assets" / "fonts" / "Roboto-Medium.ttf").string();

   float fontSize = 16.0f;
   ImFont* font = io.Fonts->AddFontFromFileTTF(regularFontPath.c_str(), fontSize);
   if (font)
   {
      io.Fonts->AddFontFromFileTTF(mediumFontPath.c_str(), fontSize);
   }
   else
   {
      io.Fonts->AddFontDefault();
   }
}

void WindowManager::SetupTheme()
{
   ImGui::StyleColorsDark();
   ImGuiStyle& style = ImGui::GetStyle();

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

   ImVec4* colors = style.Colors;
   colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
   colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
   colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
   colors[ImGuiCol_Border] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
   colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
   colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
   colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.27f, 1.00f);
   colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
   colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
   colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.15f, 0.22f, 1.00f);
   colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
   colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
   colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
   colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.26f, 0.33f, 1.00f);
   colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.34f, 0.43f, 1.00f);
   colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
   colors[ImGuiCol_CheckMark] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
   colors[ImGuiCol_SliderGrab] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
   colors[ImGuiCol_SliderGrabActive] = ImVec4(0.22f, 0.80f, 0.90f, 1.00f);
   colors[ImGuiCol_Button] = ImVec4(0.14f, 0.40f, 0.52f, 1.00f);
   colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.52f, 0.67f, 1.00f);
   colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.32f, 0.44f, 1.00f);
   colors[ImGuiCol_Header] = ImVec4(0.14f, 0.40f, 0.52f, 0.80f);
   colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.52f, 0.67f, 0.90f);
   colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
   colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
   colors[ImGuiCol_SeparatorHovered] = ImVec4(0.18f, 0.65f, 0.75f, 0.80f);
   colors[ImGuiCol_SeparatorActive] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
   colors[ImGuiCol_ResizeGrip] = ImVec4(0.18f, 0.65f, 0.75f, 0.30f);
   colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.18f, 0.75f, 0.85f, 0.60f);
   colors[ImGuiCol_ResizeGripActive] = ImVec4(0.18f, 0.80f, 0.90f, 0.90f);
   colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.28f, 0.38f, 1.00f);
   colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.52f, 0.67f, 1.00f);
   colors[ImGuiCol_TabActive] = ImVec4(0.14f, 0.42f, 0.56f, 1.00f);
   colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.18f, 0.25f, 1.00f);
   colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.10f, 0.30f, 0.40f, 1.00f);
   colors[ImGuiCol_DockingPreview] = ImVec4(0.18f, 0.65f, 0.75f, 0.70f);
   colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
   colors[ImGuiCol_PlotLines] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
   colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.22f, 0.90f, 1.00f, 1.00f);
   colors[ImGuiCol_PlotHistogram] = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
   colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.22f, 0.80f, 0.90f, 1.00f);
   colors[ImGuiCol_TableHeaderBg] = ImVec4(0.09f, 0.20f, 0.28f, 1.00f);
   colors[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.25f, 0.33f, 1.00f);
   colors[ImGuiCol_TableBorderLight] = ImVec4(0.13f, 0.18f, 0.24f, 1.00f);
   colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
   colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
   colors[ImGuiCol_TextSelectedBg] = ImVec4(0.18f, 0.65f, 0.75f, 0.35f);
   colors[ImGuiCol_DragDropTarget] = ImVec4(0.18f, 0.80f, 0.90f, 0.90f);
   colors[ImGuiCol_NavHighlight] = ImVec4(0.18f, 0.75f, 0.85f, 1.00f);
   colors[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
   colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.45f, 0.55f, 1.00f);

   ImGuiIO& io = ImGui::GetIO();
   if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
   {
      style.WindowRounding = 8.0f;
      colors[ImGuiCol_WindowBg].w = 1.0f;
   }
}

bool WindowManager::ShouldClose() const
{
   return glfwWindowShouldClose(m_window);
}

void WindowManager::BeginFrame()
{
   glfwPollEvents();
   ImGui_ImplOpenGL3_NewFrame();
   ImGui_ImplGlfw_NewFrame();
   ImGui::NewFrame();
}

void WindowManager::EndFrame()
{
   ImGui::Render();
   int display_w, display_h;
   glfwGetFramebufferSize(m_window, &display_w, &display_h);
   glViewport(0, 0, display_w, display_h);
   glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

   ImGuiIO& io = ImGui::GetIO();
   if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
   {
      GLFWwindow* backup_current_context = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(backup_current_context);
   }

   glfwSwapBuffers(m_window);
}