#pragma once
#include <QtCore/QThread>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <memory>

#include "settingsdialog.h"
#include "ui_mainwindow.h"

class QLabel;
class QThread;

class GameListWidget;
class QtHostInterface;
class QtDisplayWidget;
class AutoUpdaterDialog;
class MemoryCardEditorDialog;
class CheatManagerDialog;
class DebuggerWindow;

class HostDisplay;
struct GameListEntry;

class GDBServer;

class MainWindow final : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QtHostInterface* host_interface);
  ~MainWindow();

  /// Initializes the window. Call once at startup.
  void initializeAndShow();

  /// Performs update check if enabled in settings.
  void startupUpdateCheck();

  /// Opens memory card editor with the specified paths.
  void openMemoryCardEditor(const QString& card_a_path, const QString& card_b_path);

public Q_SLOTS:
  /// Updates debug menu visibility (hides if disabled).
  void updateDebugMenuVisibility();

  void checkForUpdates(bool display_message);

private Q_SLOTS:
  void reportError(const QString& message);
  void reportMessage(const QString& message);
  bool confirmMessage(const QString& message);
  QtDisplayWidget* createDisplay(QThread* worker_thread, bool fullscreen, bool render_to_main);
  QtDisplayWidget* updateDisplay(QThread* worker_thread, bool fullscreen, bool render_to_main);
  void displaySizeRequested(qint32 width, qint32 height);
  void destroyDisplay();
  void focusDisplayWidget();
  void onMouseModeRequested(bool relative_mode, bool hide_cursor);
  void updateMouseMode(bool paused);

  void setTheme(const QString& theme);
  void updateTheme();

  void onEmulationStarting();
  void onEmulationStarted();
  void onEmulationStopped();
  void onEmulationPaused(bool paused);
  void onStateSaved(const QString& game_code, bool global, qint32 slot);
  void onSystemPerformanceCountersUpdated(float speed, float fps, float vps, float average_frame_time,
                                          float worst_frame_time);
  void onRunningGameChanged(const QString& filename, const QString& game_code, const QString& game_title);
  void onApplicationStateChanged(Qt::ApplicationState state);

  void onStartDiscActionTriggered();
  void onStartBIOSActionTriggered();
  void onChangeDiscFromFileActionTriggered();
  void onChangeDiscFromGameListActionTriggered();
  void onChangeDiscFromPlaylistMenuAboutToShow();
  void onChangeDiscFromPlaylistMenuAboutToHide();
  void onCheatsMenuAboutToShow();
  void onRemoveDiscActionTriggered();
  void onViewToolbarActionToggled(bool checked);
  void onViewStatusBarActionToggled(bool checked);
  void onViewGameListActionTriggered();
  void onViewGameGridActionTriggered();
  void onViewSystemDisplayTriggered();
  void onViewGamePropertiesActionTriggered();
  void onGitHubRepositoryActionTriggered();
  void onIssueTrackerActionTriggered();
  void onDiscordServerActionTriggered();
  void onAboutActionTriggered();
  void onCheckForUpdatesActionTriggered();
  void onToolsMemoryCardEditorTriggered();
  void onToolsCheatManagerTriggered();
  void onToolsOpenDataDirectoryTriggered();

  void onGameListEntrySelected(const GameListEntry* entry);
  void onGameListEntryDoubleClicked(const GameListEntry* entry);
  void onGameListContextMenuRequested(const QPoint& point, const GameListEntry* entry);
  void onGameListSetCoverImageRequested(const GameListEntry* entry);

  void onUpdateCheckComplete();

  void openCPUDebugger();
  void onCPUDebuggerClosed();
  
  void safePowerOffSystem();
  void safePowerOffSystemWithoutSaving();

protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject *obj, QEvent *event);

private:
  void setupAdditionalUi();
  void connectSignals();
  void addThemeToMenu(const QString& name, const QString& key);
  void updateEmulationActions(bool starting, bool running);
  bool isShowingGameList() const;
  void switchToGameListView();
  void switchToEmulationView();
  void saveStateToConfig();
  void restoreStateFromConfig();
  void saveDisplayWindowGeometryToConfig();
  void restoreDisplayWindowGeometryFromConfig();
  void destroyDisplayWidget();
  void setDisplayFullscreen(const std::string& fullscreen_mode);
  bool shouldHideCursorInFullscreen() const;
  SettingsDialog* getSettingsDialog();
  void doSettings(SettingsDialog::Category category = SettingsDialog::Category::Count);
  void updateDebugMenuCPUExecutionMode();
  void updateDebugMenuGPURenderer();
  void updateDebugMenuCropMode();

  Ui::MainWindow m_ui;

  QString m_unthemed_style_name;

  QtHostInterface* m_host_interface = nullptr;

  GameListWidget* m_game_list_widget = nullptr;

  HostDisplay* m_host_display = nullptr;
  QtDisplayWidget* m_display_widget = nullptr;

  QLabel* m_status_speed_widget = nullptr;
  QLabel* m_status_fps_widget = nullptr;
  QLabel* m_status_frame_time_widget = nullptr;

  SettingsDialog* m_settings_dialog = nullptr;
  AutoUpdaterDialog* m_auto_updater_dialog = nullptr;
  MemoryCardEditorDialog* m_memory_card_editor_dialog = nullptr;
  CheatManagerDialog* m_cheat_manager_dialog = nullptr;
  DebuggerWindow* m_debugger_window = nullptr;

  bool m_emulation_running = false;
  bool m_was_paused_by_focus_loss = false;
  bool m_open_debugger_on_start = false;
  bool m_relative_mouse_mode = false;
  bool m_mouse_cursor_hidden = false;

  GDBServer* m_gdb_server = nullptr;
};
