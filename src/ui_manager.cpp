#include "ui_manager.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

UIManager::UIManager() {
  LoadProfiles();
  if (m_profiles.empty()) {
    // Add default profiles if file doesn't exist
    m_profiles.push_back(
        {"x264 Medium", "libx264", "medium", 23, true, 96, "", "8-bit", true});
    m_profiles.push_back(
        {"x264 Slow", "libx264", "slow", 23, true, 96, "", "8-bit", false});
    m_profiles.push_back(
        {"x265 Medium", "libx265", "medium", 28, true, 96, "", "8-bit", false});
  }
  if (!m_profiles.empty()) {
    m_activeProfileIdx = 0;
    for (int i = 0; i < m_profiles.size(); ++i) {
      if (m_profiles[i].isDefault) {
        m_activeProfileIdx = i;
        break;
      }
    }
  }
}

UIManager::~UIManager() {
  SaveProfiles();
#ifdef _WIN32
  if (m_sleepPrevented) {
    SetThreadExecutionState(ES_CONTINUOUS);
  }
#endif
  for (auto &job : m_jobs) {
    if (job->runner)
      job->runner->Stop();
    std::error_code ec;
    if (!job->outputFile.empty())
      std::filesystem::remove(job->outputFile, ec);
    if (!job->refSegFile.empty())
      std::filesystem::remove(job->refSegFile, ec);
  }
}

void UIManager::LoadProfiles() {
  std::ifstream file("profiles.json");
  if (file.is_open()) {
    try {
      json j;
      file >> j;
      for (const auto &item : j) {
        EncodeProfile p;
        p.name = item.value("name", "Custom Profile");
        p.codec = item.value("codec", "libx264");
        p.preset = item.value("preset", "medium");
        p.startCrf = item.value("startCrf", 23);
        p.autoCrf = item.value("autoCrf", true);
        p.targetVmaf = item.value("targetVmaf", 96);
        p.extraArgs = item.value("extraArgs", "");
        p.bitDepth = item.value("bitDepth", "8-bit");
        p.isDefault = item.value("isDefault", false);
        m_profiles.push_back(p);
      }
    } catch (...) {
      std::cerr << "Failed to parse profiles.json\n";
    }
  }
}

#include <filesystem>
#include <numeric>

void UIManager::SaveProfiles() {
  json j = json::array();
  for (const auto &p : m_profiles) {
    j.push_back({{"name", p.name},
                 {"codec", p.codec},
                 {"preset", p.preset},
                 {"startCrf", p.startCrf},
                 {"autoCrf", p.autoCrf},
                 {"targetVmaf", p.targetVmaf},
                 {"extraArgs", p.extraArgs},
                 {"bitDepth", p.bitDepth},
                 {"isDefault", p.isDefault}});
  }
  std::ofstream file("profiles.json");
  if (file.is_open()) {
    file << j.dump(4);
    file.flush();
    file.close();
  }
}

std::string UIManager::OpenFileDialog() {
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
  ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
  ofn.lpstrDefExt = "mp4";

  if (GetOpenFileNameA(&ofn)) {
    result = std::string(filename);
  }
#endif
  return result;
}

void UIManager::Draw() {
  ImGui::Begin("Caliper Dashboard", nullptr, ImGuiWindowFlags_MenuBar);

  if (ImGui::BeginTabBar("MainTabs")) {
    if (ImGui::BeginTabItem("Benchmark Setup")) {
      DrawInputSection();
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      DrawProfileManager();
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      DrawJobQueue();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Results & Analysis")) {
      DrawResults();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void UIManager::DrawInputSection() {
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "1. Source Video");

  ImGui::InputText("##ReferenceVideo", m_referenceVideo.data(),
                   m_referenceVideo.capacity(), ImGuiInputTextFlags_ReadOnly);
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    std::string selected = OpenFileDialog();
    if (!selected.empty()) {
      m_referenceVideo = selected;
      // Fetch metadata
      m_referenceMeta = FFmpegRunner::GetMetadata(m_referenceVideo);
    }
  }

  if (m_referenceMeta.valid) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                       "Codec: %s | %dx%d | %.2f fps | %.1fs | %d-bit",
                       m_referenceMeta.codec.c_str(), m_referenceMeta.width,
                       m_referenceMeta.height, m_referenceMeta.framerate,
                       m_referenceMeta.duration, m_referenceMeta.bit_depth);
  } else if (!m_referenceVideo.empty()) {
    ImGui::TextColored(
        ImVec4(0.8f, 0.4f, 0.4f, 1.0f),
        "Failed to read video metadata. Ensure ffprobe is in PATH.");
  }

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "1b. Segment Settings");
  ImGui::SetNextItemWidth(120);
  ImGui::DragInt("Segment Count", &m_segmentCount, 1, 1, 10);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::DragFloat("Duration (s)", &m_segmentDuration, 1.0f, 1.0f, 300.0f);
}

void UIManager::DrawProfileManager() {
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "2. Encoding Profiles");

  if (ImGui::Button("Add Profile")) {
    m_profiles.push_back(
        {"New Profile", "libx264", "medium", 23, true, 96, "", "8-bit", false});
    m_activeProfileIdx = m_profiles.size() - 1;
  }

  ImGui::BeginChild("ProfilesList", ImVec2(200, 325), true);
  for (int i = 0; i < m_profiles.size(); ++i) {
    ImGui::PushID(i);
    if (ImGui::RadioButton(m_profiles[i].name.c_str(),
                           m_activeProfileIdx == i)) {
      if (m_activeProfileIdx != i) {
        SaveProfiles();
        m_activeProfileIdx = i;
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("ProfileEditor", ImVec2(0, 325), true);
  if (m_activeProfileIdx >= 0 && m_activeProfileIdx < m_profiles.size()) {
    auto &p = m_profiles[m_activeProfileIdx];

    char nameBuf[64];
    strcpy_s(nameBuf, p.name.c_str());
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Profile Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf))) {
      p.name = nameBuf;
      SaveProfiles();
    }

    if (ImGui::BeginTable("ProfileFields", 3)) {
      // Row 1: Quality Targets
      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Target VMAF");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::SliderInt("##Target VMAF", &p.targetVmaf, 0, 100)) {
        SaveProfiles();
      }

      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Starting CRF");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::SliderInt("##Starting CRF", &p.startCrf, 0, 51)) {
        SaveProfiles();
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        SaveProfiles();
      }

      ImGui::TableNextColumn(); // Empty space for balance

      // Row 2: Encoding Parameters
      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Codec");
      const char *codecs[] = {"libx264", "libx265", "hevc_nvenc", "h264_nvenc"};
      int codecIdx = 0;
      for (int i = 0; i < 4; ++i)
        if (p.codec == codecs[i])
          codecIdx = i;
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##Codec", &codecIdx, codecs, 4)) {
        p.codec = codecs[codecIdx];
        SaveProfiles();
      }

      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Preset");
      const char *presets[] = {"ultrafast", "superfast", "veryfast",
                               "faster",    "fast",      "medium",
                               "slow",      "slower",    "veryslow"};
      int presetIdx = 5;
      for (int i = 0; i < 9; ++i)
        if (p.preset == presets[i])
          presetIdx = i;
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##Preset", &presetIdx, presets, 9)) {
        p.preset = presets[presetIdx];
        SaveProfiles();
      }

      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Bit Depth");
      const char *depths[] = {"8-bit", "10-bit"};
      int depthIdx = (p.bitDepth == "10-bit") ? 1 : 0;
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##Bit Depth", &depthIdx, depths, 2)) {
        p.bitDepth = depths[depthIdx];
        SaveProfiles();
      }

      ImGui::EndTable();
    }

    char extraBuf[256];
    strcpy_s(extraBuf, p.extraArgs.c_str());
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                       "Extra FFmpeg Arguments");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##Extra Args", extraBuf, sizeof(extraBuf))) {
      p.extraArgs = extraBuf;
      SaveProfiles();
    }
    ImGui::TextDisabled(
        "Use standard FFmpeg flags (e.g. -x265-params key=val:key2=val2)");

    ImGui::Spacing();
    if (p.isDefault) {
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                         "Default Profile [Startup]");
    } else {
      if (ImGui::Button("Set as Default")) {
        for (auto &other : m_profiles)
          other.isDefault = false;
        p.isDefault = true;
        SaveProfiles();
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Remove Profile", ImVec2(120, 0))) {
      m_profiles.erase(m_profiles.begin() + m_activeProfileIdx);
      if (m_activeProfileIdx >= m_profiles.size())
        m_activeProfileIdx = m_profiles.size() - 1;
      SaveProfiles();
    }
  } else {
    ImGui::Spacing();
    ImGui::TextColored(
        ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
        "Please select a profile from the left to begin editing.");
    ImGui::TextDisabled("(If the list is empty, click 'Add Profile' above)");
  }
  ImGui::EndChild();
}

void UIManager::StartBenchmark() {
  if (m_referenceVideo.empty() || !m_referenceMeta.valid)
    return;
  if (m_activeProfileIdx < 0 || m_activeProfileIdx >= m_profiles.size())
    return;

  const auto &p = m_profiles[m_activeProfileIdx];

  auto job = std::make_shared<BenchmarkJob>();
  job->id = p.name;
  job->profile = p;
  job->searchActive = true;
  job->currentCrf = p.startCrf;
  job->crfMin = 0;
  job->crfMax = 51;
  job->iteration = 0;

  // Calculate segment start times
  job->segmentDuration = m_segmentDuration;
  float totalDur = m_referenceMeta.duration;
  int count = m_segmentCount;

  if (count <= 1) {
    float mid = (totalDur / 2.0f) - (m_segmentDuration / 2.0f);
    if (mid < 0.0f)
      mid = 0.0f;
    job->segmentStartTimes.push_back(mid);
  } else {
    float chunkDur = totalDur / count;
    for (int i = 0; i < count; ++i) {
      float s = (chunkDur * i) + (chunkDur / 2.0f) - (m_segmentDuration / 2.0f);
      if (s < 0.0f)
        s = 0.0f;
      if (s + m_segmentDuration > totalDur)
        s = totalDur - m_segmentDuration;
      if (s < 0.0f)
        s = 0.0f; // Prevent negative start time if totalDur < m_segmentDuration
      job->segmentStartTimes.push_back(s);
    }
  }

  job->state = JobState::INIT;
  job->runner = std::make_unique<FFmpegRunner>();
  m_jobs.push_back(job);
}

void UIManager::UpdateJob(std::shared_ptr<BenchmarkJob> &job) {
  if (job->isComplete)
    return;

  std::string jobIdx = std::to_string(std::hash<std::string>{}(job->id) % 1000);
  std::string tempDist = "temp_dist_" + jobIdx + ".mkv";
  job->outputFile = tempDist;

  switch (job->state) {
  case JobState::INIT: {
    job->currentSegmentIdx = 0;
    job->segmentVMAFs.clear();
    job->segmentBitrates.clear();
    job->segmentSizes.clear();
    job->estimatedFullSize = 0.0f;
    job->commandStarted = false;
    job->state = JobState::EXTRACTING_SEGMENT;
    break;
  }
  case JobState::EXTRACTING_SEGMENT: {
    if (!job->commandStarted) {
      // Losslessly copy the reference segment using two-stage seek.
      // This produces a clean file starting at t=0 that the encoder
      // will read from — no timing mismatch is possible.
      float start = job->segmentStartTimes[job->currentSegmentIdx];
      float preSeek = (start > 5.0f) ? start - 5.0f : 0.0f;
      float postSeek = start - preSeek;
      std::string segIdx = std::to_string(job->currentSegmentIdx);
      job->refSegFile = "temp_ref_" + jobIdx + "_" + segIdx + ".mkv";
      std::string cmd = "ffmpeg -v error -y"
                        " -ss " +
                        std::to_string(preSeek) + " -i \"" + m_referenceVideo +
                        "\""
                        " -ss " +
                        std::to_string(postSeek) + " -t " +
                        std::to_string(job->segmentDuration) +
                        " -map 0:v:0 -c:v ffv1 -an " + job->refSegFile;
      job->runner->Start(cmd);
      job->commandStarted = true;
    } else if (!job->runner->IsRunning()) {
      job->commandStarted = false;
      std::error_code ec;
      if (std::filesystem::exists(job->refSegFile, ec) &&
          std::filesystem::file_size(job->refSegFile, ec) > 0) {
        job->state = JobState::ENCODING_SEGMENT;
      } else {
        job->isComplete = true;
        job->state = JobState::DONE;
      }
    }
    break;
  }
  case JobState::ENCODING_SEGMENT: {
    if (!job->commandStarted) {
      // Encode directly from the pre-extracted reference segment.
      // Both files start at t=0 with identical content — perfect alignment.
      std::string pixFmt = "yuv420p";
      if (job->profile.bitDepth == "10-bit") {
        if (job->profile.codec.find("nvenc") != std::string::npos) {
          pixFmt = "p010le";
        } else {
          pixFmt = "yuv420p10le";
        }
      }

      std::string extra = job->profile.extraArgs;
      // Smart Heuristic: If it looks like parameters but lacks a flag,
      // auto-wrap it for x264/x265.
      if (!extra.empty()) {
        if (extra[0] != '-' && extra.find('=') != std::string::npos) {
          if (job->profile.codec == "libx264") {
            extra = "-x264-params \"" + extra + "\"";
          } else if (job->profile.codec == "libx265") {
            extra = "-x265-params \"" + extra + "\"";
          }
        }
        if (extra[0] != ' ')
          extra = " " + extra;
      }

      std::string cmd = "ffmpeg -v warning -stats -y"
                        " -i " +
                        job->refSegFile + " -c:v " + job->profile.codec +
                        " -preset " + job->profile.preset + " -pix_fmt " +
                        pixFmt + " -crf " + std::to_string(job->currentCrf) +
                        extra + " " + tempDist;
      job->runner->Start(cmd);
      job->commandStarted = true;
    } else if (!job->runner->IsRunning()) {
      job->commandStarted = false;
      if (job->runner->GetProgress().bitrate > 0) {
        job->segmentBitrates.push_back(job->runner->GetProgress().bitrate);
        std::error_code ec;
        job->segmentSizes.push_back(std::filesystem::file_size(tempDist, ec));
        job->state = JobState::VMAFFING_SEGMENT;
      } else {
        // Encode failed
        job->isComplete = true;
        job->state = JobState::DONE;
      }
    }
    break;
  }
  case JobState::VMAFFING_SEGMENT: {
    if (!job->commandStarted) {
      int threads = std::thread::hardware_concurrency();
      if (threads == 0)
        threads = 4; // Fallback

      // Compare distorted against the pre-extracted reference segment.
      // Both start at t=0, same content — VMAF will always be accurate.
      std::string cmd =
          "ffmpeg -v info"
          " -i " +
          tempDist + " -i " + job->refSegFile +
          " -lavfi \"libvmaf=n_threads=" + std::to_string(threads) +
          "\""
          " -f null -";
      job->runner->Start(cmd);
      job->commandStarted = true;
    } else if (!job->runner->IsRunning()) {
      job->commandStarted = false;
      float score = job->runner->GetVMAFScore();
      // Clean up the reference segment now that we're done with it
      std::error_code ec;
      std::filesystem::remove(job->refSegFile, ec);
      if (score > 0.0f) {
        job->segmentVMAFs.push_back(score);
        job->currentSegmentIdx++;
        if (job->currentSegmentIdx >= job->segmentStartTimes.size()) {
          job->state = JobState::CHECKING_SCORE;
        } else {
          job->state = JobState::EXTRACTING_SEGMENT;
        }
      } else {
        // VMAF failed
        job->isComplete = true;
        job->state = JobState::DONE;
      }
    }
    break;
  }
  case JobState::CHECKING_SCORE: {
    if (job->segmentVMAFs.empty()) {
      job->isComplete = true; // Error
      job->state = JobState::DONE;
      std::error_code ec;
      std::filesystem::remove(job->outputFile, ec);
      break;
    }

    float sumVmaf = std::accumulate(job->segmentVMAFs.begin(),
                                    job->segmentVMAFs.end(), 0.0f);
    job->finalVMAF = sumVmaf / job->segmentVMAFs.size();

    float sumBitrate = std::accumulate(job->segmentBitrates.begin(),
                                       job->segmentBitrates.end(), 0.0f);
    job->avgBitrate = sumBitrate / job->segmentBitrates.size();

    float sumSize = std::accumulate(job->segmentSizes.begin(),
                                    job->segmentSizes.end(), 0.0f);
    float totalSampleDur = job->segmentSizes.size() * job->segmentDuration;
    if (totalSampleDur > 0 && m_referenceMeta.duration > 0) {
      job->estimatedFullSize = (sumSize / totalSampleDur) *
                               m_referenceMeta.duration / (1024.0f * 1024.0f);
    }

    if (!job->searchActive) {
      job->isComplete = true;
      job->state = JobState::DONE;
      job->isRecommended = true;
      std::error_code ec;
      std::filesystem::remove(job->outputFile, ec);
    } else {
      float diff = job->finalVMAF - job->profile.targetVmaf;
      // We use a softer P-controller instead of strict binary search to avoid
      // aggressive swings
      if (std::abs(diff) < 0.2f || job->iteration >= 10 ||
          job->currentCrf < 0 || job->currentCrf > 51) {
        job->searchActive = false;
        job->isComplete = true;
        job->state = JobState::DONE;
        job->isRecommended = true;
        std::error_code ec;
        std::filesystem::remove(job->outputFile, ec);
      } else {
        // Spawn a new job for the next iteration
        auto nextJob = std::make_shared<BenchmarkJob>();
        nextJob->id = job->id;
        nextJob->profile = job->profile;
        nextJob->searchActive = true;

        int crfDelta = static_cast<int>(std::round(diff * 2.5f));
        if (crfDelta > 12)
          crfDelta = 12;
        if (crfDelta < -12)
          crfDelta = -12;
        if (crfDelta == 0)
          crfDelta = (diff > 0) ? 1 : -1;

        nextJob->currentCrf = job->currentCrf + crfDelta;
        if (nextJob->currentCrf < 0)
          nextJob->currentCrf = 0;
        if (nextJob->currentCrf > 51)
          nextJob->currentCrf = 51;

        nextJob->iteration = job->iteration + 1;

        nextJob->segmentDuration = job->segmentDuration;
        nextJob->segmentStartTimes = job->segmentStartTimes;

        nextJob->state = JobState::INIT;
        nextJob->runner = std::make_unique<FFmpegRunner>();

        // Mark current job as done but failed to hit target
        job->searchActive = false;
        job->isComplete = true;
        job->state = JobState::DONE;

        m_pendingJobs.push_back(nextJob);
      }
    }
    break;
  }
  case JobState::DONE:
    break;
  }
}

void UIManager::DrawJobQueue() {
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "3. Execution");

  bool anyRunning = false;
  for (const auto &job : m_jobs)
    if (!job->isComplete)
      anyRunning = true;

#ifdef _WIN32
  if (anyRunning && !m_sleepPrevented) {
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    m_sleepPrevented = true;
  } else if (!anyRunning && m_sleepPrevented) {
    SetThreadExecutionState(ES_CONTINUOUS);
    m_sleepPrevented = false;
  }
#endif

  if (anyRunning) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button("Stop Benchmark", ImVec2(150, 30))) {
      for (auto &job : m_jobs) {
        if (!job->isComplete) {
          job->isComplete = true;
          job->state = JobState::DONE;
          if (job->runner)
            job->runner->Stop();
          std::error_code ec;
          if (!job->outputFile.empty())
            std::filesystem::remove(job->outputFile, ec);
          if (!job->refSegFile.empty())
            std::filesystem::remove(job->refSegFile, ec);
        }
      }
    }
    ImGui::PopStyleColor(3);
  } else {
    if (ImGui::Button("Start Benchmark", ImVec2(150, 30))) {
      StartBenchmark();
    }
  }

  ImGui::SameLine();
  bool hasCompleted = false;
  for (const auto &job : m_jobs)
    if (job->isComplete)
      hasCompleted = true;

  if (hasCompleted) {
    if (ImGui::Button("Clear Results", ImVec2(110, 30))) {
      m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                                  [](const auto &j) { return j->isComplete; }),
                   m_jobs.end());
    }
  }

  ImGui::Spacing();

  if (m_jobs.empty()) {
    ImGui::TextDisabled("No active jobs.");
    return;
  }

  // Helper to convert "00:00:04.10" to seconds
  auto TimeToSeconds = [](const std::string &t) -> float {
    if (t.empty())
      return 0.0f;
    int h = 0, m = 0;
    float s = 0.0f;
    sscanf_s(t.c_str(), "%d:%d:%f", &h, &m, &s);
    return h * 3600.0f + m * 60.0f + s;
  };

  if (ImGui::BeginTable("JobTable", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_ScrollY,
                        ImVec2(0, 300))) {
    ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch,
                            1.75f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("CRF", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthStretch,
                            0.75f);
    ImGui::TableSetupColumn("VMAF", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Bitrate", ImGuiTableColumnFlags_WidthStretch,
                            0.8f);
    ImGui::TableSetupColumn("Est. Size", ImGuiTableColumnFlags_WidthStretch,
                            1.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < m_jobs.size(); ++i) {
      auto &job = m_jobs[i];
      ImGui::PushID(i);

      // Step the state machine
      UpdateJob(job);

      ImGui::TableNextRow();
      if (job->isRecommended) {
        ImGui::TableSetBgColor(
            ImGuiTableBgTarget_RowBg0,
            ImGui::GetColorU32(ImVec4(0.0f, 0.4f, 0.0f, 0.3f)));
      }
      ImGui::AlignTextToFramePadding();

      ImGui::TableNextColumn();
      ImGui::Text("%s", job->profile.name.c_str());

      bool running = !job->isComplete;
      FFmpegProgress prog = job->runner->GetProgress();

      ImGui::TableNextColumn();
      if (running) {
        if (job->state == JobState::ENCODING_SEGMENT) {
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             "Encoding Seg %d/%d", job->currentSegmentIdx + 1,
                             job->segmentStartTimes.size());
        } else if (job->state == JobState::VMAFFING_SEGMENT) {
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                             "Analyzing Seg %d/%d", job->currentSegmentIdx + 1,
                             job->segmentStartTimes.size());
        } else if (job->state == JobState::CHECKING_SCORE) {
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Checking Avg...");
        } else {
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Initializing...");
        }
      } else {
        if (job->isRecommended) {
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Recommended");
        } else {
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Done");
        }
      }

      ImGui::TableNextColumn();
      ImGui::Text("%d", job->currentCrf);

      ImGui::TableNextColumn();
      if (running) {
        float progressSec = TimeToSeconds(prog.time);
        int pct = (int)((progressSec / job->segmentDuration) * 100.0f);
        if (pct > 100)
          pct = 100;

        if (job->state == JobState::EXTRACTING_SEGMENT) {
          ImGui::Text("Prep: %d%%", pct);
        } else if (job->state == JobState::ENCODING_SEGMENT) {
          ImGui::Text("Encode: %d%%", pct);
        } else if (job->state == JobState::VMAFFING_SEGMENT) {
          ImGui::Text("VMAF: %d%%", pct);
        } else {
          ImGui::Text("Processing...");
        }
      } else {
        ImGui::Text("-");
      }

      ImGui::TableNextColumn();
      bool showTarget = (job->searchActive || job->iteration > 0);

      if (job->finalVMAF > 0.0f) {
        if (showTarget)
          ImGui::Text("%.2f (Target: %d)", job->finalVMAF,
                      job->profile.targetVmaf);
        else
          ImGui::Text("%.2f", job->finalVMAF);
      } else if (!job->segmentVMAFs.empty()) {
        float sum = std::accumulate(job->segmentVMAFs.begin(),
                                    job->segmentVMAFs.end(), 0.0f);
        float avg = sum / job->segmentVMAFs.size();
        if (showTarget)
          ImGui::TextDisabled("Avg: %.2f (Target: %d)", avg,
                              job->profile.targetVmaf);
        else
          ImGui::TextDisabled("Avg: %.2f", avg);
      } else {
        if (showTarget)
          ImGui::Text("- (Target: %d)", job->profile.targetVmaf);
        else
          ImGui::Text("-");
      }

      ImGui::TableNextColumn();
      if (job->avgBitrate > 0.0f) {
        ImGui::Text("%.1f", job->avgBitrate);
      } else if (!job->segmentBitrates.empty()) {
        ImGui::TextDisabled("Curr: %.1f", job->segmentBitrates.back());
      } else {
        ImGui::Text("%.1f", prog.bitrate);
      }

      ImGui::TableNextColumn();
      if (job->estimatedFullSize > 0.0f) {
        if (job->estimatedFullSize >= 1024.0f) {
          ImGui::Text("%.2f GB", job->estimatedFullSize / 1024.0f);
        } else {
          ImGui::Text("%.1f MB", job->estimatedFullSize);
        }
      } else {
        ImGui::Text("-");
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  // Append any newly spawned iteration jobs
  if (!m_pendingJobs.empty()) {
    for (auto &j : m_pendingJobs) {
      m_jobs.push_back(j);
    }
    m_pendingJobs.clear();
  }
}

void UIManager::DrawResults() {
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Benchmark Results");

  if (m_jobs.empty()) {
    ImGui::Text("Run a benchmark to see results here.");
    return;
  }

  if (ImGui::BeginTable("ResultsTable", 3,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Sortable)) {
    ImGui::TableSetupColumn("Profile");
    ImGui::TableSetupColumn("Bitrate (kbps)");
    ImGui::TableSetupColumn("VMAF Score");
    ImGui::TableHeadersRow();

    for (const auto &job : m_jobs) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%s", job->profile.name.c_str());

      float bitrate = job->runner->GetProgress().bitrate;
      if (bitrate > 0.0f)
        job->avgBitrate = bitrate; // Very simple average for now

      ImGui::TableNextColumn();
      ImGui::Text("%.1f kbps", job->avgBitrate);

      float vmaf = job->runner->GetVMAFScore();
      if (vmaf > 0.0f)
        job->finalVMAF = vmaf;

      ImGui::TableNextColumn();
      if (job->finalVMAF > 0.0f) {
        ImGui::Text("%.2f", job->finalVMAF);
      } else {
        ImGui::TextDisabled("N/A");
      }
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Plotting using ImPlot
  if (ImPlot::BeginPlot("Quality vs Bitrate", ImVec2(-1, 300))) {
    ImPlot::SetupAxes("Bitrate (kbps)", "VMAF Score");

    std::vector<double> bitrates;
    std::vector<double> vmafs;

    for (const auto &job : m_jobs) {
      if (job->finalVMAF > 0.0f && job->avgBitrate > 0.0f) {
        bitrates.push_back(job->avgBitrate);
        vmafs.push_back(job->finalVMAF);
      }
    }

    if (!bitrates.empty()) {
      ImPlot::PlotScatter("Results", bitrates.data(), vmafs.data(),
                          bitrates.size());
    }

    ImPlot::EndPlot();
  }
}
