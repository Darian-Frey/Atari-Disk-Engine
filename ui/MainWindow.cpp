#include "MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDebug>
#include <QLabel>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
}

void MainWindow::setupUi() {
    // 1. Core Engine and Model Setup
    m_engine = new Atari::AtariDiskEngine();
    m_model = new AtariFileSystemModel(this); // Matches candidate expecting 1 argument
    m_model->setEngine(m_engine);             // Assuming your model has a setter

    // 2. Central Widget (Tree View)
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->header()->setSectionResizeMode(QHeaderView::Stretch);
    setCentralWidget(m_treeView);

    // 3. Toolbar and Manual Sector Slider
    QToolBar *toolBar = addToolBar("File");
    QAction *openAction = toolBar->addAction("Open Atari Disk");
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    toolBar->addSeparator();
    
    // Manual Sector Controls
    toolBar->addWidget(new QLabel(" Manual Root Sector: "));
    m_sectorOverride = new QSpinBox(this);
    m_sectorOverride->setRange(0, 500);
    m_sectorOverride->setValue(11); // Atari ST standard default
    toolBar->addWidget(m_sectorOverride);

    m_jumpBtn = new QPushButton("Force Jump", this);
    toolBar->addWidget(m_jumpBtn);
    connect(m_jumpBtn, &QPushButton::clicked, this, &MainWindow::onManualSectorJump);

    // 4. Status Bar and Format Labels
    m_formatLabel = new QLabel("Mode: Unknown", this);
    statusBar()->addPermanentWidget(m_formatLabel);

    // 5. Connect Selection for Hex View (Future implementation)
    connect(m_treeView, &QTreeView::clicked, this, &MainWindow::onFileSelected);

    resize(800, 600);
    setWindowTitle("Atari ST Toolkit — Latitude 5480 Edition");
}

void MainWindow::onOpenFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open Atari Disk Image", "", "Atari Disks (*.st *.msa *.img)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Could not open file.");
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    // Load data into engine
    std::vector<uint8_t> stdData(data.begin(), data.end());
    m_engine->load(stdData);

    onFileLoaded();
}

void MainWindow::onFileLoaded() {
    // 1. Trigger the Directory Analysis
    m_engine->readRootDirectory();

    // 2. Refresh the Tree Model
    m_model->refresh();
    m_treeView->expandAll();

    // 3. Update UI Status based on Hatari-style detection
    m_formatLabel->setText(m_engine->getFormatInfoString());
    
    if (m_engine->lastGeometryMode() == Atari::AtariDiskEngine::GeometryMode::HatariGuess) {
        m_formatLabel->setStyleSheet("color: orange; font-weight: bold;");
        statusBar()->showMessage("Warning: Non-standard geometry. Try Manual Sector Jump if empty.", 5000);
    } else {
        m_formatLabel->setStyleSheet("color: green;");
        statusBar()->showMessage("Disk Loaded Successfully.", 3000);
    }
}

void MainWindow::onManualSectorJump() {
    int targetSector = m_sectorOverride->value();
    qDebug() << "[UI] User manually forcing Root Directory at Sector:" << targetSector;

    // Call engine with manual override
    m_engine->readRootDirectoryManual(targetSector);
    
    // Refresh UI
    m_model->refresh();
    m_treeView->expandAll();
    
    m_formatLabel->setText(QString("Manual Mode: Sector %1").arg(targetSector));
    m_formatLabel->setStyleSheet("color: red; font-weight: bold;");
}

void MainWindow::onFileSelected(const QModelIndex &index) {
    // Logic for updating the HexViewWidget goes here
    qDebug() << "Selected item:" << index.data().toString();
}
