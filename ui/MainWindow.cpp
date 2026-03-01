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
#include <QKeySequence>
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

  // 2. CREATE THE DROP-DOWN FILE MENU
  QMenu *fileMenu = menuBar()->addMenu("&File");

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

  // 3. Keep the Toolbar for quick access
  QToolBar *toolBar = addToolBar("Main");
  toolBar->addAction(openAction);

  // 4. Status Bar: Displays disk geometry and status messages
  m_formatLabel = new QLabel("Ready", this);
  statusBar()->addPermanentWidget(m_formatLabel);

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
    // Initial analysis of the disk structure
    m_engine->readRootDirectory();
    m_model->refresh();
    m_treeView->expandAll();
    m_formatLabel->setText(m_engine->getFormatInfoString());

    // Show Boot Sector (Sector 0) in Hex View by default
    m_hexView->setData(m_engine->getSector(0));
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
  QAction *deleteAct = contextMenu.addAction("Delete File");

  // Store the index so the delete slot knows what to kill
  connect(deleteAct, &QAction::triggered, this, &MainWindow::onDeleteFile);

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
