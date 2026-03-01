#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>

#include "AtariDiskEngine.h"
#include "AtariFileSystemModel.h"

// Forward declaration of your custom Hex Viewer
class HexViewWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override = default;

private slots:
  void onOpenFile();
  void onFileLoaded();
  void onSaveDisk();
  void onCloseFile();
  void onFileSelected(const QModelIndex &index);
  void onExtractFile();
  void onNewDisk();

private:
  void setupUi();

  // UI Widgets
  QTreeView *m_treeView = nullptr;
  HexViewWidget *m_hexView = nullptr; // <--- ADD THIS LINE
  QLabel *m_formatLabel = nullptr;

  // Logic Members
  Atari::AtariDiskEngine *m_engine = nullptr;
  AtariFileSystemModel *m_model = nullptr;
};

#endif
