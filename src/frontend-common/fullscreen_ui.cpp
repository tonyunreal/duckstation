#define IMGUI_DEFINE_MATH_OPERATORS

#include "fullscreen_ui.h"
#include "IconsFontAwesome5.h"
#include "IconsKenney.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/make_array.h"
#include "common/string.h"
#include "common/string_util.h"
#include "common_host_interface.h"
#include "core/cheats.h"
#include "core/cpu_core.h"
#include "core/gpu.h"
#include "core/host_display.h"
#include "core/host_interface_progress_callback.h"
#include "core/resources.h"
#include "core/settings.h"
#include "core/system.h"
#include "fullscreen_ui_progress_callback.h"
#include "game_list.h"
#include "icon.h"
#include "imgui.h"
#include "imgui_fullscreen.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "imgui_styles.h"
#include "scmversion/scmversion.h"
#include <thread>
Log_SetChannel(FullscreenUI);

static constexpr float LAYOUT_MAIN_MENU_BAR_SIZE = 20.0f; // Should be DPI scaled, not layout scaled!

using ImGuiFullscreen::g_large_font;
using ImGuiFullscreen::g_medium_font;
using ImGuiFullscreen::LAYOUT_LARGE_FONT_SIZE;
using ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE;
using ImGuiFullscreen::LAYOUT_SCREEN_HEIGHT;
using ImGuiFullscreen::LAYOUT_SCREEN_WIDTH;

using ImGuiFullscreen::ActiveButton;
using ImGuiFullscreen::BeginFullscreenColumns;
using ImGuiFullscreen::BeginFullscreenColumnWindow;
using ImGuiFullscreen::BeginFullscreenWindow;
using ImGuiFullscreen::BeginMenuButtons;
using ImGuiFullscreen::CloseChoiceDialog;
using ImGuiFullscreen::CloseFileSelector;
using ImGuiFullscreen::DPIScale;
using ImGuiFullscreen::EndFullscreenColumns;
using ImGuiFullscreen::EndFullscreenColumnWindow;
using ImGuiFullscreen::EndFullscreenWindow;
using ImGuiFullscreen::EndMenuButtons;
using ImGuiFullscreen::EnumChoiceButton;
using ImGuiFullscreen::LayoutScale;
using ImGuiFullscreen::MenuButton;
using ImGuiFullscreen::MenuButtonWithValue;
using ImGuiFullscreen::MenuHeading;
using ImGuiFullscreen::MenuImageButton;
using ImGuiFullscreen::OpenChoiceDialog;
using ImGuiFullscreen::OpenFileSelector;
using ImGuiFullscreen::RangeButton;
using ImGuiFullscreen::ToggleButton;

namespace FullscreenUI {

//////////////////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////////////////
static void ClearImGuiFocus();
static void ReturnToMainWindow();
static void DrawLandingWindow();
static void DrawSettingsWindow();
static void DrawQuickMenu();
static void DrawDebugMenu();

static CommonHostInterface* s_host_interface;
static SettingsInterface* s_settings_interface;
static MainWindowType s_current_main_window = MainWindowType::Landing;
static SettingsPage s_settings_page = SettingsPage::InterfaceSettings;
static Settings s_settings_copy;
static bool s_debug_menu_enabled;

//////////////////////////////////////////////////////////////////////////
// Resources
//////////////////////////////////////////////////////////////////////////
static std::unique_ptr<HostDisplayTexture> LoadTextureResource(const char* name);
static bool LoadResources();
static void DestroyResources();

std::unique_ptr<HostDisplayTexture> s_app_icon_texture;
std::unique_ptr<HostDisplayTexture> s_placeholder_texture;
std::array<std::unique_ptr<HostDisplayTexture>, static_cast<u32>(DiscRegion::Count)> s_disc_region_textures;
std::array<std::unique_ptr<HostDisplayTexture>, static_cast<u32>(GameListCompatibilityRating::Count)>
  s_game_compatibility_textures;

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

static void InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot, bool global);
static void InitializeSaveStateListEntry(SaveStateListEntry* li, CommonHostInterface::ExtendedSaveStateInfo* ssi);
static void PopulateSaveStateListEntries();
static void ClearSaveStateListEntries();
static void DrawSaveStateSelector(bool is_loading);

static std::vector<SaveStateListEntry> s_save_state_selector_slots;

//////////////////////////////////////////////////////////////////////////
// Game List
//////////////////////////////////////////////////////////////////////////
static void DrawGameListWindow();
static void SwitchToGameList();
static void QueueGameListRefresh();
static HostDisplayTexture* GetGameListCover(const GameListEntry* entry);
static HostDisplayTexture* GetCoverForCurrentGame();

// Lazily populated cover images.
static std::unordered_map<std::string, std::unique_ptr<HostDisplayTexture>> s_cover_image_map;
static std::thread s_game_list_load_thread;

//////////////////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////////////////

bool Initialize(CommonHostInterface* host_interface, SettingsInterface* settings_interface)
{
  s_host_interface = host_interface;
  s_settings_interface = settings_interface;
  if (!LoadResources())
    return false;

  s_settings_copy.Load(*settings_interface);
  SetDebugMenuEnabled(settings_interface->GetBoolValue("Main", "ShowDebugMenu", false));
  QueueGameListRefresh();
  return true;
}

void SystemCreated()
{
  s_current_main_window = MainWindowType::None;
  ClearImGuiFocus();
}

void SystemDestroyed()
{
  s_current_main_window = MainWindowType::Landing;
  ClearImGuiFocus();
}

void SystemPaused(bool paused)
{
  if (paused)
    OpenQuickMenu();
  else
    CloseQuickMenu();
}

void OpenQuickMenu()
{
  s_current_main_window = MainWindowType::QuickMenu;
  ClearImGuiFocus();
}

void CloseQuickMenu()
{
  s_current_main_window = MainWindowType::None;
  ClearImGuiFocus();
}

void Shutdown()
{
  if (s_game_list_load_thread.joinable())
    s_game_list_load_thread.join();

  s_save_state_selector_slots.clear();
  s_cover_image_map.clear();
  DestroyResources();

  s_settings_interface = nullptr;
  s_host_interface = nullptr;
}

void Render()
{
  DrawDebugMenu();

  ImGuiFullscreen::BeginLayout();

  switch (s_current_main_window)
  {
    case MainWindowType::Landing:
      DrawLandingWindow();
      break;
    case MainWindowType::GameList:
      DrawGameListWindow();
      break;
    case MainWindowType::Settings:
      DrawSettingsWindow();
      break;
    case MainWindowType::QuickMenu:
      DrawQuickMenu();
      break;
    case MainWindowType::LoadState:
      DrawSaveStateSelector(true);
      break;
    default:
      break;
  }

  ImGuiFullscreen::EndLayout();
}

Settings& GetSettingsCopy()
{
  return s_settings_copy;
}

void SaveAndApplySettings()
{
  s_settings_copy.Save(*s_settings_interface);
  s_settings_interface->Save();
  s_host_interface->ApplySettings();
}

void ClearImGuiFocus()
{
  ImGui::SetWindowFocus(nullptr);
}

void ReturnToMainWindow()
{
  if (System::IsValid())
    s_current_main_window = MainWindowType::QuickMenu;
  else
    s_current_main_window = MainWindowType::Landing;
}

bool LoadResources()
{
  if (!(s_app_icon_texture = LoadTextureResource("logo.png")))
    return false;

  if (!(s_placeholder_texture = s_host_interface->GetDisplay()->CreateTexture(
          PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT, PLACEHOLDER_ICON_DATA, sizeof(u32) * PLACEHOLDER_ICON_WIDTH,
          false)))
  {
    return false;
  }

  if (!(s_disc_region_textures[static_cast<u32>(DiscRegion::NTSC_U)] = LoadTextureResource("flag-uc.png")) ||
      !(s_disc_region_textures[static_cast<u32>(DiscRegion::NTSC_J)] = LoadTextureResource("flag-jp.png")) ||
      !(s_disc_region_textures[static_cast<u32>(DiscRegion::PAL)] = LoadTextureResource("flag-eu.png")) ||
      !(s_disc_region_textures[static_cast<u32>(DiscRegion::Other)] = LoadTextureResource("flag-eu.png")))
  {
    return false;
  }

  for (u32 i = 0; i < static_cast<u32>(GameListCompatibilityRating::Count); i++)
  {
    if (!(s_game_compatibility_textures[i] = LoadTextureResource(TinyString::FromFormat("star-%u.png", i))))
      return false;
  }

  return true;
}

void DestroyResources()
{
  s_app_icon_texture.reset();
  s_placeholder_texture.reset();
  for (auto& tex : s_game_compatibility_textures)
    tex.reset();
  for (auto& tex : s_disc_region_textures)
    tex.reset();
}

std::unique_ptr<HostDisplayTexture> LoadTextureResource(const char* name)
{
  std::unique_ptr<HostDisplayTexture> texture;

  const std::string path(
    s_host_interface->GetProgramDirectoryRelativePath("resources" FS_OSPATH_SEPARATOR_STR "%s", name));

  Common::RGBA8Image image;
  if (Common::LoadImageFromFile(&image, path.c_str()) && image.IsValid())
  {
    texture = s_host_interface->GetDisplay()->CreateTexture(image.GetWidth(), image.GetHeight(), image.GetPixels(),
                                                            image.GetByteStride());
    if (texture)
    {
      Log_DevPrintf("Uploaded texture resource '%s' (%ux%u)", name, image.GetWidth(), image.GetHeight());
      return texture;
    }

    Log_ErrorPrintf("failed to create %ux%u texture for resource", image.GetWidth(), image.GetHeight());
  }

  Log_ErrorPrintf("Missing resource '%s', using fallback", name);

  texture =
    s_host_interface->GetDisplay()->CreateTexture(PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT,
                                                  PLACEHOLDER_ICON_DATA, sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
  if (!texture)
    Panic("Failed to create placeholder texture");

  return texture;
}

//////////////////////////////////////////////////////////////////////////
// Utility
//////////////////////////////////////////////////////////////////////////

static void DoStartFile()
{
  const auto callback = [](const std::string& path) {
    if (!path.empty())
    {
      s_host_interface->RunLater([path]() {
        SystemBootParameters boot_params;
        boot_params.filename = std::move(path);
        s_host_interface->BootSystem(boot_params);
      });
      ClearImGuiFocus();
    }
    CloseFileSelector();
  };

  OpenFileSelector(ICON_FA_COMPACT_DISC "  Select Disc Image", false, std::move(callback),
                   {"*.bin", "*.cue", "*.iso", "*.img", "*.chd", "*.psexe", "*.exe", "*.psf"});
}

static void DoStartBIOS()
{
  s_host_interface->RunLater([]() {
    SystemBootParameters boot_params;
    s_host_interface->BootSystem(boot_params);
  });
  ClearImGuiFocus();
}

static void DoPowerOff()
{
  s_host_interface->RunLater([]() {
    if (!System::IsValid())
      return;

    if (g_settings.save_state_on_exit)
      s_host_interface->SaveResumeSaveState();
    s_host_interface->PowerOffSystem();

    ReturnToMainWindow();
  });
  ClearImGuiFocus();
}

static void DoReset()
{
  s_host_interface->RunLater([]() {
    if (!System::IsValid())
      return;

    s_host_interface->ResetSystem();
  });
}

static void DoPause()
{
  s_host_interface->RunLater([]() {
    if (!System::IsValid())
      return;

    s_host_interface->PauseSystem(!System::IsPaused());
  });
}

//////////////////////////////////////////////////////////////////////////
// Landing Window
//////////////////////////////////////////////////////////////////////////

void DrawLandingWindow()
{
  BeginFullscreenColumns();

  if (BeginFullscreenColumnWindow(0.0f, 570.0f, "logo", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    ImGui::SetCursorPos(LayoutScale(ImVec2(120.0f, 170.0f)));
    ImGui::Image(s_app_icon_texture->GetHandle(), LayoutScale(ImVec2(380.0f, 380.0f)));
  }
  EndFullscreenColumnWindow();

  if (BeginFullscreenColumnWindow(570.0f, LAYOUT_SCREEN_WIDTH, "menu"))
  {
    BeginMenuButtons(7, true);

    if (MenuButton(" " ICON_FA_PLAY_CIRCLE "  Resume",
                   "Starts the console from where it was before it was last closed."))
    {
      s_host_interface->RunLater([]() { s_host_interface->ResumeSystemFromMostRecentState(); });
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_LIST "  Open Game List",
                   "Launch a game from images scanned from your game directories."))
    {
      s_host_interface->RunLater(SwitchToGameList);
    }

    if (MenuButton(" " ICON_FA_FOLDER_OPEN "  Start File", "Launch a game by selecting a file/disc image."))
      s_host_interface->RunLater(DoStartFile);

    if (MenuButton(" " ICON_FA_TOOLBOX "  Start BIOS", "Start the console without any disc inserted."))
      s_host_interface->RunLater(DoStartBIOS);

    if (MenuButton(" " ICON_FA_UNDO "  Load State", "Loads a global save state."))
    {
      s_current_main_window = MainWindowType::LoadState;
      PopulateSaveStateListEntries();
    }

    if (MenuButton(" " ICON_FA_SLIDERS_H "  Settings", "Change settings for the emulator."))
      s_current_main_window = MainWindowType::Settings;

    if (MenuButton(" " ICON_FA_SIGN_OUT_ALT "  Exit", "Exits the program."))
      s_host_interface->RequestExit();

    EndMenuButtons();

    SmallString version_string;
    version_string.Format("%s (%s)", g_scm_tag_str, g_scm_branch_str);

    const ImVec2 text_size = ImGui::CalcTextSize(version_string) + LayoutScale(10.0f, 10.0f);
    ImGui::SetCursorPos(ImGui::GetWindowSize() - text_size);
    ImGui::PushFont(g_medium_font);
    ImGui::TextUnformatted(version_string);
    ImGui::PopFont();
  }

  EndFullscreenColumnWindow();

  EndFullscreenColumns();
}

static ImGuiFullscreen::ChoiceDialogOptions GetGameListDirectoryOptions(bool recursive_as_checked)
{
  ImGuiFullscreen::ChoiceDialogOptions options;

  for (std::string& dir : s_settings_interface->GetStringList("GameList", "Paths"))
    options.emplace_back(std::move(dir), false);

  for (std::string& dir : s_settings_interface->GetStringList("GameList", "RecursivePaths"))
    options.emplace_back(std::move(dir), recursive_as_checked);

  std::sort(options.begin(), options.end(), [](const auto& lhs, const auto& rhs) {
    return (StringUtil::Strcasecmp(lhs.first.c_str(), rhs.first.c_str()) < 0);
  });

  return options;
}

void DrawSettingsWindow()
{
  BeginFullscreenColumns();

  if (BeginFullscreenColumnWindow(0.0f, 300.0f, "settings_category", ImVec4(0.18f, 0.18f, 0.18f, 1.00f)))
  {
    static constexpr std::array<const char*, static_cast<u32>(SettingsPage::Count)> titles = {
      {ICON_FA_WINDOW_MAXIMIZE "  Interface Settings", ICON_FA_LIST "  Game List Settings",
       ICON_FA_HDD "  Console Settings", ICON_FA_MICROCHIP "  BIOS Settings", ICON_FA_GAMEPAD "  Controller Settings",
       ICON_FA_KEYBOARD "  Hotkey Settings", ICON_FA_SD_CARD "  Memory Card Settings", ICON_FA_TV "  Display Settings",
       ICON_FA_MAGIC "  Enhancement Settings", ICON_FA_HEADPHONES "  Audio Settings",
       ICON_FA_EXCLAMATION_TRIANGLE "  Advanced Settings"}};

    BeginMenuButtons(static_cast<u32>(titles.size()) + 1u, false);
    for (u32 i = 0; i < static_cast<u32>(titles.size()); i++)
    {
      if (ActiveButton(titles[i], s_settings_page == static_cast<SettingsPage>(i)))
        s_settings_page = static_cast<SettingsPage>(i);
    }

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    if (ActiveButton(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();

    EndMenuButtons();
  }

  EndFullscreenColumnWindow();

  if (BeginFullscreenColumnWindow(300.0f, LAYOUT_SCREEN_WIDTH, "settings_parent"))
  {
    bool settings_changed = false;

    switch (s_settings_page)
    {
      case SettingsPage::InterfaceSettings:
      {
        BeginMenuButtons(8, false);

        settings_changed |=
          ToggleButton("Pause On Start", "Pauses the emulator when a game is started.", &s_settings_copy.start_paused);
        settings_changed |= ToggleButton("Pause On Focus Loss",
                                         "Pauses the emulator when you minimize the window or switch to another "
                                         "application, and unpauses when you switch back.",
                                         &s_settings_copy.pause_on_focus_loss);
        settings_changed |=
          ToggleButton("Confirm Power Off",
                       "Determines whether a prompt will be displayed to confirm shutting down the emulator/game "
                       "when the hotkey is pressed.",
                       &s_settings_copy.confim_power_off);
        settings_changed |=
          ToggleButton("Save State On Exit",
                       "Automatically saves the emulator state when powering down or exiting. You can then "
                       "resume directly from where you left off next time.",
                       &s_settings_copy.save_state_on_exit);
        settings_changed |=
          ToggleButton("Start Fullscreen", "Automatically switches to fullscreen mode when a game is started.",
                       &s_settings_copy.start_fullscreen);
        settings_changed |=
          ToggleButton("Load Devices From Save States",
                       "When enabled, memory cards and controllers will be overwritten when save states are loaded.",
                       &s_settings_copy.load_devices_from_save_states);
        settings_changed |= ToggleButton(
          "Apply Per-Game Settings",
          "When enabled, per-game settings will be applied, and incompatible enhancements will be disabled.",
          &s_settings_copy.apply_game_settings);
        settings_changed |=
          ToggleButton("Automatically Load Cheats", "Automatically loads and applies cheats on game start.",
                       &s_settings_copy.auto_load_cheats);

        EndMenuButtons();
      }
      break;

      case SettingsPage::GameListSettings:
      {
        EnsureGameListLoaded();

        BeginMenuButtons(4, false);

        if (MenuButton(ICON_FA_FOLDER_PLUS "  Add Search Directory", "Adds a new directory to the game search list."))
        {
          OpenFileSelector(ICON_FA_FOLDER_PLUS "  Add Search Directory", true, [](const std::string& dir) {
            if (!dir.empty())
            {
              s_settings_interface->RemoveFromStringList("GameList", "RecursivePaths", dir.c_str());
              s_settings_interface->AddToStringList("GameList", "Paths", dir.c_str());
            }

            CloseFileSelector();
          });
        }

        if (MenuButton(ICON_FA_FOLDER_OPEN "  Change Recursive Directories",
                       "Sets whether subdirectories are searched for each game directory"))
        {
          OpenChoiceDialog(ICON_FA_FOLDER_OPEN "  Change Recursive Directories", true,
                           GetGameListDirectoryOptions(true), [](s32 index, const std::string& title, bool checked) {
                             if (index < 0)
                               return;

                             if (checked)
                             {
                               s_settings_interface->RemoveFromStringList("GameList", "Paths", title.c_str());
                               s_settings_interface->AddToStringList("GameList", "RecursivePaths", title.c_str());
                             }
                             else
                             {
                               s_settings_interface->RemoveFromStringList("GameList", "RecursivePaths", title.c_str());
                               s_settings_interface->AddToStringList("GameList", "Paths", title.c_str());
                             }

                             s_host_interface->RunLater(SaveAndApplySettings);
                           });
        }

        if (MenuButton(ICON_FA_FOLDER_MINUS "  Remove Search Directory",
                       "Removes a directory from the game search list."))
        {
          OpenChoiceDialog(ICON_FA_FOLDER_MINUS "  Remove Search Directory", false, GetGameListDirectoryOptions(false),
                           [](s32 index, const std::string& title, bool checked) {
                             if (index < 0)
                               return;

                             s_settings_interface->RemoveFromStringList("GameList", "Paths", title.c_str());
                             s_settings_interface->RemoveFromStringList("GameList", "RecursivePaths", title.c_str());
                             s_host_interface->RunLater(SaveAndApplySettings);
                             CloseChoiceDialog();
                           });
        }

        MenuHeading("Search Directories");
        for (const GameList::DirectoryEntry& entry : s_host_interface->GetGameList()->GetSearchDirectories())
          ActiveButton(entry.path.c_str(), false, false);

        EndMenuButtons();
      }
      break;

      case SettingsPage::ConsoleSettings:
      {
        static constexpr auto emulation_speeds =
          make_array(0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f,
                     3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f);
        static constexpr auto get_emulation_speed_options = [](float current_speed) {
          ImGuiFullscreen::ChoiceDialogOptions options;
          options.reserve(emulation_speeds.size());
          for (const float speed : emulation_speeds)
          {
            options.emplace_back(
              (speed != 0.0f) ?
                StringUtil::StdStringFromFormat("%d%% [%d FPS (NTSC) / %d FPS (PAL)]", static_cast<int>(speed * 100.0f),
                                                static_cast<int>(60.0f * speed), static_cast<int>(50.0f * speed)) :
                "Unlimited",
              speed == current_speed);
          }
          return options;
        };

        static constexpr auto cdrom_read_speeds =
          make_array("None (Double Speed)", "2x (Quad Speed)", "3x (6x Speed)", "4x (8x Speed)", "5x (10x Speed)",
                     "6x (12x Speed)", "7x (14x Speed)", "8x (16x Speed)", "9x (18x Speed)", "10x (20x Speed)");

        BeginMenuButtons(1, false);

        settings_changed |=
          EnumChoiceButton("Console Region", "Determines the emulated hardware type.", &s_settings_copy.region,
                           &Settings::GetConsoleRegionDisplayName, ConsoleRegion::Count);

#define MAKE_EMULATION_SPEED(setting_title, setting_var)                                                               \
  if (MenuButtonWithValue(                                                                                             \
        setting_title,                                                                                                 \
        "Sets the target emulation speed. It is not guaranteed that this speed will be reached on all systems.",       \
        (setting_var != 0.0f) ? TinyString::FromFormat("%.0f%%", setting_var * 100.0f) : TinyString("Unlimited")))     \
  {                                                                                                                    \
    OpenChoiceDialog(setting_title, false, get_emulation_speed_options(setting_var),                                   \
                     [](s32 index, const std::string& title, bool checked) {                                           \
                       if (index >= 0)                                                                                 \
                       {                                                                                               \
                         setting_var = emulation_speeds[index];                                                        \
                         s_host_interface->RunLater(SaveAndApplySettings);                                             \
                       }                                                                                               \
                       CloseChoiceDialog();                                                                            \
                     });                                                                                               \
  }

        MAKE_EMULATION_SPEED("Emulation Speed", s_settings_copy.emulation_speed);
        MAKE_EMULATION_SPEED("Fast Forward Speed", s_settings_copy.fast_forward_speed);
        MAKE_EMULATION_SPEED("Turbo Speed", s_settings_copy.turbo_speed);

#undef MAKE_EMULATION_SPEED

        settings_changed |= ToggleButton("Sync To Host Refresh Rate",
                                         "Adjusts the emulation speed so the console's refresh rate matches the host "
                                         "when VSync and Audio Resampling are enabled.",
                                         &s_settings_copy.sync_to_host_refresh_rate,
                                         s_settings_copy.video_sync_enabled && s_settings_copy.audio_resampling);

        settings_changed |= EnumChoiceButton(
          "CPU Execution Mode", "Determines how the emulated CPU executes instructions. Recompiler is recommended.",
          &s_settings_copy.cpu_execution_mode, &Settings::GetCPUExecutionModeDisplayName, CPUExecutionMode::Count);

        settings_changed |=
          ToggleButton("Enable Overclocking", "When this option is chosen, the clock speed set below will be used.",
                       &s_settings_copy.cpu_overclock_enable);

        s32 overclock_percent =
          s_settings_copy.cpu_overclock_enable ? static_cast<s32>(s_settings_copy.GetCPUOverclockPercent()) : 100;
        if (RangeButton("Overclocking Percentage",
                        "Selects the percentage of the normal clock speed the emulated hardware will run at.",
                        &overclock_percent, 10, 1000, 10, "%d%%", s_settings_copy.cpu_overclock_enable))
        {
          s_settings_copy.SetCPUOverclockPercent(static_cast<u32>(overclock_percent));
          settings_changed = true;
        }

        const u32 read_speed_index =
          std::min(g_settings.cdrom_read_speedup, static_cast<u32>(cdrom_read_speeds.size() + 1)) - 1;
        if (MenuButtonWithValue("CD-ROM Read Speedup",
                                "Speeds up CD-ROM reads by the specified factor. May improve loading speeds in some "
                                "games, and break others.",
                                cdrom_read_speeds[read_speed_index]))
        {
          ImGuiFullscreen::ChoiceDialogOptions options;
          options.reserve(cdrom_read_speeds.size());
          for (u32 i = 0; i < static_cast<u32>(cdrom_read_speeds.size()); i++)
            options.emplace_back(cdrom_read_speeds[i], i == read_speed_index);
          OpenChoiceDialog("CD-ROM Read Speedup", false, std::move(options),
                           [](s32 index, const std::string& title, bool checked) {
                             if (index >= 0)
                               s_settings_copy.cdrom_read_speedup = static_cast<u32>(index) + 1;
                             CloseChoiceDialog();
                           });
        }

        settings_changed |= ToggleButton(
          "Enable CD-ROM Read Thread",
          "Reduces hitches in emulation by reading/decompressing CD data asynchronously on a worker thread.",
          &s_settings_copy.cdrom_read_thread);
        settings_changed |= ToggleButton("Enable CD-ROM Region Check",
                                         "Simulates the region check present in original, unmodified consoles.",
                                         &s_settings_copy.cdrom_region_check);
        settings_changed |= ToggleButton(
          "Preload CD Images to RAM",
          "Loads the game image into RAM. Useful for network paths that may become unreliable during gameplay.",
          &s_settings_copy.cdrom_load_image_to_ram);

        EndMenuButtons();
      }
      break;

      case SettingsPage::BIOSSettings:
      {
        static constexpr auto config_keys = make_array("", "PathNTSCJ", "PathNTSCU", "PathPAL");
        static std::string bios_region_filenames[static_cast<u32>(ConsoleRegion::Count)];
        static std::string bios_directory;
        static bool bios_filenames_loaded = false;

        if (!bios_filenames_loaded)
        {
          for (u32 i = 0; i < static_cast<u32>(ConsoleRegion::Count); i++)
          {
            if (i == static_cast<u32>(ConsoleRegion::Auto))
              continue;
            bios_region_filenames[i] = s_settings_interface->GetStringValue("BIOS", config_keys[i]);
          }
          bios_directory = s_host_interface->GetBIOSDirectory();
          bios_filenames_loaded = true;
        }

        BeginMenuButtons(1, false);

        for (u32 i = 0; i < static_cast<u32>(ConsoleRegion::Count); i++)
        {
          const ConsoleRegion region = static_cast<ConsoleRegion>(i);
          if (region == ConsoleRegion::Auto)
            continue;

          TinyString title;
          title.Format("BIOS for %s", Settings::GetConsoleRegionName(region));

          if (MenuButtonWithValue(title,
                                  SmallString::FromFormat("BIOS to use when emulating %s consoles.",
                                                          Settings::GetConsoleRegionDisplayName(region)),
                                  bios_region_filenames[i].c_str()))
          {
            ImGuiFullscreen::ChoiceDialogOptions options;
            auto images = s_host_interface->FindBIOSImagesInDirectory(s_host_interface->GetBIOSDirectory().c_str());
            options.reserve(images.size() + 1);
            options.emplace_back("Auto-Detect", bios_region_filenames[i].empty());
            for (auto& [path, info] : images)
            {
              const bool selected = bios_region_filenames[i] == path;
              options.emplace_back(std::move(path), selected);
            }

            OpenChoiceDialog(title, false, std::move(options), [i](s32 index, const std::string& path, bool checked) {
              if (index >= 0)
              {
                bios_region_filenames[i] = path;
                s_settings_interface->SetStringValue("BIOS", config_keys[i], path.c_str());
                s_settings_interface->Save();
              }
              CloseChoiceDialog();
            });
          }
        }

        if (MenuButton("BIOS Directory", bios_directory.c_str()))
        {
          OpenFileSelector("BIOS Directory", true, [](const std::string& path) {
            if (!path.empty())
            {
              bios_directory = path;
              s_settings_interface->SetStringValue("BIOS", "SearchDirectory", path.c_str());
              s_settings_interface->Save();
            }
            CloseFileSelector();
          });
        }

        MenuHeading("Patches");

        settings_changed |=
          ToggleButton("Enable Fast Boot", "Patches the BIOS to skip the boot animation. Safe to enable.",
                       &s_settings_copy.bios_patch_fast_boot);
        settings_changed |= ToggleButton(
          "Enable TTY Output", "Patches the BIOS to log calls to printf(). Only use when debugging, can break games.",
          &s_settings_copy.bios_patch_tty_enable);

        EndMenuButtons();
      }
      break;

      case SettingsPage::ControllerSettings:
      {
        BeginMenuButtons(1, false);
        ActiveButton("Not yet implemented, please check back later.  " ICON_FA_SMILE, false, false);
        EndMenuButtons();
      }
      break;

      case SettingsPage::HotkeySettings:
      {
        BeginMenuButtons(1, false);
        ActiveButton("Not yet implemented, please check back later.  " ICON_FA_SMILE, false, false);
        EndMenuButtons();
      }
      break;

      case SettingsPage::MemoryCardSettings:
      {
        BeginMenuButtons(6, false);

        for (u32 i = 0; i < 2; i++)
        {
          settings_changed |= EnumChoiceButton(
            TinyString::FromFormat("Memory Card %u Type", i + 1),
            SmallString::FromFormat("Sets which sort of memory card image will be used for slot %u.", i + 1),
            &s_settings_copy.memory_card_types[i], &Settings::GetMemoryCardTypeDisplayName, MemoryCardType::Count);

          settings_changed |= MenuButton(TinyString::FromFormat("Shared Memory Card %u Path", i + 1),
                                         s_settings_copy.memory_card_paths[i].c_str(),
                                         s_settings_copy.memory_card_types[i] == MemoryCardType::Shared);
        }

        settings_changed |= ToggleButton(
          "Use Single Card For Playlist",
          "When using a playlist (m3u) and per-game (title) memory cards, use a single memory card for all discs.",
          &s_settings_copy.memory_card_use_playlist_title);

        static std::string memory_card_directory;
        if (memory_card_directory.empty())
          memory_card_directory = s_host_interface->GetUserDirectoryRelativePath("memcards");

        MenuButton("Per-Game Memory Card Directory", memory_card_directory.c_str(), false);

        EndMenuButtons();
      }
      break;

      case SettingsPage::DisplaySettings:
      {
        BeginMenuButtons(6, false);

        settings_changed |=
          EnumChoiceButton("GPU Renderer", "Chooses the backend to use for rendering the console/game visuals.",
                           &s_settings_copy.gpu_renderer, &Settings::GetRendererDisplayName, GPURenderer::Count);

        settings_changed |=
          ToggleButton("Enable VSync",
                       "Synchronizes presentation of the console's frames to the host. Enable for smoother animations.",
                       &s_settings_copy.video_sync_enabled);

        switch (s_settings_copy.gpu_renderer)
        {
#ifdef WIN32
          case GPURenderer::HardwareD3D11:
          {
            // TODO: FIXME
            bool use_blit_swap_chain = false;
            settings_changed |=
              ToggleButton("Use Blit Swap Chain",
                           "Uses a blit presentation model instead of flipping. This may be needed on some systems.",
                           &use_blit_swap_chain);
          }
          break;
#endif

          case GPURenderer::HardwareVulkan:
          {
            settings_changed |=
              ToggleButton("Threaded Presentation",
                           "Presents frames on a background thread when fast forwarding or vsync is disabled.",
                           &s_settings_copy.gpu_threaded_presentation);
          }
          break;

          case GPURenderer::Software:
          {
            settings_changed |= ToggleButton("Threaded Rendering",
                                             "Uses a second thread for drawing graphics. Speed boost, and safe to use.",
                                             &s_settings_copy.gpu_use_thread);
          }
          break;

          default:
            break;
        }

        settings_changed |= EnumChoiceButton(
          "Aspect Ratio", "Changes the aspect ratio used to display the console's output to the screen.",
          &s_settings_copy.display_aspect_ratio, &Settings::GetDisplayAspectRatioName, DisplayAspectRatio::Count);

        settings_changed |= EnumChoiceButton(
          "Crop Mode", "Determines how much of the area typically not visible on a consumer TV set to crop/hide.",
          &s_settings_copy.display_crop_mode, &Settings::GetDisplayCropModeDisplayName, DisplayCropMode::Count);

        settings_changed |=
          EnumChoiceButton("Downsampling",
                           "Downsamples the rendered image prior to displaying it. Can improve "
                           "overall image quality in mixed 2D/3D games.",
                           &s_settings_copy.gpu_downsample_mode, &Settings::GetDownsampleModeDisplayName,
                           GPUDownsampleMode::Count, !s_settings_copy.IsUsingSoftwareRenderer());

        settings_changed |=
          ToggleButton("Linear Upscaling", "Uses a bilinear filter when upscaling to display, smoothing out the image.",
                       &s_settings_copy.display_linear_filtering);

        settings_changed |=
          ToggleButton("Integer Upscaling", "Adds padding to ensure pixels are a whole number in size.",
                       &s_settings_copy.display_integer_scaling);

        settings_changed |= ToggleButton("Show OSD Messages", "Shows on-screen-display messages when events occur.",
                                         &s_settings_copy.display_show_osd_messages);
        settings_changed |= ToggleButton(
          "Show Game FPS", "Shows the internal frame rate of the game in the top-right corner of the display.",
          &s_settings_copy.display_show_fps);
        settings_changed |= ToggleButton("Show Display FPS (VPS)",
                                         "Shows the number of frames (or v-syncs) displayed per second by the system "
                                         "in the top-right corner of the display.",
                                         &s_settings_copy.display_show_vps);
        settings_changed |= ToggleButton(
          "Show Speed",
          "Shows the current emulation speed of the system in the top-right corner of the display as a percentage.",
          &s_settings_copy.display_show_speed);
        settings_changed |=
          ToggleButton("Show Resolution",
                       "Shows the current rendering resolution of the system in the top-right corner of the display.",
                       &s_settings_copy.display_show_resolution);

        EndMenuButtons();
      }
      break;

      case SettingsPage::EnhancementSettings:
      {
        static const auto resolution_scale_text_callback = [](u32 value) -> const char* {
          static constexpr std::array<const char*, 17> texts = {
            {"Automatic based on window size", "1x", "2x", "3x (for 720p)", "4x", "5x (for 1080p)", "6x (for 1440p)",
             "7x", "8x", "9x (for 4K)", "10x", "11x", "12x", "13x", "14x", "15x", "16x"

            }};
          return (value >= texts.size()) ? "" : texts[value];
        };

        BeginMenuButtons(13, false);

        settings_changed |= EnumChoiceButton<u32, u32>(
          "Internal Resolution Scale",
          "Scales internal VRAM resolution by the specified multiplier. Some games require 1x VRAM resolution.",
          &s_settings_copy.gpu_resolution_scale, resolution_scale_text_callback, 17);
        settings_changed |= EnumChoiceButton(
          "Texture Filtering",
          "Smooths out the blockyness of magnified textures on 3D objects. Will have a greater effect "
          "on higher resolution scales.",
          &s_settings_copy.gpu_texture_filter, &Settings::GetTextureFilterDisplayName, GPUTextureFilter::Count);
        settings_changed |=
          ToggleButton("True Color Rendering",
                       "Disables dithering and uses the full 8 bits per channel of color information. May break "
                       "rendering in some games.",
                       &s_settings_copy.gpu_true_color);
        settings_changed |= ToggleButton(
          "Scaled Dithering",
          "Scales the dithering pattern with the internal rendering resolution, making it less noticeable. "
          "Usually safe to enable.",
          &s_settings_copy.gpu_scaled_dithering, s_settings_copy.gpu_resolution_scale > 1);
        settings_changed |= ToggleButton(
          "Widescreen Hack", "Increases the field of view from 4:3 to the chosen display aspect ratio in 3D games.",
          &s_settings_copy.gpu_widescreen_hack);
        settings_changed |=
          ToggleButton("Disable Interlacing",
                       "Disables interlaced rendering and display in the GPU. Some games can render in 480p this way, "
                       "but others will break.",
                       &s_settings_copy.gpu_disable_interlacing);
        settings_changed |= ToggleButton(
          "Force NTSC Timings",
          "Forces PAL games to run at NTSC timings, i.e. 60hz. Some PAL games will run at their \"normal\" "
          "speeds, while others will break.",
          &s_settings_copy.gpu_force_ntsc_timings);
        settings_changed |=
          ToggleButton("Force 4:3 For 24-Bit Display",
                       "Switches back to 4:3 display aspect ratio when displaying 24-bit content, usually FMVs.",
                       &s_settings_copy.display_force_4_3_for_24bit);
        settings_changed |= ToggleButton(
          "Chroma Smoothing For 24-Bit Display",
          "Smooths out blockyness between colour transitions in 24-bit content, usually FMVs. Only applies "
          "to the hardware renderers.",
          &s_settings_copy.gpu_24bit_chroma_smoothing);
        settings_changed |=
          ToggleButton("PGXP Geometry Correction",
                       "Reduces \"wobbly\" polygons by attempting to preserve the fractional component through memory "
                       "transfers.",
                       &s_settings_copy.gpu_pgxp_enable);
        settings_changed |=
          ToggleButton("PGXP Texture Correction",
                       "Uses perspective-correct interpolation for texture coordinates and colors, straightening out "
                       "warped textures.",
                       &s_settings_copy.gpu_pgxp_texture_correction, s_settings_copy.gpu_pgxp_enable);
        settings_changed |=
          ToggleButton("PGXP Culling Correction",
                       "Increases the precision of polygon culling, reducing the number of holes in geometry.",
                       &s_settings_copy.gpu_pgxp_culling, s_settings_copy.gpu_pgxp_enable);
        settings_changed |= ToggleButton(
          "PGXP Depth Buffer", "Reduces polygon Z-fighting through depth testing. Low compatibility with games.",
          &s_settings_copy.gpu_pgxp_depth_buffer,
          s_settings_copy.gpu_pgxp_enable && s_settings_copy.gpu_pgxp_texture_correction);

        EndMenuButtons();
      }
      break;

      case SettingsPage::AudioSettings:
      {
        BeginMenuButtons(1, false);

        settings_changed |= RangeButton("Output Volume", "Controls the volume of the audio played on the host.",
                                        &s_settings_copy.audio_output_volume, 0, 100, 1, "%d%%");
        settings_changed |= RangeButton("Fast Forward Volume",
                                        "Controls the volume of the audio played on the host when fast forwarding.",
                                        &s_settings_copy.audio_output_volume, 0, 100, 1, "%d%%");
        settings_changed |= ToggleButton("Mute All Sound", "Prevents the emulator from producing any audible sound.",
                                         &s_settings_copy.audio_output_muted);
        settings_changed |= ToggleButton("Mute CD Audio",
                                         "Forcibly mutes both CD-DA and XA audio from the CD-ROM. Can be used to "
                                         "disable background music in some games.",
                                         &s_settings_copy.cdrom_mute_cd_audio);

        settings_changed |= ToggleButton("Sync To Output",
                                         "Throttles the emulation speed based on the audio backend pulling audio "
                                         "frames. Enable to reduce the chances of crackling.",
                                         &s_settings_copy.audio_sync_enabled);
        settings_changed |= ToggleButton(
          "Resampling",
          "When running outside of 100% speed, resamples audio from the target speed instead of dropping frames.",
          &s_settings_copy.audio_resampling);
        settings_changed |= EnumChoiceButton(
          "Audio Backend",
          "The audio backend determines how frames produced by the emulator are submitted to the host.",
          &s_settings_copy.audio_backend, &Settings::GetAudioBackendDisplayName, AudioBackend::Count);
        settings_changed |= RangeButton(
          "Buffer Size", "The buffer size determines the size of the chunks of audio which will be pulled by the host.",
          reinterpret_cast<s32*>(&s_settings_copy.audio_buffer_size), 1024, 8192, 128, "%d Frames");

        EndMenuButtons();
      }
      break;

      case SettingsPage::AdvancedSettings:
      {
        BeginMenuButtons(1, false);

        bool debug_menu = s_debug_menu_enabled;
        if (ToggleButton("Enable Debug Menu", "Shows a debug menu bar with additional statistics and quick settings.",
                         &debug_menu))
        {
          s_host_interface->RunLater([debug_menu]() { SetDebugMenuEnabled(debug_menu, true); });
        }

        settings_changed |=
          ToggleButton("Disable All Enhancements", "Temporarily disables all enhancements, useful when testing.",
                       &s_settings_copy.disable_all_enhancements);
#if 0
        settings_changed |= RangeButton("Display FPS Limit", "Limits how many frames are displayed to the screen. These frames are still rendered.", &s_settings_copy.display_max_fps);
#endif
        settings_changed |=
          ToggleButton("Enable PGXP CPU Mode", "Uses PGXP for all instructions, not just memory operations.",
                       &s_settings_copy.gpu_pgxp_cpu);
        settings_changed |= ToggleButton(
          "Enable PGXP Vertex Cache", "Uses screen positions to resolve PGXP data. May improve visuals in some games.",
          &s_settings_copy.gpu_pgxp_vertex_cache);
        settings_changed |=
          ToggleButton("Enable PGXP Preserve Projection Precision",
                       "Adds additional precision to PGXP data post-projection. May improve visuals in some games.",
                       &s_settings_copy.gpu_pgxp_preserve_proj_fp);
#if 0
        settings_changed |= ToggleButton("PGXP Geometry Tolerance", "", &s_settings_copy.gpu_pgxp_tolerance);
        settings_changed |= ToggleButton("PGXP Depth Clear Threshold", "", &s_settings_copy.gpu_pgxp_tolerance);
#endif

        settings_changed |= ToggleButton("Enable VRAM Write Texture Replacement",
                                         "Enables the replacement of background textures in supported games.",
                                         &s_settings_copy.texture_replacements.enable_vram_write_replacements);
        settings_changed |= ToggleButton("Preload Replacement Textures",
                                         "Loads all replacement texture to RAM, reducing stuttering at runtime.",
                                         &s_settings_copy.texture_replacements.preload_textures,
                                         s_settings_copy.texture_replacements.AnyReplacementsEnabled());
        settings_changed |=
          ToggleButton("Dump Replacable VRAM Writes", "Writes textures which can be replaced to the dump directory.",
                       &s_settings_copy.texture_replacements.dump_vram_writes);
        settings_changed |=
          ToggleButton("Set VRAM Write Dump Alpha Channel", "Clears the mask/transparency bit in VRAM write dumps.",
                       &s_settings_copy.texture_replacements.dump_vram_write_force_alpha_channel);

        settings_changed |=
          ToggleButton("Enable Recompiler ICache",
                       "Simulates the CPU's instruction cache in the recompiler. Can help with games running too fast.",
                       &s_settings_copy.cpu_recompiler_icache);
        settings_changed |= ToggleButton("Enable Recompiler Memory Exceptions",
                                         "Enables alignment and bus exceptions. Not needed for any known games.",
                                         &s_settings_copy.cpu_recompiler_memory_exceptions);
        settings_changed |= EnumChoiceButton("Recompiler Fast Memory Access",
                                             "Avoids calls to C++ code, significantly speeding up the recompiler.",
                                             &s_settings_copy.cpu_fastmem_mode, &Settings::GetCPUFastmemModeDisplayName,
                                             CPUFastmemMode::Count, !s_settings_copy.cpu_recompiler_memory_exceptions);

        EndMenuButtons();
      }
      break;
    }

    if (settings_changed)
      s_host_interface->RunLater(SaveAndApplySettings);
  }

  EndFullscreenColumnWindow();

  EndFullscreenColumns();
}

void DrawQuickMenu()
{
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, IM_COL32(255, 255, 255, 60));

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));

  if (BeginFullscreenWindow(-0.5f, -0.5f, 500.0f, 460.0f, "pause_menu", HEX_TO_IMVEC4(0x212121, 240)))
  {
    ImGui::SetCursorPos(LayoutScale(20.0f, 20.0f));
    ImGui::Image(GetCoverForCurrentGame()->GetHandle(), LayoutScale(50.0f, 50.0f));
    ImGui::SetCursorPos(LayoutScale(90.0f, 20.0f));
    ImGui::PushFont(g_large_font);
    ImGui::TextUnformatted(System::GetRunningTitle().c_str());
    ImGui::PopFont();
    ImGui::SetCursorPosX(LayoutScale(90.0f));
    ImGui::PushFont(g_medium_font);
    ImGui::TextUnformatted(System::GetRunningPath().c_str());
    ImGui::PopFont();

    ImGui::SetCursorPosY(LayoutScale(80.0f));

    BeginMenuButtons(9, false);

    ActiveButton(ICON_FA_BACKWARD "  Back To Game", false);
    ActiveButton(ICON_FA_UNDO "  Load State", false);
    ActiveButton(ICON_FA_SAVE "  Save State", false);
    ActiveButton(ICON_FA_FAST_FORWARD "  Fast Forward", false);
    ActiveButton(ICON_FA_SYNC "  Reset", false);
    ActiveButton(ICON_FA_FROWN_OPEN "  Cheats", false);

    if (ActiveButton(ICON_FA_SLIDERS_H "  Settings", false))
      s_current_main_window = MainWindowType::Settings;

    if (ActiveButton(ICON_FA_POWER_OFF "  Exit Game", false))
      s_host_interface->RunLater(DoPowerOff);

    EndMenuButtons();
  }

  ImGui::PopStyleVar();

  EndFullscreenWindow();
}

void InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot, bool global)
{
  if (global)
    li->title = StringUtil::StdStringFromFormat("Global Slot %d##global_slot_%d", slot, slot);
  else
    li->title =
      StringUtil::StdStringFromFormat("%s Slot %d##game_slot_%d", System::GetRunningTitle().c_str(), slot, slot);

  li->summary = "No Save State";

  std::string().swap(li->path);
  li->slot = slot;
  li->global = global;
}

void InitializeSaveStateListEntry(SaveStateListEntry* li, CommonHostInterface::ExtendedSaveStateInfo* ssi)
{
  if (ssi->global)
  {
    li->title =
      StringUtil::StdStringFromFormat("Global Save %d - %s##global_slot_%d", ssi->slot, ssi->title.c_str(), ssi->slot);
  }
  else
  {
    li->title = StringUtil::StdStringFromFormat("%s Slot %d##game_slot_%d", ssi->title.c_str(), ssi->slot, ssi->slot);
  }

  li->summary =
    StringUtil::StdStringFromFormat("%s - Saved %s", ssi->game_code.c_str(),
                                    Timestamp::FromUnixTimestamp(ssi->timestamp).ToString("%c").GetCharArray());
  li->slot = ssi->slot;
  li->global = ssi->global;
  li->path = std::move(ssi->path);

  li->preview_texture.reset();
  if (ssi && !ssi->screenshot_data.empty())
  {
    li->preview_texture = s_host_interface->GetDisplay()->CreateTexture(ssi->screenshot_width, ssi->screenshot_height,
                                                                        ssi->screenshot_data.data(),
                                                                        sizeof(u32) * ssi->screenshot_width, false);
  }
  else
  {
    li->preview_texture =
      s_host_interface->GetDisplay()->CreateTexture(PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT,
                                                    PLACEHOLDER_ICON_DATA, sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
  }

  if (!li->preview_texture)
    Log_ErrorPrintf("Failed to upload save state image to GPU");
}

void PopulateSaveStateListEntries()
{
  s_save_state_selector_slots.clear();

  if (!System::GetRunningCode().empty())
  {
    for (s32 i = 1; i <= CommonHostInterface::PER_GAME_SAVE_STATE_SLOTS; i++)
    {
      std::optional<CommonHostInterface::ExtendedSaveStateInfo> ssi =
        s_host_interface->GetExtendedSaveStateInfo(System::GetRunningCode().c_str(), i);

      SaveStateListEntry li;
      if (ssi)
        InitializeSaveStateListEntry(&li, &ssi.value());
      else
        InitializePlaceholderSaveStateListEntry(&li, i, false);

      s_save_state_selector_slots.push_back(std::move(li));
    }
  }

  for (s32 i = 1; i <= CommonHostInterface::GLOBAL_SAVE_STATE_SLOTS; i++)
  {
    std::optional<CommonHostInterface::ExtendedSaveStateInfo> ssi =
      s_host_interface->GetExtendedSaveStateInfo(nullptr, i);

    SaveStateListEntry li;
    if (ssi)
      InitializeSaveStateListEntry(&li, &ssi.value());
    else
      InitializePlaceholderSaveStateListEntry(&li, i, true);

    s_save_state_selector_slots.push_back(std::move(li));
  }
}

void ClearSaveStateListEntries()
{
  s_save_state_selector_slots.clear();
}

void DrawSaveStateSelector(bool is_loading)
{
  const HostDisplayTexture* selected_texture = s_placeholder_texture.get();

  if (!BeginFullscreenColumns())
  {
    EndFullscreenColumns();
    return;
  }

  // drawn back the front so the hover changes the image
  if (BeginFullscreenColumnWindow(570.0f, LAYOUT_SCREEN_WIDTH, "save_state_selector_slots"))
  {
    BeginMenuButtons(static_cast<u32>(s_save_state_selector_slots.size()), true);

    for (const SaveStateListEntry& entry : s_save_state_selector_slots)
    {
      if (MenuButton(entry.title.c_str(), entry.summary.c_str()))
      {
        const std::string& path = entry.path;
        s_host_interface->RunLater([path]() { s_host_interface->LoadState(path.c_str()); });
      }

      if (ImGui::IsItemHovered())
        selected_texture = entry.preview_texture.get();
    }

    EndMenuButtons();
  }
  EndFullscreenColumnWindow();

  if (BeginFullscreenColumnWindow(0.0f, 570.0f, "save_state_selector_preview", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    ImGui::SetCursorPos(LayoutScale(20.0f, 20.0f));
    ImGui::PushFont(g_large_font);
    ImGui::TextUnformatted(is_loading ? ICON_FA_FOLDER_OPEN "  Load State" : ICON_FA_SAVE "  Save State");
    ImGui::PopFont();

    ImGui::SetCursorPos(LayoutScale(ImVec2(85.0f, 160.0f)));
    ImGui::Image(selected_texture ? selected_texture->GetHandle() : s_placeholder_texture->GetHandle(),
                 LayoutScale(ImVec2(400.0f, 400.0f)));

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    BeginMenuButtons(1, false);
    if (ActiveButton(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();
    EndMenuButtons();
  }
  EndFullscreenColumnWindow();

  EndFullscreenColumns();
}

void DrawGameListWindow()
{
  const GameListEntry* selected_entry = nullptr;

  if (!BeginFullscreenColumns())
  {
    EndFullscreenColumns();
    return;
  }

  if (BeginFullscreenColumnWindow(450.0f, 1220.0f, "game_list_entries"))
  {
    BeginMenuButtons(s_host_interface->GetGameList()->GetEntryCount(), false);

    for (const GameListEntry& entry : s_host_interface->GetGameList()->GetEntries())
    {
      const HostDisplayTexture* cover_texture = GetGameListCover(&entry);
      const float cover_ar =
        static_cast<float>(cover_texture->GetWidth()) / static_cast<float>(cover_texture->GetHeight());

      SmallString summary;
      summary.AppendFormattedString("%s - ", entry.code.c_str());
      summary.AppendString(System::GetTitleForPath(entry.path.c_str()));

      if (MenuButton(entry.title.c_str(), summary))
      {
        // launch game
        const std::string& path_to_launch(entry.path);
        s_host_interface->RunLater(
          [path_to_launch]() { s_host_interface->ResumeSystemFromState(path_to_launch.c_str(), true); });
      }

      if (ImGui::IsItemHovered())
        selected_entry = &entry;
    }

    EndMenuButtons();
  }
  EndFullscreenColumnWindow();

  if (BeginFullscreenColumnWindow(0.0f, 450.0f, "game_list_info", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    const auto* window = ImGui::GetCurrentWindow();
    const ImVec2 base_pos(window->DC.CursorPos);

    ImGui::SetCursorPos(LayoutScale(ImVec2(50.0f, 50.0f)));
    ImGui::Image(selected_entry ? GetGameListCover(selected_entry)->GetHandle() : s_placeholder_texture->GetHandle(),
                 LayoutScale(ImVec2(350.0f, 350.0f)));

    if (selected_entry)
    {
      const float work_width = ImGui::GetCurrentWindow()->WorkRect.GetWidth();
      const float field_margin_y = 10.0f;
      const float start_x = 50.0f;
      const float end_x = 400.0f;
      float text_y = 425.0f;
      float text_width;
      SmallString text;

      ImGui::SetCursorPos(LayoutScale(start_x, text_y));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, field_margin_y));
      ImGui::BeginGroup();

      // title
      ImGui::PushFont(g_large_font);
      text_width = ImGui::CalcTextSize(selected_entry->title.c_str(), nullptr, false, work_width).x;
      ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
      ImGui::TextWrapped("%s", selected_entry->title.c_str());
      ImGui::PopFont();

      ImGui::PushFont(g_medium_font);

      // code
      text_width = ImGui::CalcTextSize(selected_entry->code.c_str(), nullptr, false, work_width).x;
      ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
      ImGui::TextWrapped("%s", selected_entry->code.c_str());
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);

      // region
      ImGui::TextUnformatted("Region: ");
      ImGui::SameLine();
      ImGui::Image(s_disc_region_textures[static_cast<u32>(selected_entry->region)]->GetHandle(),
                   LayoutScale(23.0f, 16.0f));
      ImGui::SameLine();
      ImGui::Text(" (%s)", Settings::GetDiscRegionDisplayName(selected_entry->region));

      // compatibility
      ImGui::TextUnformatted("Compatibility: ");
      ImGui::SameLine();
      ImGui::Image(s_game_compatibility_textures[static_cast<u32>(selected_entry->compatibility_rating)]->GetHandle(),
                   LayoutScale(64.0f, 16.0f));
      ImGui::SameLine();
      ImGui::Text(" (%s)", GameList::GetGameListCompatibilityRatingString(selected_entry->compatibility_rating));

      // size
      ImGui::Text("Size: %.2f MB", static_cast<float>(selected_entry->total_size) / 1048576.0f);

      // TODO: last played
      ImGui::Text("Last Played: Never");

      // TODO: game settings
      ImGui::Text("4 Per-Game Settings Set");

      ImGui::PopFont();

      ImGui::EndGroup();
      ImGui::PopStyleVar();
    }

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    BeginMenuButtons(1, false);
    if (ActiveButton(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();
    EndMenuButtons();
  }
  EndFullscreenColumnWindow();

  if (BeginFullscreenColumnWindow(1220.0f, LAYOUT_SCREEN_WIDTH, "game_list_quick_select"))
  {
    const float height = 24.0f;
    BeginMenuButtons(29, false, 0.0f, 0.0f);

    ImGui::SetCursorPos(LayoutScale(ImVec2(17.0f, 4.0f)));
    ImGui::PushFont(g_large_font);
    ImGui::TextUnformatted(ICON_KI_BUTTON_LB);
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiFullscreen::UIPrimaryDisabledTextColor());
    ActiveButton("0", false, false, height, g_medium_font);
    ImGui::PopStyleColor();

    for (char letter = 'A'; letter <= 'Z'; letter++)
    {
      TinyString str;
      str.Format("%c", letter);
      ActiveButton(str, false, true, height, g_medium_font);
    }

    ImGui::SetCursorPosX(LayoutScale(17.0f));
    ImGui::PushFont(g_large_font);
    ImGui::TextUnformatted(ICON_KI_BUTTON_RB);
    ImGui::PopFont();

    EndMenuButtons();
  }
  EndFullscreenColumnWindow();

  EndFullscreenColumns();
}

void EnsureGameListLoaded()
{
  // not worth using a condvar here
  if (s_game_list_load_thread.joinable())
    s_game_list_load_thread.join();
}

static void GameListRefreshThread()
{
  ProgressCallback cb("game_list_refresh");
  s_host_interface->GetGameList()->Refresh(false, false, &cb);
}

void QueueGameListRefresh()
{
  if (s_game_list_load_thread.joinable())
    s_game_list_load_thread.join();

  s_host_interface->GetGameList()->SetSearchDirectoriesFromSettings(*s_settings_interface);
  s_game_list_load_thread = std::thread(GameListRefreshThread);
}

void SwitchToGameList()
{
  EnsureGameListLoaded();
  s_current_main_window = MainWindowType::GameList;
}

HostDisplayTexture* GetGameListCover(const GameListEntry* entry)
{
  // lookup and grab cover image
  auto cover_it = s_cover_image_map.find(entry->path);
  if (cover_it == s_cover_image_map.end())
  {
    const std::string cover_path(s_host_interface->GetGameList()->GetCoverImagePathForEntry(entry));
    std::unique_ptr<HostDisplayTexture> texture;
    if (!cover_path.empty())
    {
      Log_DevPrintf("Trying to load cover from '%s' for '%s'", cover_path.c_str(), entry->path.c_str());

      Common::RGBA8Image image;
      if (Common::LoadImageFromFile(&image, cover_path.c_str()) || !image.IsValid())
      {
        texture = s_host_interface->GetDisplay()->CreateTexture(image.GetWidth(), image.GetHeight(), image.GetPixels(),
                                                                image.GetByteStride());
        if (!texture)
          Log_ErrorPrintf("Failed to upload %ux%u texture to GPU", image.GetWidth(), image.GetHeight());
      }
      else
      {
        Log_ErrorPrintf("Failed to load cover from '%s'", cover_path.c_str());
      }
    }

    cover_it = s_cover_image_map.emplace(entry->path, std::move(texture)).first;
  }

  return cover_it->second ? cover_it->second.get() : s_placeholder_texture.get();
}

HostDisplayTexture* GetCoverForCurrentGame()
{
  EnsureGameListLoaded();

  const GameListEntry* entry = s_host_interface->GetGameList()->GetEntryForPath(System::GetRunningPath().c_str());
  if (!entry)
    return s_placeholder_texture.get();

  return GetGameListCover(entry);
}

//////////////////////////////////////////////////////////////////////////
// Debug Menu
//////////////////////////////////////////////////////////////////////////

void SetDebugMenuEnabled(bool enabled, bool save_to_ini)
{
  if (s_debug_menu_enabled == enabled)
    return;

  const float size = enabled ? DPIScale(LAYOUT_MAIN_MENU_BAR_SIZE) : 0.0f;
  s_host_interface->GetDisplay()->SetDisplayTopMargin(static_cast<s32>(size));
  ImGuiFullscreen::SetMenuBarSize(size);
  s_debug_menu_enabled = enabled;

  if (save_to_ini)
  {
    s_settings_interface->SetBoolValue("Main", "ShowDebugMenu", enabled);
    s_settings_interface->Save();
  }
}

static void DrawDebugStats();
static void DrawDebugSystemMenu();
static void DrawDebugSettingsMenu();
static void DrawDebugDebugMenu();

void DrawDebugMenu()
{
  if (!s_debug_menu_enabled)
    return;

  if (!ImGui::BeginMainMenuBar())
    return;

  if (ImGui::BeginMenu("System"))
  {
    DrawDebugSystemMenu();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Settings"))
  {
    DrawDebugSettingsMenu();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Debug"))
  {
    DrawDebugDebugMenu();
    ImGui::EndMenu();
  }

  DrawDebugStats();

  ImGui::EndMainMenuBar();
}

void DrawDebugStats()
{
  if (!System::IsShutdown())
  {
    const float framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale.x;

    if (System::IsPaused())
    {
      ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - (50.0f * framebuffer_scale));
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Paused");
    }
    else
    {
      ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - (420.0f * framebuffer_scale));
      ImGui::Text("Average: %.2fms", System::GetAverageFrameTime());

      ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - (310.0f * framebuffer_scale));
      ImGui::Text("Worst: %.2fms", System::GetWorstFrameTime());

      ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - (210.0f * framebuffer_scale));

      const float speed = System::GetEmulationSpeed();
      const u32 rounded_speed = static_cast<u32>(std::round(speed));
      if (speed < 90.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%u%%", rounded_speed);
      else if (speed < 110.0f)
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%u%%", rounded_speed);
      else
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%u%%", rounded_speed);

      ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - (165.0f * framebuffer_scale));
      ImGui::Text("FPS: %.2f", System::GetFPS());

      ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - (80.0f * framebuffer_scale));
      ImGui::Text("VPS: %.2f", System::GetVPS());
    }
  }
}

void DrawDebugSystemMenu()
{
  const bool system_enabled = static_cast<bool>(!System::IsShutdown());

  if (ImGui::MenuItem("Start Disc", nullptr, false, !system_enabled))
  {
    DoStartFile();
    ClearImGuiFocus();
  }

  if (ImGui::MenuItem("Start BIOS", nullptr, false, !system_enabled))
  {
    DoStartBIOS();
    ClearImGuiFocus();
  }

  ImGui::Separator();

  if (ImGui::MenuItem("Power Off", nullptr, false, system_enabled))
  {
    DoPowerOff();
    ClearImGuiFocus();
  }

  if (ImGui::MenuItem("Reset", nullptr, false, system_enabled))
  {
    DoReset();
    ClearImGuiFocus();
  }

  if (ImGui::MenuItem("Pause", nullptr, System::IsPaused(), system_enabled))
  {
    DoPause();
    ClearImGuiFocus();
  }

  ImGui::Separator();

  if (ImGui::MenuItem("Change Disc", nullptr, false, system_enabled))
  {
#if 0
    DoChangeDisc();
#endif
    ClearImGuiFocus();
  }

  if (ImGui::MenuItem("Remove Disc", nullptr, false, system_enabled))
  {
    s_host_interface->RunLater([]() { System::RemoveMedia(); });
    ClearImGuiFocus();
  }

  if (ImGui::MenuItem("Frame Step", nullptr, false, system_enabled))
  {
#if 0
    s_host_interface->RunLater([]() { DoFrameStep(); });
#endif
    ClearImGuiFocus();
  }

  ImGui::Separator();

  if (ImGui::BeginMenu("Load State"))
  {
    for (u32 i = 1; i <= CommonHostInterface::GLOBAL_SAVE_STATE_SLOTS; i++)
    {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "State %u", i);
      if (ImGui::MenuItem(buf))
      {
        s_host_interface->RunLater([i]() { s_host_interface->LoadState(true, i); });
        ClearImGuiFocus();
      }
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Save State", system_enabled))
  {
    for (u32 i = 1; i <= CommonHostInterface::GLOBAL_SAVE_STATE_SLOTS; i++)
    {
      TinyString buf;
      buf.Format("State %u", i);
      if (ImGui::MenuItem(buf))
      {
        s_host_interface->RunLater([i]() { s_host_interface->SaveState(true, i); });
        ClearImGuiFocus();
      }
    }
    ImGui::EndMenu();
  }

  ImGui::Separator();

  if (ImGui::BeginMenu("Cheats", system_enabled))
  {
    const bool has_cheat_file = System::HasCheatList();
    if (ImGui::BeginMenu("Enabled Cheats", has_cheat_file))
    {
      CheatList* cl = System::GetCheatList();
      for (u32 i = 0; i < cl->GetCodeCount(); i++)
      {
        const CheatCode& cc = cl->GetCode(i);
        if (ImGui::MenuItem(cc.description.c_str(), nullptr, cc.enabled, true))
          s_host_interface->SetCheatCodeState(i, !cc.enabled, g_settings.auto_load_cheats);
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Apply Cheat", has_cheat_file))
    {
      CheatList* cl = System::GetCheatList();
      for (u32 i = 0; i < cl->GetCodeCount(); i++)
      {
        const CheatCode& cc = cl->GetCode(i);
        if (ImGui::MenuItem(cc.description.c_str()))
          s_host_interface->ApplyCheatCode(i);
      }

      ImGui::EndMenu();
    }

    ImGui::EndMenu();
  }

  ImGui::Separator();

  if (ImGui::MenuItem("Exit"))
    s_host_interface->RequestExit();
}

void DrawDebugSettingsMenu()
{
  bool settings_changed = false;

  if (ImGui::BeginMenu("CPU Execution Mode"))
  {
    const CPUExecutionMode current = s_settings_copy.cpu_execution_mode;
    for (u32 i = 0; i < static_cast<u32>(CPUExecutionMode::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetCPUExecutionModeDisplayName(static_cast<CPUExecutionMode>(i)), nullptr,
                          i == static_cast<u32>(current)))
      {
        s_settings_copy.cpu_execution_mode = static_cast<CPUExecutionMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::MenuItem("CPU Clock Control", nullptr, &s_settings_copy.cpu_overclock_enable))
  {
    settings_changed = true;
    s_settings_copy.UpdateOverclockActive();
  }

  if (ImGui::BeginMenu("CPU Clock Speed"))
  {
    static constexpr auto values = make_array(10u, 25u, 50u, 75u, 100u, 125u, 150u, 175u, 200u, 225u, 250u, 275u, 300u,
                                              350u, 400u, 450u, 500u, 600u, 700u, 800u);
    const u32 percent = s_settings_copy.GetCPUOverclockPercent();
    for (u32 value : values)
    {
      if (ImGui::MenuItem(TinyString::FromFormat("%u%%", value), nullptr, percent == value))
      {
        s_settings_copy.SetCPUOverclockPercent(value);
        s_settings_copy.UpdateOverclockActive();
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |=
    ImGui::MenuItem("Recompiler Memory Exceptions", nullptr, &s_settings_copy.cpu_recompiler_memory_exceptions);
  if (ImGui::BeginMenu("Recompiler Fastmem"))
  {
    for (u32 i = 0; i < static_cast<u32>(CPUFastmemMode::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetCPUFastmemModeDisplayName(static_cast<CPUFastmemMode>(i)), nullptr,
                          s_settings_copy.cpu_fastmem_mode == static_cast<CPUFastmemMode>(i)))
      {
        s_settings_copy.cpu_fastmem_mode = static_cast<CPUFastmemMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("Recompiler ICache", nullptr, &s_settings_copy.cpu_recompiler_icache);

  ImGui::Separator();

  if (ImGui::BeginMenu("Renderer"))
  {
    const GPURenderer current = s_settings_copy.gpu_renderer;
    for (u32 i = 0; i < static_cast<u32>(GPURenderer::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetRendererDisplayName(static_cast<GPURenderer>(i)), nullptr,
                          i == static_cast<u32>(current)))
      {
        s_settings_copy.gpu_renderer = static_cast<GPURenderer>(i);
        settings_changed = true;
      }
    }

    settings_changed |= ImGui::MenuItem("GPU on Thread", nullptr, &s_settings_copy.gpu_use_thread);

    ImGui::EndMenu();
  }

  bool fullscreen = s_host_interface->IsFullscreen();
  if (ImGui::MenuItem("Fullscreen", nullptr, &fullscreen))
    s_host_interface->RunLater([fullscreen] { s_host_interface->SetFullscreen(fullscreen); });

  if (ImGui::BeginMenu("Resize to Game", System::IsValid()))
  {
    static constexpr auto scales = make_array(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    for (const u32 scale : scales)
    {
      if (ImGui::MenuItem(TinyString::FromFormat("%ux Scale", scale)))
        s_host_interface->RunLater(
          [scale]() { s_host_interface->RequestRenderWindowScale(static_cast<float>(scale)); });
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("VSync", nullptr, &s_settings_copy.video_sync_enabled);

  ImGui::Separator();

  if (ImGui::BeginMenu("Resolution Scale"))
  {
    const u32 current_internal_resolution = s_settings_copy.gpu_resolution_scale;
    for (u32 scale = 1; scale <= GPU::MAX_RESOLUTION_SCALE; scale++)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%ux (%ux%u)", scale, scale * VRAM_WIDTH, scale * VRAM_HEIGHT);

      if (ImGui::MenuItem(buf, nullptr, current_internal_resolution == scale))
      {
        s_settings_copy.gpu_resolution_scale = scale;
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Multisampling"))
  {
    const u32 current_multisamples = s_settings_copy.gpu_multisamples;
    const bool current_ssaa = s_settings_copy.gpu_per_sample_shading;

    if (ImGui::MenuItem("None", nullptr, (current_multisamples == 1)))
    {
      s_settings_copy.gpu_multisamples = 1;
      s_settings_copy.gpu_per_sample_shading = false;
      settings_changed = true;
    }

    for (u32 i = 2; i <= 32; i *= 2)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%ux MSAA", i);

      if (ImGui::MenuItem(buf, nullptr, (current_multisamples == i && !current_ssaa)))
      {
        s_settings_copy.gpu_multisamples = i;
        s_settings_copy.gpu_per_sample_shading = false;
        settings_changed = true;
      }
    }

    for (u32 i = 2; i <= 32; i *= 2)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%ux SSAA", i);

      if (ImGui::MenuItem(buf, nullptr, (current_multisamples == i && current_ssaa)))
      {
        s_settings_copy.gpu_multisamples = i;
        s_settings_copy.gpu_per_sample_shading = true;
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("PGXP"))
  {
    settings_changed |= ImGui::MenuItem("PGXP Enabled", nullptr, &s_settings_copy.gpu_pgxp_enable);
    settings_changed |=
      ImGui::MenuItem("PGXP Culling", nullptr, &s_settings_copy.gpu_pgxp_culling, s_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Texture Correction", nullptr,
                                        &s_settings_copy.gpu_pgxp_texture_correction, s_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Vertex Cache", nullptr, &s_settings_copy.gpu_pgxp_vertex_cache,
                                        s_settings_copy.gpu_pgxp_enable);
    settings_changed |=
      ImGui::MenuItem("PGXP CPU Instructions", nullptr, &s_settings_copy.gpu_pgxp_cpu, s_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Preserve Projection Precision", nullptr,
                                        &s_settings_copy.gpu_pgxp_preserve_proj_fp, s_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Depth Buffer", nullptr, &s_settings_copy.gpu_pgxp_depth_buffer,
                                        s_settings_copy.gpu_pgxp_enable);
    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("True (24-Bit) Color", nullptr, &s_settings_copy.gpu_true_color);
  settings_changed |= ImGui::MenuItem("Scaled Dithering", nullptr, &s_settings_copy.gpu_scaled_dithering);

  if (ImGui::BeginMenu("Texture Filtering"))
  {
    const GPUTextureFilter current = s_settings_copy.gpu_texture_filter;
    for (u32 i = 0; i < static_cast<u32>(GPUTextureFilter::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetTextureFilterDisplayName(static_cast<GPUTextureFilter>(i)), nullptr,
                          i == static_cast<u32>(current)))
      {
        s_settings_copy.gpu_texture_filter = static_cast<GPUTextureFilter>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Disable Interlacing", nullptr, &s_settings_copy.gpu_disable_interlacing);
  settings_changed |= ImGui::MenuItem("Widescreen Hack", nullptr, &s_settings_copy.gpu_widescreen_hack);
  settings_changed |= ImGui::MenuItem("Force NTSC Timings", nullptr, &s_settings_copy.gpu_force_ntsc_timings);
  settings_changed |= ImGui::MenuItem("24-Bit Chroma Smoothing", nullptr, &s_settings_copy.gpu_24bit_chroma_smoothing);

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Display Linear Filtering", nullptr, &s_settings_copy.display_linear_filtering);
  settings_changed |= ImGui::MenuItem("Display Integer Scaling", nullptr, &s_settings_copy.display_integer_scaling);

  if (ImGui::BeginMenu("Aspect Ratio"))
  {
    for (u32 i = 0; i < static_cast<u32>(DisplayAspectRatio::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetDisplayAspectRatioName(static_cast<DisplayAspectRatio>(i)), nullptr,
                          s_settings_copy.display_aspect_ratio == static_cast<DisplayAspectRatio>(i)))
      {
        s_settings_copy.display_aspect_ratio = static_cast<DisplayAspectRatio>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Crop Mode"))
  {
    for (u32 i = 0; i < static_cast<u32>(DisplayCropMode::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetDisplayCropModeDisplayName(static_cast<DisplayCropMode>(i)), nullptr,
                          s_settings_copy.display_crop_mode == static_cast<DisplayCropMode>(i)))
      {
        s_settings_copy.display_crop_mode = static_cast<DisplayCropMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Downsample Mode"))
  {
    for (u32 i = 0; i < static_cast<u32>(GPUDownsampleMode::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetDownsampleModeDisplayName(static_cast<GPUDownsampleMode>(i)), nullptr,
                          s_settings_copy.gpu_downsample_mode == static_cast<GPUDownsampleMode>(i)))
      {
        s_settings_copy.gpu_downsample_mode = static_cast<GPUDownsampleMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("Force 4:3 For 24-bit", nullptr, &s_settings_copy.display_force_4_3_for_24bit);

  ImGui::Separator();

  if (ImGui::MenuItem("Dump Audio", nullptr, s_host_interface->IsDumpingAudio(), System::IsValid()))
  {
    if (!s_host_interface->IsDumpingAudio())
      s_host_interface->StartDumpingAudio();
    else
      s_host_interface->StopDumpingAudio();
  }

  if (ImGui::MenuItem("Save Screenshot"))
    s_host_interface->RunLater([]() { s_host_interface->SaveScreenshot(); });

  if (settings_changed)
    s_host_interface->RunLater(SaveAndApplySettings);
}

void DrawDebugDebugMenu()
{
  const bool system_valid = System::IsValid();
  Settings::DebugSettings& debug_settings = g_settings.debugging;
  bool settings_changed = false;

  if (ImGui::BeginMenu("Log Level"))
  {
    for (u32 i = LOGLEVEL_NONE; i < LOGLEVEL_COUNT; i++)
    {
      if (ImGui::MenuItem(Settings::GetLogLevelDisplayName(static_cast<LOGLEVEL>(i)), nullptr,
                          g_settings.log_level == static_cast<LOGLEVEL>(i)))
      {
        s_settings_copy.log_level = static_cast<LOGLEVEL>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("Log To Console", nullptr, &s_settings_copy.log_to_console);
  settings_changed |= ImGui::MenuItem("Log To Debug", nullptr, &s_settings_copy.log_to_debug);
  settings_changed |= ImGui::MenuItem("Log To File", nullptr, &s_settings_copy.log_to_file);

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Disable All Enhancements", nullptr, &s_settings_copy.disable_all_enhancements);
  settings_changed |= ImGui::MenuItem("Dump CPU to VRAM Copies", nullptr, &debug_settings.dump_cpu_to_vram_copies);
  settings_changed |= ImGui::MenuItem("Dump VRAM to CPU Copies", nullptr, &debug_settings.dump_vram_to_cpu_copies);

  if (ImGui::MenuItem("CPU Trace Logging", nullptr, CPU::IsTraceEnabled()))
  {
    if (!CPU::IsTraceEnabled())
      CPU::StartTrace();
    else
      CPU::StopTrace();
  }

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Show VRAM", nullptr, &debug_settings.show_vram);
  settings_changed |= ImGui::MenuItem("Show GPU State", nullptr, &debug_settings.show_gpu_state);
  settings_changed |= ImGui::MenuItem("Show CDROM State", nullptr, &debug_settings.show_cdrom_state);
  settings_changed |= ImGui::MenuItem("Show SPU State", nullptr, &debug_settings.show_spu_state);
  settings_changed |= ImGui::MenuItem("Show Timers State", nullptr, &debug_settings.show_timers_state);
  settings_changed |= ImGui::MenuItem("Show MDEC State", nullptr, &debug_settings.show_mdec_state);
  settings_changed |= ImGui::MenuItem("Show DMA State", nullptr, &debug_settings.show_dma_state);

  if (settings_changed)
  {
    // have to apply it to the copy too, otherwise it won't save
    Settings::DebugSettings& debug_settings_copy = s_settings_copy.debugging;
    debug_settings_copy.show_gpu_state = debug_settings.show_gpu_state;
    debug_settings_copy.show_vram = debug_settings.show_vram;
    debug_settings_copy.dump_cpu_to_vram_copies = debug_settings.dump_cpu_to_vram_copies;
    debug_settings_copy.dump_vram_to_cpu_copies = debug_settings.dump_vram_to_cpu_copies;
    debug_settings_copy.show_cdrom_state = debug_settings.show_cdrom_state;
    debug_settings_copy.show_spu_state = debug_settings.show_spu_state;
    debug_settings_copy.show_timers_state = debug_settings.show_timers_state;
    debug_settings_copy.show_mdec_state = debug_settings.show_mdec_state;
    debug_settings_copy.show_dma_state = debug_settings.show_dma_state;
    s_host_interface->RunLater(SaveAndApplySettings);
  }
}

} // namespace FullscreenUI