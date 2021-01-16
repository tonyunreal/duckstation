#define IMGUI_DEFINE_MATH_OPERATORS

#include "fullscreen_ui.h"
#include "IconsFontAwesome5.h"
#include "IconsKenney.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_util.h"
#include "common_host_interface.h"
#include "core/host_display.h"
#include "core/host_interface_progress_callback.h"
#include "core/resources.h"
#include "core/settings.h"
#include "core/system.h"
#include "game_list.h"
#include "icon.h"
#include "imgui.h"
#include "imgui_fullscreen.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "imgui_styles.h"
#include "scmversion/scmversion.h"
Log_SetChannel(FullscreenUI);

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
using ImGuiFullscreen::EndFullscreenColumns;
using ImGuiFullscreen::EndFullscreenColumnWindow;
using ImGuiFullscreen::EndFullscreenWindow;
using ImGuiFullscreen::EndMenuButtons;
using ImGuiFullscreen::EnumChoiceButton;
using ImGuiFullscreen::LayoutScale;
using ImGuiFullscreen::MenuButton;
using ImGuiFullscreen::MenuImageButton;
using ImGuiFullscreen::OpenChoiceDialog;
using ImGuiFullscreen::OpenFileSelector;
using ImGuiFullscreen::ToggleButton;

namespace FullscreenUI {

//////////////////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////////////////
static void ReturnToMainWindow();
static void DrawLandingWindow();
static void DrawSettingsWindow();
static void DrawQuickMenu();
static void ClearImGuiFocus();

static CommonHostInterface* s_host_interface;
static SettingsInterface* s_settings_interface;
static MainWindowType s_current_main_window = MainWindowType::Landing;
static SettingsPage s_settings_page = SettingsPage::InterfaceSettings;
static Settings s_settings_copy;

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
static void LoadGameList();
static void SwitchToGameList();
static HostDisplayTexture* GetGameListCover(const GameListEntry* entry);
static HostDisplayTexture* GetCoverForCurrentGame();

// Lazily populated cover images.
static std::unordered_map<std::string, std::unique_ptr<HostDisplayTexture>> s_cover_image_map;
static bool s_game_list_loaded = false;

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
  s_save_state_selector_slots.clear();
  s_cover_image_map.clear();
  s_game_list_loaded = false;
  DestroyResources();

  s_settings_interface = nullptr;
  s_host_interface = nullptr;
}

void Render()
{
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
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_FOLDER_OPEN "  Start File", "Launch a game by selecting a file/disc image."))
    {
      s_host_interface->RunLater([]() {
        const auto callback = [](const std::string& path) {
          if (!path.empty())
          {
            s_host_interface->RunLater([path]() {
              SystemBootParameters boot_params;
              boot_params.filename = std::move(path);
              s_host_interface->BootSystem(boot_params);
            });
          }
          CloseFileSelector();
        };

        OpenFileSelector(ICON_FA_COMPACT_DISC "  Select Disc Image", false, std::move(callback),
                         {"*.bin", "*.cue", "*.iso", "*.img", "*.chd", "*.psexe", "*.exe"});
      });
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_TOOLBOX "  Start BIOS", "Start the console without any disc inserted."))
    {
      s_host_interface->RunLater([]() {
        SystemBootParameters boot_params;
        s_host_interface->BootSystem(boot_params);
      });
      ClearImGuiFocus();
    }

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

        EndMenuButtons();
      }
      break;

      case SettingsPage::ConsoleSettings:
        break;

      case SettingsPage::ControllerSettings:
        break;

      case SettingsPage::HotkeySettings:
        break;

      case SettingsPage::MemoryCardSettings:
        break;

      case SettingsPage::DisplaySettings:
      {
        BeginMenuButtons(6, false);

        settings_changed |=
          EnumChoiceButton("GPU Renderer", "Chooses the backend to use for rendering the console/game visuals.",
                           &s_settings_copy.gpu_renderer, &Settings::GetRendererDisplayName, GPURenderer::Count);

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
        break;

      case SettingsPage::AdvancedSettings:
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

    ImGui::SetCursorPosY(LayoutScale(90.0f));

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
      s_host_interface->PowerOffSystem();

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

void LoadGameList()
{
  if (s_game_list_loaded)
    return;

  s_game_list_loaded = true;

  HostInterfaceProgressCallback cb;
  s_host_interface->GetGameList()->SetSearchDirectoriesFromSettings(*s_settings_interface);
  s_host_interface->GetGameList()->Refresh(false, false, &cb);
}

void RefreshGameList()
{
  s_game_list_loaded = false;
  LoadGameList();
}

void SwitchToGameList()
{
  LoadGameList();
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
  if (!s_game_list_loaded)
    s_host_interface->RunLater(LoadGameList);

  const GameListEntry* entry = s_host_interface->GetGameList()->GetEntryForPath(System::GetRunningPath().c_str());
  if (!entry)
    return s_placeholder_texture.get();

  return GetGameListCover(entry);
}

} // namespace FullscreenUI