#pragma once

#include <string>
#include <string_view>

struct GLFWwindow;

class WindowManager
{
public:
   WindowManager(int width, int height, std::string_view title);
   ~WindowManager();

   // Prevent copying to ensure strict RAII ownership
   WindowManager(const WindowManager&) = delete;
   WindowManager& operator=(const WindowManager&) = delete;

   bool Initialize();
   bool ShouldClose() const;

   // Frame lifecycle
   void BeginFrame();
   void EndFrame();

   GLFWwindow* GetNativeWindow() const
   {
      return m_window;
   }

private:
   void SetupImGui();

   GLFWwindow* m_window = nullptr;
   int m_width;
   int m_height;
   std::string m_title;
};