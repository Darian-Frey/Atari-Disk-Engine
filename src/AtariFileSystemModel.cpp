#include "AtariFileSystemModel.h"
#include <QDebug>
#include <QRegExp>

AtariFileSystemModel::AtariFileSystemModel(QObject *parent)
    : QAbstractItemModel(parent), m_engine(nullptr) {
  // Initialize with an empty root node
  m_root = std::make_unique<Node>();
}

void AtariFileSystemModel::setEngine(Atari::AtariDiskEngine *engine) {
  m_engine = engine;
  buildTree();
}

void AtariFileSystemModel::buildTree() {
  beginResetModel();
  m_root = std::make_unique<Node>(); // Clear old data

  if (m_engine && m_engine->isLoaded()) {
    // Retrieve entries using the Hatari-style probing logic
    std::vector<Atari::DirEntry> rootEntries = m_engine->readRootDirectory();

    for (const auto &entry : rootEntries) {
      auto child = std::make_unique<Node>(entry, m_root.get());
      if (entry.isDirectory()) {
        buildChildren(child.get());
      }
      m_root->children.push_back(std::move(child));
    }
  }
  endResetModel();
}

void AtariFileSystemModel::buildChildren(Node *parentNode) {
  if (!m_engine)
    return;

  // Safety: If the start cluster is 0 or 1, and it's not the root,
  // it's likely a fake directory entry.
  if (parentNode->entry.getStartCluster() < 2) {
    qDebug() << "[MODEL] Skipping sub-directory scan for suspicious cluster:"
             << parentNode->entry.getStartCluster();
    return;
  }

  std::vector<Atari::DirEntry> subEntries =
      m_engine->readSubDirectory(parentNode->entry.getStartCluster());

  for (const auto &entry : subEntries) {
    // Only add if the name actually looks printable
    QString name = Atari::AtariDiskEngine::toQString(entry.getFilename());
    if (name.contains(QRegExp("[^a-zA-Z0-9\\.\\s]"))) {
      qDebug() << "[MODEL] Blocking scrambled child entry:" << name;
      continue;
    }

    auto child = std::make_unique<Node>(entry, parentNode);
    if (entry.isDirectory() && entry.name[0] != '.') {
      buildChildren(child.get());
    }
    parentNode->children.push_back(std::move(child));
  }
}

AtariFileSystemModel::Node *
AtariFileSystemModel::nodeFromIndex(const QModelIndex &index) const {
  if (index.isValid()) {
    return static_cast<Node *>(index.internalPointer());
  }
  return m_root.get();
}

Atari::DirEntry AtariFileSystemModel::getEntry(const QModelIndex &index) const {
  if (!index.isValid()) {
    qDebug() << "[MODEL] Warning: getEntry called with invalid index.";
    return Atari::DirEntry();
  }

  Node *node = static_cast<Node *>(index.internalPointer());
  if (!node) {
    qDebug() << "[MODEL] CRITICAL: Node pointer is NULL for index" << index;
    return Atari::DirEntry();
  }

  qDebug() << "[MODEL] Selection -> File:"
           << Atari::AtariDiskEngine::toQString(node->entry.getFilename())
           << "| Start Cluster:" << node->entry.getStartCluster()
           << "| Size:" << node->entry.getFileSize();

  return node->entry;
}
// =============================================================================
//  QAbstractItemModel Overrides
// =============================================================================

QModelIndex AtariFileSystemModel::index(int row, int column,
                                        const QModelIndex &parent) const {
  if (!hasIndex(row, column, parent))
    return QModelIndex();

  Node *parentNode = nodeFromIndex(parent);
  if (row < static_cast<int>(parentNode->children.size())) {
    return createIndex(row, column, parentNode->children[row].get());
  }
  return QModelIndex();
}

QModelIndex AtariFileSystemModel::parent(const QModelIndex &index) const {
  if (!index.isValid())
    return QModelIndex();

  Node *childNode = static_cast<Node *>(index.internalPointer());
  Node *parentNode = childNode->parent;

  if (parentNode == m_root.get() || !parentNode)
    return QModelIndex();

  // Find the row of the parent within the grandparent
  Node *grandParent = parentNode->parent;
  for (int i = 0; i < static_cast<int>(grandParent->children.size()); ++i) {
    if (grandParent->children[i].get() == parentNode) {
      return createIndex(i, 0, parentNode);
    }
  }
  return QModelIndex();
}

int AtariFileSystemModel::rowCount(const QModelIndex &parent) const {
  if (parent.column() > 0)
    return 0;
  Node *parentNode = nodeFromIndex(parent);
  return static_cast<int>(parentNode->children.size());
}

int AtariFileSystemModel::columnCount(const QModelIndex &) const {
  return 1; // Filename column
}

QVariant AtariFileSystemModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  Node *node = static_cast<Node *>(index.internalPointer());
  if (role == Qt::DisplayRole) {
    return Atari::AtariDiskEngine::toQString(node->entry.getFilename());
  }
  return QVariant();
}

Qt::ItemFlags AtariFileSystemModel::flags(const QModelIndex &index) const {
  if (!index.isValid())
    return Qt::NoItemFlags;
  return QAbstractItemModel::flags(index);
}