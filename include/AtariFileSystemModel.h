#ifndef ATARIFILESYSTEMMODEL_H
#define ATARIFILESYSTEMMODEL_H

#include "AtariDiskEngine.h"
#include <QAbstractItemModel>
#include <memory>

class AtariFileSystemModel : public QAbstractItemModel {
  Q_OBJECT
public:
  explicit AtariFileSystemModel(QObject *parent = nullptr);
  void setEngine(Atari::AtariDiskEngine *engine);
  void refresh() { buildTree(); }

  struct Node {
    Atari::DirEntry entry;
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    Node(const Atari::DirEntry &e, Node *p) : entry(e), parent(p) {}
    Node() = default;
  };

  Atari::DirEntry getEntry(const QModelIndex &index) const;

  QModelIndex index(int row, int column,
                    const QModelIndex &parent) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
  void buildTree();
  void buildChildren(Node *parentNode);
  Node *nodeFromIndex(const QModelIndex &index) const;

  Atari::AtariDiskEngine *m_engine = nullptr;
  std::unique_ptr<Node> m_root;
};
#endif
