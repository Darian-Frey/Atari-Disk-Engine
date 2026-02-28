#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>

#include "../include/AtariDiskEngine.h"
#include "../include/AtariFileSystemModel.h"

/**
 * MainWindow for the Atari ST Toolkit.
 * Handles UI coordination between the disk engine and the file system view.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override = default;

private slots:
  /**
   * Triggers the file dialog to open an Atari ST disk image (.st, .msa, .img).
   */
  void onOpenFile();

  /**
   * Logic to update the UI once a disk is loaded into the engine.
   */
  void onFileLoaded();

  /**
   * Manually forces the engine to jump to a specific root directory sector.
   * Uses the value from m_sectorOverride.
   */
  void onManualSectorJump();

  /**
   * Handles selection changes in the TreeView for hex viewing or extraction.
   */
  void onFileSelected(const QModelIndex &index);

private:
  /**
   * Initializes the UI layout, toolbar, and status bar widgets.
   */
  void setupUi();

  // UI Widgets
  QTreeView *m_treeView = nullptr;
  QSpinBox *m_sectorOverride = nullptr;
  QPushButton *m_jumpBtn = nullptr;
  QLabel *m_formatLabel = nullptr;

  // Logic Members
  Atari::AtariDiskEngine *m_engine = nullptr;
  AtariFileSystemModel *m_model = nullptr;
};

#endif // MAINWINDOW_H
