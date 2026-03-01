// =============================================================================
//  AtariDiskEngine.cpp
//  Atari ST Toolkit — Hatari-Synchronized Implementation
// =============================================================================

#include "../include/AtariDiskEngine.h"
#include <QByteArray>
#include <QDebug>
#include <QFile>
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
  // Mode is Unknown until readRootDirectory determines geometry
  m_geoMode = GeometryMode::Unknown;
}

// =============================================================================
//  Core Logic & Geometry Helpers
// =============================================================================

/*static*/ bool
AtariDiskEngine::validateBootChecksum(const uint8_t *sector512) noexcept {
  uint16_t sum = 0;
  // HATARI CHECK: Sum of 16-bit big-endian words must be 0x1234
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
  // Reserved Sectors at offset 0x0B defines the FAT start
  return static_cast<uint32_t>(readLE16(base + 0x0B)) * SECTOR_SIZE;
}

uint32_t
Atari::AtariDiskEngine::clusterOffset(uint16_t cluster) const noexcept {
  if (m_image.empty())
    return 0;
  if (cluster < 2)
    cluster = 2; // Treat reserved clusters as the start of data

  // 1. Default fallback (Standard Atari ST 720KB layout)
  uint32_t dataStartSector = 18;

  // 2. Logic for Standard Disks (using the BPB from the Boot Sector)
  if (m_geoMode == GeometryMode::BPB) {
    const uint8_t *base = m_image.data() + m_internalOffset;
    uint16_t reserved = readLE16(base + 0x0B);
    uint8_t fatCount = base[0x10]; // Note: index 0x10 is standard for FAT count
    uint16_t fatSize =
        readLE16(base + 0x16); // Note: index 0x16 is standard for FAT size
    uint16_t maxEntries =
        readLE16(base + 0x11); // Note: index 0x11 is standard for Root size

    // Swap to Big-Endian if values look like garbage (typical for some Atari
    // formats)
    if (reserved > 500 || reserved == 0) {
      reserved = readBE16(base + 0x0B);
      fatSize = readBE16(base + 0x16);
      maxEntries = readBE16(base + 0x11);
    }

    uint32_t rootDirSectors = ((maxEntries * 32) + 511) / 512;
    dataStartSector = reserved + (fatCount * fatSize) + rootDirSectors;
  }
  // 3. Logic for the Vectronix Disk (Sector 7 discovery)
  else if (m_geoMode == GeometryMode::HatariGuess) {
    // If the directory was found at Sector 7, data usually starts 7 sectors
    // later
    dataStartSector = 14;
  }

  // 4. Manual Override (if you decide to bring the slider/input back later)
  if (m_useManualOverride) {
    dataStartSector = m_manualRootSector + 7;
  }

  // Standard Atari disks use 2 sectors per cluster.
  // Compact disks (HatariGuess) often use 1 sector per cluster.
  uint32_t sectorsPerCluster = (m_geoMode == GeometryMode::HatariGuess) ? 1 : 2;

  return m_internalOffset +
         (dataStartSector + (cluster - 2) * sectorsPerCluster) * SECTOR_SIZE;
}

// =============================================================================
//  Directory Parsing Logic
// =============================================================================

std::vector<Atari::DirEntry> Atari::AtariDiskEngine::readRootDirectory() const {
  std::vector<DirEntry> entries;
  if (!isLoaded())
    return entries;

  // 1. Force Alignment
  auto *self = const_cast<AtariDiskEngine *>(this);
  self->m_internalOffset = 0;
  const uint8_t *d = m_image.data();

  qDebug() << "[DIAG] Starting Sector-Aligned Brute Scan...";

  uint32_t foundOffset = 0;

  // 2. Scan sectors for the directory start (Sector 1 to 300)
  for (uint32_t sector = 1; sector < 300; ++sector) {
    uint32_t probeOffset = sector * SECTOR_SIZE;
    if (probeOffset + DIRENT_SIZE > m_image.size())
      break;

    const uint8_t *ptr = d + probeOffset;

    // Sniffer: Look for a valid entry (Alphanumeric name + Valid attribute)
    bool hasValidAttr = (ptr[11] == 0x00 || ptr[11] == 0x20 || ptr[11] == 0x10);
    bool hasValidName =
        (ptr[0] >= 'A' && ptr[0] <= 'Z') || (ptr[0] >= '0' && ptr[0] <= '9');

    if (hasValidName && hasValidAttr) {
      foundOffset = probeOffset;
      self->m_geoMode = GeometryMode::HatariGuess;
      qDebug() << "[DIAG] SUCCESS: Aligned Directory found at Sector" << sector
               << "Offset:" << QString::number(foundOffset, 16).toUpper();
      break;
    }
  }

  // Fallback to standard Sector 11 if scan fails
  if (foundOffset == 0) {
    qDebug() << "[DIAG] Aligned scan failed. Defaulting to Sector 11.";
    foundOffset = 11 * SECTOR_SIZE;
  }

  // 3. Parse Entries with "Garbage Detection"
  const uint8_t *dirPtr = d + foundOffset;

  for (int i = 0; i < 112; ++i) {
    uint32_t entryPos = i * 32;
    if (foundOffset + entryPos + 32 > m_image.size())
      break;

    const uint8_t *p = dirPtr + entryPos;

    // Hard Stop: First byte is 0x00 (End of list)
    if (p[0] == 0x00)
      break;

    // Skip deleted: First byte is 0xE5
    if (p[0] == 0xE5)
      continue;

    // GARBAGE FILTER: Check if the name contains non-printable characters
    // FastCopy disks don't always clear the rest of the sector, leaving game
    // code behind.
    bool isGarbage = false;
    for (int j = 0; j < 8; ++j) {
      // If we see characters outside the printable ASCII range or lowercase
      // (Atari uses CAPS)
      if (p[j] != ' ' && (p[j] < 33 || p[j] > 122)) {
        isGarbage = true;
        break;
      }
    }

    if (isGarbage) {
      qDebug() << "[DIAG] Garbage/Code detected at entry" << i
               << ". Stopping parse.";
      break;
    }

    // Final Attribute check to ensure it's not a false positive
    if (p[11] > 0x3F) { // Attributes higher than this are impossible in FAT12
      qDebug() << "[DIAG] Invalid attribute" << p[11]
               << "detected. Stopping parse.";
      break;
    }

    DirEntry entry;
    std::memcpy(&entry, p, 32);

    // Filter out volume labels (0x08)
    if (!(entry.attr & 0x08)) {
      entries.push_back(entry);
    }
  }

  qDebug() << "[DIAG] Parsed" << entries.size() << "clean entries.";
  return entries;
}

// =============================================================================
//  FAT12 Management & File IO
// =============================================================================

uint16_t
AtariDiskEngine::getNextCluster(uint16_t currentCluster) const noexcept {
  const uint8_t *fat = m_image.data() + fat1Offset();
  uint32_t byteOffset = (static_cast<uint32_t>(currentCluster) * 3) / 2;
  if (byteOffset + 1 >= m_image.size())
    return 0xFFF;
  uint16_t raw = readLE16(fat + byteOffset);
  return (currentCluster & 1) ? (raw >> 4) : (raw & 0x0FFF);
}

std::vector<uint16_t>
AtariDiskEngine::getClusterChain(uint16_t startCluster) const {
  std::vector<uint16_t> chain;
  uint16_t current = startCluster;
  const size_t maxClusters = m_image.size() / SECTOR_SIZE;
  while (current >= 2 && current < 0xFF8) {
    chain.push_back(current);
    current = getNextCluster(current);
    if (chain.size() > maxClusters)
      break;
  }
  return chain;
}

std::vector<Atari::DirEntry>
Atari::AtariDiskEngine::readSubDirectory(uint16_t startCluster) const {
  std::vector<DirEntry> entries;
  uint32_t offset = clusterOffset(startCluster);

  qDebug() << "[ENGINE] Reading Sub-Directory at Cluster" << startCluster
           << "Offset" << hex << offset;

  if (offset + DIRENT_SIZE > m_image.size())
    return entries;

  const uint8_t *ptr = m_image.data() + offset;

  // Subdirectories in Atari/DOS usually occupy one cluster (2 sectors = 1024
  // bytes)
  for (int i = 0; i < 32; ++i) {
    const uint8_t *p = ptr + (i * 32);

    if (p[0] == 0x00)
      break; // End of list
    if (p[0] == 0xE5)
      continue; // Deleted

    // GARBAGE FILTER: Same as Root
    bool isGarbage = false;
    for (int j = 0; j < 5; ++j) { // Check first 5 chars
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

  qDebug() << "[ENGINE] readFile called for cluster" << startCluster << "size"
           << fileSize;

  if (fileSize == 0)
    return {};
  if (fileSize > 20 * 1024 * 1024) { // 20MB Safety Cap for Atari Disks
    qDebug() << "[ENGINE] Error: File size is suspiciously large.";
    return {};
  }

  std::vector<uint8_t> data;
  auto chain = getClusterChain(startCluster);

  qDebug() << "[ENGINE] Cluster chain resolved. Length:" << chain.size();

  for (uint16_t cluster : chain) {
    uint32_t offset = clusterOffset(cluster);
    if (offset + SECTOR_SIZE > m_image.size()) {
      qDebug() << "[ENGINE] CRITICAL: Cluster" << cluster
               << "is out of bounds at offset" << offset;
      break;
    }

    // Safety: only read what we need
    uint32_t toRead =
        std::min((uint32_t)SECTOR_SIZE, fileSize - (uint32_t)data.size());
    const uint8_t *ptr = m_image.data() + offset;
    data.insert(data.end(), ptr, ptr + toRead);

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
    return "BPB (Trusting Disk)";
  case GeometryMode::HatariGuess:
    return "Hatari Guess (Standard ST)";
  default:
    return "Not Analyzed";
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

QByteArray AtariDiskEngine::readFileQt(const DirEntry &entry) const {
  std::vector<uint8_t> data = readFile(entry);
  return QByteArray(reinterpret_cast<const char *>(data.data()), data.size());
}

QString AtariDiskEngine::toQString(const std::string &s) {
  return QString::fromStdString(s);
}

bool Atari::AtariDiskEngine::isValidDirectoryEntry(const uint8_t *d) const {
  // Check 1: Filename must start with an alphanumeric char (A-Z, 0-9)
  if (!((d[0] >= 'A' && d[0] <= 'Z') || (d[0] >= '0' && d[0] <= '9')))
    return false;

  // Check 2: Attribute byte (offset 11) must have reasonable flags
  // (We ignore Volume Label 0x08, but standard files are 0x00, 0x01, 0x20,
  // etc.)
  if (d[11] & 0x08)
    return false;

  // Check 3: Extension (offsets 8-10) should be printable
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

  if (m_image.empty()) {
    qDebug() << "[ENGINE] Disk cleared.";
    return; // Exit early without calling init() or validation
  }

  // Only run initialization if we actually have data
  init();
}

QByteArray Atari::AtariDiskEngine::readFileQt(const DirEntry &entry) const {
  std::vector<uint8_t> buffer = readFile(entry);
  if (buffer.empty())
    return QByteArray();

  return QByteArray(reinterpret_cast<const char *>(buffer.data()),
                    buffer.size());
}

} // namespace Atari
