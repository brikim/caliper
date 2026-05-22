#pragma once

#include "imgui.h"
#include "job_manager.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
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

   // Preview generation
   void GeneratePreview();
   unsigned int LoadBMPTexture(const std::string& filepath, int& outWidth,
                               int& outHeight);

   // Core Managers & State
   JobManager m_jobManager;

   std::string m_referenceVideo;
   VideoMetadata m_referenceMeta;

   std::vector<EncodeProfile> m_profiles;

   int m_activeProfileIdx = 0; // The currently selected/active profile
   int m_maxConcurrentJobs = 1;
   int m_selectedJobId = -1;
   bool m_profilesDirty = false;

   std::thread m_previewThread;
   std::atomic<bool> m_previewLoading{false};
   std::atomic<bool> m_previewReady{false};
   std::atomic<bool> m_previewFailed{false};
   unsigned int m_previewTextureID = 0; // GLuint
   int m_previewWidth = 0;
   int m_previewHeight = 0;
   bool m_showPreviewPopup = false;

   float m_zoomLevel = 1.0f;
   float m_panX = 0.0f;
   float m_panY = 0.0f;
   std::string m_previewVideoPath;
};