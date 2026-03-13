#include "MainWindow.h"
#include "HexViewWidget.h"
#include <QAction>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) { setupUi(); }

void MainWindow::setupUi() {
  /**
   * Initializes the core logic engine and the filesystem model.
   * The model acts as the glue between the Atari FAT12 structures and the Qt
   * TreeView.
   */
  m_engine = new Atari::AtariDiskEngine();
  m_model = new AtariFileSystemModel(this);
  m_model->setEngine(m_engine);

  // 1. Setup the Layout (Splitter for Tree and Hex View)
  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
  m_treeView = new QTreeView(this);
  m_treeView->setModel(m_model);
  m_treeView->header()->setSectionResizeMode(QHeaderView::Stretch);

  m_hexView = new HexViewWidget(this);
  splitter->addWidget(m_treeView);
  splitter->addWidget(m_hexView);
  splitter->setStretchFactor(1, 1); // Allow hex view to expand more than tree
  setCentralWidget(splitter);

  // Enable context menu for the tree view
  m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_treeView, &QTreeView::customContextMenuRequested, this,
          &MainWindow::onCustomContextMenu);

  // CREATE THE DROP-DOWN FILE MENU
  QMenu *fileMenu = menuBar()->addMenu("&File");

  // CREATE THE DISK MENU
  QMenu *diskMenu = menuBar()->addMenu("&Disk");
  QAction *infoAction = diskMenu->addAction("Disk &Information");
  infoAction->setShortcut(QKeySequence("Ctrl+I"));
  connect(infoAction, &QAction::triggered, this, &MainWindow::onDiskInfo);

  // Open Action: Loads an image from disk
  QAction *openAction =
      new QAction(QIcon::fromTheme("document-open"), "&Open Disk...", this);
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
  fileMenu->addAction(openAction);

  // Close Action: Resets the state
  QAction *closeAction = new QAction("&Close Image", this);
  connect(closeAction, &QAction::triggered, this, &MainWindow::onCloseFile);

  // Save Action: Writes current memory image to a file
  QAction *saveAction = new QAction("&Save Disk As...", this);
  saveAction->setShortcut(QKeySequence::Save);
  connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveDisk);
  fileMenu->addAction(saveAction);
  fileMenu->insertAction(closeAction, saveAction);

  fileMenu->addAction(closeAction);

  // Extract Action: Saves selected file to host system
  QAction *extractAction = new QAction("&Extract Selected File...", this);
  extractAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_E));
  connect(extractAction, &QAction::triggered, this, &MainWindow::onExtractFile);
  fileMenu->addAction(extractAction);

  // New Disk Action: Creates a blank 720K template
  QAction *newAction = new QAction("&New 720K Disk", this);
  newAction->setShortcut(QKeySequence::New);
  connect(newAction, &QAction::triggered, this, &MainWindow::onNewDisk);
  fileMenu->insertAction(openAction, newAction);

  fileMenu->addSeparator();

  // Exit Action
  QAction *exitAction = new QAction("E&xit", this);
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, &QWidget::close);
  fileMenu->addAction(exitAction);

  // Inject Action: Inserts a local file into the disk
  QAction *injectAction = new QAction("&Inject File TO Disk...", this);
  connect(injectAction, &QAction::triggered, this, &MainWindow::onInjectFile);
  fileMenu->insertAction(extractAction, injectAction);

  // Keep the Toolbar for quick access
  QToolBar *toolBar = addToolBar("Main");
  toolBar->addAction(openAction);

  // Status Bar: Displays disk geometry and status messages
  m_formatLabel = new QLabel("Ready", this);
  statusBar()->addPermanentWidget(m_formatLabel);

  // Format Disk Action
  QAction *formatAct = diskMenu->addAction("&Format Disk");
  formatAct->setShortcut(QKeySequence("Ctrl+Shift+F"));
  connect(formatAct, &QAction::triggered, this, &MainWindow::onFormatDisk);

  // OEM Label Action
  QAction *oemAct = diskMenu->addAction("Edit &OEM Label...");
  connect(oemAct, &QAction::triggered, this, &MainWindow::onEditOemLabel);

  // FAT Map Action
  QAction *fatMapAct = diskMenu->addAction("View &FAT Map");
  connect(fatMapAct, &QAction::triggered, this, &MainWindow::onViewFatTable);

  // Search Action
  QAction *searchAct = diskMenu->addAction("&Search Disk...");
  searchAct->setShortcut(QKeySequence::Find); // Ctrl+F
  connect(searchAct, &QAction::triggered, this, &MainWindow::onSearchDisk);

  // Add "Make Bootable" to Disk Menu
  QAction *fixBootAct = diskMenu->addAction("Make Disk Bootable");
  connect(fixBootAct, &QAction::triggered, this, &MainWindow::onFixBoot);

  connect(m_treeView, &QTreeView::clicked, this, &MainWindow::onFileSelected);

  resize(1100, 750);
  setWindowTitle("Atari ST Toolkit");
}

void MainWindow::onCloseFile() {
  qDebug() << "[UI] Closing file...";

  // Reset engine, hex display, and tree model
  if (m_engine) {
    m_engine->load({});
  }

  if (m_hexView) {
    m_hexView->setData(QByteArray());
  }

  if (m_model) {
    m_model->refresh();
  }

  if (m_formatLabel) {
    m_formatLabel->setText("No Disk Loaded");
  }

  setWindowTitle("Atari ST Toolkit");
}

void MainWindow::onOpenFile() {
  QString fileName = QFileDialog::getOpenFileName(this, "Open Disk", "",
                                                  "Atari Disks (*.st *.msa)");

  if (!fileName.isEmpty() && m_engine->loadImage(fileName)) {
    // 1. Structural Analysis
    m_engine->readRootDirectory();
    m_model->refresh();
    m_treeView->expandAll();
    m_formatLabel->setText(m_engine->getFormatInfoString());

    // 2. FIX: Load the FULL buffer so Search can navigate the whole disk
    // We use your setDiskData method and pass the engine's internal vector
    m_hexView->setDiskData(m_engine->getFullImageBuffer());

    // 3. Reset view to top
    m_hexView->scrollToOffset(0);

    qDebug() << "[UI] File loaded and full hex buffer populated.";
  }
}

void MainWindow::onFileSelected(const QModelIndex &index) {
  if (!index.isValid())
    return;

  Atari::DirEntry entry = m_model->getEntry(index);
  QString name = Atari::AtariDiskEngine::toQString(entry.getFilename());

  if (entry.isDirectory()) {
    // We don't show hex for directories currently
    m_hexView->setData(QByteArray());
    return;
  }

  // Load file content using the engine's FAT traversal logic
  QByteArray fileData = m_engine->readFileQt(entry);

  if (fileData.isEmpty() && entry.getFileSize() > 0) {
    statusBar()->showMessage("Error: Could not read file data", 3000);
  } else {
    m_hexView->setData(fileData);
    statusBar()->showMessage(
        QString("Viewing %1 (%2 bytes)").arg(name).arg(fileData.size()));
  }
}

void MainWindow::onFileLoaded() {
  /**
   * Helper slot called when an image is loaded programmatically or
   * after manual operations requiring a full UI refresh.
   */
  m_engine->readRootDirectory();
  m_model->refresh();
  m_treeView->expandAll();
  m_formatLabel->setText(m_engine->getFormatInfoString());
}

void MainWindow::onExtractFile() {
  /**
   * Reads data from the virtual disk and writes it to a host file.
   */
  QModelIndex index = m_treeView->currentIndex();
  if (!index.isValid()) {
    QMessageBox::warning(this, "Extract", "Please select a file first.");
    return;
  }

  Atari::DirEntry entry = m_model->getEntry(index);
  if (entry.isDirectory()) {
    QMessageBox::warning(this, "Extract", "Cannot extract a directory.");
    return;
  }

  QString originalName =
      Atari::AtariDiskEngine::toQString(entry.getFilename()).trimmed();
  QString savePath = QFileDialog::getSaveFileName(
      this, "Extract File", QDir::homePath() + "/" + originalName);

  if (savePath.isEmpty())
    return;

  QByteArray data = m_engine->readFileQt(entry);
  if (data.isEmpty() && entry.getFileSize() > 0) {
    QMessageBox::critical(this, "Error",
                          "Failed to read data from disk image.");
    return;
  }

  QFile outFile(savePath);
  if (outFile.open(QIODevice::WriteOnly)) {
    outFile.write(data);
    outFile.close();
    statusBar()->showMessage("Extracted to " + savePath, 3000);
  } else {
    QMessageBox::critical(this, "Error", "Could not write to local file.");
  }
}

void MainWindow::onNewDisk() {
  /**
   * Creates a fresh in-memory 720K master disk.
   */
  if (m_engine->isLoaded()) {
    auto res = QMessageBox::question(
        this, "New Disk", "Clear current disk and create a new 720KB image?");
    if (res != QMessageBox::Yes)
      return;
  }

  m_engine->createNew720KImage();
  m_model->refresh();
  m_hexView->setData(m_engine->getSector(0)); // Show the new bootsector
  m_formatLabel->setText("New 720KB Disk (Unsaved)");
  setWindowTitle("Atari ST Toolkit - [New Disk]");
}

void MainWindow::onSaveDisk() {
  /**
   * Serializes the current m_image buffer to a file.
   */
  if (!m_engine || !m_engine->isLoaded()) {
    QMessageBox::warning(this, "Save Disk", "No disk image in memory to save.");
    return;
  }

  QString savePath = QFileDialog::getSaveFileName(this, "Save Atari Disk Image",
                                                  QDir::homePath(),
                                                  "Atari Disk Images (*.st)");

  if (savePath.isEmpty())
    return;

  if (!savePath.endsWith(".st", Qt::CaseInsensitive)) {
    savePath += ".st";
  }

  QFile outFile(savePath);
  if (outFile.open(QIODevice::WriteOnly)) {
    const std::vector<uint8_t> &data = m_engine->getRawImageData();
    outFile.write(reinterpret_cast<const char *>(data.data()), data.size());
    outFile.close();

    statusBar()->showMessage("Disk saved successfully: " + savePath, 3000);
    setWindowTitle("Atari ST Toolkit - " + QFileInfo(savePath).fileName());
  } else {
    QMessageBox::critical(this, "Error", "Could not write disk image to file.");
  }
}

void MainWindow::onInjectFile() {
  /**
   * Triggers the engine's file injection logic for the currently loaded disk.
   */
  if (!m_engine->isLoaded())
    return;

  QString localFile =
      QFileDialog::getOpenFileName(this, "Select File to Inject");
  if (localFile.isEmpty())
    return;

  if (m_engine->injectFile(localFile)) {
    m_model->refresh();
    statusBar()->showMessage("File injected successfully", 3000);
  } else {
    QMessageBox::critical(this, "Error",
                          "Failed to inject file. Disk might be full.");
  }
}

/**
 * @brief Handles custom context menu requests for the tree view.
 */
void MainWindow::onCustomContextMenu(const QPoint &pos) {
  QModelIndex index = m_treeView->indexAt(pos);
  if (!index.isValid())
    return;

  QMenu contextMenu(this);
  contextMenu.addAction("Save File As...", this,
                        &MainWindow::onSaveFileAs); // New
  contextMenu.addSeparator();
  contextMenu.addAction("Rename File", this, &MainWindow::onRenameFile);
  contextMenu.addAction("Delete File", this, &MainWindow::onDeleteFile);

  contextMenu.exec(m_treeView->mapToGlobal(pos));
}

/**
 * @brief Deletes the currently selected file from the disk image.
 */
void MainWindow::onDeleteFile() {
  QModelIndex index = m_treeView->currentIndex();
  if (!index.isValid())
    return;

  Atari::DirEntry entry = m_model->getEntry(index);
  QString fileName = Atari::AtariDiskEngine::toQString(entry.getFilename());

  auto reply =
      QMessageBox::question(this, "Confirm Delete",
                            "Are you sure you want to delete " + fileName + "?",
                            QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    if (m_engine->deleteFile(entry)) {
      m_model->refresh();               // Refresh the tree
      m_hexView->setData(QByteArray()); // Clear hex view
      statusBar()->showMessage("File deleted successfully", 3000);
    } else {
      QMessageBox::critical(this, "Error", "Could not delete file.");
    }
  }
}

/**
 * @brief Shows disk information in a dialog.
 */
void MainWindow::onDiskInfo() {
  if (!m_engine->isLoaded())
    return;

  Atari::DiskStats stats = m_engine->getDiskStats();
  // Assuming you added checkBootSector() to the engine as discussed
  Atari::BootSectorInfo boot = m_engine->checkBootSector();

  QString bootStatus =
      boot.isExecutable
          ? "<font color='#00AA00'><b>Executable (Bootable)</b></font>"
          : "<font color='#AA0000'><b>Non-Executable (Data Only)</b></font>";

  QString info = QString("<h3>System & Boot</h3>"
                         "<b>OEM Signature:</b> %1<br>"
                         "<b>Boot Status:</b> %2<br>"
                         "<b>Checksum:</b> %3 (Target: 0x1234)<br>"
                         "<hr>"
                         "<h3>Disk Geometry</h3>"
                         "<b>Mode:</b> %4<br>"
                         "<b>Total Size:</b> %5 KB<br>"
                         "<b>Space Used:</b> %6 KB<br>"
                         "<b>Space Free:</b> %7 KB<br><br>"
                         "<b>Files:</b> %8 | <b>Directories:</b> %9<br>"
                         "<b>Clusters:</b> %10 total (%11 free)<br>"
                         "<b>Sectors Per Cluster:</b> %12")
                     .arg(boot.oemName.isEmpty() ? "None" : boot.oemName)
                     .arg(bootStatus)
                     .arg(QString("0x%1")
                              .arg(boot.currentChecksum, 4, 16, QChar('0'))
                              .toUpper())
                     .arg((m_engine->getGeometryMode() ==
                           Atari::AtariDiskEngine::GeometryMode::BPB)
                              ? "Standard (BPB)"
                              : "Hatari/Vectronix")
                     .arg(stats.totalBytes / 1024)
                     .arg(stats.usedBytes / 1024)
                     .arg(stats.freeBytes / 1024)
                     .arg(stats.fileCount)
                     .arg(stats.dirCount)
                     .arg(stats.totalClusters)
                     .arg(stats.freeClusters)
                     .arg(stats.sectorsPerCluster);

  QMessageBox::information(this, "Atari ST Disk Information", info);
}

void MainWindow::onFixBoot() {
  if (!m_engine->isLoaded())
    return;

  if (m_engine->fixBootChecksum()) {
    QMessageBox::information(
        this, "Success",
        "Boot sector checksum adjusted to 0x1234.\nThis disk is now bootable.");
    // Refresh display if needed
    onDiskInfo();
  } else {
    QMessageBox::critical(this, "Error", "Failed to modify boot sector.");
  }
}

void MainWindow::onRenameFile() {
  QModelIndex index = m_treeView->currentIndex();
  if (!index.isValid())
    return;

  Atari::DirEntry entry = m_model->getEntry(index);
  QString oldName = Atari::AtariDiskEngine::toQString(entry.getFilename());

  bool ok;
  QString newName = QInputDialog::getText(
      this, "Rename File", "New Name (8.3 format):", QLineEdit::Normal, oldName,
      &ok);

  if (ok && !newName.isEmpty()) {
    if (m_engine->renameFile(entry, newName)) {
      m_model->refresh();
      statusBar()->showMessage("File renamed", 3000);
    } else {
      QMessageBox::critical(
          this, "Error", "Could not rename file. Is the disk write-protected?");
    }
  }
}

void MainWindow::onSaveFileAs() {
  QModelIndex index = m_treeView->currentIndex();
  if (!index.isValid())
    return;

  Atari::DirEntry entry = m_model->getEntry(index);
  QString fileName = Atari::AtariDiskEngine::toQString(entry.getFilename());

  QString savePath =
      QFileDialog::getSaveFileName(this, "Extract File", fileName);
  if (savePath.isEmpty())
    return;

  QByteArray fileData = m_engine->getFileData(entry);

  QFile file(savePath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(fileData);
    file.close();
    statusBar()->showMessage("File extracted to " + savePath, 3000);
  } else {
    QMessageBox::critical(this, "Error", "Could not write to local file.");
  }
}

void MainWindow::onFormatDisk() {
  if (!m_engine->isLoaded())
    return;

  auto reply =
      QMessageBox::warning(this, "Confirm Format",
                           "ARE YOU SURE?\n\nThis will permanently delete ALL "
                           "files and directories on this virtual disk.",
                           QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    if (m_engine->formatDisk()) {
      m_model->refresh();
      m_hexView->setData(QByteArray()); // Clear the hex viewer cache
      statusBar()->showMessage("Disk formatted successfully", 5000);

      // Re-run Disk Info to show the new empty state
      onDiskInfo();
    } else {
      QMessageBox::critical(this, "Error", "Format failed.");
    }
  }
}

void MainWindow::onEditOemLabel() {
  if (!m_engine->isLoaded())
    return;

  // Get current label via the checkBootSector info we already wrote
  Atari::BootSectorInfo info = m_engine->checkBootSector();

  bool ok;
  QString newLabel = QInputDialog::getText(
      this, "Edit OEM Signature",
      "Enter 6-character Signature:", QLineEdit::Normal, info.oemName, &ok);

  if (ok && !newLabel.isEmpty()) {
    if (m_engine->setOemLabel(newLabel)) {
      statusBar()->showMessage("OEM Label updated and Checksum recalculated.",
                               3000);
      // Refresh the Disk Info if it's open, or just show success
      onDiskInfo();
    } else {
      QMessageBox::critical(this, "Error", "Could not update Boot Sector.");
    }
  }
}

void MainWindow::onViewFatTable() {
  if (!m_engine->isLoaded())
    return;

  Atari::ClusterMap map = m_engine->getClusterMap();
  Atari::DiskStats stats = m_engine->getDiskStats();

  QDialog *dlg = new QDialog(this);
  dlg->setWindowTitle("Advanced Disk Map & FAT Analysis");
  dlg->resize(700, 500);

  QHBoxLayout *mainLayout = new QHBoxLayout(dlg);

  // --- LEFT SIDE: THE MAP (Using Table for rendering stability) ---
  QTextEdit *mapView = new QTextEdit(dlg);
  mapView->setReadOnly(true);
  mapView->setFont(QFont("Monospace", 10));

  // We use a table because Qt's HTML engine handles cell borders more reliably
  // than spans
  QString html = "<h3>Cluster Usage Map</h3>"
                 "<table cellspacing='1' cellpadding='0' "
                 "style='border-collapse: separate;'>";

  for (int i = 0; i < map.clusters.size(); ++i) {
    if (i % 32 == 0)
      html += "<tr>";

    QString color;
    QString tip;
    switch (map.clusters[i]) {
    case Atari::ClusterStatus::Free:
      color = "#EEEEEE";
      tip = "Free";
      break;
    case Atari::ClusterStatus::Used:
      color = "#3498DB";
      tip = "Used";
      break;
    case Atari::ClusterStatus::EndOfChain:
      color = "#F1C40F";
      tip = "EOF";
      break;
    case Atari::ClusterStatus::Bad:
      color = "#E74C3C";
      tip = "Bad";
      break;
    }

    // Width and Height are set as attributes, bgcolor for the fill,
    // and style for the black 'out line'
    html += QString("<td width='12' height='12' bgcolor='%1' "
                    "style='border: 1px solid black;' title='Cluster %2: "
                    "%3'>&nbsp;</td>")
                .arg(color)
                .arg(i + 2)
                .arg(tip);

    if ((i + 1) % 32 == 0)
      html += "</tr>";
  }

  if (map.clusters.size() % 32 != 0)
    html += "</tr>";
  html += "</table>";
  mapView->setHtml(html);

  // --- RIGHT SIDE: THE INFO PANEL ---
  QWidget *infoPanel = new QWidget(dlg);
  QVBoxLayout *infoLayout = new QVBoxLayout(infoPanel);

  QLabel *statsLabel = new QLabel(
      QString("<h4>Disk Health</h4>"
              "<b>Total Clusters:</b> %1<br>"
              "<b>Used:</b> %2<br>"
              "<b>Free:</b> %3<br>"
              "<b>Capacity Used:</b> %4%<br>"
              "<hr>"
              "<h4>Geometry</h4>"
              "<b>Cluster Size:</b> %5 bytes<br>"
              "<b>Data Start:</b> Sector 18<br>"
              "<b>FAT Type:</b> FAT12 (Atari)")
          .arg(map.totalClusters)
          .arg(map.totalClusters - stats.freeClusters)
          .arg(stats.freeClusters)
          .arg(formatPercent(
              (1.0 - (double)stats.freeClusters / map.totalClusters) * 100, 1))
          .arg(stats.sectorsPerCluster * 512));

  infoLayout->addWidget(statsLabel);
  infoLayout->addStretch();

  // Legend - Also using fixed widths to ensure visibility
  infoLayout->addWidget(new QLabel(
      "<b>Legend:</b><br>"
      "<table cellspacing='2'>"
      "<tr><td width='12' height='12' bgcolor='#3498DB' style='border:1px "
      "solid black;'></td><td>Data Cluster</td></tr>"
      "<tr><td width='12' height='12' bgcolor='#F1C40F' style='border:1px "
      "solid black;'></td><td>Last Cluster (EOF)</td></tr>"
      "<tr><td width='12' height='12' bgcolor='#EEEEEE' style='border:1px "
      "solid black;'></td><td>Empty Space</td></tr>"
      "<tr><td width='12' height='12' bgcolor='#E74C3C' style='border:1px "
      "solid black;'></td><td>Bad Sector</td></tr>"
      "</table>"));

  mainLayout->addWidget(mapView, 2);
  mainLayout->addWidget(infoPanel, 1);

  dlg->exec();
}

void MainWindow::onSearchDisk() {
  if (!m_engine->isLoaded())
    return;

  bool ok;
  QString searchTerm = QInputDialog::getText(
      this, "Disk Search", "Enter Text or Hex (0x...):", QLineEdit::Normal, "",
      &ok);
  if (!ok || searchTerm.isEmpty())
    return;

  QByteArray pattern;
  if (searchTerm.startsWith("0x")) {
    // Hex Search: 0x601A -> \x60\x1A
    pattern = QByteArray::fromHex(searchTerm.mid(2).toUtf8());
  } else {
    // ASCII Search
    pattern = searchTerm.toUtf8();
  }

  QVector<Atari::SearchResult> results = m_engine->searchPattern(pattern);

  if (results.isEmpty()) {
    QMessageBox::information(this, "Search", "No matches found.");
    return;
  }

  // Display Results in a list
  QDialog dlg(this);
  dlg.setWindowTitle("Search Results");
  dlg.resize(400, 300);
  QVBoxLayout *layout = new QVBoxLayout(&dlg);
  QListWidget *list = new QListWidget(&dlg);

  for (const auto &res : results) {
    list->addItem(QString("Sector %1 (Offset %2) [Total: 0x%3]")
                      .arg(res.sector)
                      .arg(res.offsetInSector)
                      .arg(QString::number(res.offset, 16).toUpper()));
  }

  layout->addWidget(
      new QLabel(QString("Found %1 matches:").arg(results.size())));
  layout->addWidget(list);

  connect(list, &QListWidget::itemDoubleClicked,
          [this, results, &dlg, list](QListWidgetItem *item) {
            int row = list->row(item);
            if (row >= 0 && row < results.size()) {
              uint32_t targetOffset = results[row].offset;

              // Use the hex view pointer directly
              this->m_hexView->scrollToOffset(targetOffset);

              // Force the Hex View to be the active widget
              this->m_hexView->setFocus();

              // Close the dialog so you can see the results immediately
              dlg.accept();
            }
          });

  dlg.exec();
}

// Helper for decimal formatting
QString MainWindow::formatPercent(double value, int precision) {
  return QString::number(value, 'f', precision);
}