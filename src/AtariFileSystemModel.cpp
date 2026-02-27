#include "AtariFileSystemModel.h"

AtariFileSystemModel::AtariFileSystemModel(QObject *parent)
    : QAbstractItemModel(parent),
      m_root(std::make_unique<Node>()) {
    m_root->parent = nullptr;
    m_root->entry.name[0] = '/'; // Conceptual root
}

void AtariFileSystemModel::setEngine(Atari::AtariDiskEngine *engine) {
    beginResetModel();
    m_engine = engine;
    buildTree();
    endResetModel();
}

void AtariFileSystemModel::reload() {
    if (!m_engine) return;
    beginResetModel();
    buildTree();
    endResetModel();
}

void AtariFileSystemModel::buildTree() {
    m_root->children.clear();
    if (!m_engine || !m_engine->isLoaded()) return;

    const auto rootEntries = m_engine->readRootDirectory();
    for (const auto &e : rootEntries) {
        auto node = std::make_unique<Node>();
        node->entry = e;
        node->parent = m_root.get();
        if (e.isDirectory()) {
            buildChildren(node.get());
        }
        m_root->children.push_back(std::move(node));
    }
}

void AtariFileSystemModel::buildChildren(Node *parentNode) {
    if (!m_engine || !parentNode->entry.isDirectory()) return;

    const auto entries = m_engine->readSubDirectory(parentNode->entry.getStartCluster());
    for (const auto &e : entries) {
        // Skip . and .. entries to avoid infinite recursion
        std::string name = e.getFilename();
        if (name == "." || name == "..") continue;

        auto node = std::make_unique<Node>();
        node->entry = e;
        node->parent = parentNode;
        if (e.isDirectory()) {
            buildChildren(node.get());
        }
        parentNode->children.push_back(std::move(node));
    }
}

AtariFileSystemModel::Node *AtariFileSystemModel::nodeFromIndex(const QModelIndex &index) const {
    if (!index.isValid()) return m_root.get();
    return static_cast<Node *>(index.internalPointer());
}

QModelIndex AtariFileSystemModel::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();

    Node *parentNode = nodeFromIndex(parent);
    if (row < parentNode->children.size()) {
        return createIndex(row, column, parentNode->children[row].get());
    }
    return QModelIndex();
}

QModelIndex AtariFileSystemModel::parent(const QModelIndex &child) const {
    if (!child.isValid()) return QModelIndex();

    Node *childNode = static_cast<Node *>(child.internalPointer());
    Node *parentNode = childNode->parent;

    if (parentNode == m_root.get() || !parentNode) return QModelIndex();

    Node *grandParent = parentNode->parent;
    int row = 0;
    for (int i = 0; i < grandParent->children.size(); ++i) {
        if (grandParent->children[i].get() == parentNode) {
            row = i;
            break;
        }
    }
    return createIndex(row, 0, parentNode);
}

int AtariFileSystemModel::rowCount(const QModelIndex &parent) const {
    if (parent.column() > 0) return 0;
    Node *parentNode = nodeFromIndex(parent);
    return static_cast<int>(parentNode->children.size());
}

int AtariFileSystemModel::columnCount(const QModelIndex &) const {
    return 1;
}

QVariant AtariFileSystemModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return QVariant();

    Node *node = static_cast<Node *>(index.internalPointer());
    if (role == Qt::DisplayRole) {
        return Atari::AtariDiskEngine::toQString(node->entry.getFilename());
    }
    return QVariant();
}

Qt::ItemFlags AtariFileSystemModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index);
}

const Atari::DirEntry *AtariFileSystemModel::entryFromIndex(const QModelIndex &index) const {
    if (!index.isValid()) return nullptr;
    return &static_cast<Node *>(index.internalPointer())->entry;
}
