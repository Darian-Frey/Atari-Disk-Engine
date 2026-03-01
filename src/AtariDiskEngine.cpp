// =============================================================================
//  AtariDiskEngine.cpp
//  Atari ST Toolkit — Hatari-Synchronized Implementation
//
//  This file implements the logic for parsing and manipulating Atari ST floppy
//  disk images, which primarily use the FAT12 filesystem.
// =============================================================================

#include "../include/AtariDiskEngine.h"
#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>
#include <algorithm>
#include <cassert>
#include <cstring> // Required for std::memcpy
#include <stdexcept>

namespace Atari {

// =============================================================================
//  DirEntry Implementation
// =============================================================================

std::string DirEntry::getFilename() const {
  /**
   * Atari filenames are stored as 8.3 format, space-padded.
   * We need to strip spaces and combine them with a dot.
   */
  auto isLegalAtariChar = [](uint8_t c) {
    return (c > 32 && c < 127); // Standard printable ASCII
  };

  std::string base;
  for (int i = 0; i < 8; ++i) {
    if (isLegalAtariChar(name[i]))
      base += static_cast<char>(name[i]);
  }

  std::string extension;
  for (int i = 0; i < 3; ++i) {
    if (isLegalAtariChar(ext[i]))
      extension += static_cast<char>(ext[i]);
  }

  if (extension.empty())
    return base;
  return base + "." + extension;
}

// =============================================================================
//  AtariDiskEngine — Construction & Initialization
// =============================================================================

AtariDiskEngine::AtariDiskEngine(std::vector<uint8_t> imageData)
    : m_image(std::move(imageData)) {
  init();
}

AtariDiskEngine::AtariDiskEngine(const uint8_t *data, std::size_t byteCount)
    : m_image(data, data + byteCount) {
  init();
}

void AtariDiskEngine::init() {
  if (m_image.size() < SECTOR_SIZE) {
    throw std::runtime_error("AtariDiskEngine: File too small.");
  }
  // Mode remains Unknown until readRootDirectory performs its analysis.
  m_geoMode = GeometryMode::Unknown;
}

// =============================================================================
//  Core Logic & Geometry Helpers
// =============================================================================

/*static*/ bool
AtariDiskEngine::validateBootChecksum(const uint8_t *sector512) noexcept {
  /**
   * The Atari TOS boot sector is considered "executable" if the sum of all
   * 16-bit big-endian words in the sector is 0x1234.
   */
  uint16_t sum = 0;
  for (int i = 0; i < 512; i += 2) {
    sum += (static_cast<uint16_t>(sector512[i]) << 8) | sector512[i + 1];
  }
  return sum == BOOT_CHECKSUM_TARGET;
}

bool AtariDiskEngine::validateBootChecksum() const noexcept {
  return validateBootChecksum(m_image.data() + m_internalOffset);
}

uint32_t AtariDiskEngine::fat1Offset() const noexcept {
  const uint8_t *base = m_image.data() + m_internalOffset;
  // BPB offset 0x0E (Reserved Sectors) typically is 1 for Atari ST.
  return static_cast<uint32_t>(readLE16(base + 0x0B)) * SECTOR_SIZE;
}

uint32_t
Atari::AtariDiskEngine::clusterOffset(uint16_t cluster) const noexcept {
  if (m_image.empty() || cluster < 2)
    return 0;

  // Default values for standard 720KB (80 tracks, 9 sectors, 2 sides)
  uint32_t dataStartSector = 18;

  if (m_geoMode == GeometryMode::BPB) {
    const uint8_t *base = m_image.data() + m_internalOffset;
    uint16_t reserved = readLE16(base + 0x0E);
    uint8_t fatCount = base[0x10];
    uint16_t fatSize = readLE16(base + 0x16);
    uint16_t maxRoot = readLE16(base + 0x11);

    /**
     * Some Atari disks use Big-Endian values in the BPB (not standard but
     * exists). We perform a heuristic check to see if we need to swap.
     */
    if (reserved == 0 || reserved > 500) {
      reserved = readBE16(base + 0x0E);
      fatSize = readBE16(base + 0x16);
      maxRoot = readBE16(base + 0x11);
    }
    uint32_t rootSectors = ((maxRoot * 32) + 511) / 512;
    dataStartSector = reserved + (fatCount * fatSize) + rootSectors;
  } else if (m_geoMode == GeometryMode::HatariGuess) {
    // Compact/Vectronix disks often have different layouts
    dataStartSector = 14;
  }

  // SPC (Sectors Per Cluster):
  // Standard 720K uses 2. Certain custom formats use 1.
  uint32_t spc = (m_geoMode == GeometryMode::HatariGuess) ? 1 : 2;

  return m_internalOffset +
         (dataStartSector + (cluster - 2) * spc) * SECTOR_SIZE;
}

// =============================================================================
//  Directory Parsing Logic
// =============================================================================

std::vector<Atari::DirEntry> Atari::AtariDiskEngine::readRootDirectory() const {
  std::vector<DirEntry> entries;
  if (!isLoaded())
    return entries;

  auto *self = const_cast<AtariDiskEngine *>(this);
  const uint8_t *d = m_image.data() + m_internalOffset;

  uint32_t foundOffset = 0;
  bool standardBpbFound = false;

  // 1. Detection via BIOS Parameter Block (BPB) in Sector 0
  uint16_t reservedSectors = readLE16(d + 0x0E);
  if (reservedSectors == 0 || reservedSectors > 500) {
    reservedSectors = readBE16(d + 0x0E);
  }

  if (reservedSectors > 0 && reservedSectors < 10) {
    uint8_t fatCount = d[0x10];
    uint16_t fatSize = readLE16(d + 0x16);
    if (fatSize == 0 || fatSize > 500)
      fatSize = readBE16(d + 0x16);

    if (fatCount > 0 && fatCount <= 2 && fatSize > 0) {
      self->m_geoMode = GeometryMode::BPB;
      foundOffset = (reservedSectors + (fatCount * fatSize)) * SECTOR_SIZE;
      standardBpbFound = true;
      qDebug() << "[DIAG] Standard BPB Detected. Root at Sector:"
               << (foundOffset / SECTOR_SIZE);
    }
  }

  // 2. Fallback: Brute Force Scan
  // For custom formats (like Vectronix or Compact disks) that don't have a
  // valid BPB, we scan early sectors for things that look like filenames.
  if (!standardBpbFound) {
    qDebug() << "[DIAG] No standard BPB found. Starting Brute Scan...";
    for (uint32_t sector = 1; sector < 30; ++sector) {
      uint32_t probeOffset = sector * SECTOR_SIZE;
      if (probeOffset + 32 > m_image.size())
        break;

      const uint8_t *ptr = d + probeOffset;
      // Valid names start with alphanumeric char, and have reasonable
      // attributes.
      bool hasValidName =
          (ptr[0] >= 'A' && ptr[0] <= 'Z') || (ptr[0] >= '0' && ptr[0] <= '9');
      bool hasValidAttr = (ptr[11] <= 0x3F);

      if (hasValidName && hasValidAttr) {
        foundOffset = probeOffset;
        self->m_geoMode = GeometryMode::HatariGuess;
        qDebug() << "[DIAG] SUCCESS: Aligned Directory found via Brute Scan at "
                    "Sector"
                 << sector;
        break;
      }
    }
  }

  // Final fallback to Sector 11 (standard 720K root start)
  if (foundOffset == 0) {
    qDebug() << "[DIAG] All discovery failed. Defaulting to Sector 11.";
    foundOffset = 11 * SECTOR_SIZE;
    self->m_geoMode = GeometryMode::BPB;
  }

  // 3. Extraction of entries
  const uint8_t *dirPtr = d + foundOffset;
  for (int i = 0; i < 112; ++i) { // Standard root max is 112 entries
    uint32_t entryPos = i * 32;
    if (foundOffset + entryPos + 32 > m_image.size())
      break;

    const uint8_t *p = dirPtr + entryPos;
    if (p[0] == 0x00)
      break; // End of directory list
    if (p[0] == 0xE5)
      continue; // Skip deleted file marker

    // Heuristic: Check for binary garbage to avoid false positives
    bool isGarbage = false;
    for (int j = 0; j < 8; ++j) {
      if (p[j] != ' ' && (p[j] < 32 || p[j] > 126)) {
        isGarbage = true;
        break;
      }
    }
    if (isGarbage)
      break;

    DirEntry entry;
    std::memcpy(&entry, p, 32);
    if (!(entry.attr & 0x08)) { // Skip volume labels
      entries.push_back(entry);
    }
  }

  return entries;
}

// =============================================================================
//  FAT12 Management & File IO
// =============================================================================

uint16_t
AtariDiskEngine::getNextCluster(uint16_t currentCluster) const noexcept {
  /**
   * FAT12 uses 1.5 bytes per entry.
   * Even clusters: bits 0-11 of two bytes.
   * Odd clusters: bits 4-15 of two bytes.
   */
  const uint8_t *fat = m_image.data() + fat1Offset();
  uint32_t byteOffset = (static_cast<uint32_t>(currentCluster) * 3) / 2;
  if (byteOffset + 1 >= m_image.size())
    return 0xFFF;
  uint16_t raw = readLE16(fat + byteOffset);
  return (currentCluster & 1) ? (raw >> 4) : (raw & 0x0FFF);
}

std::vector<uint16_t>
Atari::AtariDiskEngine::getClusterChain(uint16_t startCluster) const {
  std::vector<uint16_t> chain;

  if (m_image.empty() || startCluster < 2 || startCluster >= 0xFF0) {
    return chain;
  }

  uint16_t current = startCluster;
  const uint8_t *img = m_image.data() + m_internalOffset;
  uint32_t fatOffset = 1 * SECTOR_SIZE;

  while (current >= 2 && current < 0xFF0) {
    chain.push_back(current);

    // Inlined FAT12 logic for performance and robustness in chains
    uint32_t idx = (current * 3) / 2;
    if (fatOffset + idx + 1 >= m_image.size())
      break;

    uint16_t next;
    if (current % 2 == 0) {
      next = img[fatOffset + idx] | ((img[fatOffset + idx + 1] & 0x0F) << 8);
    } else {
      next = (img[fatOffset + idx] >> 4) | (img[fatOffset + idx + 1] << 4);
    }

    if (next >= 0xFF8 || next == 0x000)
      break;
    if (next == current || chain.size() > 1440)
      break; // Protect against cyclic chains or corrupt FATs

    current = next;
  }

  return chain;
}

std::vector<Atari::DirEntry>
Atari::AtariDiskEngine::readSubDirectory(uint16_t startCluster) const {
  std::vector<DirEntry> entries;
  uint32_t offset = clusterOffset(startCluster);

  if (offset + DIRENT_SIZE > m_image.size())
    return entries;

  const uint8_t *ptr = m_image.data() + offset;

  // Most subdirectories on floppy occupy at least one cluster.
  for (int i = 0; i < 32; ++i) {
    const uint8_t *p = ptr + (i * 32);

    if (p[0] == 0x00)
      break;
    if (p[0] == 0xE5)
      continue;

    bool isGarbage = false;
    for (int j = 0; j < 5; ++j) {
      if (p[j] < 32 || p[j] > 126) {
        isGarbage = true;
        break;
      }
    }

    if (isGarbage || p[11] > 0x3F)
      break;

    DirEntry entry;
    std::memcpy(&entry, p, 32);
    entries.push_back(entry);
  }
  return entries;
}

std::vector<uint8_t>
Atari::AtariDiskEngine::readFile(const DirEntry &entry) const {
  uint32_t fileSize = entry.getFileSize();
  uint16_t startCluster = entry.getStartCluster();

  if (fileSize == 0 || m_image.empty())
    return {};

  // Safety cap to avoid memory exhaustion on malformed images.
  if (fileSize > 4 * 1024 * 1024) {
    return {};
  }

  std::vector<uint8_t> data;
  data.reserve(fileSize);

  auto chain = getClusterChain(startCluster);
  uint32_t spc = (m_geoMode == GeometryMode::HatariGuess) ? 1 : 2;

  for (size_t cIdx = 0; cIdx < chain.size(); ++cIdx) {
    uint16_t cluster = chain[cIdx];
    uint32_t clusterBase = clusterOffset(cluster);

    for (uint32_t s = 0; s < spc; ++s) {
      uint32_t sectorOffset = clusterBase + (s * SECTOR_SIZE);
      uint32_t currentRead = data.size();
      uint32_t remaining = fileSize - currentRead;
      uint32_t toRead = std::min((uint32_t)SECTOR_SIZE, remaining);

      if (toRead > 0) {
        if (sectorOffset + toRead <= m_image.size()) {
          const uint8_t *ptr = m_image.data() + sectorOffset;
          data.insert(data.end(), ptr, ptr + toRead);
        } else {
          break; // OOB
        }
      }

      if (data.size() >= fileSize)
        break;
    }
    if (data.size() >= fileSize)
      break;
  }

  return data;
}

QString AtariDiskEngine::getFormatInfoString() const {
  if (m_useManualOverride)
    return QString("Manual Override: Sector %1").arg(m_manualRootSector);
  switch (m_geoMode) {
  case GeometryMode::BPB:
    return "BPB (Standard)";
  case GeometryMode::HatariGuess:
    return "Custom Layout (Vectronix/Compact)";
  default:
    return "Unknown/Uninitialized";
  }
}

// =============================================================================
//  Qt Bridge Implementation
// =============================================================================

bool AtariDiskEngine::loadImage(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  QByteArray data = file.readAll();
  load(std::vector<uint8_t>(data.begin(), data.end()));
  return true;
}

QByteArray Atari::AtariDiskEngine::getSector(uint32_t sectorIndex) const {
  if (m_image.empty())
    return QByteArray();

  uint32_t offset = m_internalOffset + (sectorIndex * SECTOR_SIZE);
  if (offset + SECTOR_SIZE > m_image.size())
    return QByteArray();

  return QByteArray(reinterpret_cast<const char *>(m_image.data() + offset),
                    SECTOR_SIZE);
}

QString AtariDiskEngine::toQString(const std::string &s) {
  return QString::fromStdString(s);
}

bool Atari::AtariDiskEngine::isValidDirectoryEntry(const uint8_t *d) const {
  if (!((d[0] >= 'A' && d[0] <= 'Z') || (d[0] >= '0' && d[0] <= '9')))
    return false;

  if (d[11] & 0x08) // Volume label
    return false;

  for (int i = 8; i < 11; ++i) {
    if (d[i] != ' ' && (d[i] < 'A' || d[i] > 'Z') && (d[i] < '0' || d[i] > '9'))
      return false;
  }
  return true;
}

void Atari::AtariDiskEngine::load(const std::vector<uint8_t> &data) {
  m_image = data;
  m_internalOffset = 0;
  m_useManualOverride = false;
  m_geoMode = GeometryMode::Unknown;

  if (m_image.empty())
    return;
  init();
}

QByteArray Atari::AtariDiskEngine::readFileQt(const DirEntry &entry) const {
  std::vector<uint8_t> buffer = readFile(entry);
  if (buffer.empty())
    return QByteArray();

  return QByteArray(reinterpret_cast<const char *>(buffer.data()),
                    buffer.size());
}

void Atari::AtariDiskEngine::createNew720KImage() {
  /**
   * Generates a template 737,280 byte image with standard 720KB
   * floppy geometry (80 tracks, 9 sectors, 2 sides).
   */
  const uint32_t DISK_720K_SIZE = 737280;
  m_image.assign(DISK_720K_SIZE, 0);

  uint8_t *b = m_image.data();

  // BIOS Parameter Block for 720KB
  b[0x00] = 0xEB;
  b[0x01] = 0x34;
  b[0x02] = 0x90;                    // Standard JMP
  std::memcpy(b + 3, "ANTIGRAV", 8); // OEM Name

  b[0x0B] = 0x00;
  b[0x0C] = 0x02; // Bytes per sector (512)
  b[0x0D] = 0x02; // Sectors per cluster (1KB)
  b[0x0E] = 0x01;
  b[0x0F] = 0x00; // Reserved sectors (1)
  b[0x10] = 0x02; // Number of FATs
  b[0x11] = 0x70;
  b[0x12] = 0x00; // Max root entries (112)
  b[0x13] = 0xA0;
  b[0x14] = 0x05; // Total sectors (1440)
  b[0x15] = 0xF9; // Media descriptor (3.5" DD)
  b[0x16] = 0x05;
  b[0x17] = 0x00; // Sectors per FAT (5)

  // Calculate and set boot checksum to make disk executable
  uint16_t sum = 0;
  for (int i = 0; i < 510; i += 2) {
    sum += (b[i] << 8) | b[i + 1];
  }
  uint16_t checksum = 0x1234 - sum;
  b[510] = (checksum >> 8) & 0xFF;
  b[511] = checksum & 0xFF;

  m_geoMode = GeometryMode::BPB;
  m_internalOffset = 0;
  qDebug() << "[ENGINE] New 720KB Disk Template Created.";
}

void Atari::AtariDiskEngine::writeLE16(uint8_t *ptr, uint16_t val) {
  ptr[0] = val & 0xFF;
  ptr[1] = (val >> 8) & 0xFF;
}

void Atari::AtariDiskEngine::writeLE32(uint8_t *ptr, uint32_t val) {
  ptr[0] = val & 0xFF;
  ptr[1] = (val >> 8) & 0xFF;
  ptr[2] = (val >> 16) & 0xFF;
  ptr[3] = (val >> 24) & 0xFF;
}

bool Atari::AtariDiskEngine::injectFile(const QString &localPath) {
  /**
   * Injection Logic:
   * 1. Locate free entry in root directory.
   * 2. Calculate clusters needed for file content.
   * 3. Update FAT12 chain starting at Cluster 2.
   * 4. Fill directory entry (Name, Ext, Attr, StartCluster, Size).
   * 5. Write raw bytes to the cluster data area.
   *
   * Note: Currently only supports injecting into root, beginning at cluster 2.
   */
  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  QByteArray fileData = file.readAll();

  if (fileData.size() > 700 * 1024)
    return false;

  QFileInfo info(localPath);
  QString baseName = info.baseName().toUpper().left(8).leftJustified(8, ' ');
  QString ext = info.suffix().toUpper().left(3).leftJustified(3, ' ');

  uint32_t rootOffset = 11 * SECTOR_SIZE;
  int entryIndex = -1;
  for (int i = 0; i < 112; ++i) {
    if (m_image[rootOffset + (i * 32)] == 0x00) {
      entryIndex = i;
      break;
    }
  }
  if (entryIndex == -1)
    return false;

  uint16_t startCluster = 2;
  uint32_t clustersNeeded = (fileData.size() + 1023) / 1024;

  uint32_t fatOffset = 1 * SECTOR_SIZE;
  for (uint32_t i = 0; i < clustersNeeded; ++i) {
    uint16_t current = startCluster + i;
    uint16_t next = (i == clustersNeeded - 1) ? 0xFFF : (current + 1);

    uint32_t idx = (current * 3) / 2;
    if (current % 2 == 0) {
      m_image[fatOffset + idx] = next & 0xFF;
      m_image[fatOffset + idx + 1] =
          (m_image[fatOffset + idx + 1] & 0xF0) | ((next >> 8) & 0x0F);
    } else {
      m_image[fatOffset + idx] =
          (m_image[fatOffset + idx] & 0x0F) | ((next << 4) & 0xF0);
      m_image[fatOffset + idx + 1] = (next >> 4) & 0xFF;
    }
  }

  // Update secondary FAT mirror
  std::memcpy(&m_image[6 * SECTOR_SIZE], &m_image[1 * SECTOR_SIZE],
              5 * SECTOR_SIZE);

  uint8_t *entryPtr = &m_image[rootOffset + (entryIndex * 32)];
  std::memcpy(entryPtr, baseName.toStdString().c_str(), 8);
  std::memcpy(entryPtr + 8, ext.toStdString().c_str(), 3);
  entryPtr[11] = 0x20;
  writeLE16(entryPtr + 26, startCluster);
  writeLE32(entryPtr + 28, fileData.size());

  uint32_t physOffset = (18 * SECTOR_SIZE);
  if (physOffset + fileData.size() <= m_image.size()) {
    std::memcpy(&m_image[physOffset], fileData.data(), fileData.size());
  }

  return true;
}

bool Atari::AtariDiskEngine::deleteFile(const DirEntry &entry) {
  if (!isLoaded())
    return false;

  // Check if it's a directory
  if (entry.isDirectory()) {
    qDebug() << "[ENGINE] Cannot delete directory with this function.";
    return false;
  }

  uint16_t startCluster = entry.getStartCluster();
  uint32_t rootOffset = 11 * SECTOR_SIZE; // Default for our 720K

  // 1. Locate and Mark Directory Entry as Deleted (0xE5)
  bool entryFound = false;
  for (int i = 0; i < 112; ++i) {
    uint32_t offset = rootOffset + (i * 32);
    // Compare the 11-byte name/ext to find the exact match
    if (std::memcmp(&m_image[offset], entry.name, 8) == 0 &&
        std::memcmp(&m_image[offset + 8], entry.ext, 3) == 0) {
      m_image[offset] = 0xE5; // Standard FAT "Deleted" marker
      entryFound = true;
      break;
    }
  }

  if (!entryFound)
    return false;

  // 2. Clear the FAT Chain
  uint32_t fatOffset = 1 * SECTOR_SIZE;
  uint16_t current = startCluster;

  while (current >= 2 && current < 0xFF0) {
    // Look up the next cluster before we wipe the current one
    uint32_t idx = (current * 3) / 2;
    uint16_t next;
    if (current % 2 == 0) {
      next = m_image[fatOffset + idx] |
             ((m_image[fatOffset + idx + 1] & 0x0F) << 8);
    } else {
      next =
          (m_image[fatOffset + idx] >> 4) | (m_image[fatOffset + idx + 1] << 4);
    }

    // Wipe current entry in FAT1 (set to 0x000)
    if (current % 2 == 0) {
      m_image[fatOffset + idx] = 0x00;
      m_image[fatOffset + idx + 1] &= 0xF0;
    } else {
      m_image[fatOffset + idx] &= 0x0F;
      m_image[fatOffset + idx + 1] = 0x00;
    }

    if (next >= 0xFF8 || next == 0x000)
      break;
    current = next;
  }

  // 3. Sync FAT2
  std::memcpy(&m_image[6 * SECTOR_SIZE], &m_image[1 * SECTOR_SIZE],
              5 * SECTOR_SIZE);

  qDebug() << "[ENGINE] Deleted file starting at cluster" << startCluster;
  return true;
}

Atari::DiskStats Atari::AtariDiskEngine::getDiskStats() const {
  DiskStats stats;
  if (!isLoaded())
    return stats;

  stats.totalBytes = m_image.size();
  stats.sectorsPerCluster = (m_geoMode == GeometryMode::HatariGuess) ? 1 : 2;

  // 1. Count Files and Directories in Root
  stats.fileCount = 0;
  stats.dirCount = 0;
  auto entries = readRootDirectory();
  for (const auto &e : entries) {
    if (e.attr & 0x10)
      stats.dirCount++;
    else
      stats.fileCount++;
  }

  // 2. Scan FAT for Free Space
  // Total clusters for 720K is usually 711
  stats.totalClusters = (m_image.size() - (18 * SECTOR_SIZE)) /
                        (stats.sectorsPerCluster * SECTOR_SIZE);
  stats.freeClusters = 0;

  uint32_t fatOffset = 1 * SECTOR_SIZE;
  for (int i = 2; i < stats.totalClusters + 2; ++i) {
    uint32_t idx = (i * 3) / 2;
    uint16_t val;
    if (i % 2 == 0) {
      val = m_image[fatOffset + idx] |
            ((m_image[fatOffset + idx + 1] & 0x0F) << 8);
    } else {
      val =
          (m_image[fatOffset + idx] >> 4) | (m_image[fatOffset + idx + 1] << 4);
    }
    if (val == 0x000)
      stats.freeClusters++;
  }

  stats.freeBytes =
      stats.freeClusters * (stats.sectorsPerCluster * SECTOR_SIZE);
  stats.usedBytes = stats.totalBytes - stats.freeBytes;

  return stats;
}

} // namespace Atari
