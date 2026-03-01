#include "MainWindow.h"
#include "HexViewWidget.h"
#include <QDebug>
#include <QFileDialog>
#include <QHeaderView>
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) { setupUi(); }

void MainWindow::setupUi() {
  m_engine = new Atari::AtariDiskEngine();
  m_model = new AtariFileSystemModel(this);
  m_model->setEngine(m_engine);

  // Main Splitter for Tree and Hex View
  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

  m_treeView = new QTreeView(this);
  m_treeView->setModel(m_model);
  m_treeView->header()->setSectionResizeMode(QHeaderView::Stretch);

  m_hexView = new HexViewWidget(this);

  splitter->addWidget(m_treeView);
  splitter->addWidget(m_hexView);
  splitter->setStretchFactor(1, 1);
  setCentralWidget(splitter);

  // Toolbar
  QToolBar *toolBar = addToolBar("File");
  toolBar->addAction("Open Atari Disk", this, &MainWindow::onOpenFile);

  // Status
  m_formatLabel = new QLabel("Ready", this);
  statusBar()->addPermanentWidget(m_formatLabel);

  connect(m_treeView, &QTreeView::clicked, this, &MainWindow::onFileSelected);

  resize(1000, 700);
  setWindowTitle("Atari ST Toolkit — Lotus Edition");
}

void MainWindow::onOpenFile() {
  QString fileName = QFileDialog::getOpenFileName(this, "Open Disk", "",
                                                  "Atari Disks (*.st *.msa)");
  if (!fileName.isEmpty() && m_engine->loadImage(fileName)) {
    m_engine->readRootDirectory();
    m_model->refresh();
    m_treeView->expandAll();
    m_formatLabel->setText(m_engine->getFormatInfoString());

    // Show Boot Sector in Hex View by default
    m_hexView->setData(m_engine->getSector(0));
  }
}

void MainWindow::onFileSelected(const QModelIndex &index) {
  qDebug() << "[UI] File Selected. Index Row:" << index.row();

  Atari::DirEntry entry = m_model->getEntry(index);

  if (entry.isDirectory()) {
    qDebug() << "[UI] Item is a directory. Skipping Hex View.";
    return;
  }

  QByteArray fileData = m_engine->readFileQt(entry);

  if (fileData.isEmpty() && entry.getFileSize() > 0) {
    qDebug() << "[UI] ERROR: Engine returned empty data for non-empty file.";
    return;
  }

  qDebug() << "[UI] Updating Hex View with" << fileData.size() << "bytes.";
  m_hexView->setData(fileData);
}

void MainWindow::onFileLoaded() {
  // 1. Trigger the Hatari-style directory probing
  m_engine->readRootDirectory();

  // 2. Refresh the tree model to show the new files
  m_model->refresh();

  // 3. Expand the tree so the user sees the files immediately
  m_treeView->expandAll();

  // 4. Update the status bar with the disk format info
  m_formatLabel->setText(m_engine->getFormatInfoString());
}