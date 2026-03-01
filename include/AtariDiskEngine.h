/**
 * @file AtariDiskEngine.h
 * @brief Core classes and utilities for handling Atari ST disk images.
 */

#ifndef ATARIDISKENGINE_H
#define ATARIDISKENGINE_H

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @namespace Atari
 * @brief Contains all classes and functions related to Atari ST disk
 * operations.
 */
namespace Atari {

/** @brief Standard sector size for Atari ST disks (512 bytes). */
inline constexpr uint16_t SECTOR_SIZE = 512;
/** @brief Size of a directory entry in FAT12 (32 bytes). */
inline constexpr uint16_t DIRENT_SIZE = 32;
/** @brief Target value for the boot sector checksum (0x1234). */
inline constexpr uint16_t BOOT_CHECKSUM_TARGET = 0x1234;

/**
 * @brief Reads a 16-bit little-endian value from a buffer.
 * @param p Pointer to the start of the 16-bit value.
 * @return The 16-bit value in host byte order.
 */
inline uint16_t readLE16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/**
 * @brief Reads a 16-bit big-endian value from a buffer.
 * @param p Pointer to the start of the 16-bit value.
 * @return The 16-bit value in host byte order.
 */
inline uint16_t readBE16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

/**
 * @brief Writes a 16-bit value in little-endian format to a buffer.
 * @param p Pointer to the destination buffer.
 * @param val The 16-bit value to write.
 */
inline void writeLE16(uint8_t *p, uint16_t val) {
  p[0] = val & 0xFF;
  p[1] = (val >> 8) & 0xFF;
}

/**
 * @brief Writes a 16-bit value in big-endian format to a buffer.
 * @param p Pointer to the destination buffer.
 * @param val The 16-bit value to write.
 */
inline void writeBE16(uint8_t *p, uint16_t val) {
  p[0] = (val >> 8) & 0xFF;
  p[1] = val & 0xFF;
}

/**
 * @struct DirEntry
 * @brief Represents a single directory entry in the FAT12 filesystem.
 */
struct DirEntry {
  uint8_t name[8];         /**< Filename (space padded). */
  uint8_t ext[3];          /**< Extension (space padded). */
  uint8_t attr;            /**< File attributes. */
  uint8_t reserved[10];    /**< Reserved area. */
  uint8_t time[2];         /**< Modification time. */
  uint8_t date[2];         /**< Modification date. */
  uint8_t startCluster[2]; /**< Starting cluster of the file. */
  uint8_t fileSize[4];     /**< File size in bytes. */

  /** @return True if this entry represents a directory. */
  bool isDirectory() const { return attr & 0x10; }

  /** @return The starting cluster number (little-endian conversion). */
  uint16_t getStartCluster() const { return readLE16(startCluster); }

  /** @return The file size in bytes (little-endian conversion). */
  uint32_t getFileSize() const {
    return fileSize[0] | (fileSize[1] << 8) | (fileSize[2] << 16) |
           (fileSize[3] << 24);
  }

  /** @return The reconstructed filename string "NAME.EXT". */
  std::string getFilename() const;
};

/**
 * @class AtariDiskEngine
 * @brief Engine for reading, writing, and manipulating Atari ST floppy disk
 * images.
 *
 * This class handles FAT12 filesystem structures, boot sector validation,
 * and high-level file operations within a disk image.
 */
class AtariDiskEngine {
public:
  /**
   * @brief Deletes a file from the disk image.
   * @param entry The directory entry of the file to delete.
   * @return True if successful.
   */
  bool deleteFile(const DirEntry &entry);

  /** @brief Modes for interpreting disk geometry. */
  enum class GeometryMode { Unknown, BPB, HatariGuess };

  /**
   * @brief Injects a local file into the disk image.
   * @param localPath Path to the file on the host system.
   * @return True if successful.
   */
  bool injectFile(const QString &localPath);

  /**
   * @brief Reads a file content from the disk image as a QByteArray.
   * @param entry The directory entry of the file to read.
   * @return File contents.
   */
  QByteArray readFileQt(const DirEntry &entry) const;

  /** @return Const reference to the raw disk image data. */
  const std::vector<uint8_t> &getRawImageData() const { return m_image; }

  AtariDiskEngine() = default;

  /** @brief Constructs an engine with existing image data. */
  AtariDiskEngine(std::vector<uint8_t> imageData);

  /** @brief Constructs an engine from a raw data buffer. */
  AtariDiskEngine(const uint8_t *data, std::size_t byteCount);

  /** @brief Loads disk image data into the engine. */
  void load(const std::vector<uint8_t> &data);

  /** @return True if an image is currently loaded. */
  bool isLoaded() const { return !m_image.empty(); }

  /** @return List of entries in the root directory. */
  std::vector<DirEntry> readRootDirectory() const;

  /** @return List of entries in a subdirectory starting at the given cluster.
   */
  std::vector<DirEntry> readSubDirectory(uint16_t startCluster) const;

  /** @return Raw bytes of a file specified by its directory entry. */
  std::vector<uint8_t> readFile(const DirEntry &entry) const;

  /** @brief Loads an image from a file path. */
  bool loadImage(const QString &path);

  /** @return Raw data of a specific sector. */
  QByteArray getSector(uint32_t sectorIndex) const;

  /** @return The geometry mode used for the current image. */
  GeometryMode lastGeometryMode() const { return m_geoMode; }

  /** @return A human-readable string describing the disk format. */
  QString getFormatInfoString() const;

  /** @brief Utility to convert std::string to QString. */
  static QString toQString(const std::string &s);

  /** @return True if the boot sector checksum (executable flag) is valid. */
  bool validateBootChecksum() const noexcept;

  /** @brief Validates checksum for an arbitrary 512-byte buffer. */
  static bool validateBootChecksum(const uint8_t *sector512) noexcept;

  /** @brief Initializes the image with a standard 720KB empty format. */
  void createNew720KImage();

private:
  /** @brief Checks if a block of data appears to be a valid directory entry. */
  bool isValidDirectoryEntry(const uint8_t *data) const;

  /** @brief Internal initialization after data load. */
  void init();

  /** @return Byte offset to the first FAT. */
  uint32_t fat1Offset() const noexcept;

  /** @return Byte offset to the start of a specific cluster. */
  uint32_t clusterOffset(uint16_t cluster) const noexcept;

  /** @return The next cluster in the chain from the FAT. */
  uint16_t getNextCluster(uint16_t currentCluster) const noexcept;

  // Writing helpers (member versions to match common usage in this class)
  void writeLE16(uint8_t *p, uint16_t val) const noexcept {
    Atari::writeLE16(p, val);
  }
  void writeBE16(uint8_t *p, uint16_t val) const noexcept {
    Atari::writeBE16(p, val);
  }

  /** @return List of all cluster indices in a file's chain. */
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
