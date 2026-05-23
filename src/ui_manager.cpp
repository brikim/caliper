#include "ui_manager.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <string>
#include <vector>

using json = nlohmann::json;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// Leave a space here so formatting does not put it below commdlg.h
#include <commdlg.h>
#endif

namespace
{
   const auto UI_EXTRACT_COLOR = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
   const auto UI_IN_PROG_COLOR = ImVec4(0.1f, 0.9f, 0.9f, 1.0f);
   const auto UI_QUEUED_COLOR = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
   const auto UI_SUCESS_COLOR = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
   const auto UI_CANCEL_COLOR = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
   const auto UI_SELECTED_COLOR = ImVec4(0.2f, 0.2f, 0.5f, 0.3f);
   const auto UI_ROW_RUNNING_COLOR = ImVec4(0.0f, 0.4f, 0.4f, 0.3f);
   const auto UI_BUTTON_CANCEL_COLOR = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
   const auto UI_BUTTON_CANCEL_HOVER_COLOR = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
   const auto UI_BUTTON_CANCEL_ACTIVE_COLOR = ImVec4(0.7f, 0.1f, 0.1f, 1.0f);

   const auto UI_BUTTON_SIZE = ImVec2(120, 30);
};

static void DrawSizeColumn(float sizeMB, bool running = false)
{
   if (sizeMB > 0.0f)
   {
      if (running)
      {
         if (sizeMB >= 1024.0f)
            ImGui::TextDisabled("Est: %.2f GB", sizeMB / 1024.0f);
         else
            ImGui::TextDisabled("Est: %.1f MB", sizeMB);
      }
      else
      {
         if (sizeMB >= 1024.0f)
            ImGui::Text("%.2f GB", sizeMB / 1024.0f);
         else
            ImGui::Text("%.1f MB", sizeMB);
      }
   }
   else
   {
      ImGui::Text("-");
   }
}

UIManager::UIManager()
{
   LoadProfiles();
   if (m_profiles.empty())
   {
      m_profiles.push_back({"x264 Medium", "libx264", "medium", DEFAULT_CRF, true,
                            96, "", "8-bit", true});
      m_profiles.push_back({"x264 Slow", "libx264", "slow", DEFAULT_CRF, true, 96,
                            "", "8-bit", false});
      m_profiles.push_back({"x265 Medium", "libx265", "medium", DEFAULT_CRF, true,
                            96, "", "8-bit", false});
   }
   if (!m_profiles.empty())
   {
      m_activeProfileIdx = 0;
      for (int i = 0; i < m_profiles.size(); ++i)
      {
         if (m_profiles[i].isDefault)
         {
            m_activeProfileIdx = i;
            break;
         }
      }
   }
}

UIManager::~UIManager()
{}

void UIManager::LoadProfiles()
{
   std::ifstream file("profiles.json");
   if (file.is_open())
   {
      try
      {
         json j;
         file >> j;

         if (j.is_array())
         {
            for (const auto& item : j)
            {
               EncodeProfile p;
               p.name = item.value("name", "Custom Profile");
               p.codec = item.value("codec", "libx264");
               p.preset = item.value("preset", "medium");
               p.startCrf = item.value("startCrf", DEFAULT_CRF);
               p.autoCrf = item.value("autoCrf", true);
               p.targetVmaf = item.value("targetVmaf", 96);
               p.extraArgs = item.value("extraArgs", "");
               p.bitDepth = item.value("bitDepth", "8-bit");
               p.isDefault = item.value("isDefault", false);
               p.svtTune = item.value("svtTune", 0);
               p.segmentCount = item.value("segmentCount", 4);
               p.segmentDuration = item.value("segmentDuration", 60.0f);
               m_profiles.push_back(p);
            }
         }
         else if (j.is_object())
         {
            m_maxConcurrentJobs = j.value("maxConcurrentJobs", 1);

            if (j.contains("profiles") && j["profiles"].is_array())
            {
               for (const auto& item : j["profiles"])
               {
                  EncodeProfile p;
                  p.name = item.value("name", "Custom Profile");
                  p.codec = item.value("codec", "libx264");
                  p.preset = item.value("preset", "medium");
                  p.startCrf = item.value("startCrf", DEFAULT_CRF);
                  p.autoCrf = item.value("autoCrf", true);
                  p.targetVmaf = item.value("targetVmaf", 96);
                  p.extraArgs = item.value("extraArgs", "");
                  p.bitDepth = item.value("bitDepth", "8-bit");
                  p.isDefault = item.value("isDefault", false);
                  p.svtTune = item.value("svtTune", 0);
                  p.segmentCount = item.value("segmentCount", 4);
                  p.segmentDuration = item.value("segmentDuration", 60.0f);
                  m_profiles.push_back(p);
               }
            }
         }
      }
      catch (...)
      {
         std::cerr << "Failed to parse profiles.json\n";
      }
   }
}

void UIManager::SaveProfiles()
{
   json jProfiles = json::array();
   for (const auto& p : m_profiles)
   {
      jProfiles.push_back({{"name", p.name},
                           {"codec", p.codec},
                           {"preset", p.preset},
                           {"startCrf", p.startCrf},
                           {"autoCrf", p.autoCrf},
                           {"targetVmaf", p.targetVmaf},
                           {"extraArgs", p.extraArgs},
                           {"bitDepth", p.bitDepth},
                           {"isDefault", p.isDefault},
                           {"svtTune", p.svtTune},
                           {"segmentCount", p.segmentCount},
                           {"segmentDuration", p.segmentDuration}});
   }

   json jRoot;
   jRoot["maxConcurrentJobs"] = m_maxConcurrentJobs;
   jRoot["profiles"] = jProfiles;
   std::ofstream file("profiles.json");
   if (file.is_open())
   {
      file << jRoot.dump(4);
      file.flush();
      file.close();
   }

   m_profilesDirty = false;
}

std::string UIManager::OpenFileDialog()
{
   std::string result = "";
#ifdef _WIN32
   char filename[MAX_PATH] = {0};
   OPENFILENAMEA ofn;
   ZeroMemory(&ofn, sizeof(ofn));
   ofn.lStructSize = sizeof(ofn);
   ofn.hwndOwner = NULL;
   ofn.lpstrFilter = "Video Files\0*.mp4;*.mkv;*.avi;*.mov\0All Files\0*.*\0";
   ofn.lpstrFile = filename;
   ofn.nMaxFile = MAX_PATH;
   ofn.Flags =
      OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
   ofn.lpstrDefExt = "mp4";

   if (GetOpenFileNameA(&ofn))
   {
      result = std::string(filename);
   }
#endif
   return result;
}

void UIManager::Draw()
{
   // Drive the job lifecycle manager processing loops
   m_jobManager.SetMaxConcurrentJobs(m_maxConcurrentJobs);
   m_jobManager.Update();

   ImGui::Begin("CaliperMain", nullptr, SetupImGuiStyle());
   ImGui::PopStyleVar(3);

   auto sectionSepFunc = []() {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
   };

   DrawInputSection();
   sectionSepFunc();
   DrawProfileManager();
   sectionSepFunc();
   DrawJobQueue();

   if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) &&
       !ImGui::IsAnyItemHovered())
   {
      m_selectedJobId = -1;
   }

   ImGui::End();
}

ImGuiWindowFlags UIManager::SetupImGuiStyle()
{
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   ImGui::SetNextWindowPos(viewport->WorkPos);
   ImGui::SetNextWindowSize(viewport->WorkSize);
   ImGui::SetNextWindowViewport(viewport->ID);

   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));

   return ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
}

void UIManager::DrawInputSection()
{
   ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "1. Source Video");

   if (ImGui::Button("Browse...", UI_BUTTON_SIZE))
   {
      std::string selected = OpenFileDialog();
      if (!selected.empty())
      {
         m_referenceVideo = selected;
         m_referenceMeta = FFmpegRunner::GetMetadata(m_referenceVideo);
      }
   }
   ImGui::SameLine();
   ImGui::InputText("##ReferenceVideo", m_referenceVideo.data(),
                    m_referenceVideo.capacity(), ImGuiInputTextFlags_ReadOnly);

   if (m_referenceMeta.valid)
   {
      int hours = static_cast<int>(m_referenceMeta.duration) / 3600;
      int minutes = (static_cast<int>(m_referenceMeta.duration) % 3600) / 60;
      int seconds = static_cast<int>(m_referenceMeta.duration) % 60;

      ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                         "Codec: %s | %dx%d | %.2f fps | %d-bit | %02d:%02d:%02d",
                         m_referenceMeta.codec.c_str(), m_referenceMeta.width,
                         m_referenceMeta.height, m_referenceMeta.framerate,
                         m_referenceMeta.bit_depth, hours, minutes, seconds);
   }
   else if (!m_referenceVideo.empty())
   {
      ImGui::TextColored(
          ImVec4(0.8f, 0.4f, 0.4f, 1.0f),
          "Failed to read video metadata. Ensure ffprobe is in PATH.");
   }
   else
   {
      ImGui::NewLine();
   }
}

void UIManager::DrawProfileManager()
{
   ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "2. Encoding Profiles");

   if (ImGui::Button("Add Profile", UI_BUTTON_SIZE))
   {
      m_profiles.push_back({"New Profile", "libx264", "medium", DEFAULT_CRF, true,
                            96, "", "8-bit", false});
      m_activeProfileIdx = m_profiles.size() - 1;
      m_profilesDirty = true;
   }
   ImGui::SameLine();

   bool currentProfileDirty = m_profilesDirty;
   if (!currentProfileDirty)
      ImGui::BeginDisabled();
   if (ImGui::Button("Save Profiles", UI_BUTTON_SIZE))
   {
      SaveProfiles();
   }
   if (!currentProfileDirty)
      ImGui::EndDisabled();

   if (m_activeProfileIdx >= 0 && m_activeProfileIdx < (int)m_profiles.size())
   {
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(10, 0));
      ImGui::SameLine();

      bool currentDefaultProfile = m_profiles[m_activeProfileIdx].isDefault;
      if (currentDefaultProfile)
         ImGui::BeginDisabled();
      if (ImGui::Button("Set as Default", UI_BUTTON_SIZE))
      {
         for (auto& other : m_profiles)
            other.isDefault = false;
         m_profiles[m_activeProfileIdx].isDefault = true;
         SaveProfiles();
      }
      if (currentDefaultProfile)
         ImGui::EndDisabled();

      ImGui::SameLine();

      if (ImGui::Button("Remove Profile", UI_BUTTON_SIZE))
      {
         ImGui::OpenPopup("Confirm Remove Profile");
      }

      if (ImGui::BeginPopupModal("Confirm Remove Profile", NULL, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("Are you sure you want to remove this profile?");
         ImGui::Separator();

         // Calculate the total width of both buttons + the spacing between them
         float spacing = ImGui::GetStyle().ItemSpacing.x;
         float totalButtonWidth = (UI_BUTTON_SIZE.x * 2) + spacing;

         // Calculate the offset to center the buttons and set the cursor
         float cursorX = (ImGui::GetWindowSize().x - totalButtonWidth) * 0.5f;
         ImGui::SetCursorPosX(cursorX);

         // Draw the buttons
         if (ImGui::Button("Yes", UI_BUTTON_SIZE))
         {
            m_profiles.erase(m_profiles.begin() + m_activeProfileIdx);
            if (m_activeProfileIdx >= (int)m_profiles.size())
            {
               m_activeProfileIdx = m_profiles.size() > 0 ? (int)m_profiles.size() - 1 : 0;
            }
            SaveProfiles();
            ImGui::CloseCurrentPopup();
         }
         ImGui::SetItemDefaultFocus();

         ImGui::SameLine();

         if (ImGui::Button("No", UI_BUTTON_SIZE))
         {
            ImGui::CloseCurrentPopup();
         }

         ImGui::EndPopup();
      }
   }

   ImGui::BeginChild("ProfilesList", ImVec2(250, 300), true);
   for (int i = 0; i < m_profiles.size(); ++i)
   {
      ImGui::PushID(i);

      // 1. Draw the radio button with just the profile name
      bool clicked = ImGui::RadioButton(m_profiles[i].name.c_str(), m_activeProfileIdx == i);

      // 2. If it's the default profile, append the green text on the same line
      if (m_profiles[i].isDefault)
      {
         // Remove default spacing between the radio button and the text
         ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(Default)");

         // Optional: Make the "(Default)" text clickable so it acts like the radio button
         if (ImGui::IsItemClicked())
         {
            clicked = true;
         }
      }

      // 3. Handle the state change
      if (clicked)
      {
         if (m_activeProfileIdx != i)
         {
            m_activeProfileIdx = i;
         }
      }

      ImGui::PopID();
   }
   ImGui::EndChild();

   ImGui::SameLine();

   ImGui::BeginChild("ProfileEditor", ImVec2(0, 300), true);
   if (m_activeProfileIdx >= 0 && m_activeProfileIdx < m_profiles.size())
   {
      auto& p = m_profiles[m_activeProfileIdx];

      std::array<char, 64> nameBuf;
      snprintf(nameBuf.data(), nameBuf.size(), "%s", p.name.c_str());
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Profile Name");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputText("##Name", nameBuf.data(), nameBuf.size()))
      {
         p.name = nameBuf.data();
         m_profilesDirty = true;
      }

      if (ImGui::BeginTable("ProfileFields", 4))
      {
         ImGui::TableNextColumn();
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Target VMAF");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderInt("##Target VMAF", &p.targetVmaf, 0, 100))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Starting CRF");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderInt("##Starting CRF", &p.startCrf, 0,
                              (p.codec == "libsvtav1" ? 63 : 51)))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Segment Count");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderInt("##SegCount", &p.segmentCount, 1, 50))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                            "Segment Duration (s)");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderFloat("##SegDuration", &p.segmentDuration, 1.0f, 300.0f,
                                "%.0fs"))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Codec");
         static const char* codecs[] = {"libx264", "libx265", "hevc_nvenc",
                                        "h264_nvenc", "libsvtav1"};
         static const char* codecNames[] = {"H.264", "H.265", "H.265 (NVENC)",
                                            "H.264 (NVENC)", "AV1 (10-bit)"};
         int codecIdx = 0;
         for (int i = 0; i < 5; ++i)
            if (p.codec == codecs[i])
               codecIdx = i;
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::Combo("##Codec", &codecIdx, codecNames, 5))
         {
            p.codec = codecs[codecIdx];
            if (p.codec == "libsvtav1")
            {
               p.preset = "8";
            }
            else if (p.preset.length() <= 2)
            {
               p.preset = "medium";
            }
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Preset");
         if (p.codec == "libsvtav1")
         {
            static const char* svtPresets[] = {"0",  "1",  "2",  "3", "4",
                                               "5",  "6",  "7",  "8", "9",
                                               "10", "11", "12", "13"};
            int presetIdx = 8;
            for (int i = 0; i < 14; ++i)
               if (p.preset == svtPresets[i])
                  presetIdx = i;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Preset", &presetIdx, svtPresets, 14))
            {
               p.preset = svtPresets[presetIdx];
               m_profilesDirty = true;
            }
         }
         else
         {
            static const char* presets[] = {"ultrafast", "superfast", "veryfast",
                                            "faster",    "fast",      "medium",
                                            "slow",      "slower",    "veryslow"};
            int presetIdx = 5;
            for (int i = 0; i < 9; ++i)
               if (p.preset == presets[i])
                  presetIdx = i;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Preset", &presetIdx, presets, 9))
            {
               p.preset = presets[presetIdx];
               m_profilesDirty = true;
            }
         }

         ImGui::TableNextColumn();
         if (p.codec == "libsvtav1")
         {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Encoder Tune");
            const char* tunes[] = {"VQ (0)", "PSNR (1)", "SSIM (2)"};
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Tune", &p.svtTune, tunes, 3))
            {
               m_profilesDirty = true;
            }
         }
         else
         {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Bit Depth");
            const char* depths[] = {"8-bit", "10-bit"};
            int depthIdx = (p.bitDepth == "10-bit") ? 1 : 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Bit Depth", &depthIdx, depths, 2))
            {
               p.bitDepth = depths[depthIdx];
               m_profilesDirty = true;
            }
         }

         ImGui::EndTable();
      }

      std::array<char, 256> extraBuf;
      snprintf(extraBuf.data(), extraBuf.size(), "%s", p.extraArgs.c_str());

      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                         "Extra FFmpeg Arguments");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputText("##Extra Args", extraBuf.data(), extraBuf.size()))
      {
         p.extraArgs = extraBuf.data();
         m_profilesDirty = true;
      }

      ImGui::TextDisabled(
          "Use standard FFmpeg flags (e.g. -x265-params key=val:key2=val2)");

   }
   else
   {
      ImGui::Spacing();
      ImGui::TextColored(
          ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
          "Please select a profile from the left to begin editing.");
      ImGui::TextDisabled("(If the list is empty, click 'Add Profile' above)");
   }
   ImGui::EndChild();
}

void UIManager::DrawJobQueue()
{
   ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "3. Execution");

   const auto& jobs = m_jobManager.GetJobs();

   bool anyRunning = false;
   for (const auto& job : jobs)
      if (!job->isComplete)
         anyRunning = true;

   bool canEnqueue = !m_referenceVideo.empty() && m_referenceMeta.valid;
   if (!canEnqueue)
      ImGui::BeginDisabled();
   if (ImGui::Button("Enqueue Job", UI_BUTTON_SIZE))
   {
      if (m_activeProfileIdx >= 0 && m_activeProfileIdx < m_profiles.size())
      {
         const auto& p = m_profiles[m_activeProfileIdx];
         m_jobManager.AddJob(m_referenceVideo, m_referenceMeta, p, p.segmentCount,
                             p.segmentDuration);
      }
   }
   if (!canEnqueue)
      ImGui::EndDisabled();

   ImGui::SameLine();
   bool hasCompleted = false;
   for (const auto& job : jobs)
      if (job->isComplete)
         hasCompleted = true;

   if (hasCompleted)
   {
      if (ImGui::Button("Clear Results", ImVec2(110, 30)))
      {
         m_jobManager.ClearCompletedJobs();
      }
   }

   bool selectedIsValid = false;
   bool selectedIsProcessing = false;
   if (m_selectedJobId != -1)
   {
      for (const auto& job : jobs)
      {
         if (job->jobId == m_selectedJobId)
         {
            selectedIsValid = true;
            if (!job->isComplete && job->state != JobState::INIT)
            {
               selectedIsProcessing = true;
               break;
            }
         }
      }
   }

   auto dummyButtonSize = ImVec2(10.0f, 0.0f);
   float rightAlignWidth =
      (UI_BUTTON_SIZE.x * 3) + (ImGui::GetStyle().ItemSpacing.x * 2) + dummyButtonSize.x * 2.0f;
   float alignX = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() -
      rightAlignWidth;
   if (alignX > ImGui::GetCursorPosX())
   {
      ImGui::SameLine(alignX);
   }
   else
   {
      ImGui::SameLine();
   }

   if (!selectedIsValid || selectedIsProcessing)
      ImGui::BeginDisabled();
   if (ImGui::Button("Remove Job", UI_BUTTON_SIZE))
   {
      m_jobManager.RemoveJob(m_selectedJobId);
      m_selectedJobId = -1;
   }
   if (!selectedIsValid || selectedIsProcessing)
      ImGui::EndDisabled();

   ImGui::SameLine();

   if (!selectedIsProcessing)
      ImGui::BeginDisabled();
   ImGui::PushStyleColor(ImGuiCol_Button, UI_BUTTON_CANCEL_COLOR);
   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI_BUTTON_CANCEL_HOVER_COLOR);
   ImGui::PushStyleColor(ImGuiCol_ButtonActive, UI_BUTTON_CANCEL_ACTIVE_COLOR);
   if (ImGui::Button("Cancel Job", UI_BUTTON_SIZE))
   {
      ImGui::OpenPopup("Confirm Cancel Job");
   }
   ImGui::PopStyleColor(3);
   if (!selectedIsProcessing)
      ImGui::EndDisabled();

   ImGui::SameLine();
   ImGui::Dummy(dummyButtonSize);
   ImGui::SameLine();

   if (!anyRunning)
      ImGui::BeginDisabled();
   ImGui::PushStyleColor(ImGuiCol_Button, UI_BUTTON_CANCEL_COLOR);
   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI_BUTTON_CANCEL_HOVER_COLOR);
   ImGui::PushStyleColor(ImGuiCol_ButtonActive, UI_BUTTON_CANCEL_ACTIVE_COLOR);
   if (ImGui::Button("Stop All Jobs", UI_BUTTON_SIZE))
   {
      ImGui::OpenPopup("Confirm Stop All Jobs");
   }
   ImGui::PopStyleColor(3);
   if (!anyRunning)
      ImGui::EndDisabled();

   // Modals
   if (ImGui::BeginPopupModal("Confirm Stop All Jobs", NULL, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::Text("Are you sure you want to stop all active jobs?");
      ImGui::Separator();

      // 1. Calculate the total width of both buttons + the spacing between them
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      float totalButtonWidth = (UI_BUTTON_SIZE.x * 2.0f) + spacing;

      // 2. Calculate the offset to center the buttons and set the cursor
      float cursorX = (ImGui::GetWindowSize().x - totalButtonWidth) * 0.5f;
      ImGui::SetCursorPosX(cursorX);

      // 3. Draw the buttons
      if (ImGui::Button("Yes", UI_BUTTON_SIZE))
      {
         m_jobManager.StopAllJobs();
         ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();

      ImGui::SameLine();

      if (ImGui::Button("No", UI_BUTTON_SIZE))
      {
         ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
   }

   if (ImGui::BeginPopupModal("Confirm Cancel Job", NULL, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::Text("Are you sure you want to cancel the selected job?");
      ImGui::Separator();

      // Calculate the total width of both buttons + the spacing between them
      float buttonWidth = 120.0f;
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      float totalButtonWidth = (buttonWidth * 2) + spacing;

      // Calculate the offset to center the buttons and set the cursor
      float cursorX = (ImGui::GetWindowSize().x - totalButtonWidth) * 0.5f;
      ImGui::SetCursorPosX(cursorX);

      // Draw the buttons
      if (ImGui::Button("Yes", UI_BUTTON_SIZE))
      {
         m_jobManager.CancelJob(m_selectedJobId);
         ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();

      ImGui::SameLine();

      if (ImGui::Button("No", UI_BUTTON_SIZE))
      {
         ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
   }

   ImGui::Spacing();
   ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Job Queue");

   if (ImGui::BeginTable("JobTable", 8,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                         ImGuiTableFlags_SizingStretchProp,
                         ImVec2(0, 150)))
   {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30);
      ImGui::TableSetupColumn("Source File", ImGuiTableColumnFlags_WidthStretch,
                              2.0f);
      ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch,
                              1.0f);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Current CRF", ImGuiTableColumnFlags_WidthStretch,
                              0.5f);
      ImGui::TableSetupColumn("Target VMAF", ImGuiTableColumnFlags_WidthStretch,
                              0.5f);
      ImGui::TableSetupColumn("Recommended CRF",
                              ImGuiTableColumnFlags_WidthStretch, 0.8f);
      ImGui::TableSetupColumn("Est. Size", ImGuiTableColumnFlags_WidthStretch,
                              0.5f);
      ImGui::TableHeadersRow();

      std::vector<int> uniqueIds;
      for (const auto& j : jobs)
      {
         if (std::find(uniqueIds.begin(), uniqueIds.end(), j->jobId) ==
             uniqueIds.end())
         {
            uniqueIds.push_back(j->jobId);
         }
      }

      for (int id : uniqueIds)
      {
         std::shared_ptr<BenchmarkJob> latestIter = nullptr;
         std::shared_ptr<BenchmarkJob> recIter = nullptr;
         bool rowRunning = false;
         for (const auto& j : jobs)
         {
            if (j->jobId == id)
            {
               if (!latestIter || j->iteration > latestIter->iteration)
                  latestIter = j;
               if (j->isRecommended)
                  recIter = j;
               if (!j->isComplete && j->state != JobState::INIT)
                  rowRunning = true;
            }
         }
         if (!latestIter)
            continue;

         ImGui::TableNextRow();
         bool isSelected = (id == m_selectedJobId);
         if (isSelected)
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(UI_SELECTED_COLOR));
         else if (rowRunning)
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(UI_ROW_RUNNING_COLOR));

         ImGui::TableNextColumn();
         char idLabel[32];
         snprintf(idLabel, sizeof(idLabel), "%d##%d", id, id);
         if (ImGui::Selectable(idLabel, isSelected,
                               ImGuiSelectableFlags_SpanAllColumns))
         {
            m_selectedJobId = id;
         }

         ImGui::TableNextColumn();
         std::string fileName =
            std::filesystem::path(latestIter->sourceFile).filename().string();
         ImGui::Text("%s", fileName.c_str());

         ImGui::TableNextColumn();
         ImGui::Text("%s", latestIter->profile.name.c_str());

         ImGui::TableNextColumn();
         if (rowRunning)
         {
            if (latestIter->state == JobState::EXTRACTING_SEGMENT)
               ImGui::TextColored(UI_EXTRACT_COLOR,
                                  "Extracting Seg %d/%d",
                                  latestIter->currentSegmentIdx + 1,
                                  (int)latestIter->segmentStartTimes.size());
            else
               ImGui::TextColored(UI_IN_PROG_COLOR,
                                  "Running (Iter %d)", latestIter->iteration + 1);
         }
         else if (latestIter->isCanceled)
            ImGui::TextColored(UI_CANCEL_COLOR, "Canceled");
         else if (latestIter->isComplete)
            ImGui::TextColored(UI_SUCESS_COLOR, "Complete");
         else
            ImGui::TextColored(UI_QUEUED_COLOR, "Queued");

         ImGui::TableNextColumn();
         ImGui::Text("%d", latestIter->currentCrf);

         ImGui::TableNextColumn();
         ImGui::Text("%d", latestIter->profile.targetVmaf);

         ImGui::TableNextColumn();
         if (recIter)
            ImGui::TextColored(UI_SUCESS_COLOR, "CRF %d",
                               recIter->currentCrf);
         else
            ImGui::Text("-");

         ImGui::TableNextColumn();
         DrawSizeColumn(recIter ? recIter->estimatedFullSize : 0.0f, false);
      }
      ImGui::EndTable();
   }

   ImGui::Spacing();
   ImGui::Separator();
   ImGui::Spacing();
   ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Iteration Details");

   if (jobs.empty())
   {
      ImGui::TextDisabled("No active jobs.");
      return;
   }

   auto TimeToSeconds = [](const std::string& t) -> float {
      if (t.empty())
         return 0.0f;
      int h = 0, m = 0;
      float s = 0.0f;
      sscanf_s(t.c_str(), "%d:%d:%f", &h, &m, &s);
      return h * 3600.0f + m * 60.0f + s;
   };

   ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
   if (ImGui::BeginTable("LiveIterationTable", 8,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                         ImGuiTableFlags_SizingStretchProp |
                         ImGuiTableFlags_ScrollY,
                         ImVec2(0, 300)))
   {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 25);
      ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch,
                              1.25f);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.5f);
      ImGui::TableSetupColumn("CRF", ImGuiTableColumnFlags_WidthStretch, 0.4f);
      ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthStretch,
                              0.75f);
      ImGui::TableSetupColumn("VMAF", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Bitrate", ImGuiTableColumnFlags_WidthStretch,
                              0.7f);
      ImGui::TableSetupColumn("Est. Size", ImGuiTableColumnFlags_WidthStretch,
                              0.9f);
      ImGui::TableHeadersRow();

      for (int i = 0; i < jobs.size(); ++i)
      {
         auto& job = jobs[i];

         // Filter: Only display elements that have broken out of INIT or are fully
         // completed
         if (job->state == JobState::INIT && !job->isComplete)
         {
            continue;
         }

         ImGui::PushID(i);
         ImGui::TableNextRow();

         bool isSelected = (job->jobId == m_selectedJobId);
         if (isSelected)
         {
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(UI_SELECTED_COLOR));
         }
         else if (!job->isComplete && job->state != JobState::INIT)
         {
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(UI_ROW_RUNNING_COLOR));
         }
         ImGui::AlignTextToFramePadding();

         ImGui::TableNextColumn();
         ImGui::Text("%d", job->jobId);

         ImGui::TableNextColumn();
         ImGui::Text("%s", job->profile.name.c_str());

         bool running = !job->isComplete;
         FFmpegProgress prog = job->runner->GetProgress();

         ImGui::TableNextColumn();
         if (running)
         {
            if (job->state == JobState::INIT)
               ImGui::TextColored(UI_QUEUED_COLOR, "Queued...");
            else if (job->state == JobState::EXTRACTING_SEGMENT)
               ImGui::TextColored(UI_EXTRACT_COLOR,
                                  "Extracting Seg %d/%d", job->currentSegmentIdx + 1,
                                  (int)job->segmentStartTimes.size());
            else if (job->state == JobState::ENCODING_SEGMENT)
               ImGui::TextColored(UI_IN_PROG_COLOR,
                                  "Encoding Seg %d/%d", job->currentSegmentIdx + 1,
                                  job->segmentStartTimes.size());
            else if (job->state == JobState::VMAFFING_SEGMENT)
               ImGui::TextColored(UI_IN_PROG_COLOR,
                                  "Analyzing Seg %d/%d", job->currentSegmentIdx + 1,
                                  job->segmentStartTimes.size());
            else if (job->state == JobState::CHECKING_SCORE)
               ImGui::TextColored(UI_EXTRACT_COLOR, "Checking Avg...");
            else
               ImGui::TextColored(UI_IN_PROG_COLOR, "Initializing...");
         }
         else
         {
            if (job->isCanceled)
               ImGui::TextColored(UI_CANCEL_COLOR, "Canceled - Seg %d/%d Completed",
                                  job->currentSegmentIdx + 1, (int)job->segmentStartTimes.size());
            else
               ImGui::TextColored(UI_SUCESS_COLOR,
                                  job->isRecommended ? "Recommended" : "Done");
         }

         ImGui::TableNextColumn();
         ImGui::Text("%d", job->currentCrf);

         ImGui::TableNextColumn();
         if (running)
         {
            float progressSec = TimeToSeconds(prog.time);
            int pct = (int)((progressSec / job->segmentDuration) * 100.0f);
            if (pct > 100)
               pct = 100;

            if (job->state == JobState::EXTRACTING_SEGMENT)
               ImGui::Text("Prep: %d%%", pct);
            else if (job->state == JobState::ENCODING_SEGMENT)
               ImGui::Text("Encode: %d%%", pct);
            else if (job->state == JobState::VMAFFING_SEGMENT)
               ImGui::Text("VMAF: %d%%", pct);
            else
               ImGui::Text("Processing...");
         }
         else
         {
            ImGui::Text("-");
         }

         ImGui::TableNextColumn();
         bool showTarget =
            (job->searchActive || job->iteration > 0 || job->profile.autoCrf);

         if (job->finalVMAF > 0.0f)
         {
            if (showTarget)
               ImGui::Text("%.2f (Target: %d)", job->finalVMAF,
                           job->profile.targetVmaf);
            else
               ImGui::Text("%.2f", job->finalVMAF);
         }
         else if (!job->segmentVMAFs.empty())
         {
            float sum = std::accumulate(job->segmentVMAFs.begin(),
                                        job->segmentVMAFs.end(), 0.0f);
            float avg = sum / job->segmentVMAFs.size();
            if (showTarget)
               ImGui::TextDisabled("Avg: %.2f (Target: %d)", avg,
                                   job->profile.targetVmaf);
            else
               ImGui::TextDisabled("Avg: %.2f", avg);
         }
         else
         {
            if (showTarget)
               ImGui::Text("- (Target: %d)", job->profile.targetVmaf);
            else
               ImGui::Text("-");
         }

         ImGui::TableNextColumn();
         if (job->avgBitrate > 0.0f)
         {
            if (running)
               ImGui::TextDisabled("Avg: %.1f", job->avgBitrate);
            else
               ImGui::Text("%.1f", job->avgBitrate);
         }
         else
            ImGui::Text("%.1f", prog.bitrate);

         ImGui::TableNextColumn();
         DrawSizeColumn(job->estimatedFullSize, running);

         ImGui::PopID();
      }
      ImGui::EndTable();
      ImGui::PopStyleColor();
   }
}
// Helper to run command silently (windows)
#ifdef _WIN32
static void RunCommandSilent(const std::string& cmd)
{
   STARTUPINFOA si;
   PROCESS_INFORMATION pi;
   ZeroMemory(&si, sizeof(si));
   si.cb = sizeof(si);
   si.dwFlags = STARTF_USESHOWWINDOW;
   si.wShowWindow = SW_HIDE;
   ZeroMemory(&pi, sizeof(pi));
   if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE,
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
   {
      WaitForSingleObject(pi.hProcess, INFINITE);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
   }
}
#endif
