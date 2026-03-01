#ifndef ATARIDISKENGINE_H
#define ATARIDISKENGINE_H

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>

namespace Atari {

inline constexpr uint16_t SECTOR_SIZE = 512;
inline constexpr uint16_t DIRENT_SIZE = 32;
inline constexpr uint16_t BOOT_CHECKSUM_TARGET = 0x1234;

inline uint16_t readLE16(const uint8_t *p) { return p[0] | (p[1] << 8); }
inline uint16_t readBE16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

inline void writeLE16(uint8_t *p, uint16_t val) {
  p[0] = val & 0xFF;
  p[1] = (val >> 8) & 0xFF;
}

inline void writeBE16(uint8_t *p, uint16_t val) {
  p[0] = (val >> 8) & 0xFF;
  p[1] = val & 0xFF;
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
    return fileSize[0] | (fileSize[1] << 8) | (fileSize[2] << 16) |
           (fileSize[3] << 24);
  }
  std::string getFilename() const;
};

class AtariDiskEngine {
public:
  bool injectFile(const QString &localPath);
  enum class GeometryMode { Unknown, BPB, HatariGuess };

  QByteArray readFileQt(const DirEntry &entry) const;

  const std::vector<uint8_t> &getRawImageData() const { return m_image; }

  AtariDiskEngine() = default;
  AtariDiskEngine(std::vector<uint8_t> imageData);
  AtariDiskEngine(const uint8_t *data, std::size_t byteCount);

  void load(const std::vector<uint8_t> &data);
  bool isLoaded() const { return !m_image.empty(); }

  std::vector<DirEntry> readRootDirectory() const;
  std::vector<DirEntry> readSubDirectory(uint16_t startCluster) const;
  std::vector<uint8_t> readFile(const DirEntry &entry) const;

  bool loadImage(const QString &path);
  QByteArray getSector(uint32_t sectorIndex) const;

  GeometryMode lastGeometryMode() const { return m_geoMode; }
  QString getFormatInfoString() const;
  static QString toQString(const std::string &s);

  bool validateBootChecksum() const noexcept;
  static bool validateBootChecksum(const uint8_t *sector512) noexcept;

  void createNew720KImage();

private:
  bool isValidDirectoryEntry(const uint8_t *data) const;
  void init();
  uint32_t fat1Offset() const noexcept;
  uint32_t clusterOffset(uint16_t cluster) const noexcept;
  uint16_t getNextCluster(uint16_t currentCluster) const noexcept;

  // Writing helpers (member versions to match common usage in this class)
  void writeLE16(uint8_t *p, uint16_t val) const noexcept {
    Atari::writeLE16(p, val);
  }
  void writeBE16(uint8_t *p, uint16_t val) const noexcept {
    Atari::writeBE16(p, val);
  }
  std::vector<uint16_t> getClusterChain(uint16_t startCluster) const;

  std::vector<uint8_t> m_image;
  uint32_t m_internalOffset = 0;
  mutable GeometryMode m_geoMode = GeometryMode::Unknown;
  uint32_t m_manualRootSector = 11;
  bool m_useManualOverride = false;

  void writeLE16(uint8_t *ptr, uint16_t val);
  void writeLE32(uint8_t *ptr, uint32_t val);
  int findFreeCluster() const;
  void setFATEntry(int cluster, uint16_t value);
};

} // namespace Atari
#endif
