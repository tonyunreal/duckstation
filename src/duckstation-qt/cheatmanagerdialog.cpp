#include "cheatmanagerdialog.h"
#include "common/string_util.h"
#include "qtutils.h"
#include <QtCore/QFileInfo>
#include <QtGui/QColor>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <array>

static QString formatHexValue(u32 value)
{
  return QStringLiteral("0x%1").arg(static_cast<uint>(value), 8, 16, QChar('0'));
}

static QString formatValue(u32 value, bool is_signed)
{
  if (is_signed)
    return QStringLiteral("%1").arg(static_cast<int>(value));
  else
    return QStringLiteral("%1").arg(static_cast<uint>(value));
}

CheatManagerDialog::CheatManagerDialog(QWidget* parent) : QDialog(parent)
{
  m_ui.setupUi(this);

  setupAdditionalUi();
  connectUi();
}

CheatManagerDialog::~CheatManagerDialog() = default;

void CheatManagerDialog::setupAdditionalUi()
{
  m_ui.scanStartAddress->setText(formatHexValue(m_scanner.GetStartAddress()));
  m_ui.scanEndAddress->setText(formatHexValue(m_scanner.GetEndAddress()));

  setUpdateTimerEnabled(true);
}

void CheatManagerDialog::connectUi()
{
  connect(m_ui.scanValue, &QLineEdit::textChanged, this, &CheatManagerDialog::updateScanValue);
  connect(m_ui.scanValueBase, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int index) { updateScanValue(); });
  connect(m_ui.scanSize, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    m_scanner.SetSize(static_cast<MemoryAccessSize>(index));
    m_scanner.ResetSearch();
    updateResults();
  });
  connect(m_ui.scanValueSigned, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    m_scanner.SetValueSigned(index == 0);
    m_scanner.ResetSearch();
    updateResults();
  });
  connect(m_ui.scanOperator, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int index) { m_scanner.SetOperator(static_cast<MemoryScan::Operator>(index)); });
  connect(m_ui.scanNewSearch, &QPushButton::clicked, [this]() {
    m_scanner.Search();
    updateResults();
  });
  connect(m_ui.scanSearchAgain, &QPushButton::clicked, [this]() {
    m_scanner.SearchAgain();
    updateResults();
  });
  connect(m_ui.scanResetSearch, &QPushButton::clicked, [this]() {
    m_scanner.ResetSearch();
    updateResults();
  });
  connect(m_ui.scanAddWatch, &QPushButton::clicked, this, &CheatManagerDialog::addToWatchClicked);
  connect(m_ui.scanRemoveWatch, &QPushButton::clicked, this, &CheatManagerDialog::removeWatchClicked);
  connect(m_ui.scanTable, &QTableWidget::currentItemChanged, this, &CheatManagerDialog::scanCurrentItemChanged);
  connect(m_ui.watchTable, &QTableWidget::currentItemChanged, this, &CheatManagerDialog::watchCurrentItemChanged);
  connect(m_ui.scanTable, &QTableWidget::itemChanged, this, &CheatManagerDialog::scanItemChanged);
  connect(m_ui.watchTable, &QTableWidget::itemChanged, this, &CheatManagerDialog::watchItemChanged);
}

void CheatManagerDialog::showEvent(QShowEvent* event)
{
  QDialog::showEvent(event);
  resizeColumns();
}

void CheatManagerDialog::resizeEvent(QResizeEvent* event)
{
  QDialog::resizeEvent(event);
  resizeColumns();
}

void CheatManagerDialog::resizeColumns()
{
  QtUtils::ResizeColumnsForTableView(m_ui.scanTable, {-1, 100, 100});
  QtUtils::ResizeColumnsForTableView(m_ui.watchTable, {50, -1, 100, 150, 100});
}

void CheatManagerDialog::setUpdateTimerEnabled(bool enabled)
{
  if ((!m_update_timer && !enabled) && m_update_timer->isActive() == enabled)
    return;

  if (!m_update_timer)
  {
    m_update_timer = new QTimer(this);
    connect(m_update_timer, &QTimer::timeout, this, &CheatManagerDialog::updateScanUi);
  }

  if (enabled)
    m_update_timer->start(100);
  else
    m_update_timer->stop();
}

int CheatManagerDialog::getSelectedResultIndex() const
{
  QList<QTableWidgetSelectionRange> sel = m_ui.scanTable->selectedRanges();
  if (sel.isEmpty())
    return -1;

  return sel.front().topRow();
}

int CheatManagerDialog::getSelectedWatchIndex() const
{
  QList<QTableWidgetSelectionRange> sel = m_ui.watchTable->selectedRanges();
  if (sel.isEmpty())
    return -1;

  return sel.front().topRow();
}

void CheatManagerDialog::addToWatchClicked()
{
  const int index = getSelectedResultIndex();
  if (index < 0)
    return;

  const MemoryScan::Result& res = m_scanner.GetResults()[static_cast<u32>(index)];
  m_watch.AddEntry(StringUtil::StdStringFromFormat("0x%08x", res.address), res.address, m_scanner.GetSize(),
                   m_scanner.GetValueSigned(), false);
  updateWatch();
}

void CheatManagerDialog::removeWatchClicked()
{
  const int index = getSelectedWatchIndex();
  if (index < 0)
    return;

  m_watch.RemoveEntry(static_cast<u32>(index));
  updateWatch();
}

void CheatManagerDialog::scanCurrentItemChanged(QTableWidgetItem* current, QTableWidgetItem* previous)
{
  m_ui.scanAddWatch->setEnabled((current != nullptr));
}

void CheatManagerDialog::watchCurrentItemChanged(QTableWidgetItem* current, QTableWidgetItem* previous)
{
  m_ui.scanRemoveWatch->setEnabled((current != nullptr));
}

void CheatManagerDialog::scanItemChanged(QTableWidgetItem* item)
{
  const u32 index = static_cast<u32>(item->row());
  switch (item->column())
  {
    case 1:
    {
      bool value_ok = false;
      if (m_scanner.GetValueSigned())
      {
        int value = item->text().toInt(&value_ok);
        if (value_ok)
          m_scanner.SetResultValue(index, static_cast<u32>(value));
      }
      else
      {
        uint value = item->text().toUInt(&value_ok);
        if (value_ok)
          m_scanner.SetResultValue(index, static_cast<u32>(value));
      }
    }
    break;

    default:
      break;
  }
}

void CheatManagerDialog::watchItemChanged(QTableWidgetItem* item)
{
  const u32 index = static_cast<u32>(item->row());
  if (index >= m_watch.GetEntryCount())
    return;

  switch (item->column())
  {
    case 0:
    {
      m_watch.SetEntryFreeze(index, (item->checkState() == Qt::Checked));
    }
    break;

    case 1:
    {
      m_watch.SetEntryDescription(index, item->text().toStdString());
    }
    break;

    case 4:
    {
      const MemoryWatchList::Entry& entry = m_watch.GetEntry(index);
      bool value_ok = false;
      if (entry.is_signed)
      {
        int value = item->text().toInt(&value_ok);
        if (value_ok)
          m_watch.SetEntryValue(index, static_cast<u32>(value));
      }
      else
      {
        uint value = item->text().toUInt(&value_ok);
        if (value_ok)
          m_watch.SetEntryValue(index, static_cast<u32>(value));
      }
    }
    break;

    default:
      break;
  }
}

void CheatManagerDialog::updateScanValue()
{
  QString value = m_ui.scanValue->text();
  if (value.startsWith(QStringLiteral("0x")))
    value.remove(0, 2);

  bool ok = false;
  uint uint_value = value.toUInt(&ok, (m_ui.scanValueBase->currentIndex() > 0) ? 16 : 10);
  if (ok)
    m_scanner.SetValue(uint_value);
}

void CheatManagerDialog::updateResults()
{
  QSignalBlocker sb(m_ui.scanTable);
  m_ui.scanTable->setRowCount(0);

  const MemoryScan::ResultVector& results = m_scanner.GetResults();
  if (!results.empty())
  {
    int row = 0;
    for (const MemoryScan::Result& res : m_scanner.GetResults())
    {
      m_ui.scanTable->insertRow(row);

      QTableWidgetItem* address_item = new QTableWidgetItem(formatHexValue(res.address));
      address_item->setFlags(address_item->flags() & ~(Qt::ItemIsEditable));
      m_ui.scanTable->setItem(row, 0, address_item);

      QTableWidgetItem* value_item = new QTableWidgetItem(formatValue(res.value, m_scanner.GetValueSigned()));
      m_ui.scanTable->setItem(row, 1, value_item);

      QTableWidgetItem* previous_item = new QTableWidgetItem(formatValue(res.last_value, m_scanner.GetValueSigned()));
      previous_item->setFlags(address_item->flags() & ~(Qt::ItemIsEditable));
      m_ui.scanTable->setItem(row, 2, previous_item);
      row++;
    }
  }

  m_ui.scanResetSearch->setEnabled(!results.empty());
  m_ui.scanSearchAgain->setEnabled(!results.empty());
  m_ui.scanAddWatch->setEnabled(false);
}

void CheatManagerDialog::updateResultsValues()
{
  m_scanner.UpdateResultsValues();

  QSignalBlocker sb(m_ui.scanTable);

  int row = 0;
  for (const MemoryScan::Result& res : m_scanner.GetResults())
  {
    if (res.value_changed)
    {
      QTableWidgetItem* item = m_ui.scanTable->item(row, 1);
      item->setText(formatValue(res.value, m_scanner.GetValueSigned()));
      item->setForeground(Qt::red);
    }

    row++;
  }
}

void CheatManagerDialog::updateWatch()
{
  static constexpr std::array<const char*, 6> size_strings = {
    {QT_TR_NOOP("Byte"), QT_TR_NOOP("Halfword"), QT_TR_NOOP("Word"), QT_TR_NOOP("Signed Byte"),
     QT_TR_NOOP("Signed Halfword"), QT_TR_NOOP("Signed Word")}};

  m_watch.UpdateValues();

  QSignalBlocker sb(m_ui.watchTable);
  m_ui.watchTable->setRowCount(0);

  const MemoryWatchList::EntryVector& entries = m_watch.GetEntries();
  if (!entries.empty())
  {
    int row = 0;
    for (const MemoryWatchList::Entry& res : entries)
    {
      m_ui.watchTable->insertRow(row);

      QTableWidgetItem* freeze_item = new QTableWidgetItem();
      freeze_item->setFlags(freeze_item->flags() | (Qt::ItemIsEditable | Qt::ItemIsUserCheckable));
      freeze_item->setCheckState(res.freeze ? Qt::Checked : Qt::Unchecked);
      m_ui.watchTable->setItem(row, 0, freeze_item);

      QTableWidgetItem* description_item = new QTableWidgetItem(QString::fromStdString(res.description));
      m_ui.watchTable->setItem(row, 1, description_item);

      QTableWidgetItem* address_item = new QTableWidgetItem(formatHexValue(res.address));
      address_item->setFlags(address_item->flags() & ~(Qt::ItemIsEditable));
      m_ui.watchTable->setItem(row, 2, address_item);

      QTableWidgetItem* size_item =
        new QTableWidgetItem(tr(size_strings[static_cast<u32>(res.size) + (res.is_signed ? 3 : 0)]));
      size_item->setFlags(address_item->flags() & ~(Qt::ItemIsEditable));
      m_ui.watchTable->setItem(row, 3, size_item);

      QTableWidgetItem* value_item = new QTableWidgetItem(formatValue(res.value, res.is_signed));
      m_ui.watchTable->setItem(row, 4, value_item);

      row++;
    }
  }

  m_ui.scanSaveWatch->setEnabled(!entries.empty());
  m_ui.scanRemoveWatch->setEnabled(false);
}

void CheatManagerDialog::updateWatchValues()
{
  m_watch.UpdateValues();

  QSignalBlocker sb(m_ui.watchTable);
  int row = 0;
  for (const MemoryWatchList::Entry& res : m_watch.GetEntries())
  {
    if (res.changed)
      m_ui.watchTable->item(row, 4)->setText(formatValue(res.value, res.is_signed));

    row++;
  }
}

void CheatManagerDialog::updateScanUi()
{
  updateResultsValues();
  updateWatchValues();
}
