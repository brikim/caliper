#include "ui_manager.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <numeric>
#include <ranges>
#include <set>
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
   constexpr auto UI_HEADER_COLOR = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
   constexpr auto UI_EXTRACT_COLOR = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
   constexpr auto UI_IN_PROG_COLOR = ImVec4(0.1f, 0.9f, 0.9f, 1.0f);
   constexpr auto UI_QUEUED_COLOR = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
   constexpr auto UI_SUCESS_COLOR = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
   constexpr auto UI_CANCEL_COLOR = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
   constexpr auto UI_SELECTED_COLOR = ImVec4(0.2f, 0.2f, 0.5f, 0.3f);
   constexpr auto UI_ROW_RUNNING_COLOR = ImVec4(0.0f, 0.4f, 0.4f, 0.3f);
   constexpr auto UI_BUTTON_CANCEL_COLOR = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
   constexpr auto UI_BUTTON_CANCEL_HOVER_COLOR = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
   constexpr auto UI_BUTTON_CANCEL_ACTIVE_COLOR = ImVec4(0.7f, 0.1f, 0.1f, 1.0f);

   constexpr auto UI_BUTTON_SIZE = ImVec2(120, 30);

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

   auto TimeToSeconds(std::string_view t)
   {
      if (t.empty())
         return 0.0f;

      // Helper to find the next colon
      auto getNextPart = [&t](std::string_view& sv) -> std::string_view {
         size_t pos = sv.find(':');
         std::string_view part = sv.substr(0, pos);
         sv.remove_prefix(pos == std::string_view::npos ? sv.size() : pos + 1);
         return part;
      };

      // Parse Hours
      std::string_view partH = getNextPart(t);
      int h = 0;
      std::from_chars(partH.data(), partH.data() + partH.size(), h);

      // Parse Minutes
      std::string_view partM = getNextPart(t);
      int m = 0;
      std::from_chars(partM.data(), partM.data() + partM.size(), m);

      // Parse Seconds (float)
      float s = 0.0f;
      std::from_chars(t.data(), t.data() + t.size(), s);

      return (static_cast<float>(h) * 3600.0f) + (static_cast<float>(m) * 60.0f) + s;
   };
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

UIManager::UIManager(std::reference_wrapper<JobManager> jobManager)
   : m_jobManager(jobManager)
{
   LoadFonts();
   SetupTheme();
   LoadProfiles();

   if (m_profiles.empty())
   {
      m_profiles.push_back(EncodeProfile{
         .name = "x264 Medium",
         .codec = "libx264",
         .preset = "medium",
         .startCrf = DEFAULT_CRF,
         .autoCrf = true,
         .targetVmaf = 96,
         .extraArgs = "",
         .bitDepth = "8-bit",
         .isDefault = false
      });
      m_profiles.push_back(EncodeProfile{
         .name = "x264 Slow",
         .codec = "libx264",
         .preset = "slow",
         .startCrf = DEFAULT_CRF,
         .autoCrf = true,
         .targetVmaf = 96,
         .extraArgs = "",
         .bitDepth = "8-bit",
         .isDefault = false
      });
      m_profiles.push_back(EncodeProfile{
         .name = "x265 Medium",
         .codec = "libx265",
         .preset = "medium",
         .startCrf = DEFAULT_CRF,
         .autoCrf = true,
         .targetVmaf = 96,
         .extraArgs = "",
         .bitDepth = "10-bit",
         .isDefault = true
      });
   }

   auto it = std::ranges::find_if(m_profiles, &EncodeProfile::isDefault);
   if (it != m_profiles.end())
   {
      m_activeProfileIdx = std::distance(m_profiles.begin(), it);
   }

   FFmpegRunner tempRunner;
   m_ffmpegVersion = tempRunner.GetFFmpegVersion();
}

UIManager::~UIManager()
{}

void UIManager::LoadFonts()
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

void UIManager::SetupTheme()
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
}

void UIManager::LoadProfiles()
{
   std::ifstream file("profiles.json");
   if (!file.is_open())
   {
      return;
   }

   try
   {
      json j;
      file >> j;

      if (j.is_array())
      {
         for (const auto& item : j)
         {
            m_profiles.push_back(EncodeProfile{
               .name = item.value("name", "Custom Profile"),
               .codec = item.value("codec", "libx264"),
               .preset = item.value("preset", "medium"),
               .startCrf = item.value("startCrf", DEFAULT_CRF),
               .autoCrf = item.value("autoCrf", true),
               .targetVmaf = item.value("targetVmaf", 96),
               .extraArgs = item.value("extraArgs", ""),
               .bitDepth = item.value("bitDepth", "8-bit"),
               .isDefault = item.value("isDefault", false),
               .segmentCount = item.value("segmentCount", 4),
               .segmentDuration = item.value("segmentDuration", 60.0f),
               .svtTune = item.value("svtTune", 0)
            });
         }
      }
      else if (j.is_object())
      {
         if (j.contains("profiles") && j["profiles"].is_array())
         {
            for (const auto& item : j["profiles"])
            {
               m_profiles.push_back(EncodeProfile{
                  .name = item.value("name", "Custom Profile"),
                  .codec = item.value("codec", "libx264"),
                  .preset = item.value("preset", "medium"),
                  .startCrf = item.value("startCrf", DEFAULT_CRF),
                  .autoCrf = item.value("autoCrf", true),
                  .targetVmaf = item.value("targetVmaf", 96),
                  .extraArgs = item.value("extraArgs", ""),
                  .bitDepth = item.value("bitDepth", "8-bit"),
                  .isDefault = item.value("isDefault", false),
                  .segmentCount = item.value("segmentCount", 4),
                  .segmentDuration = item.value("segmentDuration", 60.0f),
                  .svtTune = item.value("svtTune", 0)
               });
            }
         }
      }
   }
   catch (...)
   {
      std::cerr << "Failed to parse profiles.json\n";
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

std::filesystem::path UIManager::OpenFileDialog()
{
   std::filesystem::path result;
#ifdef _WIN32
   std::array<char, MAX_PATH> filename = {0};
   OPENFILENAMEA ofn;
   ZeroMemory(&ofn, sizeof(ofn));
   ofn.lStructSize = sizeof(ofn);
   ofn.hwndOwner = NULL;
   ofn.lpstrFilter = "Video Files\0*.mp4;*.mkv;*.avi;*.mov\0All Files\0*.*\0";
   ofn.lpstrFile = filename.data();
   ofn.nMaxFile = static_cast<DWORD>(filename.size());
   ofn.Flags =
      OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
   ofn.lpstrDefExt = "mp4";

   if (GetOpenFileNameA(&ofn))
   {
      result = filename.data();
   }
#endif
   return result;
}

void UIManager::Draw()
{
   ImGui::Begin("CaliperMain", nullptr, SetupImGuiStyle());
   ImGui::PopStyleVar(3);

   // Disable the entire UI if FFmpeg wasn't found
   bool ffmpegMissing = !m_ffmpegVersion.has_value();
   if (ffmpegMissing)
   {
      // Optionally draw a warning message that is NOT disabled
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                         "ERROR: FFmpeg not detected! Please ensure ffmpeg is in your PATH.");

      ImGui::BeginDisabled();
   }

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

   if (ffmpegMissing)
   {
      ImGui::EndDisabled();
   }

   ImGui::End();
}

ImGuiWindowFlags UIManager::SetupImGuiStyle()
{
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   ImGui::SetNextWindowPos(viewport->WorkPos);
   ImGui::SetNextWindowSize(viewport->WorkSize);

   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));

   return ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
}

void UIManager::DrawInputSection()
{
   ImGui::TextColored(UI_HEADER_COLOR, "1. Source Video");

   std::string versionText = m_ffmpegVersion.has_value() ?
      std::format("FFmpeg v{}", m_ffmpegVersion.value()) : "FFmpeg not found!";
   ImVec2 textSize = ImGui::CalcTextSize(versionText.c_str());

   float posX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textSize.x;
   ImGui::SameLine();
   ImGui::SetCursorPosX(posX);

   // 4. Draw the text
   if (m_ffmpegVersion.has_value())
   {
      ImGui::TextDisabled("%s", versionText.c_str());
   }
   else
   {
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", versionText.c_str());
   }

   if (ImGui::Button("Browse...", UI_BUTTON_SIZE))
   {
      auto selected = OpenFileDialog();
      if (!selected.empty())
      {
         m_referenceVideo = selected;

         FFmpegRunner newRunner;
         m_referenceMeta = newRunner.GetMetadata(m_referenceVideo);
      }
   }
   ImGui::SameLine();
   // Center the next item vertically relative to the button
   float buttonHeight = ImGui::GetItemRectSize().y; // Gets the height of the Button
   float inputHeight = ImGui::GetFrameHeight();     // Gets the standard height of InputText

   if (buttonHeight > inputHeight)
   {
      float offsetY = (buttonHeight - inputHeight) * 0.5f;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
   }
   ImGui::SetNextItemWidth(-FLT_MIN);

   auto videoString = m_referenceVideo.empty() ? "No video selected" : m_referenceVideo.filename().string();
   ImGui::InputText("##ReferenceVideo", &videoString, ImGuiInputTextFlags_ReadOnly);

   if (m_referenceMeta)
   {
      const auto& meta = m_referenceMeta.value();
      std::chrono::seconds total_duration{static_cast<int>(meta.duration)};
      std::chrono::hh_mm_ss hms{total_duration};

      ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                         "Codec: %s | %dx%d | %.2f fps | %d-bit | %02d:%02d:%02lld",
                         meta.codec.c_str(), meta.width,
                         meta.height, meta.framerate,
                         meta.bit_depth, hms.hours().count(),
                         hms.minutes().count(), hms.seconds().count());
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
   ImGui::TextColored(UI_HEADER_COLOR, "2. Encoding Profiles");

   if (ImGui::Button("Add Profile", UI_BUTTON_SIZE))
   {
      m_profiles.push_back(EncodeProfile{
         .name = "New Profile",
         .codec = "libx264",
         .preset = "medium",
         .startCrf = DEFAULT_CRF,
         .autoCrf = true,
         .targetVmaf = 96,
         .extraArgs = "",
         .bitDepth = "8-bit",
         .isDefault = false
      });
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
         float totalButtonWidth = (UI_BUTTON_SIZE.x * 2.0f) + spacing;

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
   for (int i = 0; i < static_cast<int>(m_profiles.size()); ++i)
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

      ImGui::TextColored(UI_HEADER_COLOR, "Profile Name");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputText("##Name", &p.name))
      {
         m_profilesDirty = true;
      }

      if (ImGui::BeginTable("ProfileFields", 4))
      {
         ImGui::TableNextColumn();
         ImGui::TextColored(UI_HEADER_COLOR, "Target VMAF");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderInt("##Target VMAF", &p.targetVmaf, 0, 100))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(UI_HEADER_COLOR, "Starting CRF");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderInt("##Starting CRF", &p.startCrf, 0,
                              (p.codec == "libsvtav1" ? 63 : 51)))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(UI_HEADER_COLOR, "Segment Count");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderInt("##SegCount", &p.segmentCount, 1, 50))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(UI_HEADER_COLOR,
                            "Segment Duration (s)");
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::SliderFloat("##SegDuration", &p.segmentDuration,
                                1.0f, 300.0f, "%.0fs"))
         {
            m_profilesDirty = true;
         }

         ImGui::TableNextColumn();
         ImGui::TextColored(UI_HEADER_COLOR, "Codec");
         static constexpr std::array<const char*, 5> codecs = {"libx264", "libx265", "hevc_nvenc",
                                                               "h264_nvenc", "libsvtav1"};
         static constexpr std::array<const char*, 5> codecNames = {"H.264", "H.265", "H.265 (NVENC)",
                                                                   "H.264 (NVENC)", "AV1 (SVT-AV1)"};
         int codecIdx = 0;
         for (int i = 0; i < codecs.size(); ++i)
            if (p.codec == codecs[i])
               codecIdx = i;
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::Combo("##Codec", &codecIdx, codecNames.data(), static_cast<int>(codecNames.size())))
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
         ImGui::TextColored(UI_HEADER_COLOR, "Preset");
         if (p.codec == "libsvtav1")
         {
            static constexpr std::array<const char*, 14> svtPresets = {
               "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13"
            };
            auto it = std::ranges::find(svtPresets, p.preset);
            auto presetIdx = (it != svtPresets.end()) ?
               static_cast<int>(std::distance(svtPresets.begin(), it)) : 8;

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Preset", &presetIdx, svtPresets.data(), static_cast<int>(svtPresets.size())))
            {
               p.preset = svtPresets[presetIdx];
               m_profilesDirty = true;
            }
         }
         else
         {
            static constexpr std::array<const char*, 9> presets = {
               "ultrafast", "superfast", "veryfast",
               "faster", "fast", "medium",
               "slow", "slower", "veryslow"
            };
            int presetIdx = 5;
            for (size_t i = 0u; i < presets.size(); ++i)
               if (p.preset == presets[i])
                  presetIdx = i;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Preset", &presetIdx, presets.data(), static_cast<int>(presets.size())))
            {
               p.preset = presets[presetIdx];
               m_profilesDirty = true;
            }
         }

         ImGui::TableNextColumn();
         if (p.codec == "libsvtav1")
         {
            ImGui::TextColored(UI_HEADER_COLOR, "Encoder Tune");
            static constexpr std::array<const char*, 3> svtTunes = {"VQ (0)", "PSNR (1)", "SSIM (2)"};
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Tune", &p.svtTune, svtTunes.data(), static_cast<int>(svtTunes.size())))
            {
               m_profilesDirty = true;
            }
         }
         else
         {
            ImGui::TextColored(UI_HEADER_COLOR, "Bit Depth");
            static constexpr std::array<const char*, 2> depths = {"8-bit", "10-bit"};
            int depthIdx = (p.bitDepth == "10-bit") ? 1 : 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##Bit Depth", &depthIdx, depths.data(), static_cast<int>(depths.size())))
            {
               p.bitDepth = depths[depthIdx];
               m_profilesDirty = true;
            }
         }

         ImGui::EndTable();
      }

      ImGui::TextColored(UI_HEADER_COLOR,
                         "Extra FFmpeg Arguments");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputText("##Extra Args", &p.extraArgs))
      {
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
   ImGui::TextColored(UI_HEADER_COLOR, "3. Execution");

   const auto& jobs = m_jobManager.get().GetJobs();

   bool anyRunning = false;
   for (const auto& job : jobs)
      if (!job->isComplete)
         anyRunning = true;

   bool canEnqueue = !m_referenceVideo.empty() && m_referenceMeta.has_value();
   if (!canEnqueue)
      ImGui::BeginDisabled();
   if (ImGui::Button("Enqueue Job", UI_BUTTON_SIZE))
   {
      if (m_activeProfileIdx >= 0 && m_activeProfileIdx < m_profiles.size())
      {
         const auto& p = m_profiles[m_activeProfileIdx];
         m_jobManager.get().AddJob(m_referenceVideo, m_referenceMeta.value(), p, p.segmentCount,
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
         m_jobManager.get().ClearCompletedJobs();
      }
   }

   bool selectedIsValid = false;
   bool selectedIsProcessing = false;
   bool selectedIsPaused = false;
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

               // Check the runner's pause state
               if (job->runner && job->runner->IsPaused())
               {
                  selectedIsPaused = true;
               }
               break;
            }
         }
      }
   }

   auto dummyButtonSize = ImVec2(10.0f, 0.0f);
   float rightAlignWidth =
      (UI_BUTTON_SIZE.x * 4.0f) + (ImGui::GetStyle().ItemSpacing.x * 4.0f) + dummyButtonSize.x * 2.0f;
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

   if (!selectedIsValid || !selectedIsProcessing)
      ImGui::BeginDisabled();

   const char* pauseLabel = selectedIsPaused ? "Resume Job" : "Pause Job";
   if (ImGui::Button(pauseLabel, UI_BUTTON_SIZE))
   {
      // Route this to your JobManager to update the specific runner
      if (selectedIsPaused)
         m_jobManager.get().ResumeJob(m_selectedJobId);
      else
         m_jobManager.get().PauseJob(m_selectedJobId);
   }

   if (!selectedIsValid || !selectedIsProcessing)
      ImGui::EndDisabled();

   ImGui::SameLine();
   ImGui::Dummy(dummyButtonSize); // The matching gap you requested
   ImGui::SameLine();

   if (!selectedIsValid || selectedIsProcessing)
      ImGui::BeginDisabled();
   if (ImGui::Button("Remove Job", UI_BUTTON_SIZE))
   {
      m_jobManager.get().RemoveJob(m_selectedJobId);
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
         m_jobManager.get().StopAllJobs();
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
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      float totalButtonWidth = (UI_BUTTON_SIZE.x * 2.0f) + spacing;

      // Calculate the offset to center the buttons and set the cursor
      float cursorX = (ImGui::GetWindowSize().x - totalButtonWidth) * 0.5f;
      ImGui::SetCursorPosX(cursorX);

      // Draw the buttons
      if (ImGui::Button("Yes", UI_BUTTON_SIZE))
      {
         m_jobManager.get().CancelJob(m_selectedJobId);
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

      std::set<int> uniqueIds;
      for (const auto& j : jobs)
      {
         uniqueIds.insert(j->jobId);
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
         auto idLabel = std::format("{}##{}", id, id);
         if (ImGui::Selectable(idLabel.c_str(), isSelected,
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
            if (latestIter->runner && latestIter->runner->IsPaused())
               ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Paused");
            else if (latestIter->state == JobState::EXTRACTING_SEGMENT)
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
   ImGui::TextColored(UI_HEADER_COLOR, "Iteration Details");

   if (jobs.empty())
   {
      ImGui::TextDisabled("No active jobs.");
      return;
   }

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
            auto progressSec = TimeToSeconds(prog.time);
            auto pct = static_cast<int>((progressSec / job->segmentDuration) * 100.0f);
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
            auto sum = std::accumulate(job->segmentVMAFs.begin(),
                                       job->segmentVMAFs.end(), 0.0f);
            auto avg = sum / job->segmentVMAFs.size();
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
