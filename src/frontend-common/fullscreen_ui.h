#pragma once
#include "common/types.h"

class CommonHostInterface;
class SettingsInterface;
struct Settings;

namespace FullscreenUI
{
enum class MainWindowType
{
  None,
  Landing,
  GameList,
  Settings,
  QuickMenu,
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

bool Initialize(CommonHostInterface* host_interface, SettingsInterface* settings_interface);
void SystemCreated();
void SystemDestroyed();
void SystemPaused(bool paused);
void OpenQuickMenu();
void CloseQuickMenu();
void Shutdown();
void Render();

Settings& GetSettingsCopy();
void SaveAndApplySettings();

}
