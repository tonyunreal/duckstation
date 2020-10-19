#pragma once
#include "core/cheats.h"
#include "ui_cheatmanagerdialog.h"
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtCore/QTimer>
#include <optional>

class CheatManagerDialog : public QDialog
{
  Q_OBJECT

public:
  CheatManagerDialog(QWidget* parent);
  ~CheatManagerDialog();

protected:
  void showEvent(QShowEvent* event);
  void resizeEvent(QResizeEvent* event);

private Q_SLOTS:
  void addToWatchClicked();
  void removeWatchClicked();
  void scanCurrentItemChanged(QTableWidgetItem* current, QTableWidgetItem* previous);
  void watchCurrentItemChanged(QTableWidgetItem* current, QTableWidgetItem* previous);
  void scanItemChanged(QTableWidgetItem* item);
  void watchItemChanged(QTableWidgetItem* item);
  void updateScanValue();
  void updateResults();
  void updateResultsValues();
  void updateWatch();
  void updateWatchValues();
  void updateScanUi();

private:
  void setupAdditionalUi();
  void connectUi();
  void resizeColumns();
  void setUpdateTimerEnabled(bool enabled);

  int getSelectedResultIndex() const;
  int getSelectedWatchIndex() const;

  Ui::CheatManagerDialog m_ui;

  MemoryScan m_scanner;
  MemoryWatchList m_watch;

  QTimer* m_update_timer = nullptr;
};
