#include "job_manager.h"
#include "ui_manager.h"
#include "window_manager.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <string>

int main(int argc, char** argv);

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
   return main(__argc, __argv);
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
   // Set working directory to the exe's folder so relative asset paths work
   char exePath[MAX_PATH];
   if (GetModuleFileNameA(NULL, exePath, MAX_PATH))
   {
      std::string exeDir(exePath);
      size_t lastSlash = exeDir.find_last_of("\\/");
      if (lastSlash != std::string::npos)
      {
         exeDir = exeDir.substr(0, lastSlash);
         SetCurrentDirectoryA(exeDir.c_str());
      }
   }
#endif

   WindowManager window(1280, 1100, "Caliper - FFmpeg Benchmark");

   if (!window.Initialize())
   {
      return 1;
   }

   JobManager jobManager;
   UIManager uiManager(jobManager);

   // Main render loop
   while (!window.ShouldClose())
   {
      window.BeginFrame();

      jobManager.Update();
      uiManager.Draw();

      window.EndFrame();
   }

   return 0; // WindowManager destructor handles all cleanup natively
}