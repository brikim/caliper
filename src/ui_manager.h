#pragma once

#include "imgui.h"
#include "job_manager.h"

#include <memory>
#include <string>
#include <vector>

class UIManager
{
public:
   UIManager();
   ~UIManager();

   void Draw();

   void LoadProfiles();
   void SaveProfiles();

private:
   void DrawInputSection();
   void DrawProfileManager();
   void DrawJobQueue();

   ImGuiWindowFlags SetupImGuiStyle();
   std::string OpenFileDialog();

   // Core Managers & State
   JobManager m_jobManager;

   std::string m_referenceVideo;
   VideoMetadata m_referenceMeta;

   std::vector<EncodeProfile> m_profiles;

   int m_activeProfileIdx = 0; // The currently selected/active profile
   int m_maxConcurrentJobs = 1;
   int m_selectedJobId = -1;

   // Segment Settings
   int m_segmentCount = 4;
   float m_segmentDuration = 60.0f;
};