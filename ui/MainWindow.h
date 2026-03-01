/**
 * @file MainWindow.h
 * @brief Main window class for the Atari Disk Engine GUI.
 */

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

/**
 * @class MainWindow
 * @brief The primary application window providing the user interface for disk
 * operations.
 *
 * Manages the display of the disk filesystem, hex view of sectors, and
 * high-level actions like opening/saving images and injecting files.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  /** @brief Constructs the main window. */
  explicit MainWindow(QWidget *parent = nullptr);

  /** @brief Default destructor. */
  ~MainWindow() override = default;

private slots:
  /** @brief Opens a file dialog to select and load an existing disk image (.ST,
   * .MSA, etc). */
  void onOpenFile();

  /** @brief Callback triggered when a disk image is successfully loaded into
   * the engine. */
  void onFileLoaded();

  /** @brief Saves the current modified disk image back to a file. */
  void onSaveDisk();

  /** @brief Closes the currently open disk image and resets the UI. */
  void onCloseFile();

  /** @brief Handles selection changes in the tree view to update the hex viewer
   * and extraction state. */
  void onFileSelected(const QModelIndex &index);

  /** @brief Extracts the currently selected file from the disk image to the
   * host system. */
  void onExtractFile();

  /** @brief Creates a new, empty virtual floppy disk image. */
  void onNewDisk();

  /** @brief Injects a file from the host system into the current disk image. */
  void onInjectFile();

private:
  /** @brief Initializes UI components, layouts, and signal/slot connections. */
  void setupUi();

  // UI Widgets
  QTreeView *m_treeView =
      nullptr; /**< Displays the FAT12 filesystem hierarchy. */
  HexViewWidget *m_hexView =
      nullptr; /**< Custom widget for viewing raw sector data. */
  QLabel *m_formatLabel =
      nullptr; /**< Status label showing disk geometry information. */

  // Logic Members
  Atari::AtariDiskEngine *m_engine =
      nullptr; /**< Pointer to the core disk manipulation engine. */
  AtariFileSystemModel *m_model =
      nullptr; /**< Qt Model bridging the engine to the QTreeView. */
};

#endif
