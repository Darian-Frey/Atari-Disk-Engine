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

uint32_t AtariDiskEngine::clusterOffset(uint16_t cluster) const noexcept {
  if (cluster < 2)
    return 0;

  // Default ST Data Start (Sector 18)
  uint32_t dataStartSector = 18;

  if (m_geoMode == GeometryMode::BPB) {
    const uint8_t *base = m_image.data() + m_internalOffset;
    uint16_t reserved = readLE16(base + 0x0B);
    uint8_t fatCount = base[0x0D];
    uint16_t fatSize = readLE16(base + 0x13);
    uint16_t maxEntries = readLE16(base + 0x0E);

    // Swapping to Big-Endian if LE values are physically impossible
    if (reserved > 100 || reserved == 0) {
      reserved = readBE16(base + 0x0B);
      fatSize = readBE16(base + 0x13);
      maxEntries = readBE16(base + 0x0E);
    }

    uint32_t rootDirSectors = ((maxEntries * 32) + 511) / 512;
    dataStartSector = reserved + (fatCount * fatSize) + rootDirSectors;
  }

  // Apply User Manual Override if active
  if (m_useManualOverride) {
    dataStartSector =
        m_manualRootSector + 7; // Estimate Data start based on Root jump
  }

  return m_internalOffset + (dataStartSector + (cluster - 2) * 2) * SECTOR_SIZE;
}

// =============================================================================
//  Directory Parsing Logic
// =============================================================================

std::vector<DirEntry> AtariDiskEngine::readRootDirectory() const {
  std::vector<DirEntry> entries;
  if (!isLoaded())
    return entries;

  const uint8_t *d = m_image.data();
  uint32_t align = 0;

  // 1. Alignment Probe: Search first 16 bytes for 'DJM' or 'IBM'
  for (uint32_t i = 0; i < 16; ++i) {
    if (d[i + 2] == 'D' || d[i + 2] == 'I') {
      align = i;
      break;
    }
  }

  auto *self = const_cast<AtariDiskEngine *>(this);
  self->m_internalOffset = align;
  const uint8_t *base = d + align;

  uint32_t offset;
  uint16_t maxEntries;

  if (m_useManualOverride) {
    // MANUAL OVERRIDE PATH
    offset = align + (m_manualRootSector * SECTOR_SIZE);
    maxEntries = 112;
    qDebug() << "[INFO] Manual Override Active. Sector:" << m_manualRootSector;
  } else {
    // AUTOMATED DETECTION PATH
    uint16_t reserved = readLE16(base + 0x0B);
    uint8_t fatCount = base[0x0D];
    uint16_t fatSize = readLE16(base + 0x13);
    maxEntries = readLE16(base + 0x0E);
    uint8_t spc = base[0x0D];

    if (reserved > 100 || reserved == 0) {
      reserved = readBE16(base + 0x0B);
      fatSize = readBE16(base + 0x13);
      maxEntries = readBE16(base + 0x0E);
    }

    offset = align + (static_cast<uint32_t>(reserved + (fatCount * fatSize)) *
                      SECTOR_SIZE);
    bool isExecutable = validateBootChecksum(base);

    // Trigger Hatari-style Fallback if BPB is invalid (e.g. SPC=0)
    if (offset >= m_image.size() || (offset / SECTOR_SIZE) > 500 ||
        (spc == 0 && !isExecutable)) {
      self->m_geoMode = GeometryMode::HatariGuess;
      offset = align + (11 * SECTOR_SIZE); // Default Atari Sector 11
      maxEntries = 112;
    } else {
      self->m_geoMode = GeometryMode::BPB;
    }
  }

  if (offset + (maxEntries * DIRENT_SIZE) > m_image.size())
    return entries;

  const uint8_t *ptr = m_image.data() + offset;
  for (uint16_t i = 0; i < maxEntries; ++i) {
    DirEntry entry;
    std::memcpy(&entry, ptr + (i * DIRENT_SIZE), DIRENT_SIZE);
    if (entry.name[0] == 0x00)
      break;
    if (static_cast<uint8_t>(entry.name[0]) == 0xE5)
      continue;
    if (!(entry.attr & 0x08))
      entries.push_back(entry);
  }
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

std::vector<DirEntry>
AtariDiskEngine::readSubDirectory(uint16_t startCluster) const {
  std::vector<DirEntry> entries;
  if (!isLoaded() || startCluster < 2)
    return entries;
  uint32_t offset = clusterOffset(startCluster);
  if (offset + DIRENT_SIZE > m_image.size())
    return entries;

  const uint8_t *ptr = m_image.data() + offset;
  uint32_t entriesInCluster = (SECTOR_SIZE * 2) / DIRENT_SIZE;
  for (uint32_t i = 0; i < entriesInCluster; ++i) {
    if (offset + (i * DIRENT_SIZE) + DIRENT_SIZE > m_image.size())
      break;
    DirEntry entry;
    std::memcpy(&entry, ptr + (i * DIRENT_SIZE), DIRENT_SIZE);
    if (entry.name[0] == 0x00)
      break;
    if (static_cast<uint8_t>(entry.name[0]) == 0xE5)
      continue;
    if (!(entry.attr & 0x08))
      entries.push_back(entry);
  }
  return entries;
}

std::vector<uint8_t> AtariDiskEngine::readFile(const DirEntry &entry) const {
  if (entry.isDirectory())
    return {};
  std::vector<uint8_t> data;
  uint32_t fileSize = entry.getFileSize();
  data.reserve(fileSize);
  auto chain = getClusterChain(entry.getStartCluster());
  uint32_t bytesPerCluster = 2 * SECTOR_SIZE;
  for (uint16_t cluster : chain) {
    uint32_t remaining = fileSize - static_cast<uint32_t>(data.size());
    uint32_t toCopy = std::min(remaining, bytesPerCluster);
    const uint8_t *ptr = m_image.data() + clusterOffset(cluster);
    data.insert(data.end(), ptr, ptr + toCopy);
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

QByteArray AtariDiskEngine::getSector(uint32_t sectorIndex) const {
  if (m_image.empty() || (sectorIndex + 1) * SECTOR_SIZE > m_image.size())
    return QByteArray();
  return QByteArray(reinterpret_cast<const char *>(m_image.data() +
                                                   (sectorIndex * SECTOR_SIZE)),
                    SECTOR_SIZE);
}

QByteArray AtariDiskEngine::readFileQt(const DirEntry &entry) const {
  std::vector<uint8_t> data = readFile(entry);
  return QByteArray(reinterpret_cast<const char *>(data.data()), data.size());
}

QString AtariDiskEngine::toQString(const std::string &s) {
  return QString::fromStdString(s);
}

} // namespace Atari
