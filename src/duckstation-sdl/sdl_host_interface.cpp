#include "sdl_host_interface.h"
#include "common/assert.h"
#include "common/byte_stream.h"
#include "common/file_system.h"
#include "common/image.h"
#include "common/log.h"
#include "common/make_array.h"
#include "common/string_util.h"
#include "core/cheats.h"
#include "core/controller.h"
#include "core/cpu_core.h"
#include "core/gpu.h"
#include "core/host_display.h"
#include "core/host_interface_progress_callback.h"
#include "core/system.h"
#include "frontend-common/IconsFontAwesome5.h"
#include "frontend-common/IconsKenney.h"
#include "frontend-common/game_list.h"
#include "frontend-common/icon.h"
#include "frontend-common/imgui_fullscreen.h"
#include "frontend-common/imgui_styles.h"
#include "frontend-common/ini_settings_interface.h"
#include "frontend-common/opengl_host_display.h"
#include "frontend-common/sdl_audio_stream.h"
#include "frontend-common/sdl_controller_interface.h"
#include "frontend-common/vulkan_host_display.h"
#include "imgui_impl_sdl.h"
#include "scmversion/scmversion.h"
#include "sdl_key_names.h"
#include "sdl_util.h"
#include <cinttypes>
#include <cmath>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nfd.h>
Log_SetChannel(SDLHostInterface);

#ifdef WIN32
#include "frontend-common/d3d11_host_display.h"
#endif

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

SDLHostInterface::SDLHostInterface()
{
  m_run_later_event_id = SDL_RegisterEvents(1);
}

SDLHostInterface::~SDLHostInterface() = default;

const char* SDLHostInterface::GetFrontendName() const
{
  return "DuckStation SDL/ImGui Frontend";
}

ALWAYS_INLINE static TinyString GetWindowTitle()
{
  return TinyString::FromFormat("DuckStation %s (%s)", g_scm_tag_str, g_scm_branch_str);
}

bool SDLHostInterface::CreateSDLWindow()
{
  static constexpr u32 DEFAULT_WINDOW_WIDTH = 1280;
  static constexpr u32 DEFAULT_WINDOW_HEIGHT = 720 + 20;
  // static constexpr u32 DEFAULT_WINDOW_WIDTH = 900;
  // static constexpr u32 DEFAULT_WINDOW_HEIGHT = 700;

  // Create window.
  const u32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  u32 window_width = DEFAULT_WINDOW_WIDTH;
  u32 window_height = DEFAULT_WINDOW_HEIGHT;

  // macOS does DPI scaling differently..
#ifndef __APPLE__
  {
    // scale by default monitor's DPI
    float scale = SDLUtil::GetDPIScaleFactor(nullptr);
    window_width = static_cast<u32>(std::round(static_cast<float>(window_width) * scale));
    window_height = static_cast<u32>(std::round(static_cast<float>(window_height) * scale));
  }
#endif

  m_window = SDL_CreateWindow(GetWindowTitle(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width,
                              window_height, window_flags);
  if (!m_window)
    return false;

  // Set window icon.
  SDL_Surface* icon_surface =
    SDL_CreateRGBSurfaceFrom(const_cast<unsigned int*>(WINDOW_ICON_DATA), WINDOW_ICON_WIDTH, WINDOW_ICON_HEIGHT, 32,
                             WINDOW_ICON_WIDTH * sizeof(u32), UINT32_C(0x000000FF), UINT32_C(0x0000FF00),
                             UINT32_C(0x00FF0000), UINT32_C(0xFF000000));
  if (icon_surface)
  {
    SDL_SetWindowIcon(m_window, icon_surface);
    SDL_FreeSurface(icon_surface);
  }

  if (m_fullscreen)
    SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);

  // Process events so that we have everything sorted out before creating a child window for the GL context (X11).
  SDL_PumpEvents();
  return true;
}

void SDLHostInterface::DestroySDLWindow()
{
  SDL_DestroyWindow(m_window);
  m_window = nullptr;
}

bool SDLHostInterface::CreateDisplay()
{
  std::optional<WindowInfo> wi = SDLUtil::GetWindowInfoForSDLWindow(m_window);
  if (!wi.has_value())
  {
    ReportError("Failed to get window info from SDL window");
    return false;
  }

  switch (g_settings.gpu_renderer)
  {
    case GPURenderer::HardwareVulkan:
      m_display = std::make_unique<FrontendCommon::VulkanHostDisplay>();
      break;

    case GPURenderer::HardwareOpenGL:
#ifndef WIN32
    default:
#endif
      m_display = std::make_unique<FrontendCommon::OpenGLHostDisplay>();
      break;

#ifdef WIN32
    case GPURenderer::HardwareD3D11:
    default:
      m_display = std::make_unique<FrontendCommon::D3D11HostDisplay>();
      break;
#endif
  }

  Assert(m_display);
  if (!m_display->CreateRenderDevice(wi.value(), g_settings.gpu_adapter, g_settings.gpu_use_debug_device,
                                     g_settings.gpu_threaded_presentation) ||
      !m_display->InitializeRenderDevice(GetShaderCacheBasePath(), g_settings.gpu_use_debug_device,
                                         g_settings.gpu_threaded_presentation))
  {
    ReportError("Failed to create/initialize display render device");
    m_display.reset();
    return false;
  }

  // safe to init imgui data structures now
  ImGui::GetIO().DisplaySize =
    ImVec2(static_cast<float>(m_display->GetWindowWidth()), static_cast<float>(m_display->GetWindowHeight()));
  ImGuiFullscreen::UpdateLayoutScale();
  ImGuiFullscreen::UpdateFonts();

  bool imgui_result;
  switch (m_display->GetRenderAPI())
  {
#ifdef WIN32
    case HostDisplay::RenderAPI::D3D11:
      imgui_result = ImGui_ImplSDL2_InitForD3D(m_window);
      break;
#endif

    case HostDisplay::RenderAPI::Vulkan:
      imgui_result = ImGui_ImplSDL2_InitForVulkan(m_window);
      break;

    case HostDisplay::RenderAPI::OpenGL:
    case HostDisplay::RenderAPI::OpenGLES:
      imgui_result = ImGui_ImplSDL2_InitForOpenGL(m_window, nullptr);
      break;

    default:
      imgui_result = true;
      break;
  }
  if (!imgui_result || !m_display->CreateImGuiContext())
  {
    ReportError("Failed to initialize ImGui on device");
    m_display->DestroyRenderDevice();
    m_display.reset();
    return false;
  }

  if (!LoadResources())
  {
    m_display->DestroyImGuiContext();
    m_display->DestroyRenderDevice();
    m_display.reset();
    return false;
  }

  m_display->SetDisplayTopMargin(m_fullscreen ? 0 : static_cast<int>(20.0f * ImGui::GetIO().DisplayFramebufferScale.x));
  return true;
}

void SDLHostInterface::DestroyDisplay()
{
  ClearSaveStateListEntries();
  DestroyResources();
  m_display->DestroyImGuiContext();
  m_display->DestroyRenderDevice();
  m_display.reset();
}

void SDLHostInterface::CreateImGuiContext()
{
  const float framebuffer_scale = SDLUtil::GetDPIScaleFactor(m_window);

  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::GetIO().DisplayFramebufferScale.x = framebuffer_scale;
  ImGui::GetIO().DisplayFramebufferScale.y = framebuffer_scale;
  ImGui::GetStyle().ScaleAllSizes(framebuffer_scale);

  ImGui::StyleColorsDarker();
}

void SDLHostInterface::UpdateFramebufferScale()
{
  ImGuiIO& io = ImGui::GetIO();
  const float framebuffer_scale = SDLUtil::GetDPIScaleFactor(m_window);
  if (framebuffer_scale != io.DisplayFramebufferScale.x)
  {
    io.DisplayFramebufferScale = ImVec2(framebuffer_scale, framebuffer_scale);
    ImGui::Render();
    ImGui::GetStyle().ScaleAllSizes(framebuffer_scale);
    ImGui::NewFrame();
  }

  if (ImGuiFullscreen::UpdateLayoutScale())
  {
    ImGui::Render();

    if (ImGuiFullscreen::UpdateFonts())
    {
      if (!m_display->UpdateImGuiFontTexture())
        Panic("Failed to update font texture");
    }

    ImGui::NewFrame();
  }
}

bool SDLHostInterface::AcquireHostDisplay()
{
  // Handle renderer switch if required.
  const HostDisplay::RenderAPI render_api = m_display->GetRenderAPI();
  bool needs_switch = false;
  switch (g_settings.gpu_renderer)
  {
#ifdef WIN32
    case GPURenderer::HardwareD3D11:
      needs_switch = (render_api != HostDisplay::RenderAPI::D3D11);
      break;
#endif

    case GPURenderer::HardwareVulkan:
      needs_switch = (render_api != HostDisplay::RenderAPI::Vulkan);
      break;

    case GPURenderer::HardwareOpenGL:
      needs_switch = (render_api != HostDisplay::RenderAPI::OpenGL && render_api != HostDisplay::RenderAPI::OpenGLES);
      break;

    case GPURenderer::Software:
    default:
      needs_switch = false;
      break;
  }

  if (needs_switch)
  {
    ImGui::EndFrame();
    DestroyDisplay();

    // We need to recreate the window, otherwise bad things happen...
    DestroySDLWindow();
    if (!CreateSDLWindow())
      Panic("Failed to recreate SDL window on GPU renderer switch");

    if (!CreateDisplay())
      Panic("Failed to recreate display on GPU renderer switch");

    ImGui::NewFrame();
  }

  if (!CreateHostDisplayResources())
    return false;

  return true;
}

void SDLHostInterface::ReleaseHostDisplay()
{
  ReleaseHostDisplayResources();

  if (m_fullscreen)
    SetFullscreen(false);

  // restore vsync, since we don't want to burn cycles at the menu
  m_display->SetVSync(true);
}

std::optional<CommonHostInterface::HostKeyCode> SDLHostInterface::GetHostKeyCode(const std::string_view key_code) const
{
  const std::optional<u32> code = SDLKeyNames::ParseKeyString(key_code);
  if (!code)
    return std::nullopt;

  return static_cast<HostKeyCode>(*code);
}

void SDLHostInterface::UpdateInputMap()
{
  CommonHostInterface::UpdateInputMap(*m_settings_interface.get());
}

void SDLHostInterface::OnSystemCreated()
{
  CommonHostInterface::OnSystemCreated();

  m_current_main_window = MainWindowType::None;
  ClearImGuiFocus();
}

void SDLHostInterface::OnSystemPaused(bool paused)
{
  CommonHostInterface::OnSystemPaused(paused);

  if (!paused)
  {
    m_current_main_window = MainWindowType::None;
    ClearImGuiFocus();
  }
  else
  {
    m_current_main_window = MainWindowType::Pause;
  }
}

void SDLHostInterface::OnSystemDestroyed()
{
  CommonHostInterface::OnSystemDestroyed();
  ReportFormattedMessage("System shut down.");
  m_current_main_window = MainWindowType::Landing;
}

void SDLHostInterface::OnRunningGameChanged()
{
  CommonHostInterface::OnRunningGameChanged();

  Settings old_settings(std::move(g_settings));
  CommonHostInterface::LoadSettings(*m_settings_interface.get());
  CommonHostInterface::ApplyGameSettings(true);
  CommonHostInterface::FixIncompatibleSettings(true);
  CheckForSettingsChanges(old_settings);

  if (!System::GetRunningTitle().empty())
    SDL_SetWindowTitle(m_window, System::GetRunningTitle().c_str());
  else
    SDL_SetWindowTitle(m_window, GetWindowTitle());
}

void SDLHostInterface::RequestExit()
{
  m_quit_request = true;
}

void SDLHostInterface::RunLater(std::function<void()> callback)
{
  SDL_Event ev = {};
  ev.type = SDL_USEREVENT;
  ev.user.code = m_run_later_event_id;
  ev.user.data1 = new std::function<void()>(std::move(callback));
  SDL_PushEvent(&ev);
}

void SDLHostInterface::SaveAndUpdateSettings()
{
  m_settings_copy.Save(*m_settings_interface.get());

  Settings old_settings(std::move(g_settings));
  CommonHostInterface::LoadSettings(*m_settings_interface.get());
  CommonHostInterface::ApplyGameSettings(false);
  CommonHostInterface::FixIncompatibleSettings(false);
  CheckForSettingsChanges(old_settings);

  m_settings_interface->Save();
}

bool SDLHostInterface::IsFullscreen() const
{
  return m_fullscreen;
}

bool SDLHostInterface::SetFullscreen(bool enabled)
{
  if (m_fullscreen == enabled)
    return true;

  SDL_SetWindowFullscreen(m_window, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

  // We set the margin only in windowed mode, the menu bar is not drawn fullscreen.
  m_display->SetDisplayTopMargin(enabled ? 0 : static_cast<int>(20.0f * ImGui::GetIO().DisplayFramebufferScale.x));

  int window_width, window_height;
  SDL_GetWindowSize(m_window, &window_width, &window_height);
  m_display->ResizeRenderWindow(window_width, window_height);

  if (!System::IsShutdown())
    g_gpu->UpdateResolutionScale();

  m_fullscreen = enabled;
  return true;
}

std::unique_ptr<SDLHostInterface> SDLHostInterface::Create()
{
  return std::make_unique<SDLHostInterface>();
}

bool SDLHostInterface::Initialize()
{
  if (!CommonHostInterface::Initialize())
    return false;

  // Change to the user directory so that all default/relative paths in the config are after this.
  if (!FileSystem::SetWorkingDirectory(m_user_directory.c_str()))
    Log_ErrorPrintf("Failed to set working directory to '%s'", m_user_directory.c_str());

  if (!CreateSDLWindow())
  {
    Log_ErrorPrintf("Failed to create SDL window");
    return false;
  }

  CreateImGuiContext();
  if (!CreateDisplay())
  {
    Log_ErrorPrintf("Failed to create host display");
    return false;
  }

  ImGui::NewFrame();

  // process events to pick up controllers before updating input map
  ProcessEvents();
  UpdateInputMap();
  return true;
}

void SDLHostInterface::Shutdown()
{
  DestroySystem();

  CommonHostInterface::Shutdown();

  if (m_display)
  {
    DestroyDisplay();
    ImGui::DestroyContext();
  }

  if (m_window)
    DestroySDLWindow();
}

std::string SDLHostInterface::GetStringSettingValue(const char* section, const char* key,
                                                    const char* default_value /*= ""*/)
{
  return m_settings_interface->GetStringValue(section, key, default_value);
}

bool SDLHostInterface::GetBoolSettingValue(const char* section, const char* key, bool default_value /* = false */)
{
  return m_settings_interface->GetBoolValue(section, key, default_value);
}

int SDLHostInterface::GetIntSettingValue(const char* section, const char* key, int default_value /* = 0 */)
{
  return m_settings_interface->GetIntValue(section, key, default_value);
}

float SDLHostInterface::GetFloatSettingValue(const char* section, const char* key, float default_value /* = 0.0f */)
{
  return m_settings_interface->GetFloatValue(section, key, default_value);
}

bool SDLHostInterface::RequestRenderWindowSize(s32 new_window_width, s32 new_window_height)
{
  if (new_window_width <= 0 || new_window_height <= 0 || m_fullscreen)
    return false;

  // use imgui scale as the dpr
  const float dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
  const s32 scaled_width =
    std::max<s32>(static_cast<s32>(std::ceil(static_cast<float>(new_window_width) * dpi_scale)), 1);
  const s32 scaled_height = std::max<s32>(
    static_cast<s32>(std::ceil(static_cast<float>(new_window_height) * dpi_scale)) + m_display->GetDisplayTopMargin(),
    1);

  SDL_SetWindowSize(m_window, scaled_width, scaled_height);

  s32 window_width, window_height;
  SDL_GetWindowSize(m_window, &window_width, &window_height);
  m_display->ResizeRenderWindow(window_width, window_height);

  UpdateFramebufferScale();

  if (!System::IsShutdown())
    g_gpu->UpdateResolutionScale();

  return true;
}

void SDLHostInterface::LoadSettings()
{
  // Settings need to be loaded prior to creating the window for OpenGL bits.
  m_settings_interface = std::make_unique<INISettingsInterface>(GetSettingsFileName());
  m_settings_copy.Load(*m_settings_interface);
  CommonHostInterface::LoadSettings(*m_settings_interface.get());
  CommonHostInterface::FixIncompatibleSettings(false);
}

void SDLHostInterface::ReportError(const char* message)
{
  const bool was_fullscreen = IsFullscreen();
  if (was_fullscreen)
    SetFullscreen(false);

  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuckStation", message, m_window);

  if (was_fullscreen)
    SetFullscreen(true);
}

void SDLHostInterface::ReportMessage(const char* message)
{
  AddOSDMessage(message, 2.0f);
}

bool SDLHostInterface::ConfirmMessage(const char* message)
{
  const bool was_fullscreen = IsFullscreen();
  if (was_fullscreen)
    SetFullscreen(false);

  SDL_MessageBoxData mbd = {};
  mbd.flags = SDL_MESSAGEBOX_INFORMATION;
  mbd.window = m_window;
  mbd.title = "DuckStation";
  mbd.message = message;
  mbd.numbuttons = 2;

  // Why the heck these are reversed I have no idea...
  SDL_MessageBoxButtonData buttons[2] = {};
  buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
  buttons[1].buttonid = 0;
  buttons[1].text = "Yes";
  buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
  buttons[0].buttonid = 1;
  buttons[0].text = "No";
  mbd.buttons = buttons;
  mbd.numbuttons = countof(buttons);

  int button_id = 0;
  SDL_ShowMessageBox(&mbd, &button_id);
  const bool result = (button_id == 0);

  if (was_fullscreen)
    SetFullscreen(true);

  return result;
}

void SDLHostInterface::HandleSDLEvent(const SDL_Event* event)
{
  ImGui_ImplSDL2_ProcessEvent(event);

  if (m_controller_interface &&
      static_cast<SDLControllerInterface*>(m_controller_interface.get())->ProcessSDLEvent(event))
  {
    return;
  }

  switch (event->type)
  {
    case SDL_WINDOWEVENT:
    {
      if (event->window.event == SDL_WINDOWEVENT_RESIZED)
      {
        m_display->ResizeRenderWindow(event->window.data1, event->window.data2);
        UpdateFramebufferScale();

        if (!System::IsShutdown())
          g_gpu->UpdateResolutionScale();
      }
      else if (event->window.event == SDL_WINDOWEVENT_MOVED)
      {
        UpdateFramebufferScale();
      }
    }
    break;

    case SDL_QUIT:
      m_quit_request = true;
      break;

    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
      if (!ImGui::GetIO().WantCaptureKeyboard && event->key.repeat == 0)
      {
        const HostKeyCode code = static_cast<HostKeyCode>(SDLKeyNames::KeyEventToInt(event));
        const bool pressed = (event->type == SDL_KEYDOWN);
        HandleHostKeyEvent(code, pressed);
      }
    }
    break;

    case SDL_MOUSEMOTION:
    {
      m_display->SetMousePosition(event->motion.x, event->motion.y);
    }
    break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    {
      if (!ImGui::GetIO().WantCaptureMouse)
      {
        const s32 button = static_cast<s32>(ZeroExtend32(event->button.button));
        const bool pressed = (event->type == SDL_MOUSEBUTTONDOWN);
        HandleHostMouseEvent(button, pressed);
      }
    }
    break;

    case SDL_USEREVENT:
    {
      if (static_cast<u32>(event->user.code) == m_run_later_event_id)
      {
        std::function<void()>* callback = static_cast<std::function<void()>*>(event->user.data1);
        Assert(callback);
        (*callback)();
        delete callback;
      }
    }
    break;
  }
}

void SDLHostInterface::PollAndUpdate()
{
  CommonHostInterface::PollAndUpdate();
  ProcessEvents();
}

void SDLHostInterface::ProcessEvents()
{
  for (;;)
  {
    SDL_Event ev;
    if (SDL_PollEvent(&ev))
      HandleSDLEvent(&ev);
    else
      break;
  }
}

void SDLHostInterface::DrawImGuiWindows()
{
  if (!m_fullscreen)
    DrawMainMenuBar();

  CommonHostInterface::DrawImGuiWindows();

  ImGuiFullscreen::BeginLayout();

  if (m_current_main_window != MainWindowType::None)
    DrawMainWindow();

  ImGuiFullscreen::EndLayout();

  if (m_settings_window_open)
    DrawOldSettingsWindow();

  if (m_about_window_open)
    DrawAboutWindow();

  ImGui::Render();
}

void SDLHostInterface::DrawMainMenuBar()
{
  if (!ImGui::BeginMainMenuBar())
    return;

  const bool system_enabled = static_cast<bool>(!System::IsShutdown());

  if (ImGui::BeginMenu("System"))
  {
    if (ImGui::MenuItem("Start Disc", nullptr, false, !system_enabled))
    {
      RunLater([this]() { DoStartDisc(); });
      ClearImGuiFocus();
    }
    if (ImGui::MenuItem("Start BIOS", nullptr, false, !system_enabled))
    {
      RunLater([this]() {
        SystemBootParameters boot_params;
        BootSystem(boot_params);
      });
      ClearImGuiFocus();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Power Off", nullptr, false, system_enabled))
    {
      RunLater([this]() {
        if (g_settings.save_state_on_exit)
          SaveResumeSaveState();
        PowerOffSystem();
      });
      ClearImGuiFocus();
    }

    if (ImGui::MenuItem("Reset", nullptr, false, system_enabled))
    {
      RunLater([this]() { ResetSystem(); });
      ClearImGuiFocus();
    }

    if (ImGui::MenuItem("Pause", nullptr, System::IsPaused(), system_enabled))
    {
      RunLater([this]() { PauseSystem(!System::IsPaused()); });
      ClearImGuiFocus();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Change Disc", nullptr, false, system_enabled))
    {
      RunLater([this]() { DoChangeDisc(); });
      ClearImGuiFocus();
    }

    if (ImGui::MenuItem("Remove Disc", nullptr, false, system_enabled))
    {
      RunLater([this]() { System::RemoveMedia(); });
      ClearImGuiFocus();
    }

    if (ImGui::MenuItem("Frame Step", nullptr, false, system_enabled))
    {
      RunLater([this]() { DoFrameStep(); });
      ClearImGuiFocus();
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("Load State"))
    {
      for (u32 i = 1; i <= GLOBAL_SAVE_STATE_SLOTS; i++)
      {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "State %u", i);
        if (ImGui::MenuItem(buf))
        {
          RunLater([this, i]() { LoadState(true, i); });
          ClearImGuiFocus();
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Save State", system_enabled))
    {
      for (u32 i = 1; i <= GLOBAL_SAVE_STATE_SLOTS; i++)
      {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "State %u", i);
        if (ImGui::MenuItem(buf))
        {
          RunLater([this, i]() { SaveState(true, i); });
          ClearImGuiFocus();
        }
      }
      ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("Cheats", system_enabled))
    {
      const bool has_cheat_file = System::HasCheatList();

      if (ImGui::MenuItem("Load Cheats..."))
      {
        nfdchar_t* path = nullptr;
        if (NFD_OpenDialog("cht", nullptr, &path) && path && std::strlen(path) > 0)
          LoadCheatList(path);
      }

      if (ImGui::MenuItem("Save Cheats...", nullptr, false, has_cheat_file))
      {
        nfdchar_t* path = nullptr;
        if (NFD_SaveDialog("cht", nullptr, &path) && path && std::strlen(path) > 0)
          SaveCheatList(path);
      }

      if (ImGui::BeginMenu("Enabled Cheats", has_cheat_file))
      {
        CheatList* cl = System::GetCheatList();
        for (u32 i = 0; i < cl->GetCodeCount(); i++)
        {
          const CheatCode& cc = cl->GetCode(i);
          if (ImGui::MenuItem(cc.description.c_str(), nullptr, cc.enabled, true))
            SetCheatCodeState(i, !cc.enabled, g_settings.auto_load_cheats);
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
            ApplyCheatCode(i);
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Exit"))
      m_quit_request = true;

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Settings"))
  {
    if (ImGui::MenuItem("Change Settings..."))
      m_settings_window_open = true;

    ImGui::Separator();

    DrawQuickSettingsMenu();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Debug"))
  {
    DrawDebugMenu();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Help"))
  {
    if (ImGui::MenuItem("GitHub Repository"))
    {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Add URL Opener", "https://github.com/stenzek/duckstation",
                               m_window);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("About"))
      m_about_window_open = true;

    ImGui::EndMenu();
  }

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

  ImGui::EndMainMenuBar();
}

void SDLHostInterface::DrawQuickSettingsMenu()
{
  bool settings_changed = false;

  if (ImGui::BeginMenu("CPU Execution Mode"))
  {
    const CPUExecutionMode current = m_settings_copy.cpu_execution_mode;
    for (u32 i = 0; i < static_cast<u32>(CPUExecutionMode::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetCPUExecutionModeDisplayName(static_cast<CPUExecutionMode>(i)), nullptr,
                          i == static_cast<u32>(current)))
      {
        m_settings_copy.cpu_execution_mode = static_cast<CPUExecutionMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::MenuItem("CPU Clock Control", nullptr, &m_settings_copy.cpu_overclock_enable))
  {
    settings_changed = true;
    m_settings_copy.UpdateOverclockActive();
  }

  if (ImGui::BeginMenu("CPU Clock Speed"))
  {
    static constexpr auto values = make_array(10u, 25u, 50u, 75u, 100u, 125u, 150u, 175u, 200u, 225u, 250u, 275u, 300u,
                                              350u, 400u, 450u, 500u, 600u, 700u, 800u);
    const u32 percent = m_settings_copy.GetCPUOverclockPercent();
    for (u32 value : values)
    {
      if (ImGui::MenuItem(TinyString::FromFormat("%u%%", value), nullptr, percent == value))
      {
        m_settings_copy.SetCPUOverclockPercent(value);
        m_settings_copy.UpdateOverclockActive();
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |=
    ImGui::MenuItem("Recompiler Memory Exceptions", nullptr, &m_settings_copy.cpu_recompiler_memory_exceptions);
  if (ImGui::BeginMenu("Recompiler Fastmem"))
  {
    for (u32 i = 0; i < static_cast<u32>(CPUFastmemMode::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetCPUFastmemModeDisplayName(static_cast<CPUFastmemMode>(i)), nullptr,
                          m_settings_copy.cpu_fastmem_mode == static_cast<CPUFastmemMode>(i)))
      {
        m_settings_copy.cpu_fastmem_mode = static_cast<CPUFastmemMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("Recompiler ICache", nullptr, &m_settings_copy.cpu_recompiler_icache);

  ImGui::Separator();

  if (ImGui::BeginMenu("Renderer"))
  {
    const GPURenderer current = m_settings_copy.gpu_renderer;
    for (u32 i = 0; i < static_cast<u32>(GPURenderer::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetRendererDisplayName(static_cast<GPURenderer>(i)), nullptr,
                          i == static_cast<u32>(current)))
      {
        m_settings_copy.gpu_renderer = static_cast<GPURenderer>(i);
        settings_changed = true;
      }
    }

    settings_changed |= ImGui::MenuItem("GPU on Thread", nullptr, &m_settings_copy.gpu_use_thread);

    ImGui::EndMenu();
  }

  bool fullscreen = m_fullscreen;
  if (ImGui::MenuItem("Fullscreen", nullptr, &fullscreen))
    RunLater([this, fullscreen] { SetFullscreen(fullscreen); });

  if (ImGui::BeginMenu("Resize to Game", System::IsValid()))
  {
    static constexpr auto scales = make_array(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    for (const u32 scale : scales)
    {
      if (ImGui::MenuItem(TinyString::FromFormat("%ux Scale", scale)))
        RunLater([this, scale]() { RequestRenderWindowScale(static_cast<float>(scale)); });
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("VSync", nullptr, &m_settings_copy.video_sync_enabled);

  ImGui::Separator();

  if (ImGui::BeginMenu("Resolution Scale"))
  {
    const u32 current_internal_resolution = m_settings_copy.gpu_resolution_scale;
    for (u32 scale = 1; scale <= GPU::MAX_RESOLUTION_SCALE; scale++)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%ux (%ux%u)", scale, scale * VRAM_WIDTH, scale * VRAM_HEIGHT);

      if (ImGui::MenuItem(buf, nullptr, current_internal_resolution == scale))
      {
        m_settings_copy.gpu_resolution_scale = scale;
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Multisampling"))
  {
    const u32 current_multisamples = m_settings_copy.gpu_multisamples;
    const bool current_ssaa = m_settings_copy.gpu_per_sample_shading;

    if (ImGui::MenuItem("None", nullptr, (current_multisamples == 1)))
    {
      m_settings_copy.gpu_multisamples = 1;
      m_settings_copy.gpu_per_sample_shading = false;
      settings_changed = true;
    }

    for (u32 i = 2; i <= 32; i *= 2)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%ux MSAA", i);

      if (ImGui::MenuItem(buf, nullptr, (current_multisamples == i && !current_ssaa)))
      {
        m_settings_copy.gpu_multisamples = i;
        m_settings_copy.gpu_per_sample_shading = false;
        settings_changed = true;
      }
    }

    for (u32 i = 2; i <= 32; i *= 2)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%ux SSAA", i);

      if (ImGui::MenuItem(buf, nullptr, (current_multisamples == i && current_ssaa)))
      {
        m_settings_copy.gpu_multisamples = i;
        m_settings_copy.gpu_per_sample_shading = true;
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("PGXP"))
  {
    settings_changed |= ImGui::MenuItem("PGXP Enabled", nullptr, &m_settings_copy.gpu_pgxp_enable);
    settings_changed |=
      ImGui::MenuItem("PGXP Culling", nullptr, &m_settings_copy.gpu_pgxp_culling, m_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Texture Correction", nullptr,
                                        &m_settings_copy.gpu_pgxp_texture_correction, m_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Vertex Cache", nullptr, &m_settings_copy.gpu_pgxp_vertex_cache,
                                        m_settings_copy.gpu_pgxp_enable);
    settings_changed |=
      ImGui::MenuItem("PGXP CPU Instructions", nullptr, &m_settings_copy.gpu_pgxp_cpu, m_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Preserve Projection Precision", nullptr,
                                        &m_settings_copy.gpu_pgxp_preserve_proj_fp, m_settings_copy.gpu_pgxp_enable);
    settings_changed |= ImGui::MenuItem("PGXP Depth Buffer", nullptr, &m_settings_copy.gpu_pgxp_depth_buffer,
                                        m_settings_copy.gpu_pgxp_enable);
    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("True (24-Bit) Color", nullptr, &m_settings_copy.gpu_true_color);
  settings_changed |= ImGui::MenuItem("Scaled Dithering", nullptr, &m_settings_copy.gpu_scaled_dithering);

  if (ImGui::BeginMenu("Texture Filtering"))
  {
    const GPUTextureFilter current = m_settings_copy.gpu_texture_filter;
    for (u32 i = 0; i < static_cast<u32>(GPUTextureFilter::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetTextureFilterDisplayName(static_cast<GPUTextureFilter>(i)), nullptr,
                          i == static_cast<u32>(current)))
      {
        m_settings_copy.gpu_texture_filter = static_cast<GPUTextureFilter>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Disable Interlacing", nullptr, &m_settings_copy.gpu_disable_interlacing);
  settings_changed |= ImGui::MenuItem("Widescreen Hack", nullptr, &m_settings_copy.gpu_widescreen_hack);
  settings_changed |= ImGui::MenuItem("Force NTSC Timings", nullptr, &m_settings_copy.gpu_force_ntsc_timings);
  settings_changed |= ImGui::MenuItem("24-Bit Chroma Smoothing", nullptr, &m_settings_copy.gpu_24bit_chroma_smoothing);

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Display Linear Filtering", nullptr, &m_settings_copy.display_linear_filtering);
  settings_changed |= ImGui::MenuItem("Display Integer Scaling", nullptr, &m_settings_copy.display_integer_scaling);

  if (ImGui::BeginMenu("Aspect Ratio"))
  {
    for (u32 i = 0; i < static_cast<u32>(DisplayAspectRatio::Count); i++)
    {
      if (ImGui::MenuItem(Settings::GetDisplayAspectRatioName(static_cast<DisplayAspectRatio>(i)), nullptr,
                          m_settings_copy.display_aspect_ratio == static_cast<DisplayAspectRatio>(i)))
      {
        m_settings_copy.display_aspect_ratio = static_cast<DisplayAspectRatio>(i);
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
                          m_settings_copy.display_crop_mode == static_cast<DisplayCropMode>(i)))
      {
        m_settings_copy.display_crop_mode = static_cast<DisplayCropMode>(i);
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
                          m_settings_copy.gpu_downsample_mode == static_cast<GPUDownsampleMode>(i)))
      {
        m_settings_copy.gpu_downsample_mode = static_cast<GPUDownsampleMode>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("Force 4:3 For 24-bit", nullptr, &m_settings_copy.display_force_4_3_for_24bit);

  ImGui::Separator();

  if (ImGui::MenuItem("Dump Audio", nullptr, IsDumpingAudio(), System::IsValid()))
  {
    if (!IsDumpingAudio())
      StartDumpingAudio();
    else
      StopDumpingAudio();
  }

  if (ImGui::MenuItem("Save Screenshot"))
    RunLater([this]() { SaveScreenshot(); });

  if (settings_changed)
    RunLater([this]() { SaveAndUpdateSettings(); });
}

void SDLHostInterface::DrawDebugMenu()
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
        m_settings_copy.log_level = static_cast<LOGLEVEL>(i);
        settings_changed = true;
      }
    }

    ImGui::EndMenu();
  }

  settings_changed |= ImGui::MenuItem("Log To Console", nullptr, &m_settings_copy.log_to_console);
  settings_changed |= ImGui::MenuItem("Log To Debug", nullptr, &m_settings_copy.log_to_debug);
  settings_changed |= ImGui::MenuItem("Log To File", nullptr, &m_settings_copy.log_to_file);

  ImGui::Separator();

  settings_changed |= ImGui::MenuItem("Disable All Enhancements", nullptr, &m_settings_copy.disable_all_enhancements);
  settings_changed |= ImGui::MenuItem("Dump CPU to VRAM Copies", nullptr, &debug_settings.dump_cpu_to_vram_copies);
  settings_changed |= ImGui::MenuItem("Dump VRAM to CPU Copies", nullptr, &debug_settings.dump_vram_to_cpu_copies);

  if (ImGui::MenuItem("CPU Trace Logging", nullptr, CPU::IsTraceEnabled()))
  {
    if (!CPU::IsTraceEnabled())
      CPU::StartTrace();
    else
      CPU::StopTrace();
  }

  if (ImGui::MenuItem("Dump RAM...", nullptr, nullptr, system_valid))
    DoDumpRAM();

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
    Settings::DebugSettings& debug_settings_copy = m_settings_copy.debugging;
    debug_settings_copy.show_gpu_state = debug_settings.show_gpu_state;
    debug_settings_copy.show_vram = debug_settings.show_vram;
    debug_settings_copy.dump_cpu_to_vram_copies = debug_settings.dump_cpu_to_vram_copies;
    debug_settings_copy.dump_vram_to_cpu_copies = debug_settings.dump_vram_to_cpu_copies;
    debug_settings_copy.show_cdrom_state = debug_settings.show_cdrom_state;
    debug_settings_copy.show_spu_state = debug_settings.show_spu_state;
    debug_settings_copy.show_timers_state = debug_settings.show_timers_state;
    debug_settings_copy.show_mdec_state = debug_settings.show_mdec_state;
    debug_settings_copy.show_dma_state = debug_settings.show_dma_state;
    RunLater([this]() { SaveAndUpdateSettings(); });
  }
}

bool SDLHostInterface::LoadResources()
{
#if 0
  m_app_icon_texture =
    m_display->CreateTexture(APP_ICON_WIDTH, APP_ICON_HEIGHT, APP_ICON_DATA, APP_ICON_WIDTH * sizeof(u32));
#else
  m_app_icon_texture = LoadTextureResource("logo.png");
#endif
  m_placeholder_texture = m_display->CreateTexture(PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT,
                                                   PLACEHOLDER_ICON_DATA, sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
  if (!m_app_icon_texture || !m_placeholder_texture)
    return false;

  m_disc_region_textures[static_cast<u32>(DiscRegion::NTSC_U)] = LoadTextureResource("flag-uc.png");
  m_disc_region_textures[static_cast<u32>(DiscRegion::NTSC_J)] = LoadTextureResource("flag-jp.png");
  m_disc_region_textures[static_cast<u32>(DiscRegion::PAL)] = LoadTextureResource("flag-eu.png");

  return true;
}

void SDLHostInterface::DestroyResources()
{
  m_app_icon_texture.reset();
  m_placeholder_texture.reset();
  for (auto& tex : m_disc_region_textures)
    tex.reset();
}

std::unique_ptr<HostDisplayTexture> SDLHostInterface::LoadTextureResource(const char* name)
{
  std::unique_ptr<HostDisplayTexture> texture;

  const std::string path(GetProgramDirectoryRelativePath("resources" FS_OSPATH_SEPARATOR_STR "%s", name));

  Common::RGBA8Image image;
  if (Common::LoadImageFromFile(&image, path.c_str()) && image.IsValid())
  {
    texture = m_display->CreateTexture(image.GetWidth(), image.GetHeight(), image.GetPixels(), image.GetByteStride());
    if (texture)
    {
      Log_DevPrintf("Uploaded texture resource '%s' (%ux%u)", name, image.GetWidth(), image.GetHeight());
      return texture;
    }

    Log_ErrorPrintf("failed to create %ux%u texture for resource", image.GetWidth(), image.GetHeight());
  }

  Log_ErrorPrintf("Missing resource '%s', using fallback", name);

  texture = m_display->CreateTexture(PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT, PLACEHOLDER_ICON_DATA,
                                     sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
  if (!texture)
    Panic("Failed to create placeholder texture");

  return texture;
}

void SDLHostInterface::DrawMainWindow()
{
  switch (m_current_main_window)
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
    case MainWindowType::Pause:
      DrawPauseWindow();
      break;
    case MainWindowType::LoadState:
      DrawSaveStateSelector(true);
      break;
    default:
      break;
  }
}

void SDLHostInterface::ReturnToMainWindow()
{
  if (System::IsValid())
    m_current_main_window = MainWindowType::Pause;
  else
    m_current_main_window = MainWindowType::Landing;
}

void SDLHostInterface::DrawLandingWindow()
{
  if (BeginFullscreenColumnWindow(0.0f, 571.0f, "logo", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
  {
    ImGui::SetCursorPos(LayoutScale(ImVec2(120.0f, 170.0f)));
    ImGui::Image(m_app_icon_texture->GetHandle(), LayoutScale(ImVec2(380.0f, 380.0f)));
  }
  EndFullscreenWindow();

  if (BeginFullscreenColumnWindow(570.0f, LAYOUT_SCREEN_WIDTH, "menu"))
  {
    BeginMenuButtons(7, true);

    if (MenuButton(" " ICON_FA_PLAY_CIRCLE "  Resume",
                   "Starts the console from where it was before it was last closed."))
    {
      RunLater([this]() { ResumeSystemFromMostRecentState(); });
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_LIST "  Open Game List",
                   "Launch a game from images scanned from your game directories."))
    {
      RunLater([this]() { SwitchToGameList(); });
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_FOLDER_OPEN "  Start File", "Launch a game by selecting a file/disc image."))
    {
      RunLater([this]() { DoStartDisc(); });
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_TOOLBOX "  Start BIOS", "Start the console without any disc inserted."))
    {
      RunLater([this]() {
        SystemBootParameters boot_params;
        BootSystem(boot_params);
      });
      ClearImGuiFocus();
    }

    if (MenuButton(" " ICON_FA_UNDO "  Load State", "Loads a global save state."))
    {
      m_current_main_window = MainWindowType::LoadState;
      PopulateSaveStateListEntries();
    }

    if (MenuButton(" " ICON_FA_SLIDERS_H "  Settings", "Change settings for the emulator."))
      m_current_main_window = MainWindowType::Settings;

    if (MenuButton(" " ICON_FA_SIGN_OUT_ALT "  Exit", "Exits the program."))
      m_quit_request = true;

    EndMenuButtons();
  }

  EndFullscreenWindow();
}

void SDLHostInterface::DrawSettingsWindow()
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
      if (MenuCategory(titles[i], m_settings_page == static_cast<SettingsPage>(i)))
        m_settings_page = static_cast<SettingsPage>(i);
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

    switch (m_settings_page)
    {
      case SettingsPage::InterfaceSettings:
      {
        BeginMenuButtons(8, false);

        settings_changed |=
          ToggleButton("Pause On Start", "Pauses the emulator when a game is started.", &m_settings_copy.start_paused);
        settings_changed |= ToggleButton("Pause On Focus Loss",
                                         "Pauses the emulator when you minimize the window or switch to another "
                                         "application, and unpauses when you switch back.",
                                         &m_settings_copy.pause_on_focus_loss);
        settings_changed |=
          ToggleButton("Confirm Power Off",
                       "Determines whether a prompt will be displayed to confirm shutting down the emulator/game "
                       "when the hotkey is pressed.",
                       &m_settings_copy.confim_power_off);
        settings_changed |=
          ToggleButton("Save State On Exit",
                       "Automatically saves the emulator state when powering down or exiting. You can then "
                       "resume directly from where you left off next time.",
                       &m_settings_copy.save_state_on_exit);
        settings_changed |=
          ToggleButton("Start Fullscreen", "Automatically switches to fullscreen mode when a game is started.",
                       &m_settings_copy.start_fullscreen);
        settings_changed |=
          ToggleButton("Load Devices From Save States",
                       "When enabled, memory cards and controllers will be overwritten when save states are loaded.",
                       &m_settings_copy.load_devices_from_save_states);
        settings_changed |= ToggleButton(
          "Apply Per-Game Settings",
          "When enabled, per-game settings will be applied, and incompatible enhancements will be disabled.",
          &m_settings_copy.apply_game_settings);
        settings_changed |=
          ToggleButton("Automatically Load Cheats", "Automatically loads and applies cheats on game start.",
                       &m_settings_copy.auto_load_cheats);

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
          &m_settings_copy.gpu_resolution_scale, resolution_scale_text_callback, 17);
        settings_changed |= EnumChoiceButton(
          "Texture Filtering",
          "Smooths out the blockyness of magnified textures on 3D objects. Will have a greater effect "
          "on higher resolution scales.",
          &m_settings_copy.gpu_texture_filter, &Settings::GetTextureFilterDisplayName, GPUTextureFilter::Count);
        settings_changed |=
          ToggleButton("True Color Rendering",
                       "Disables dithering and uses the full 8 bits per channel of color information. May break "
                       "rendering in some games.",
                       &m_settings_copy.gpu_true_color);
        settings_changed |= ToggleButton(
          "Scaled Dithering",
          "Scales the dithering pattern with the internal rendering resolution, making it less noticeable. "
          "Usually safe to enable.",
          &m_settings_copy.gpu_scaled_dithering);
        settings_changed |= ToggleButton(
          "Widescreen Hack", "Increases the field of view from 4:3 to the chosen display aspect ratio in 3D games.",
          &m_settings_copy.gpu_widescreen_hack);
        settings_changed |=
          ToggleButton("Disable Interlacing",
                       "Disables interlaced rendering and display in the GPU. Some games can render in 480p this way, "
                       "but others will break.",
                       &m_settings_copy.gpu_disable_interlacing);
        settings_changed |= ToggleButton(
          "Force NTSC Timings",
          "Forces PAL games to run at NTSC timings, i.e. 60hz. Some PAL games will run at their \"normal\" "
          "speeds, while others will break.",
          &m_settings_copy.gpu_force_ntsc_timings);
        settings_changed |=
          ToggleButton("Force 4:3 For 24-Bit Display",
                       "Switches back to 4:3 display aspect ratio when displaying 24-bit content, usually FMVs.",
                       &m_settings_copy.display_force_4_3_for_24bit);
        settings_changed |= ToggleButton(
          "Chroma Smoothing For 24-Bit Display",
          "Smooths out blockyness between colour transitions in 24-bit content, usually FMVs. Only applies "
          "to the hardware renderers.",
          &m_settings_copy.gpu_24bit_chroma_smoothing);
        settings_changed |=
          ToggleButton("PGXP Geometry Correction",
                       "Reduces \"wobbly\" polygons by attempting to preserve the fractional component through memory "
                       "transfers.",
                       &m_settings_copy.gpu_pgxp_enable, m_settings_copy.gpu_pgxp_enable);
        settings_changed |=
          ToggleButton("PGXP Texture Correction",
                       "Uses perspective-correct interpolation for texture coordinates and colors, straightening out "
                       "warped textures.",
                       &m_settings_copy.gpu_pgxp_texture_correction, m_settings_copy.gpu_pgxp_enable);
        settings_changed |=
          ToggleButton("PGXP Culling Correction",
                       "Increases the precision of polygon culling, reducing the number of holes in geometry.",
                       &m_settings_copy.gpu_pgxp_culling, m_settings_copy.gpu_pgxp_enable);
        settings_changed |= ToggleButton(
          "PGXP Depth Buffer", "Reduces polygon Z-fighting through depth testing. Low compatibility with games.",
          &m_settings_copy.gpu_pgxp_depth_buffer,
          m_settings_copy.gpu_pgxp_enable && m_settings_copy.gpu_pgxp_texture_correction);

        EndMenuButtons();
      }
      break;

      case SettingsPage::AudioSettings:
        break;

      case SettingsPage::AdvancedSettings:
        break;
    }

    if (settings_changed)
      RunLater([this]() { SaveAndUpdateSettings(); });
  }

  EndFullscreenWindow();
}

void SDLHostInterface::DrawPauseWindow()
{
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  //dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, IM_COL32(255, 255, 255, 60));

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
      m_current_main_window = MainWindowType::Settings;

    if (MenuCategory(ICON_FA_POWER_OFF "  Exit Game", false))
      m_quit_request = true;

    EndMenuButtons();
  }

  ImGui::PopStyleVar();

  EndFullscreenWindow();
}

void SDLHostInterface::InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot, bool global)
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

void SDLHostInterface::InitializeSaveStateListEntry(SaveStateListEntry* li,
                                                    CommonHostInterface::ExtendedSaveStateInfo* ssi)
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
    li->preview_texture =
      m_display->CreateTexture(ssi->screenshot_width, ssi->screenshot_height, ssi->screenshot_data.data(),
                               sizeof(u32) * ssi->screenshot_width, false);
  }
  else
  {
    li->preview_texture = m_display->CreateTexture(PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT,
                                                   PLACEHOLDER_ICON_DATA, sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
  }

  if (!li->preview_texture)
    Log_ErrorPrintf("Failed to upload save state image to GPU");
}

void SDLHostInterface::PopulateSaveStateListEntries()
{
  m_save_state_selector_slots.clear();

  if (!System::GetRunningCode().empty())
  {
    for (s32 i = 1; i <= CommonHostInterface::PER_GAME_SAVE_STATE_SLOTS; i++)
    {
      std::optional<CommonHostInterface::ExtendedSaveStateInfo> ssi =
        GetExtendedSaveStateInfo(System::GetRunningCode().c_str(), i);

      SaveStateListEntry li;
      if (ssi)
        InitializeSaveStateListEntry(&li, &ssi.value());
      else
        InitializePlaceholderSaveStateListEntry(&li, i, false);

      m_save_state_selector_slots.push_back(std::move(li));
    }
  }

  for (s32 i = 1; i <= CommonHostInterface::GLOBAL_SAVE_STATE_SLOTS; i++)
  {
    std::optional<CommonHostInterface::ExtendedSaveStateInfo> ssi = GetExtendedSaveStateInfo(nullptr, i);

    SaveStateListEntry li;
    if (ssi)
      InitializeSaveStateListEntry(&li, &ssi.value());
    else
      InitializePlaceholderSaveStateListEntry(&li, i, true);

    m_save_state_selector_slots.push_back(std::move(li));
  }
}

void SDLHostInterface::ClearSaveStateListEntries()
{
  m_save_state_selector_slots.clear();
}

void SDLHostInterface::DrawSaveStateSelector(bool is_loading)
{
  const HostDisplayTexture* selected_texture = m_placeholder_texture.get();

  // drawn back the front so the hover changes the image
  if (BeginFullscreenColumnWindow(570.0f, LAYOUT_SCREEN_WIDTH, "save_state_selector_slots"))
  {
    BeginMenuButtons(static_cast<u32>(m_save_state_selector_slots.size()), true);

    for (const SaveStateListEntry& entry : m_save_state_selector_slots)
    {
      if (MenuButton(entry.title.c_str(), entry.summary.c_str()))
      {
        const std::string& path = entry.path;
        RunLater([this, path]() { LoadState(path.c_str()); });
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
    ImGui::Image(selected_texture ? selected_texture->GetHandle() : m_placeholder_texture->GetHandle(),
                 LayoutScale(ImVec2(400.0f, 400.0f)));

    ImGui::SetCursorPosY(LayoutScale(670.0f));
    if (MenuCategory(ICON_FA_BACKWARD "  Back", false))
      ReturnToMainWindow();
  }
  EndFullscreenWindow();
}

void SDLHostInterface::DrawGameListWindow()
{
  const GameListEntry* selected_entry = nullptr;

  if (BeginFullscreenColumnWindow(1220.0f, LAYOUT_SCREEN_WIDTH, "game_list_quick_select"))
  {
    const float height = 24.0f;
    BeginMenuButtons(29, false);

    ImGui::SetCursorPos(LayoutScale(ImVec2(17.0f, 4.0f)));
    ImGui::PushFont(ImGuiFullscreen::g_large_font);
    ImGui::TextUnformatted(ICON_KI_BUTTON_L1);
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
    ImGui::TextUnformatted(ICON_KI_BUTTON_R1);
    ImGui::PopFont();

    EndMenuButtons();
  }
  EndFullscreenWindow();

  if (BeginFullscreenColumnWindow(450.0f, 1220.0f, "game_list_entries"))
  {
    BeginMenuButtons(m_game_list->GetEntryCount(), false);

    for (const GameListEntry& entry : m_game_list->GetEntries())
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
    ImGui::Image(selected_entry ? GetGameListCover(selected_entry)->GetHandle() : m_placeholder_texture->GetHandle(),
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

void SDLHostInterface::LoadGameList()
{
  if (m_game_list_loaded)
    return;

  HostInterfaceProgressCallback cb;
  m_game_list->SetSearchDirectoriesFromSettings(*m_settings_interface.get());
  m_game_list->Refresh(false, false, &cb);
  m_game_list_loaded = true;
}

void SDLHostInterface::SwitchToGameList()
{
  LoadGameList();
  m_current_main_window = MainWindowType::GameList;
}

HostDisplayTexture* SDLHostInterface::GetGameListCover(const GameListEntry* entry)
{
  // lookup and grab cover image
  auto cover_it = m_cover_image_map.find(entry->path);
  if (cover_it == m_cover_image_map.end())
  {
    const std::string cover_path(m_game_list->GetCoverImagePathForEntry(entry));
    std::unique_ptr<HostDisplayTexture> texture;
    if (!cover_path.empty())
    {
      Log_DevPrintf("Trying to load cover from '%s' for '%s'", cover_path.c_str(), entry->path.c_str());

      Common::RGBA8Image image;
      if (Common::LoadImageFromFile(&image, cover_path.c_str()) || !image.IsValid())
      {
        texture =
          m_display->CreateTexture(image.GetWidth(), image.GetHeight(), image.GetPixels(), image.GetByteStride());
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

  return cover_it->second ? cover_it->second.get() : m_placeholder_texture.get();
}

HostDisplayTexture* SDLHostInterface::GetCoverForCurrentGame()
{
  if (!m_game_list_loaded)
    RunLater([this]() { LoadGameList(); });

  const GameListEntry* entry = m_game_list->GetEntryForPath(System::GetRunningPath().c_str());
  if (!entry)
    return m_placeholder_texture.get();

  return GetGameListCover(entry);
}

static bool DrawSettingsSectionHeader(const char* title)
{
  return ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen /* | ImGuiTreeNodeFlags_Leaf*/);
}

void SDLHostInterface::DrawOldSettingsWindow()
{
  const float framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale.x;

  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(500 * framebuffer_scale, 400.0f * framebuffer_scale), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Settings", &m_settings_window_open, ImGuiWindowFlags_NoResize))
  {
    ImGui::End();
    return;
  }

  bool settings_changed = false;

  if (ImGui::BeginTabBar("SettingsTabBar", 0))
  {
    const float indent = 150.0f * framebuffer_scale;

    if (ImGui::BeginTabItem("General"))
    {
      if (DrawSettingsSectionHeader("Console"))
      {
        ImGui::Text("Region:");
        ImGui::SameLine(indent);

        int region = static_cast<int>(m_settings_copy.region);
        if (ImGui::Combo(
              "##region", &region,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetConsoleRegionDisplayName(static_cast<ConsoleRegion>(index));
                return true;
              },
              nullptr, static_cast<int>(ConsoleRegion::Count)))
        {
          m_settings_copy.region = static_cast<ConsoleRegion>(region);
          settings_changed = true;
        }

        settings_changed |= ImGui::Checkbox("Enable TTY Output", &m_settings_copy.bios_patch_tty_enable);
        settings_changed |= ImGui::Checkbox("Fast Boot", &m_settings_copy.bios_patch_fast_boot);
      }

      ImGui::NewLine();
      if (DrawSettingsSectionHeader("Behavior"))
      {
        ImGui::Text("Emulation Speed:");
        ImGui::SameLine(indent);

        settings_changed |= ImGui::SliderFloat("##speed", &m_settings_copy.emulation_speed, 0.25f, 5.0f);
        settings_changed |= ImGui::Checkbox("Increase Timer Resolution", &m_settings_copy.increase_timer_resolution);
        settings_changed |= ImGui::Checkbox("Pause On Start", &m_settings_copy.start_paused);
        settings_changed |= ImGui::Checkbox("Start Fullscreen", &m_settings_copy.start_fullscreen);
        settings_changed |= ImGui::Checkbox("Save State On Exit", &m_settings_copy.save_state_on_exit);
        settings_changed |= ImGui::Checkbox("Apply Game Settings", &m_settings_copy.apply_game_settings);
        settings_changed |= ImGui::Checkbox("Automatically Load Cheats", &m_settings_copy.auto_load_cheats);
        settings_changed |=
          ImGui::Checkbox("Load Devices From Save States", &m_settings_copy.load_devices_from_save_states);
      }

      ImGui::NewLine();
      if (DrawSettingsSectionHeader("CDROM Emulation"))
      {
        settings_changed |= ImGui::Checkbox("Use Read Thread (Asynchronous)", &m_settings_copy.cdrom_read_thread);
        settings_changed |= ImGui::Checkbox("Enable Region Check", &m_settings_copy.cdrom_region_check);
        settings_changed |= ImGui::Checkbox("Preload Image To RAM", &m_settings_copy.cdrom_load_image_to_ram);
      }

      ImGui::NewLine();
      if (DrawSettingsSectionHeader("Audio"))
      {
        ImGui::Text("Backend:");
        ImGui::SameLine(indent);

        int backend = static_cast<int>(m_settings_copy.audio_backend);
        if (ImGui::Combo(
              "##backend", &backend,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetAudioBackendDisplayName(static_cast<AudioBackend>(index));
                return true;
              },
              nullptr, static_cast<int>(AudioBackend::Count)))
        {
          m_settings_copy.audio_backend = static_cast<AudioBackend>(backend);
          settings_changed = true;
        }

        settings_changed |= ImGui::Checkbox("Output Sync", &m_settings_copy.audio_sync_enabled);
        settings_changed |= ImGui::Checkbox("Start Dumping On Boot", &m_settings_copy.audio_dump_on_boot);
        settings_changed |= ImGui::Checkbox("Mute CD Audio", &m_settings_copy.cdrom_mute_cd_audio);
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Ports"))
    {
      for (int i = 0; i < 2; i++)
      {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Front Port %d", 1 + i);

        if (DrawSettingsSectionHeader(buf))
        {
          ImGui::Text("Controller:");
          ImGui::SameLine(indent);

          int controller_type = static_cast<int>(m_settings_copy.controller_types[i]);
          if (ImGui::Combo(
                TinyString::FromFormat("##controller_type%d", i), &controller_type,
                [](void*, int index, const char** out_text) {
                  *out_text = Settings::GetControllerTypeDisplayName(static_cast<ControllerType>(index));
                  return true;
                },
                nullptr, static_cast<int>(ControllerType::Count)))
          {
            m_settings_copy.controller_types[i] = static_cast<ControllerType>(controller_type);
            settings_changed = true;
          }

          ImGui::Text("Memory Card Type:");
          ImGui::SameLine(indent);

          int memory_card_type = static_cast<int>(m_settings_copy.memory_card_types[i]);
          if (ImGui::Combo(
                TinyString::FromFormat("##memory_card_type%d", i), &memory_card_type,
                [](void*, int index, const char** out_text) {
                  *out_text = Settings::GetMemoryCardTypeDisplayName(static_cast<MemoryCardType>(index));
                  return true;
                },
                nullptr, static_cast<int>(MemoryCardType::Count)))
          {
            m_settings_copy.memory_card_types[i] = static_cast<MemoryCardType>(memory_card_type);
            settings_changed = true;
          }

          ImGui::Text("Shared Card Path:");
          ImGui::SameLine(indent);

          std::string* path_ptr = &m_settings_copy.memory_card_paths[i];
          std::snprintf(buf, sizeof(buf), "##memcard_%c_path", 'a' + i);
          settings_changed |= DrawFileChooser(buf, path_ptr);
        }

        ImGui::NewLine();
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("CPU"))
    {
      ImGui::Text("Execution Mode:");
      ImGui::SameLine(indent);

      int execution_mode = static_cast<int>(m_settings_copy.cpu_execution_mode);
      if (ImGui::Combo(
            "##execution_mode", &execution_mode,
            [](void*, int index, const char** out_text) {
              *out_text = Settings::GetCPUExecutionModeDisplayName(static_cast<CPUExecutionMode>(index));
              return true;
            },
            nullptr, static_cast<int>(CPUExecutionMode::Count)))
      {
        m_settings_copy.cpu_execution_mode = static_cast<CPUExecutionMode>(execution_mode);
        settings_changed = true;
      }

      settings_changed |= ImGui::Checkbox("Enable CPU Clock Control", &m_settings_copy.cpu_overclock_enable);
      if (m_settings_copy.cpu_overclock_enable)
      {
        ImGui::Text("Overclock:");
        ImGui::SameLine(indent);

        int overclock_percent = static_cast<int>(m_settings_copy.GetCPUOverclockPercent());
        if (ImGui::SliderInt("##overclock_percent", &overclock_percent, 1, 1000, "%d%%"))
        {
          m_settings_copy.SetCPUOverclockPercent(static_cast<u32>(overclock_percent));
          settings_changed = true;
        }
      }

      settings_changed |=
        ImGui::Checkbox("Enable Recompiler Memory Exceptions", &m_settings_copy.cpu_recompiler_memory_exceptions);

      settings_changed |= ImGui::Checkbox("Enable Recompiler ICache", &m_settings_copy.cpu_recompiler_icache);

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("GPU"))
    {
      if (DrawSettingsSectionHeader("Basic"))
      {
        ImGui::Text("Renderer:");
        ImGui::SameLine(indent);

        int gpu_renderer = static_cast<int>(m_settings_copy.gpu_renderer);
        if (ImGui::Combo(
              "##gpu_renderer", &gpu_renderer,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetRendererDisplayName(static_cast<GPURenderer>(index));
                return true;
              },
              nullptr, static_cast<int>(GPURenderer::Count)))
        {
          m_settings_copy.gpu_renderer = static_cast<GPURenderer>(gpu_renderer);
          settings_changed = true;
        }
      }

      ImGui::NewLine();

      if (DrawSettingsSectionHeader("Display Output"))
      {
        ImGui::Text("Aspect Ratio:");
        ImGui::SameLine(indent);
        int display_aspect_ratio = static_cast<int>(m_settings_copy.display_aspect_ratio);
        if (ImGui::Combo(
              "##display_aspect_ratio", &display_aspect_ratio,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetDisplayAspectRatioName(static_cast<DisplayAspectRatio>(index));
                return true;
              },
              nullptr, static_cast<int>(DisplayAspectRatio::Count)))
        {
          m_settings_copy.display_aspect_ratio = static_cast<DisplayAspectRatio>(display_aspect_ratio);
          settings_changed = true;
        }

        ImGui::Text("Crop:");
        ImGui::SameLine(indent);

        int display_crop_mode = static_cast<int>(m_settings_copy.display_crop_mode);
        if (ImGui::Combo(
              "##display_crop_mode", &display_crop_mode,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetDisplayCropModeDisplayName(static_cast<DisplayCropMode>(index));
                return true;
              },
              nullptr, static_cast<int>(DisplayCropMode::Count)))
        {
          m_settings_copy.display_crop_mode = static_cast<DisplayCropMode>(display_crop_mode);
          settings_changed = true;
        }

        ImGui::Text("Downsample Mode:");
        ImGui::SameLine(indent);
        int gpu_downsample_mode = static_cast<int>(m_settings_copy.gpu_downsample_mode);
        if (ImGui::Combo(
              "##gpu_downsample_mode", &gpu_downsample_mode,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetDownsampleModeDisplayName(static_cast<GPUDownsampleMode>(index));
                return true;
              },
              nullptr, static_cast<int>(GPUDownsampleMode::Count)))
        {
          m_settings_copy.gpu_downsample_mode = static_cast<GPUDownsampleMode>(gpu_downsample_mode);
          settings_changed = true;
        }

        settings_changed |= ImGui::Checkbox("Use Debug Device", &m_settings_copy.gpu_use_debug_device);
        settings_changed |= ImGui::Checkbox("Linear Filtering", &m_settings_copy.display_linear_filtering);
        settings_changed |= ImGui::Checkbox("Integer Scaling", &m_settings_copy.display_integer_scaling);
        settings_changed |= ImGui::Checkbox("VSync", &m_settings_copy.video_sync_enabled);
      }

      ImGui::NewLine();

      if (DrawSettingsSectionHeader("Enhancements"))
      {
        ImGui::Text("Resolution Scale:");
        ImGui::SameLine(indent);

        static constexpr std::array<const char*, GPU::MAX_RESOLUTION_SCALE> resolutions = {{
          "1x (1024x512 VRAM)",
          "2x (2048x1024 VRAM)",
          "3x (3072x1536 VRAM)",
          "4x (4096x2048 VRAM)",
          "5x (5120x2560 VRAM)",
          "6x (6144x3072 VRAM)",
          "7x (7168x3584 VRAM)",
          "8x (8192x4096 VRAM)",
          "9x (9216x4608 VRAM)",
          "10x (10240x5120 VRAM)",
          "11x (11264x5632 VRAM)",
          "12x (12288x6144 VRAM)",
          "13x (13312x6656 VRAM)",
          "14x (14336x7168 VRAM)",
          "15x (15360x7680 VRAM)",
          "16x (16384x8192 VRAM)",
        }};

        int current_resolution_index = static_cast<int>(m_settings_copy.gpu_resolution_scale) - 1;
        if (ImGui::Combo("##gpu_resolution_scale", &current_resolution_index, resolutions.data(),
                         static_cast<int>(resolutions.size())))
        {
          m_settings_copy.gpu_resolution_scale = static_cast<u32>(current_resolution_index + 1);
          settings_changed = true;
        }

        ImGui::Text("Texture Filtering:");
        ImGui::SameLine(indent);
        int gpu_texture_filter = static_cast<int>(m_settings_copy.gpu_texture_filter);
        if (ImGui::Combo(
              "##gpu_texture_filter", &gpu_texture_filter,
              [](void*, int index, const char** out_text) {
                *out_text = Settings::GetTextureFilterDisplayName(static_cast<GPUTextureFilter>(index));
                return true;
              },
              nullptr, static_cast<int>(GPUTextureFilter::Count)))
        {
          m_settings_copy.gpu_texture_filter = static_cast<GPUTextureFilter>(gpu_texture_filter);
          settings_changed = true;
        }

        settings_changed |= ImGui::Checkbox("True 24-bit Color (disables dithering)", &m_settings_copy.gpu_true_color);
        settings_changed |= ImGui::Checkbox("Disable Interlacing", &m_settings_copy.gpu_disable_interlacing);
        settings_changed |= ImGui::Checkbox("Force NTSC Timings", &m_settings_copy.gpu_force_ntsc_timings);
        settings_changed |= ImGui::Checkbox("Widescreen Hack", &m_settings_copy.gpu_widescreen_hack);
        settings_changed |=
          ImGui::Checkbox("Force 4:3 For 24-Bit Display", &m_settings_copy.display_force_4_3_for_24bit);
        settings_changed |= ImGui::Checkbox("24-Bit Chroma Smoothing", &m_settings_copy.gpu_24bit_chroma_smoothing);

        settings_changed |= ImGui::Checkbox("PGXP Enabled", &m_settings_copy.gpu_pgxp_enable);
        settings_changed |= ImGui::Checkbox("PGXP Culling", &m_settings_copy.gpu_pgxp_culling);
        settings_changed |= ImGui::Checkbox("PGXP Texture Correction", &m_settings_copy.gpu_pgxp_texture_correction);
        settings_changed |= ImGui::Checkbox("PGXP Vertex Cache", &m_settings_copy.gpu_pgxp_vertex_cache);
        settings_changed |= ImGui::Checkbox("PGXP CPU Instructions", &m_settings_copy.gpu_pgxp_cpu);
        settings_changed |= ImGui::Checkbox("PGXP Preserve Projection Precision", &m_settings_copy.gpu_pgxp_enable);
        settings_changed |= ImGui::Checkbox("PGXP Depth Buffer", &m_settings_copy.gpu_pgxp_depth_buffer);

        ImGui::Text("PGXP Depth Clear Threshold:");
        ImGui::SameLine(indent);

        float depth_clear_threshold = m_settings_copy.GetPGXPDepthClearThreshold();
        if (ImGui::SliderFloat("##clear_threshold", &depth_clear_threshold, 0.0f, 4096.0f))
        {
          m_settings_copy.SetPGXPDepthClearThreshold(depth_clear_threshold);
          settings_changed = true;
        }
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Advanced"))
    {
      ImGui::Text("These options are tweakable to improve performance/game compatibility.");
      ImGui::Text("Use at your own risk, modified values will not be supported.");
      ImGui::NewLine();

      ImGui::Text("DMA Max Slice Ticks:");
      ImGui::SameLine(indent);

      int dma_max_slice_ticks = static_cast<int>(m_settings_copy.dma_max_slice_ticks);
      if (ImGui::SliderInt("##dma_max_slice_ticks", &dma_max_slice_ticks, 100, 10000))
      {
        m_settings_copy.dma_max_slice_ticks = dma_max_slice_ticks;
        settings_changed = true;
      }

      ImGui::Text("DMA Halt Ticks:");
      ImGui::SameLine(indent);

      int dma_halt_ticks = static_cast<int>(m_settings_copy.dma_halt_ticks);
      if (ImGui::SliderInt("##dma_halt_ticks", &dma_halt_ticks, 100, 10000))
      {
        m_settings_copy.dma_halt_ticks = dma_halt_ticks;
        settings_changed = true;
      }

      ImGui::Text("FIFO Size:");
      ImGui::SameLine(indent);

      int gpu_fifo_size = static_cast<int>(m_settings_copy.gpu_fifo_size);
      if (ImGui::SliderInt("##gpu_fifo_size", &gpu_fifo_size, 16, GPU::MAX_FIFO_SIZE))
      {
        m_settings_copy.gpu_fifo_size = gpu_fifo_size;
        settings_changed = true;
      }

      ImGui::Text("Max Run-Ahead:");
      ImGui::SameLine(indent);

      int gpu_max_run_ahead = static_cast<int>(m_settings_copy.gpu_max_run_ahead);
      if (ImGui::SliderInt("##gpu_max_run_ahead", &gpu_max_run_ahead, 0, 1000))
      {
        m_settings_copy.gpu_max_run_ahead = gpu_max_run_ahead;
        settings_changed = true;
      }

      if (ImGui::Button("Reset"))
      {
        m_settings_copy.dma_max_slice_ticks = static_cast<TickCount>(Settings::DEFAULT_DMA_MAX_SLICE_TICKS);
        m_settings_copy.dma_halt_ticks = static_cast<TickCount>(Settings::DEFAULT_DMA_HALT_TICKS);
        m_settings_copy.gpu_fifo_size = Settings::DEFAULT_GPU_FIFO_SIZE;
        m_settings_copy.gpu_max_run_ahead = static_cast<TickCount>(Settings::DEFAULT_GPU_MAX_RUN_AHEAD);
        settings_changed = true;
      }

      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::End();

  if (settings_changed)
    RunLater([this]() { SaveAndUpdateSettings(); });
}

void SDLHostInterface::DrawAboutWindow()
{
  const float framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale.x;

  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  ImGui::OpenPopup("About DuckStation");
  if (!ImGui::BeginPopupModal("About DuckStation", &m_about_window_open, ImGuiWindowFlags_NoResize))
    return;

  ImGui::Text("DuckStation");
  ImGui::NewLine();
  ImGui::Text("Authors:");
  ImGui::Text("  Connor McLaughlin <stenzek@gmail.com>");
  ImGui::NewLine();
  ImGui::Text("Uses Dear ImGui (https://github.com/ocornut/imgui)");
  ImGui::Text("Uses libcue (https://github.com/lipnitsk/libcue)");
  ImGui::Text("Uses stb_image_write (https://github.com/nothings/stb)");
  ImGui::Text("Uses simpleini (https://github.com/brofield/simpleini)");
  ImGui::NewLine();
  ImGui::Text("Duck icon by icons8 (https://icons8.com/icon/74847/platforms.undefined.short-title)");

  ImGui::NewLine();

  ImGui::SetCursorPosX((ImGui::GetWindowSize().x - (60.0f * framebuffer_scale)) / 2.0f);
  if (ImGui::Button("Close", ImVec2(60.0f * framebuffer_scale, 20.0f * framebuffer_scale)))
    m_about_window_open = false;

  ImGui::EndPopup();
}

bool SDLHostInterface::DrawFileChooser(const char* label, std::string* path, const char* filter /* = nullptr */)
{
  const float framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale.x;

  ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - (50.0f * framebuffer_scale));
  bool result = ImGui::InputText(label, path);
  ImGui::SameLine();

  ImGui::SetNextItemWidth(50.0f * framebuffer_scale);
  if (ImGui::Button("..."))
  {
    nfdchar_t* out_path = nullptr;
    nfdresult_t nfd_result = NFD_OpenDialog(filter, path->c_str(), &out_path);
    if (nfd_result == NFD_ERROR)
    {
      // try without the path - it might not be valid
      nfd_result = NFD_OpenDialog(filter, nullptr, &out_path);
    }
    if (nfd_result == NFD_OKAY)
    {
      path->assign(out_path);
      result = true;
    }
  }

  return result;
}

void SDLHostInterface::ClearImGuiFocus()
{
  ImGui::SetWindowFocus(nullptr);
}

void SDLHostInterface::DoStartDisc()
{
  Assert(System::IsShutdown());

  nfdchar_t* path = nullptr;
  if (!NFD_OpenDialog("bin,img,iso,cue,chd,exe,psexe,psf", nullptr, &path) || !path || std::strlen(path) == 0)
    return;

  AddFormattedOSDMessage(2.0f, "Starting disc from '%s'...", path);

  SystemBootParameters boot_params;
  boot_params.filename = path;
  BootSystem(boot_params);
}

void SDLHostInterface::DoChangeDisc()
{
  Assert(!System::IsShutdown());

  nfdchar_t* path = nullptr;
  if (!NFD_OpenDialog("bin,img,iso,cue,chd", nullptr, &path) || !path || std::strlen(path) == 0)
    return;

  if (System::InsertMedia(path))
    AddFormattedOSDMessage(2.0f, "Switched CD to '%s'", path);
  else
    AddOSDMessage("Failed to switch CD. The log may contain further information.");

  System::ResetPerformanceCounters();
}

void SDLHostInterface::DoDumpRAM()
{
  Assert(!System::IsShutdown());

  nfdchar_t* path = nullptr;
  if (!NFD_SaveDialog("bin", nullptr, &path) || !path || std::strlen(path) == 0)
    return;

  if (System::DumpRAM(path))
    AddFormattedOSDMessage(5.0f, "Dumped RAM to '%s'", path);
  else
    AddFormattedOSDMessage(10.0f, "Failed to dump RAM to '%s'", path);

  System::ResetPerformanceCounters();
}

void SDLHostInterface::Run()
{
  while (!m_quit_request)
  {
    PollAndUpdate();

    if (System::IsRunning())
    {
      System::RunFrame();
      UpdateControllerRumble();
      if (m_frame_step_request)
      {
        m_frame_step_request = false;
        PauseSystem(true);
      }
    }

    // rendering
    {
      DrawImGuiWindows();

      m_display->Render();
      ImGui_ImplSDL2_NewFrame(m_window);
      ImGui::NewFrame();

      if (System::IsRunning())
      {
        System::UpdatePerformanceCounters();

        if (m_throttler_enabled)
          System::Throttle();
      }
    }
  }

  // Save state on exit so it can be resumed
  if (!System::IsShutdown())
  {
    if (g_settings.save_state_on_exit)
      SaveResumeSaveState();
    DestroySystem();
  }
}
