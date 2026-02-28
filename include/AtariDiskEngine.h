#ifndef ATARIDISKENGINE_H
#define ATARIDISKENGINE_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <QString>
#include <QByteArray>

namespace Atari {

// Standard Atari ST Disk Constants
constexpr uint16_t SECTOR_SIZE = 512;
constexpr uint16_t DIRENT_SIZE = 32;
constexpr uint16_t BOOT_CHECKSUM_TARGET = 0x1234;
constexpr uint16_t FAT12_EOC_MIN = 0xFF8;

// Helper to read Little-Endian 16-bit values
inline uint16_t readLE16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

// Helper to read Big-Endian 16-bit values (Atari Native)
inline uint16_t readBE16(const uint8_t* p) {
    return (p[0] << 8) | p[1];
}

struct DirEntry {
    uint8_t name[8];
    uint8_t ext[3];
    uint8_t attr;
    uint8_t reserved[10];
    uint8_t time[2];
    uint8_t date[2];
    uint8_t startCluster[2];
    uint8_t fileSize[4];

    bool isDirectory() const { return attr & 0x10; }
    uint16_t getStartCluster() const { return readLE16(startCluster); }
    uint32_t getFileSize() const { 
        return fileSize[0] | (fileSize[1] << 8) | (fileSize[2] << 16) | (fileSize[3] << 24); 
    }
    std::string getFilename() const;
};

class AtariDiskEngine {
public:
    enum class GeometryMode { Unknown, BPB, HatariGuess };

    AtariDiskEngine() = default;
    explicit AtariDiskEngine(std::vector<uint8_t> imageData);
    AtariDiskEngine(const uint8_t* data, std::size_t byteCount);

    // Data Loading
    void load(const std::vector<uint8_t>& data) { 
        m_image = data; 
        m_useManualOverride = false;
        init(); 
    }
    bool isLoaded() const { return !m_image.empty(); }

    // Directory & File Operations
    std::vector<DirEntry> readRootDirectory() const;
    std::vector<DirEntry> readSubDirectory(uint16_t startCluster) const;
    std::vector<uint8_t> readFile(const DirEntry& entry) const;

    // Manual Override for the Slider
    void readRootDirectoryManual(uint32_t sector) {
        m_manualRootSector = sector;
        m_useManualOverride = true;
        m_geoMode = GeometryMode::HatariGuess;
        readRootDirectory(); 
    }

    // Status & Diagnostics
    GeometryMode lastGeometryMode() const { return m_geoMode; }
    QString getFormatInfoString() const;
    bool validateBootChecksum() const noexcept;
    static bool validateBootChecksum(const uint8_t* sector512) noexcept;

    // Qt Bridge Helpers (Missing from previous version)
    bool loadImage(const QString& path);
    QByteArray getSector(uint32_t sectorIndex) const;
    QByteArray readFileQt(const DirEntry& entry) const;
    QByteArray regionToQByteArray(uint32_t byteOffset, uint32_t byteCount) const;
    static QByteArray toQByteArray(const std::vector<uint8_t>& vec);
    static std::vector<uint8_t> fromQByteArray(const QByteArray& ba);
    static QString toQString(const std::string& s);

private:
    void init();
    uint32_t fat1Offset() const noexcept;
    uint32_t clusterOffset(uint16_t cluster) const noexcept;
    uint16_t getNextCluster(uint16_t currentCluster) const noexcept;
    std::vector<uint16_t> getClusterChain(uint16_t startCluster) const;

    std::vector<uint8_t> m_image;
    uint32_t m_internalOffset = 0;
    
    // Geometry State
    mutable GeometryMode m_geoMode = GeometryMode::Unknown;
    uint32_t m_manualRootSector = 11;
    bool m_useManualOverride = false;
};

} // namespace Atari

#endif // ATARIDISKENGINE_H
