#pragma once

#include "imgui.h"
#include "job_manager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class UIManager
{
public:
   UIManager(std::reference_wrapper<JobManager> jobManager);
   ~UIManager();

   void Draw();

   void LoadProfiles();
   void SaveProfiles();

private:
   void LoadFonts();
   void SetupTheme();
   void DrawInputSection();
   void DrawProfileManager();
   void DrawJobQueue();

   ImGuiWindowFlags SetupImGuiStyle();
   std::string OpenFileDialog();

   // Core Managers & State
   std::reference_wrapper<JobManager> m_jobManager;

   std::string m_referenceVideo;
   VideoMetadata m_referenceMeta;

   std::vector<EncodeProfile> m_profiles;

   int m_activeProfileIdx = 0; // The currently selected/active profile
   int m_selectedJobId = -1;
   bool m_profilesDirty = false;

   std::optional<std::string> m_ffmpegVersion;
};