#pragma once

#include <QMainWindow>
#include <QTreeView>
#include <QStackedWidget>
#include <QLabel>
#include <QAction>
#include "../include/AtariDiskEngine.h"
#include "../include/AtariFileSystemModel.h"
#include "HexViewWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void on_actionOpen_triggered();
    void on_treeView_clicked(const QModelIndex &index);
    void on_actionExtract_triggered();
    void on_actionViewBootSector_triggered();

private:
    void setupUi();
    void setupActions();
    void showFileInfo(const Atari::DirEntry &entry);

    Atari::AtariDiskEngine m_engine;
    AtariFileSystemModel *m_model = nullptr;

    QTreeView *m_treeView = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;
    
    QWidget *m_infoWidget = nullptr;
    QLabel *m_infoLabel = nullptr;
    HexViewWidget *m_hexView = nullptr;

    QAction *m_actionExtract = nullptr;
};
