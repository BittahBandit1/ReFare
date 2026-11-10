// ReFare's desktop pre-game window. SDL/ImGui keeps the same UI on Windows
// and Linux; the companion Python service owns preparation and subprocesses.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <picojson.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef REFARE_LAUNCHER_COMPILER
#define REFARE_LAUNCHER_COMPILER "clang"
#endif

namespace fs = std::filesystem;
namespace {
fs::path FromUTF8(std::string_view value) {
  return fs::path(std::u8string(value.begin(), value.end()));
}
std::string Text(const fs::path& path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}
std::string FileURI(const fs::path& path) {
  const auto raw = path.generic_u8string();
#ifdef _WIN32
  std::string uri = "file:///";
#else
  std::string uri = "file://";
#endif
  static constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char c : raw) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') uri += char(c);
    else { uri += '%'; uri += hex[c >> 4]; uri += hex[c & 15]; }
  }
  return uri;
}
enum class Phase { Disc, Working, Ready, Playing, Failed, Tools };
struct View {
  Phase phase = Phase::Working;
  std::string message = "Checking your game…";
  std::string driver = "System";
  std::vector<std::string> log, missing;
};
struct State {
  std::mutex mutex;
  View view;
  std::atomic<bool> busy{false};
  View Snapshot() { std::lock_guard lock(mutex); return view; }
  void Set(Phase phase, std::string message) {
    std::lock_guard lock(mutex); view.phase = phase; view.message = std::move(message);
  }
  void Line(const std::string& line, bool playing) {
    picojson::value value;
    const auto error = picojson::parse(value, line);
    std::lock_guard lock(mutex);
    if (!error.empty() || !value.is<picojson::object>()) {
      view.log.push_back(line);
    } else {
      const auto& object = value.get<picojson::object>();
      const auto string = [&](const char* key) {
        const auto it = object.find(key);
        return it != object.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string{};
      };
      const auto event = string("event"), text = string("text");
      if (event == "log") view.log.push_back(text);
      else if (event == "driver") view.driver = text;
      else if (event == "status") { view.phase = playing ? Phase::Playing : Phase::Working; view.message = text; }
      else if (event == "ready") { view.phase = Phase::Ready; view.message = text; }
      else if (event == "needs_disc") { view.phase = Phase::Disc; view.message.clear(); }
      else if (event == "error") { view.phase = Phase::Failed; view.message = text; view.log.push_back(text); }
      else if (event == "missing_tools") {
        view.phase = Phase::Tools; view.message = text; view.missing.clear();
        const auto found = object.find("missing");
        if (found != object.end() && found->second.is<picojson::array>())
          for (const auto& item : found->second.get<picojson::array>())
            if (item.is<std::string>()) view.missing.push_back(item.get<std::string>());
      }
    }
    if (view.log.size() > 400) view.log.erase(view.log.begin(), view.log.end() - 400);
  }
};

SDL_Process* Process(const std::vector<std::string>& command) {
  std::vector<const char*> args;
  for (const auto& arg : command) args.push_back(arg.c_str());
  args.push_back(nullptr);
  const auto properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
#ifdef _WIN32
  // Hide console windows. Completion/error events come from the service;
  // SDL intentionally does not return exit codes for background processes.
  SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
  auto* child = SDL_CreateProcessWithProperties(properties);
  SDL_DestroyProperties(properties);
  return child;
}

std::vector<std::string> FindPython() {
#ifdef _WIN32
  const std::vector<std::vector<std::string>> candidates{{"py", "-3"}, {"python"}, {"python3"}};
  const char delimiter = ';';
#else
  const std::vector<std::vector<std::string>> candidates{{"python3"}};
  const char delimiter = ':';
#endif
  const char* environment = SDL_getenv("PATH");
  const std::string paths = environment ? environment : "";
  for (auto candidate : candidates) {
    // Resolve the executable explicitly, skipping Windows' Store-launching
    // aliases. Missing Python should show instructions, not open the Store.
    bool found = false;
    for (size_t start = 0; start <= paths.size();) {
      const auto end = paths.find(delimiter, start);
      auto directory = paths.substr(start, end == std::string::npos ? end : end - start);
      if (directory.size() > 1 && directory.front() == '"' && directory.back() == '"')
        directory = directory.substr(1, directory.size() - 2);
#ifdef _WIN32
      auto lower = directory;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(SDL_tolower(c)); });
      if (lower.find("windowsapps") == std::string::npos) {
        const auto path = FromUTF8(directory) / (candidate[0] + ".exe");
#else
      {
        const auto path = FromUTF8(directory) / candidate[0];
#endif
        std::error_code error;
        if (!directory.empty() && fs::is_regular_file(path, error)) { candidate[0] = Text(path); found = true; break; }
      }
      if (end == std::string::npos) break;
      start = end + 1;
    }
    if (!found) continue;
    auto probe = candidate;
    probe.push_back("--version");
    auto* child = Process(probe);
    if (!child) continue;
    int code = -1;
    SDL_WaitProcess(child, true, &code);
    size_t size = 0;
    char* output = static_cast<char*>(SDL_ReadProcess(child, &size, &code));
    const std::string version = output ? std::string(output, size) : "";
    SDL_free(output); SDL_DestroyProcess(child);
    if (code == 0 && version.starts_with("Python 3.")) return candidate;
  }
  return {};
}

class Service {
public:
  State state;
  fs::path tools, support, resources, source;
  std::thread worker;
  std::optional<fs::path> pending_disc;
  ~Service() { if (worker.joinable()) worker.join(); }
  void Start(const std::string& action, std::optional<fs::path> disc = {}) {
    if (state.busy.exchange(true)) return;
    if (worker.joinable()) worker.join();
    if (disc && action == "install") pending_disc = disc;
    state.Set(action == "play" ? Phase::Playing : Phase::Working,
              action == "play" ? "Starting Crazy Taxi…" : "Checking your game…");
    worker = std::thread([this, action, disc] {
      const auto finish = [this] {
        if (state.Snapshot().phase == Phase::Ready) pending_disc.reset();
        state.busy.store(false);
      };
      try {
        auto command = FindPython();
        if (command.empty()) {
          { std::lock_guard lock(state.mutex);
            state.view.phase = Phase::Tools; state.view.message = "Python 3 is needed to prepare and launch your game.";
            state.view.missing = {"Python 3 (add it to PATH during installation)"}; }
          finish(); return;
        }
        command.insert(command.end(), {"-u", Text(resources / "launcher_backend.py"), action == "driver-system" ? "driver" : action,
          "--support-dir", Text(support), "--tools-dir", Text(tools), "--source-dir", Text(source),
          "--compiler", REFARE_LAUNCHER_COMPILER});
        if (disc) command.insert(command.end(), {action == "driver" ? "--driver-json" : "--disc", Text(*disc)});
        if (action == "driver-system") command.push_back("--system-driver");
        auto* child = Process(command);
        if (!child) { state.Set(Phase::Failed, std::string("Could not start preparation: ") + SDL_GetError()); finish(); return; }
        auto* output = SDL_GetProcessOutput(child);
        std::string pending;
        std::error_code directory_error;
        fs::create_directories(support, directory_error);
        SDL_IOStream* log = SDL_IOFromFile(Text(support / "launcher.log").c_str(), "a");
        const auto read = [&] {
          char bytes[8192];
          size_t size;
          while ((size = SDL_ReadIO(output, bytes, sizeof(bytes))) > 0) {
            if (log) SDL_WriteIO(log, bytes, size);
            pending.append(bytes, size);
            size_t end;
            while ((end = pending.find('\n')) != std::string::npos) {
              state.Line(pending.substr(0, end), action == "play"); pending.erase(0, end + 1);
            }
            // A broken tool must not grow memory without bound on one line.
            if (pending.size() > 65536) { state.Line(pending.substr(0, 65536), action == "play"); pending.clear(); }
          }
        };
        int code = -1;
        while (!SDL_WaitProcess(child, false, &code)) { read(); SDL_Delay(15); }
        read();
        if (!pending.empty()) state.Line(pending, action == "play");
        if (log) SDL_CloseIO(log);
        SDL_DestroyProcess(child);
        const auto view = state.Snapshot();
        if (view.phase == Phase::Working || view.phase == Phase::Playing ||
            (code != 0 && view.phase != Phase::Failed))
          state.Set(Phase::Failed, "The operation did not finish. Open the log below for details.");
      } catch (const std::exception& error) { state.Set(Phase::Failed, error.what()); }
      finish();
    });
  }
};

struct Dialog {
  std::mutex mutex;
  bool open = false;
  bool driver = false;
  std::optional<std::string> selected, error;
};
void Choose(SDL_Window* window, const std::shared_ptr<Dialog>& dialog, bool driver = false) {
  { std::lock_guard lock(dialog->mutex); if (dialog->open) return; dialog->open = true; dialog->driver = driver; }
  static const SDL_DialogFileFilter filters[] = {{"GameCube disc image", "iso;ciso"}};
  static const SDL_DialogFileFilter driver_filters[] = {{"Vulkan driver manifest / package", "json;zip"}, {"All files", "*"}};
  SDL_ShowOpenFileDialog([](void* data, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Dialog>> handle(static_cast<std::shared_ptr<Dialog>*>(data));
    std::lock_guard lock((*handle)->mutex);
    (*handle)->open = false;
    if (!files) {
      const char* error = SDL_GetError();
      (*handle)->error = error && *error ? error : "Could not open the file chooser. You can also drag a disc image onto the window.";
    }
    else if (files[0]) (*handle)->selected = files[0];
  }, new std::shared_ptr<Dialog>(dialog), window, driver ? driver_filters : filters,
     driver ? 2 : 1, nullptr, false);
}

void CenterText(const char* text, float scale = 1.0f) {
  ImGui::SetWindowFontScale(scale);
  const float width = ImGui::CalcTextSize(text).x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
  ImGui::TextUnformatted(text);
  ImGui::SetWindowFontScale(1.0f);
}
bool CenterButton(const char* label, float width = 220, float height = 38) {
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
  return ImGui::Button(label, {width, height});
}

void Draw(Service& service, SDL_Window* window, const std::shared_ptr<Dialog>& dialog, bool& show_log) {
  const auto view = service.state.Snapshot();
  ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  ImGui::Begin("ReFare", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  ImGui::Dummy({0, 8}); CenterText("ReFare", 2.3f);
#ifdef _WIN32
  CenterText("Crazy Taxi, recompiled for Windows");
#elif defined(__linux__)
  CenterText("Crazy Taxi, recompiled for Linux");
#else
  CenterText("Crazy Taxi, recompiled for your computer");
#endif
  ImGui::Dummy({0, 18});
#ifdef __linux__
  const float footer = show_log ? 202.0f : 94.0f;
#else
  const float footer = show_log ? 160.0f : 52.0f;
#endif
  ImGui::BeginChild("Main", {0, -footer}, ImGuiChildFlags_Borders);
  ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 22);
  ImGui::Dummy({0, 22});
  ImGui::BeginDisabled(service.state.busy.load());
  switch (view.phase) {
  case Phase::Disc:
    CenterText("Drop your Crazy Taxi (USA) disc image here"); ImGui::Spacing();
    CenterText(".iso or .ciso of the GameCube disc (GCTE51)");
    ImGui::Dummy({0, 18});
    if (CenterButton("Choose File…")) Choose(window, dialog);
    break;
  case Phase::Ready:
    CenterText("Crazy Taxi is ready to play."); ImGui::Dummy({0, 14});
    if (CenterButton("Play", 220, 48) || ImGui::IsKeyPressed(ImGuiKey_Enter)) service.Start("play");
    ImGui::Spacing();
    if (CenterButton("Use a different disc…")) service.state.Set(Phase::Disc, "");
    if (CenterButton("Show game files")) {
      const auto uri = FileURI(service.support);
      SDL_OpenURL(uri.c_str());
    }
    break;
  case Phase::Working: case Phase::Playing:
    ImGui::TextWrapped("%s", view.message.c_str());
    ImGui::Dummy({0, 16});
    ImGui::ProgressBar(-float(ImGui::GetTime()), {-1, 6}, "");
    break;
  case Phase::Failed:
    ImGui::TextWrapped("%s", view.message.c_str()); ImGui::Dummy({0, 12});
    if (CenterButton("Try another disc image…")) service.state.Set(Phase::Disc, "");
    if (CenterButton("Back")) service.Start("status");
    break;
  case Phase::Tools:
    ImGui::TextWrapped("A few free developer tools are needed to prepare the game:");
    for (const auto& missing : view.missing) ImGui::BulletText("%s", missing.c_str());
#ifdef _WIN32
    ImGui::TextWrapped("Install Python 3, CMake and Ninja. MSVC builds also need Visual Studio 2022 Build Tools with Desktop development with C++.");
#else
    ImGui::TextWrapped("Install Python 3, CMake, Ninja and a C compiler using your package manager.");
#endif
    if (CenterButton("Check Again")) service.Start(service.pending_disc ? "install" : "check", service.pending_disc);
    if (CenterButton("Back")) service.Start("status");
    break;
  }
  ImGui::EndDisabled();
  ImGui::PopTextWrapPos(); ImGui::EndChild();
#ifdef __linux__
  ImGui::BeginDisabled(service.state.busy.load());
  if (ImGui::Button("Vulkan driver…")) ImGui::OpenPopup("Choose Vulkan driver");
  ImGui::SameLine(); ImGui::TextDisabled("%.40s", view.driver.c_str());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", view.driver.c_str());
  if (ImGui::BeginPopupModal("Choose Vulkan driver", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Choose the driver used next time you press Play.");
    ImGui::Text("Current: %s", view.driver.c_str());
    ImGui::Spacing();
    if (ImGui::Button("System driver", {220, 36})) {
      service.Start("driver-system"); ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Choose Linux driver…", {220, 36})) {
      Choose(window, dialog, true); ImGui::CloseCurrentPopup();
    }
    ImGui::TextUnformatted("Select a Linux Vulkan ICD .json or Linux driver ZIP.");
    ImGui::TextUnformatted("Android driver ZIPs are not compatible with native Linux.");
    if (ImGui::Button("Back", {220, 36})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::EndDisabled();
#endif
  static Phase last_phase = Phase::Working;
  if (view.phase == Phase::Failed && last_phase != Phase::Failed) ImGui::SetNextItemOpen(true);
  last_phase = view.phase;
  show_log = ImGui::CollapsingHeader("Log");
  if (show_log) {
    if (ImGui::Button("Copy log")) {
      std::string lines;
      for (const auto& line : view.log) lines += line + '\n';
      SDL_SetClipboardText(lines.c_str());
    }
    ImGui::SameLine(); ImGui::TextDisabled("Saved in launcher.log");
    ImGui::BeginChild("Output", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2;
    for (const auto& line : view.log) ImGui::TextUnformatted(line.c_str());
    if (follow) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
  }
  ImGui::End();
}
} // namespace

int main(int argc, char** argv) {
  Service service;
  bool smoke = false, probe = false;
  std::string screenshot;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke-test") smoke = true;
    else if (arg == "--probe-service") { smoke = true; probe = true; }
    else if (arg == "--support-dir" && i + 1 < argc) service.support = FromUTF8(argv[++i]);
    else if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else return 2;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  service.tools = FromUTF8(SDL_GetBasePath());
  service.resources = service.tools / "refare-data";
  service.source = service.resources / "toolchain";
  if (service.support.empty()) {
#ifdef _WIN32
    if (const char* local = SDL_getenv("LOCALAPPDATA")) service.support = FromUTF8(local) / "ReFare";
#endif
  }
  if (service.support.empty()) {
    char* path = SDL_GetPrefPath("", "ReFare");
    if (!path) { SDL_Quit(); return 1; }
    service.support = FromUTF8(path); SDL_free(path);
  }
  service.support = fs::absolute(service.support);
  SDL_Rect display{};
  int width = 600, height = 560;
  if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &display)) {
    width = std::min(width, std::max(320, display.w - 24));
    height = std::min(height, std::max(360, display.h - 40));
  }
  SDL_Window* window = SDL_CreateWindow("ReFare", width, height, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  if (!renderer) { if (window) SDL_DestroyWindow(window); SDL_Quit(); return 1; }
  SDL_SetWindowMinimumSize(window, std::min(width,480), std::min(height,440));
  SDL_SetRenderVSync(renderer, 1);
  IMGUI_CHECKVERSION(); ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  std::vector<fs::path> fonts;
#ifdef _WIN32
  if (const char* windows = SDL_getenv("WINDIR")) fonts.push_back(FromUTF8(windows) / "Fonts/segoeui.ttf");
#elif defined(__linux__)
  fonts = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
           "/usr/share/fonts/TTF/DejaVuSans.ttf",
           "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"};
#else
  fonts = {"/System/Library/Fonts/Supplemental/Arial.ttf"};
#endif
  fonts.push_back(service.tools / "Sys/Resources/OSD_Font.ttf");
  for (const auto& font : fonts) {
    std::error_code error;
    if (fs::is_regular_file(font, error) &&
        ImGui::GetIO().Fonts->AddFontFromFileTTF(Text(font).c_str(), 17.0f)) break;
  }
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.WindowPadding = {22, 18}; style.ItemSpacing = {10, 10};
  style.FramePadding = {12, 8}; style.FrameRounding = 7; style.ChildRounding = 12;
  style.Colors[ImGuiCol_Button] = {0.74f, 0.43f, 0.06f, 1};
  style.Colors[ImGuiCol_ButtonHovered] = {0.93f, 0.59f, 0.09f, 1};
  style.Colors[ImGuiCol_ButtonActive] = {0.61f, 0.34f, 0.04f, 1};
  style.Colors[ImGuiCol_Header] = {0.18f, 0.17f, 0.15f, 1};
  style.Colors[ImGuiCol_HeaderHovered] = {0.28f, 0.23f, 0.15f, 1};
  style.Colors[ImGuiCol_HeaderActive] = {0.36f, 0.27f, 0.13f, 1};
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer); ImGui_ImplSDLRenderer3_Init(renderer);
  auto dialog = std::make_shared<Dialog>();
  if (smoke && !probe) service.state.Set(Phase::Disc, ""); else service.Start("status");
  bool quit = false, show_log = false;
  int frames = 0, result = 0;
  while (!quit) {
    SDL_Event event;
    bool close_requested = false;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) close_requested = true;
      if (event.type == SDL_EVENT_DROP_FILE && event.drop.data && !service.state.busy)
        service.Start("install", FromUTF8(event.drop.data));
    }
    if (close_requested) {
      bool choosing;
      { std::lock_guard lock(dialog->mutex); choosing = dialog->open; }
      if (!service.state.busy && !choosing) quit = true;
      else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "ReFare",
             "Wait for preparation to finish, close the file chooser, or quit the game first.", window);
    }
    {
      std::lock_guard lock(dialog->mutex);
      if (dialog->selected && !service.state.busy) {
        service.Start(dialog->driver ? "driver" : "install", FromUTF8(*dialog->selected)); dialog->selected.reset();
      }
      if (dialog->error) { service.state.Set(Phase::Failed, *dialog->error); dialog->error.reset(); }
    }
    ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
    Draw(service, window, dialog, show_log);
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 24, 24, 26, 255); SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    if (smoke && (!probe || !service.state.busy.load()) && ++frames == 4) {
      result = ImGui::GetDrawData()->TotalVtxCount > 0 ? 0 : 1;
      const auto phase = service.state.Snapshot().phase;
      if (probe && phase != Phase::Disc && phase != Phase::Ready) result = 1;
      if (!screenshot.empty()) {
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
        if (!surface || !SDL_SaveBMP(surface, screenshot.c_str())) result = 1;
        if (surface) SDL_DestroySurface(surface);
      }
      quit = true;
    }
    SDL_RenderPresent(renderer); SDL_Delay(10);
  }
  if (service.worker.joinable()) service.worker.join();
  ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
  return result;
}
