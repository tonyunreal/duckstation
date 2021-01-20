#pragma once
#include "common/event.h"
#include "core/host_interface.h"
#include "core/system.h"
#include "frontend-common/common_host_interface.h"
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class ByteStream;

class QEventLoop;
class QMenu;
class QWidget;
class QTimer;
class QTranslator;

class GameList;
struct GameListEntry;
class INISettingsInterface;

class MainWindow;
class QtDisplayWidget;

Q_DECLARE_METATYPE(std::shared_ptr<const SystemBootParameters>);

class QtHostInterface final : public QObject, public CommonHostInterface
{
  Q_OBJECT

public:
  explicit QtHostInterface(QObject* parent = nullptr);
  ~QtHostInterface();

  ALWAYS_INLINE static QtHostInterface* GetInstance() { return static_cast<QtHostInterface*>(g_host_interface); }

  const char* GetFrontendName() const override;

  bool Initialize() override;
  void Shutdown() override;

public Q_SLOTS:
  void ReportError(const char* message) override;
  void ReportMessage(const char* message) override;
  void ReportDebuggerMessage(const char* message) override;
  bool ConfirmMessage(const char* message) override;

public:
  /// Thread-safe settings access.
  std::string GetStringSettingValue(const char* section, const char* key, const char* default_value = "") override;
  bool GetBoolSettingValue(const char* section, const char* key, bool default_value = false) override;
  int GetIntSettingValue(const char* section, const char* key, int default_value = 0) override;
  float GetFloatSettingValue(const char* section, const char* key, float default_value = 0.0f) override;
  std::vector<std::string> GetSettingStringList(const char* section, const char* key);
  void SetBoolSettingValue(const char* section, const char* key, bool value);
  void SetIntSettingValue(const char* section, const char* key, int value);
  void SetFloatSettingValue(const char* section, const char* key, float value);
  void SetStringSettingValue(const char* section, const char* key, const char* value);
  void SetStringListSettingValue(const char* section, const char* key, const std::vector<std::string>& values);
  void RemoveSettingValue(const char* section, const char* key);

  TinyString TranslateString(const char* context, const char* str) const override;
  std::string TranslateStdString(const char* context, const char* str) const override;

  bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) override;
  void* GetTopLevelWindowHandle() const override;

  ALWAYS_INLINE const GameList* getGameList() const { return m_game_list.get(); }
  ALWAYS_INLINE GameList* getGameList() { return m_game_list.get(); }
  void refreshGameList(bool invalidate_cache = false, bool invalidate_database = false);

  ALWAYS_INLINE const HotkeyInfoList& getHotkeyInfoList() const { return GetHotkeyInfoList(); }
  ALWAYS_INLINE ControllerInterface* getControllerInterface() const { return GetControllerInterface(); }
  ALWAYS_INLINE bool inBatchMode() const { return InBatchMode(); }
  ALWAYS_INLINE void requestExit() { RequestExit(); }

  ALWAYS_INLINE bool isOnWorkerThread() const { return QThread::currentThread() == m_worker_thread; }

  ALWAYS_INLINE MainWindow* getMainWindow() const { return m_main_window; }
  void setMainWindow(MainWindow* window);
  HostDisplay* createHostDisplay();

  void populateSaveStateMenus(const char* game_code, QMenu* load_menu, QMenu* save_menu);

  /// Fills menu with save state info and handlers.
  void populateGameListContextMenu(const GameListEntry* entry, QWidget* parent_window, QMenu* menu);

  /// Fills menu with the current playlist entries. The disc index is marked as checked.
  void populatePlaylistEntryMenu(QMenu* menu);

  /// Fills menu with the current cheat options.
  void populateCheatsMenu(QMenu* menu);

  ALWAYS_INLINE QString getSavePathForInputProfile(const QString& name) const
  {
    return QString::fromStdString(GetSavePathForInputProfile(name.toUtf8().constData()));
  }
  ALWAYS_INLINE InputProfileList getInputProfileList() const { return GetInputProfileList(); }
  void saveInputProfile(const QString& profile_path);

  /// Returns a path relative to the user directory.
  QString getUserDirectoryRelativePath(const QString& arg) const;

  /// Returns a path relative to the application directory (for system files).
  QString getProgramDirectoryRelativePath(const QString& arg) const;

  /// Returns a list of supported languages and codes (suffixes for translation files).
  static std::vector<std::pair<QString, QString>> getAvailableLanguageList();

  /// Returns program directory as a QString.
  QString getProgramDirectory() const;

Q_SIGNALS:
  void errorReported(const QString& message);
  void messageReported(const QString& message);
  void debuggerMessageReported(const QString& message);
  bool messageConfirmed(const QString& message);
  void emulationStarting();
  void emulationStarted();
  void emulationStopped();
  void emulationPaused(bool paused);
  void stateSaved(const QString& game_code, bool global, qint32 slot);
  void gameListRefreshed();
  QtDisplayWidget* createDisplayRequested(QThread* worker_thread, bool fullscreen, bool render_to_main);
  QtDisplayWidget* updateDisplayRequested(QThread* worker_thread, bool fullscreen, bool render_to_main);
  void displaySizeRequested(qint32 width, qint32 height);
  void focusDisplayWidgetRequested();
  void destroyDisplayRequested();
  void systemPerformanceCountersUpdated(float speed, float fps, float vps, float avg_frame_time,
                                        float worst_frame_time);
  void runningGameChanged(const QString& filename, const QString& game_code, const QString& game_title);
  void exitRequested();
  void inputProfileLoaded();
  void mouseModeRequested(bool relative, bool hide_cursor);

public Q_SLOTS:
  void setDefaultSettings();
  void applySettings(bool display_osd_messages = false);
  void updateInputMap();
  void applyInputProfile(const QString& profile_path);
  void onDisplayWindowKeyEvent(int key, bool pressed);
  void onDisplayWindowMouseMoveEvent(int x, int y);
  void onDisplayWindowMouseButtonEvent(int button, bool pressed);
  void bootSystem(std::shared_ptr<const SystemBootParameters> params);
  void resumeSystemFromState(const QString& filename, bool boot_on_failure);
  void resumeSystemFromMostRecentState();
  void powerOffSystem();
  void powerOffSystemWithoutSaving();
  void synchronousPowerOffSystem();
  void resetSystem();
  void pauseSystem(bool paused, bool wait_until_paused = false);
  void changeDisc(const QString& new_disc_filename);
  void changeDiscFromPlaylist(quint32 index);
  void loadState(const QString& filename);
  void loadState(bool global, qint32 slot);
  void saveState(bool global, qint32 slot, bool block_until_done = false);
  void setAudioOutputVolume(int volume, int fast_forward_volume);
  void setAudioOutputMuted(bool muted);
  void startDumpingAudio();
  void stopDumpingAudio();
  void singleStepCPU();
  void dumpRAM(const QString& filename);
  void dumpVRAM(const QString& filename);
  void dumpSPURAM(const QString& filename);
  void saveScreenshot();
  void redrawDisplayWindow();
  void toggleFullscreen();
  void saveFullscreen(bool enabled);
  void loadCheatList(const QString& filename);
  void setCheatEnabled(quint32 index, bool enabled);
  void applyCheat(quint32 index);
  void reloadPostProcessingShaders();
  void requestRenderWindowScale(qreal scale);
  void executeOnEmulationThread(std::function<void()> callback, bool wait = false);

private Q_SLOTS:
  void doStopThread();
  void onHostDisplayWindowResized(int width, int height);
  void doBackgroundControllerPoll();
  void doSaveSettings();

protected:
  bool AcquireHostDisplay() override;
  void ReleaseHostDisplay() override;
  bool IsFullscreen() const override;
  bool SetFullscreen(bool enabled) override;
  void PollAndUpdate() override;

  void RequestExit() override;
  std::optional<HostKeyCode> GetHostKeyCode(const std::string_view key_code) const override;

  void OnSystemCreated() override;
  void OnSystemPaused(bool paused) override;
  void OnSystemDestroyed() override;
  void OnSystemPerformanceCountersUpdated() override;
  void OnRunningGameChanged() override;
  void OnSystemStateSaved(bool global, s32 slot) override;

  void LoadSettings() override;
  void SetDefaultSettings(SettingsInterface& si) override;
  void UpdateInputMap() override;

  void SetMouseMode(bool relative, bool hide_cursor) override;

private:
  enum : u32
  {
    BACKGROUND_CONTROLLER_POLLING_INTERVAL =
      100, /// Interval at which the controllers are polled when the system is not active.

    SETTINGS_SAVE_DELAY = 1000
  };

  using InputButtonHandler = std::function<void(bool)>;
  using InputAxisHandler = std::function<void(float)>;

  class Thread : public QThread
  {
  public:
    Thread(QtHostInterface* parent);
    ~Thread();

    void setInitResult(bool result);
    bool waitForInit();

  protected:
    void run() override;

  private:
    QtHostInterface* m_parent;
    std::atomic_bool m_init_result{false};
    Common::Event m_init_event;
  };

  void createBackgroundControllerPollTimer();
  void destroyBackgroundControllerPollTimer();
  void startBackgroundControllerPollTimer();
  void stopBackgroundControllerPollTimer();

  void createImGuiContext(float framebuffer_scale);
  void destroyImGuiContext();

  void createThread();
  void stopThread();
  void threadEntryPoint();
  bool initializeOnThread();
  void shutdownOnThread();
  void installTranslator();
  void renderDisplay();
  void connectDisplaySignals(QtDisplayWidget* widget);
  void updateDisplayState();
  void queueSettingsSave();
  void wakeThread();

  std::unique_ptr<INISettingsInterface> m_settings_interface;
  std::recursive_mutex m_settings_mutex;

  std::unique_ptr<QTranslator> m_translator;

  MainWindow* m_main_window = nullptr;
  QThread* m_original_thread = nullptr;
  Thread* m_worker_thread = nullptr;
  QEventLoop* m_worker_thread_event_loop = nullptr;
  Common::Event m_worker_thread_sync_execute_done;

  std::atomic_bool m_shutdown_flag{false};

  QTimer* m_background_controller_polling_timer = nullptr;
  std::unique_ptr<QTimer> m_settings_save_timer;

  bool m_is_rendering_to_main = false;
  bool m_is_fullscreen = false;
  bool m_is_exclusive_fullscreen = false;
};
