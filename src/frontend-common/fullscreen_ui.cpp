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
Log_SetChannel(FullscreenUI);

using ImGuiFullscreen::BeginFullscreenColumnFractionWindow;
using ImGuiFullscreen::BeginFullscreenColumnWindow;
using ImGuiFullscreen::BeginFullscreenWindow;
using ImGuiFullscreen::BeginMenuButtons;
using ImGuiFullscreen::EndFullscreenWindow;
using ImGuiFullscreen::EndMenuButtons;
using ImGuiFullscreen::EnumChoiceButton;
using ImGuiFullscreen::LAYOUT_SCREEN_HEIGHT;
using ImGuiFullscreen::LAYOUT_SCREEN_WIDTH;
using ImGuiFullscreen::LayoutScale;
using ImGuiFullscreen::MenuButton;
using ImGuiFullscreen::MenuCategory;
using ImGuiFullscreen::MenuImageButton;
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
static std::unordered_map<std::string, std::unique_ptr<HostDisplayTexture>> m_cover_image_map;
static bool m_game_list_loaded = false;

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
  m_cover_image_map.clear();
  m_game_list_loaded = false;
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
      !(s_disc_region_textures[static_cast<u32>(DiscRegion::PAL)] = LoadTextureResource("flag-eu.png")))
  {
    return false;
  }

  return true;
}

void DestroyResources()
{
  s_app_icon_texture.reset();
  s_placeholder_texture.reset();
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
  if (BeginFullscreenColumnWindow(0.0f, 571.0f, "logo", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    ImGui::SetCursorPos(LayoutScale(ImVec2(120.0f, 170.0f)));
    ImGui::Image(s_app_icon_texture->GetHandle(), LayoutScale(ImVec2(380.0f, 380.0f)));
  }
  EndFullscreenWindow();

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
      // RunLater([this]() { DoStartDisc(); });
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
  }

  EndFullscreenWindow();
}

void DrawSettingsWindow()
{
  if (BeginFullscreenColumnWindow(0.0f, 300.0f, "settings_category", ImVec4(0.18f, 0.18f, 0.18f, 1.00f)))
  {
    static constexpr std::array<const char*, static_cast<u32>(SettingsPage::Count)> titles = {
      {ICON_FA_WINDOW_MAXIMIZE "  Interface Settings", ICON_FA_MICROCHIP "  BIOS Settings",
       ICON_FA_HDD "  Console Settings", ICON_FA_GAMEPAD "  Controller Settings", ICON_FA_KEYBOARD "  Hotkey Settings",
       ICON_FA_SD_CARD "  Memory Card Settings", ICON_FA_TV "  Display Settings",
       ICON_FA_MAGIC "  Enhancement Settings", ICON_FA_HEADPHONES "  Audio Settings",
       ICON_FA_EXCLAMATION_TRIANGLE "  Advanced Settings"}};

    BeginMenuButtons(static_cast<u32>(titles.size()) + 1u, false);
    for (u32 i = 0; i < static_cast<u32>(titles.size()); i++)
    {
      if (MenuCategory(titles[i], s_settings_page == static_cast<SettingsPage>(i)))
        s_settings_page = static_cast<SettingsPage>(i);
    }

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    if (MenuCategory(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();

    EndMenuButtons();
  }

  EndFullscreenWindow();

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
          &s_settings_copy.gpu_scaled_dithering);
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
                       &s_settings_copy.gpu_pgxp_enable, s_settings_copy.gpu_pgxp_enable);
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

  EndFullscreenWindow();
}

void DrawQuickMenu()
{
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, IM_COL32(255, 255, 255, 60));

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));

  if (BeginFullscreenWindow(-0.5f, -0.5f, 500.0f, 430.0f, "pause_menu", HEX_TO_IMVEC4(0x212121, 240)))
  {
    ImGui::SetCursorPos(LayoutScale(20.0f, 20.0f));
    ImGui::Image(GetCoverForCurrentGame()->GetHandle(), LayoutScale(50.0f, 50.0f));
    ImGui::SetCursorPos(LayoutScale(90.0f, 20.0f));
    ImGui::PushFont(ImGuiFullscreen::g_large_font);
    ImGui::TextUnformatted(System::GetRunningTitle().c_str());
    ImGui::PopFont();
    ImGui::SetCursorPosX(LayoutScale(90.0f));
    ImGui::PushFont(ImGuiFullscreen::g_medium_font);
    ImGui::TextUnformatted(System::GetRunningPath().c_str());
    ImGui::PopFont();

    ImGui::SetCursorPosY(LayoutScale(90.0f));

    BeginMenuButtons(9, false);

    MenuCategory(ICON_FA_BACKWARD "  Back To Game", false);
    MenuCategory(ICON_FA_UNDO "  Load State", false);
    MenuCategory(ICON_FA_SAVE "  Save State", false);
    MenuCategory(ICON_FA_FAST_FORWARD "  Fast Forward", false);
    MenuCategory(ICON_FA_SYNC "  Reset", false);
    MenuCategory(ICON_FA_FROWN_OPEN "  Cheats", false);

    if (MenuCategory(ICON_FA_SLIDERS_H "  Settings", false))
      s_current_main_window = MainWindowType::Settings;

    if (MenuCategory(ICON_FA_POWER_OFF "  Exit Game", false))
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
      StringUtil::StdStringFromFormat("Global Slot %d - %s##global_slot_%d", ssi->slot, ssi->title.c_str(), ssi->slot);
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
  EndFullscreenWindow();

  if (BeginFullscreenColumnWindow(0.0f, 570.0f, "save_state_selector_preview", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    ImGui::SetCursorPos(LayoutScale(ImVec2(85.0f, 160.0f)));
    ImGui::Image(selected_texture ? selected_texture->GetHandle() : s_placeholder_texture->GetHandle(),
                 LayoutScale(ImVec2(400.0f, 400.0f)));

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    if (MenuCategory(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();
  }
  EndFullscreenWindow();
}

void DrawGameListWindow()
{
  const GameListEntry* selected_entry = nullptr;

  if (BeginFullscreenColumnWindow(1220.0f, LAYOUT_SCREEN_WIDTH, "game_list_quick_select"))
  {
    const float height = 24.0f;
    BeginMenuButtons(29, false);

    ImGui::SetCursorPos(LayoutScale(ImVec2(17.0f, 4.0f)));
    ImGui::PushFont(ImGuiFullscreen::g_large_font);
    ImGui::TextUnformatted(ICON_KI_BUTTON_LB);
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiFullscreen::UIPrimaryDisabledTextColor());
    MenuCategory("0", false, false, height, ImGuiFullscreen::g_medium_font);
    ImGui::PopStyleColor();

    for (char letter = 'A'; letter <= 'Z'; letter++)
    {
      TinyString str;
      str.Format("%c", letter);
      MenuCategory(str, false, true, height, ImGuiFullscreen::g_medium_font);
    }

    ImGui::SetCursorPosX(LayoutScale(17.0f));
    ImGui::PushFont(ImGuiFullscreen::g_large_font);
    ImGui::TextUnformatted(ICON_KI_BUTTON_RB);
    ImGui::PopFont();

    EndMenuButtons();
  }
  EndFullscreenWindow();

  if (BeginFullscreenColumnWindow(450.0f, 1220.0f, "game_list_entries"))
  {
    BeginMenuButtons(s_host_interface->GetGameList()->GetEntryCount(), false);

    for (const GameListEntry& entry : s_host_interface->GetGameList()->GetEntries())
    {
      const HostDisplayTexture* cover_texture = GetGameListCover(&entry);
      const float cover_ar =
        static_cast<float>(cover_texture->GetWidth()) / static_cast<float>(cover_texture->GetHeight());
      if (MenuButton(entry.title.c_str(), entry.path.c_str()))
      {
        // launch game
      }

      if (ImGui::IsItemHovered())
        selected_entry = &entry;
    }

    EndMenuButtons();
  }
  EndFullscreenWindow();

  if (BeginFullscreenColumnWindow(0.0f, 450.0f, "game_list_info", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    const auto* window = ImGui::GetCurrentWindow();
    const ImVec2 base_pos(window->DC.CursorPos);

    ImGui::SetCursorPos(LayoutScale(ImVec2(50.0f, 50.0f)));
    ImGui::Image(selected_entry ? GetGameListCover(selected_entry)->GetHandle() : s_placeholder_texture->GetHandle(),
                 LayoutScale(ImVec2(350.0f, 350.0f)));

    if (selected_entry)
    {
      const float field_margin_y = 10.0f;
      const float start_x = 50.0f;
      const float end_x = 400.0f;
      float text_y = 425.0f;
      SmallString text;

      // title
      ImGui::PushFont(ImGuiFullscreen::g_large_font);
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_LARGE_FONT_SIZE)),
                               selected_entry->title.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.0f), nullptr);
      ImGui::PopFont();
      text_y += ImGuiFullscreen::LAYOUT_LARGE_FONT_SIZE + field_margin_y;

      ImGui::PushFont(ImGuiFullscreen::g_medium_font);

      // code
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE)),
                               selected_entry->code.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.0f), nullptr);
      text_y += ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE + 25.0f;

      // region
      text.Format("Region: %s", Settings::GetDiscRegionDisplayName(selected_entry->region));
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE)),
                               text, nullptr, nullptr, ImVec2(0.0f, 0.0f), nullptr);
      text_y += ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE + field_margin_y;

      // size
      text.Format("Size: %.2f MB", static_cast<float>(selected_entry->total_size) / 1048576.0f);
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE)),
                               text, nullptr, nullptr, ImVec2(0.0f, 0.0f), nullptr);
      text_y += ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE + field_margin_y;

      // compatibility
      text.Format("Compatibility: %s",
                  GameList::GetGameListCompatibilityRatingString(selected_entry->compatibility_rating));
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE)),
                               text, nullptr, nullptr, ImVec2(0.0f, 0.0f), nullptr);
      text_y += ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE + field_margin_y;

      // TODO: last played
      text.Format("Last Played: Never");
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE)),
                               text, nullptr, nullptr, ImVec2(0.0f, 0.0f), nullptr);
      text_y += ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE + field_margin_y;

      // TODO: game settings
      text.Format("4 Per-Game Settings Set");
      ImGui::RenderTextClipped(base_pos + LayoutScale(ImVec2(start_x, text_y)),
                               base_pos + LayoutScale(ImVec2(end_x, text_y + ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE)),
                               text, nullptr, nullptr, ImVec2(0.0f, 0.0f), nullptr);
      text_y += ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE + field_margin_y;

      ImGui::PopFont();
    }

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    if (MenuCategory(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();
  }
  EndFullscreenWindow();
}

void LoadGameList()
{
  if (m_game_list_loaded)
    return;

  HostInterfaceProgressCallback cb;
  s_host_interface->GetGameList()->SetSearchDirectoriesFromSettings(*s_settings_interface);
  s_host_interface->GetGameList()->Refresh(false, false, &cb);
  m_game_list_loaded = true;
}

void SwitchToGameList()
{
  LoadGameList();
  s_current_main_window = MainWindowType::GameList;
}

HostDisplayTexture* GetGameListCover(const GameListEntry* entry)
{
  // lookup and grab cover image
  auto cover_it = m_cover_image_map.find(entry->path);
  if (cover_it == m_cover_image_map.end())
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

    cover_it = m_cover_image_map.emplace(entry->path, std::move(texture)).first;
  }

  return cover_it->second ? cover_it->second.get() : s_placeholder_texture.get();
}

HostDisplayTexture* GetCoverForCurrentGame()
{
  if (!m_game_list_loaded)
    s_host_interface->RunLater(LoadGameList);

  const GameListEntry* entry = s_host_interface->GetGameList()->GetEntryForPath(System::GetRunningPath().c_str());
  if (!entry)
    return s_placeholder_texture.get();

  return GetGameListCover(entry);
}

} // namespace FullscreenUI