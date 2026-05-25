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
}

WindowManager::WindowManager(int width, int height, std::string_view title)
   : m_width(width), m_height(height), m_title(title)
{}

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

   glfwSwapBuffers(m_window);
}