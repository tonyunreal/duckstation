#pragma once
#include "common/gl/program.h"
#include "common/gl/texture.h"
#include "core/host_display.h"
#include "core/host_interface.h"
#include "frontend-common/common_host_interface.h"
#include <SDL.h>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class AudioStream;

class INISettingsInterface;

struct GameListEntry;

class SDLHostInterface final : public CommonHostInterface
{
public:
  SDLHostInterface();
  ~SDLHostInterface();

  static std::unique_ptr<SDLHostInterface> Create();

  const char* GetFrontendName() const override;

  void ReportError(const char* message) override;
  void ReportMessage(const char* message) override;
  bool ConfirmMessage(const char* message) override;

  bool Initialize() override;
  void Shutdown() override;

  std::string GetStringSettingValue(const char* section, const char* key, const char* default_value = "") override;
  bool GetBoolSettingValue(const char* section, const char* key, bool default_value = false) override;
  int GetIntSettingValue(const char* section, const char* key, int default_value = 0) override;
  float GetFloatSettingValue(const char* section, const char* key, float default_value = 0.0f) override;

  bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) override;

  void Run();

protected:
  void LoadSettings() override;

  bool AcquireHostDisplay() override;
  void ReleaseHostDisplay() override;

  void OnSystemCreated() override;
  void OnSystemPaused(bool paused) override;
  void OnSystemDestroyed() override;
  void OnRunningGameChanged() override;

  void RequestExit() override;
  void PollAndUpdate() override;

  std::optional<HostKeyCode> GetHostKeyCode(const std::string_view key_code) const override;
  void UpdateInputMap() override;

private:
  enum class MainWindowType
  {
    None,
    Landing,
    GameList,
    Settings,
    Pause,
    LoadState,
  };

  enum class SettingsPage
  {
    InterfaceSettings,
    GameListSettings,
    ConsoleSettings,
    ControllerSettings,
    HotkeySettings,
    MemoryCardSettings,
    DisplaySettings,
    EnhancementSettings,
    AudioSettings,
    AdvancedSettings,
    Count
  };

  bool CreateSDLWindow();
  void DestroySDLWindow();
  bool CreateDisplay();
  void DestroyDisplay();
  void CreateImGuiContext();
  void UpdateFramebufferScale();

  /// Executes a callback later, after the UI has finished rendering. Needed to boot while rendering ImGui.
  void RunLater(std::function<void()> callback);

  void SaveAndUpdateSettings();

  bool IsFullscreen() const override;
  bool SetFullscreen(bool enabled) override;

  // We only pass mouse input through if it's grabbed
  void DrawImGuiWindows() override;
  void DoStartDisc();
  void DoChangeDisc();
  void DoDumpRAM();

  void HandleSDLEvent(const SDL_Event* event);
  void ProcessEvents();

  void DrawMainMenuBar();
  void DrawQuickSettingsMenu();
  void DrawDebugMenu();
  void DrawOldSettingsWindow();
  void DrawAboutWindow();
  bool DrawFileChooser(const char* label, std::string* path, const char* filter = nullptr);
  void ClearImGuiFocus();

  bool LoadResources();
  void DestroyResources();
  std::unique_ptr<HostDisplayTexture> LoadTextureResource(const char* name);

  void DrawMainWindow();
  void ReturnToMainWindow();

  void DrawLandingWindow();
  void DrawSettingsWindow();
  void DrawPauseWindow();

  SDL_Window* m_window = nullptr;
  std::unique_ptr<INISettingsInterface> m_settings_interface;
  u32 m_run_later_event_id = 0;

  MainWindowType m_current_main_window = MainWindowType::Landing;
  SettingsPage m_settings_page = SettingsPage::InterfaceSettings;

  bool m_fullscreen = false;
  bool m_quit_request = false;
  bool m_settings_window_open = false;
  bool m_about_window_open = false;

  // this copy of the settings is modified by imgui
  Settings m_settings_copy;

  //////////////////////////////////////////////////////////////////////////
  // Resources
  //////////////////////////////////////////////////////////////////////////
  std::unique_ptr<HostDisplayTexture> m_app_icon_texture;
  std::unique_ptr<HostDisplayTexture> m_placeholder_texture;
  std::array<std::unique_ptr<HostDisplayTexture>, static_cast<u32>(DiscRegion::Count)> m_disc_region_textures;

  //////////////////////////////////////////////////////////////////////////
  // Save State List
  //////////////////////////////////////////////////////////////////////////
  struct SaveStateListEntry
  {
    std::string title;
    std::string summary;
    std::string path;
    std::unique_ptr<HostDisplayTexture> preview_texture;
    s32 slot;
    bool global;
  };

  void InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot, bool global);
  void InitializeSaveStateListEntry(SaveStateListEntry* li, CommonHostInterface::ExtendedSaveStateInfo* ssi);
  void PopulateSaveStateListEntries();
  void ClearSaveStateListEntries();
  void DrawSaveStateSelector(bool is_loading);

  std::vector<SaveStateListEntry> m_save_state_selector_slots;

  //////////////////////////////////////////////////////////////////////////
  // Game List
  //////////////////////////////////////////////////////////////////////////
  void DrawGameListWindow();
  void LoadGameList();
  void SwitchToGameList();
  HostDisplayTexture* GetGameListCover(const GameListEntry* entry);
  HostDisplayTexture* GetCoverForCurrentGame();

  // Lazily populated cover images.
  std::unordered_map<std::string, std::unique_ptr<HostDisplayTexture>> m_cover_image_map;
  bool m_game_list_loaded = false;
};
