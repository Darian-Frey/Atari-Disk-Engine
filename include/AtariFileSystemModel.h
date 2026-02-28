#pragma once

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>
#include <memory>
#include <vector>
#include "AtariDiskEngine.h"

class AtariFileSystemModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit AtariFileSystemModel(QObject *parent = nullptr);

    void setEngine(Atari::AtariDiskEngine *engine);
    void reload();

    void refresh() {
        beginResetModel();
        // Re-scan logic here
        endResetModel();
    }

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    const Atari::DirEntry *entryFromIndex(const QModelIndex &index) const;

private:
    struct Node {
        Atari::DirEntry entry;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    Atari::AtariDiskEngine *m_engine = nullptr;
    std::unique_ptr<Node> m_root;

    void buildTree();
    void buildChildren(Node *parentNode);
    Node *nodeFromIndex(const QModelIndex &index) const;
};
