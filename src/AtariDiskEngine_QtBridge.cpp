// =============================================================================
//  AtariDiskEngine_QtBridge.cpp
//  Atari ST Toolkit — Qt Bridge Layer (Implementation)
// =============================================================================

#ifdef QT_CORE_LIB

#include "AtariDiskEngine.h"
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QByteArray>

namespace Atari {

bool AtariDiskEngine::loadImage(const QString& path) {
    m_image.clear();
    m_bpb = nullptr;
    m_loadError = QString();

    QFile file(path);
    if (!file.exists()) {
        m_loadError = QStringLiteral("File not found: %1").arg(path);
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_loadError = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    if (raw.size() < static_cast<int>(SECTOR_SIZE)) {
        m_loadError = QStringLiteral("File is too small to be a valid disk image.");
        return false;
    }

    m_image = fromQByteArray(raw);

    try {
        init();
    } catch (const std::exception& ex) {
        m_image.clear();
        m_loadError = QString::fromUtf8(ex.what());
        return false;
    }

    return true;
}

QByteArray AtariDiskEngine::getSector(uint32_t sectorIndex) const {
    uint32_t offset = sectorIndex * SECTOR_SIZE;
    if (offset + SECTOR_SIZE > m_image.size()) return QByteArray();
    return regionToQByteArray(offset, SECTOR_SIZE);
}

QByteArray AtariDiskEngine::readFileQt(const DirEntry& entry) const {
    return toQByteArray(readFile(entry));
}

QByteArray AtariDiskEngine::toQByteArray(const std::vector<uint8_t>& vec) {
    return QByteArray(reinterpret_cast<const char*>(vec.data()), static_cast<int>(vec.size()));
}

std::vector<uint8_t> AtariDiskEngine::fromQByteArray(const QByteArray& ba) {
    const uint8_t* begin = reinterpret_cast<const uint8_t*>(ba.constData());
    return std::vector<uint8_t>(begin, begin + ba.size());
}

QString AtariDiskEngine::toQString(const std::string& s) {
    return QString::fromStdString(s);
}

QByteArray AtariDiskEngine::regionToQByteArray(uint32_t byteOffset, uint32_t byteCount) const {
    if (byteOffset + byteCount > m_image.size()) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(m_image.data() + byteOffset), static_cast<int>(byteCount));
}

} // namespace Atari

#endif // QT_CORE_LIB
