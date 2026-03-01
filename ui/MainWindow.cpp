#include "MainWindow.h"
#include "HexViewWidget.h"
#include <QAction>
#include <QDebug>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) { setupUi(); }

void MainWindow::setupUi() {
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
  splitter->setStretchFactor(1, 1);
  setCentralWidget(splitter);

  // 2. CREATE THE DROP-DOWN FILE MENU
  QMenu *fileMenu = menuBar()->addMenu("&File");

  // Open Action
  QAction *openAction =
      new QAction(QIcon::fromTheme("document-open"), "&Open Disk...", this);
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
  fileMenu->addAction(openAction);

  // Close Action
  QAction *closeAction = new QAction("&Close Image", this);
  connect(closeAction, &QAction::triggered, this, &MainWindow::onCloseFile);
  fileMenu->addAction(closeAction);

  fileMenu->addSeparator();

  // Exit Action
  QAction *exitAction = new QAction("E&xit", this);
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, &QWidget::close);
  fileMenu->addAction(exitAction);

  // 3. Keep the Toolbar for quick access
  QToolBar *toolBar = addToolBar("Main");
  toolBar->addAction(openAction);

  // 4. Status Bar
  m_formatLabel = new QLabel("Ready", this);
  statusBar()->addPermanentWidget(m_formatLabel);

  connect(m_treeView, &QTreeView::clicked, this, &MainWindow::onFileSelected);

  resize(1100, 750);
  setWindowTitle("Atari ST Toolkit");
}

// Add this new slot function below setupUi
void MainWindow::onCloseFile() {
  qDebug() << "[UI] Closing file...";

  // 1. Clear the engine data
  if (m_engine) {
    m_engine->load({});
  }

  // 2. Clear the Hex View
  if (m_hexView) {
    m_hexView->setData(QByteArray());
  }

  // 3. Refresh the model (it will see the empty engine and clear the tree)
  if (m_model) {
    m_model->refresh();
  }

  // 4. Update UI labels
  if (m_formatLabel) {
    m_formatLabel->setText("No Disk Loaded");
  }

  setWindowTitle("Atari ST Toolkit");
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
