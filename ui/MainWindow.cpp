#include "MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QToolBar>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_model(new AtariFileSystemModel(this)) {
    setupUi();
    setupActions();
    m_treeView->setModel(m_model);
}

void MainWindow::setupUi() {
    auto *splitter = new QSplitter(this);

    m_treeView = new QTreeView(splitter);
    m_stackedWidget = new QStackedWidget(splitter);

    // File Info Pane
    m_infoWidget = new QWidget();
    auto *infoLayout = new QVBoxLayout(m_infoWidget);
    m_infoLabel = new QLabel(tr("Select a file or directory to view properties."));
    m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(m_infoLabel);
    
    // Hex Pane
    m_hexView = new HexViewWidget();

    m_stackedWidget->addWidget(m_infoWidget); // Index 0
    m_stackedWidget->addWidget(m_hexView);    // Index 1

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);
    setWindowTitle(tr("Atari ST Toolkit"));
    resize(1024, 768);
}

void MainWindow::setupActions() {
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *toolBar = addToolBar(tr("Main"));

    auto *openAct = new QAction(tr("&Open Image..."), this);
    connect(openAct, &QAction::triggered, this, &MainWindow::on_actionOpen_triggered);
    fileMenu->addAction(openAct);
    toolBar->addAction(openAct);

    m_actionExtract = new QAction(tr("&Extract File..."), this);
    m_actionExtract->setEnabled(false);
    connect(m_actionExtract, &QAction::triggered, this, &MainWindow::on_actionExtract_triggered);
    fileMenu->addAction(m_actionExtract);
    toolBar->addAction(m_actionExtract);

    auto *bootAct = new QAction(tr("View &Boot Sector"), this);
    connect(bootAct, &QAction::triggered, this, &MainWindow::on_actionViewBootSector_triggered);
    fileMenu->addAction(bootAct);

    connect(m_treeView, &QTreeView::clicked, this, &MainWindow::on_treeView_clicked);
}

void MainWindow::on_actionOpen_triggered() {
    QString path = QFileDialog::getOpenFileName(this, tr("Open Atari ST Image"), "", tr("ST Images (*.st *.ST)"));
    if (path.isEmpty()) return;

    if (!m_engine.loadImage(path)) {
        QMessageBox::critical(this, tr("Load Error"), m_engine.loadError());
        return;
    }

    m_model->setEngine(&m_engine);
    m_model->reload();
    statusBar()->showMessage(tr("Loaded: %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::on_treeView_clicked(const QModelIndex &index) {
    const auto *entry = m_model->entryFromIndex(index);
    if (!entry) return;

    showFileInfo(*entry);

    if (entry->isDirectory()) {
        m_stackedWidget->setCurrentIndex(0);
        m_actionExtract->setEnabled(false);
    } else {
        QByteArray data = m_engine.readFileQt(*entry);
        m_hexView->setBuffer(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
        m_stackedWidget->setCurrentIndex(1);
        m_actionExtract->setEnabled(true);
    }
}

void MainWindow::showFileInfo(const Atari::DirEntry &entry) {
    QString info = tr("Name: %1\nSize: %2 bytes\nCluster: %3\nType: %4")
        .arg(Atari::AtariDiskEngine::toQString(entry.getFilename()))
        .arg(entry.getFileSize())
        .arg(entry.getStartCluster())
        .arg(entry.isDirectory() ? tr("Directory") : tr("File"));
    m_infoLabel->setText(info);
}

void MainWindow::on_actionViewBootSector_triggered() {
    if (!m_engine.isLoaded()) return;
    QByteArray boot = m_engine.getBootSector();
    m_hexView->setBuffer(reinterpret_cast<const uint8_t*>(boot.constData()), boot.size(), 0);
    m_stackedWidget->setCurrentIndex(1);
}

void MainWindow::on_actionExtract_triggered() {
    const auto *entry = m_model->entryFromIndex(m_treeView->currentIndex());
    if (!entry || entry->isDirectory()) return;

    QString savePath = QFileDialog::getSaveFileName(this, tr("Extract File"), Atari::AtariDiskEngine::toQString(entry->getFilename()));
    if (savePath.isEmpty()) return;

    QFile outFile(savePath);
    if (outFile.open(QIODevice::WriteOnly)) {
        outFile.write(m_engine.readFileQt(*entry));
        outFile.close();
    }
}
