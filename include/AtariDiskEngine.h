#pragma once

// =============================================================================
//  AtariDiskEngine.h
//  Atari ST Toolkit — Core Disk Parsing Engine
//
//  Targets raw .ST sector-for-sector disk images.
//  All multi-byte BPB fields are accessed via inline helpers to remain safe 
//  regardless of host CPU endianness.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

// Qt bridge — only pulled in when building with Qt.
#ifdef QT_CORE_LIB
#  include <QByteArray>
#  include <QString>
#  include <QFile>
#  include <QIODevice>
#endif

namespace Atari {

// =============================================================================
//  CONSTANTS
// =============================================================================
inline constexpr uint16_t BOOT_CHECKSUM_TARGET = 0x1234;
inline constexpr uint32_t SECTOR_SIZE          = 512;
inline constexpr uint16_t FAT12_EOC_MIN        = 0xFF8;
inline constexpr uint16_t FAT12_BAD_CLUSTER    = 0xFF7;
inline constexpr uint16_t FAT12_FREE_CLUSTER   = 0x000;
inline constexpr uint32_t DIRENT_SIZE          = 32;
inline constexpr uint8_t  DIRENT_DELETED       = 0xE5;
inline constexpr uint8_t  DIRENT_END           = 0x00;

// =============================================================================
//  ENDIAN HELPERS
// =============================================================================
[[nodiscard]] inline uint16_t readLE16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

[[nodiscard]] inline uint32_t readLE32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// =============================================================================
//  BIOS PARAMETER BLOCK (BPB)
// =============================================================================
#pragma pack(push, 1)
struct AtariBPB {
    uint8_t  jumpBoot[3];
    uint8_t  oemSerial[5];
    uint8_t  bytesPerSector[2];
    uint8_t  sectorsPerCluster;
    uint8_t  reservedSectors[2];
    uint8_t  fatCount;
    uint8_t  rootEntryCount[2];
    uint8_t  totalSectors16[2];
    uint8_t  mediaDescriptor;
    uint8_t  sectorsPerFat[2];
    uint8_t  sectorsPerTrack[2];
    uint8_t  headCount[2];
    uint8_t  hiddenSectors[2];

    [[nodiscard]] uint16_t getBytesPerSector()  const noexcept { return readLE16(bytesPerSector); }
    [[nodiscard]] uint16_t getReservedSectors() const noexcept { return readLE16(reservedSectors); }
    [[nodiscard]] uint16_t getRootEntryCount()  const noexcept { return readLE16(rootEntryCount); }
    [[nodiscard]] uint16_t getTotalSectors()    const noexcept { return readLE16(totalSectors16); }
    [[nodiscard]] uint16_t getSectorsPerFat()   const noexcept { return readLE16(sectorsPerFat); }
};
#pragma pack(pop)

// =============================================================================
//  DIRECTORY ENTRY
// =============================================================================
#pragma pack(push, 1)
struct DirEntry {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attr;
    uint8_t  reserved[10];
    uint8_t  time[2];
    uint8_t  date[2];
    uint8_t  startCluster[2];
    uint8_t  fileSize[4];

    [[nodiscard]] uint16_t getStartCluster() const noexcept { return readLE16(startCluster); }
    [[nodiscard]] uint32_t getFileSize()     const noexcept { return readLE32(fileSize); }
    [[nodiscard]] bool isDirectory()         const noexcept { return (attr & 0x10) != 0; }
    [[nodiscard]] bool isEndOfDir()          const noexcept { return name[0] == DIRENT_END; }

    [[nodiscard]] std::string getFilename() const;
};
#pragma pack(pop)

// =============================================================================
//  ENGINE CLASS
// =============================================================================
class AtariDiskEngine {
public:
    AtariDiskEngine() = default;
    explicit AtariDiskEngine(std::vector<uint8_t> imageData);
    AtariDiskEngine(const uint8_t* data, std::size_t byteCount);

    [[nodiscard]] bool isLoaded() const noexcept { return !m_image.empty(); }

#ifdef QT_CORE_LIB
    // Qt Bridge API
    bool loadImage(const QString& path);
    [[nodiscard]] QString loadError() const { return m_loadError; }
    [[nodiscard]] QByteArray getSector(uint32_t sectorIndex) const;
    [[nodiscard]] QByteArray getBootSector() const { return getSector(0); }
    [[nodiscard]] QByteArray readFileQt(const DirEntry& entry) const;
    [[nodiscard]] static QByteArray toQByteArray(const std::vector<uint8_t>& vec);
    [[nodiscard]] static std::vector<uint8_t> fromQByteArray(const QByteArray& ba);
    [[nodiscard]] static QString toQString(const std::string& s);
#endif

    // Core Logic
    [[nodiscard]] const AtariBPB& bpb() const noexcept;
    [[nodiscard]] bool validateBootChecksum() const noexcept;
    [[nodiscard]] static bool validateBootChecksum(const uint8_t* sector512) noexcept;

    // Offsets & FAT
    [[nodiscard]] uint32_t fat1Offset() const noexcept;
    [[nodiscard]] uint32_t rootDirOffset() const noexcept;
    [[nodiscard]] uint32_t dataRegionOffset() const noexcept;
    [[nodiscard]] uint32_t clusterOffset(uint16_t cluster) const noexcept;
    [[nodiscard]] uint16_t getNextCluster(uint16_t currentCluster) const noexcept;
    [[nodiscard]] std::vector<uint16_t> getClusterChain(uint16_t startCluster) const;

    // Parsing
    [[nodiscard]] std::vector<DirEntry> readRootDirectory() const;
    [[nodiscard]] std::vector<DirEntry> readSubDirectory(uint16_t startCluster) const;
    [[nodiscard]] std::vector<uint8_t> readFile(const DirEntry& entry) const;

    [[nodiscard]] const uint8_t* rawData()   const noexcept { return m_image.data(); }
    [[nodiscard]] std::size_t    imageSize() const noexcept { return m_image.size(); }

private:
    std::vector<uint8_t> m_image;
    const AtariBPB* m_bpb = nullptr;
    void init();

#ifdef QT_CORE_LIB
    QString m_loadError;
    QByteArray regionToQByteArray(uint32_t byteOffset, uint32_t byteCount) const;
#endif
};

} // namespace Atari
