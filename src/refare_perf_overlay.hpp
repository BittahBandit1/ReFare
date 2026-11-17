#pragma once
// Adapted from Turbulence's turbulence_options.cpp (same project license).
#include "Core/System.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoBackendBase.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>
namespace refare::perf {
// FPS counter: frame rate plus the interval between new game frames, as the
// renderer presented them (duplicate presents of an unchanged frame are folded
// into the next interval, so a dropped frame shows as a ~33 ms spike).
constexpr const char* kPerfLabels[] = {"Off", "On", "On + frame times"};
class FrameIntervals {
public:
  static constexpr int kSize = 240;  // 4 seconds at 60 fps
  void Record(const PresentInfo& info) {
    if (info.reason == PresentInfo::PresentReason::VideoInterfaceDuplicate) return;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard guard(m_lock);
    if (m_have_last) {
      const float ms = std::chrono::duration<float, std::milli>(now - m_last).count();
      // A pause or load screen gap is not a frame interval.
      if (ms < 250.0f) { m_ring[m_next] = ms; m_next = (m_next + 1) % kSize; m_count = std::min(m_count + 1, kSize); }
    }
    m_last = now; m_have_last = true;
  }
  // Oldest first.
  int Copy(std::array<float, kSize>& out) const {
    std::lock_guard guard(m_lock);
    for (int i = 0; i < m_count; ++i) out[i] = m_ring[(m_next - m_count + i + kSize) % kSize];
    return m_count;
  }
private:
  mutable std::mutex m_lock;
  std::array<float, kSize> m_ring{};
  int m_next = 0, m_count = 0;
  bool m_have_last = false;
  std::chrono::steady_clock::time_point m_last{};
};

void DrawPerfOverlay(const FrameIntervals& intervals, int mode) {
  struct Colors {
    ImVec4 window{.055f,.065f,.08f,1}, separator{1,1,1,.3f},
      accent{1,.8f,.125f,1}, title{1,.8f,.125f,1}, danger{1,.25f,.15f,1},
      text{1,1,1,1}, text_dim{.7f,.75f,.8f,1};
  } p;
  std::array<float, FrameIntervals::kSize> data{};
  const int n = intervals.Copy(data);
  // Last second of intervals for the numbers; the graph shows all of them.
  float sum = 0, low = 1e9f, high = 0;
  const int recent = std::min(n, 60);
  for (int i = n - recent; i < n; ++i) { sum += data[i]; low = std::min(low, data[i]); high = std::max(high, data[i]); }
  const float avg = recent ? sum / recent : 0.0f;
  int over = 0;  // intervals longer than 1.5 frames (visible hitches) in the window
  for (int i = 0; i < n; ++i) if (data[i] > 25.0f) ++over;
  const auto& metrics = Core::System::GetInstance().GetPerfMetrics();
  const double fps = metrics.GetFPS(), vps = metrics.GetVPS(), speed = metrics.GetSpeed();
  const float scale = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 900.0f);
  ImGui::SetNextWindowPos(ImVec2(8 * scale, 8 * scale), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.72f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * scale, 8 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, p.window);
  ImGui::PushStyleColor(ImGuiCol_Border, p.separator);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.35f));
  ImGui::PushStyleColor(ImGuiCol_PlotLines, p.accent);
  ImGui::Begin("ReFare FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
               ImGuiWindowFlags_NoNav);
  ImGui::SetWindowFontScale(scale);
  const ImVec4 fps_color = fps >= 59.0 ? ImVec4(0.45f, 0.95f, 0.45f, 1) : fps >= 50.0 ? p.title : p.danger;
  ImGui::TextColored(fps_color, "%.1f FPS", fps);
  ImGui::SameLine(); ImGui::TextColored(p.text_dim, "%.0f%% speed", speed * 100.0);
  // The video backend actually in use (Vulkan, Direct3D 11, Metal...), to
  // confirm a backend change took effect.
  if (g_video_backend) {
    ImGui::SameLine(); ImGui::TextColored(p.text_dim, "%s", g_video_backend->GetDisplayName().c_str());
  }
  if (mode >= 2) {
    ImGui::TextColored(p.text, "Frame %.2f ms  (min %.2f / max %.2f)", avg, recent ? low : 0.0f, high);
    ImGui::TextColored(over ? p.title : p.text_dim, "VPS %.1f   hitches >25 ms: %d / %ds", vps, over, FrameIntervals::kSize / 60);
    char label[32];
    std::snprintf(label, sizeof(label), "%.1f ms", n ? data[n - 1] : 0.0f);
    const ImVec2 size(std::max(240 * scale, ImGui::GetContentRegionAvail().x), 48 * scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PlotLines("##intervals", data.data(), n, 0, label, 0.0f, 50.0f, size);
    // 16.7 ms reference line (the plot's range is 0-50 ms, drawn bottom up).
    const float y = origin.y + size.y * (1.0f - 16.667f / 50.0f);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
                                        ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f)));
  }
  ImGui::End();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(3);
}

}
