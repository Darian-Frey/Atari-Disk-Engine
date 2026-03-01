// =============================================================================
//  AtariDiskEngine.cpp
//  Atari ST Toolkit — Hatari-Synchronized Implementation
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
  if (m_image.empty() || cluster < 2)
    return 0;

  // Standard 720KB Offset: Boot(1) + FATs(10) + Root(7) = 18
  uint32_t dataStartSector = 18;

  if (m_geoMode == GeometryMode::BPB) {
    const uint8_t *base = m_image.data() + m_internalOffset;
    uint16_t reserved = readLE16(base + 0x0E);
    uint8_t fatCount = base[0x10];
    uint16_t fatSize = readLE16(base + 0x16);
    uint16_t maxRoot = readLE16(base + 0x11);

    // Atari Endian Swap Check
    if (reserved == 0 || reserved > 500) {
      reserved = readBE16(base + 0x0E);
      fatSize = readBE16(base + 0x16);
      maxRoot = readBE16(base + 0x11);
    }
    uint32_t rootSectors = ((maxRoot * 32) + 511) / 512;
    dataStartSector = reserved + (fatCount * fatSize) + rootSectors;
  } else if (m_geoMode == GeometryMode::HatariGuess) {
    dataStartSector = 14;
  }

  // SPC (Sectors Per Cluster): 720K uses 2. Compact/Vectronix uses 1.
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

  // 1. Check for Standard BPB (Sector 0)
  // A standard Atari ST 720K disk has 1 reserved sector, 2 FATs, and 7 root
  // sectors. We check if reserved sectors (at 0x0E) is 1 or 2 (Atari standard).
  uint16_t reservedSectors = readLE16(d + 0x0E);
  if (reservedSectors == 0 || reservedSectors > 500) {
    reservedSectors = readBE16(d + 0x0E); // Check Big-Endian
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

  // 2. Fallback: Brute Scan (for Vectronix/Compact disks)
  if (!standardBpbFound) {
    qDebug() << "[DIAG] No standard BPB found. Starting Brute Scan...";
    for (uint32_t sector = 1; sector < 300; ++sector) {
      uint32_t probeOffset = sector * SECTOR_SIZE;
      if (probeOffset + 32 > m_image.size())
        break;

      const uint8_t *ptr = d + probeOffset;
      // Check for valid filename start and attribute byte
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

  // Default fallback if everything fails
  if (foundOffset == 0) {
    qDebug() << "[DIAG] All discovery failed. Defaulting to Sector 11.";
    foundOffset = 11 * SECTOR_SIZE;
    self->m_geoMode = GeometryMode::BPB;
  }

  // 3. Parse the entries
  const uint8_t *dirPtr = d + foundOffset;
  for (int i = 0; i < 112; ++i) {
    uint32_t entryPos = i * 32;
    if (foundOffset + entryPos + 32 > m_image.size())
      break;

    const uint8_t *p = dirPtr + entryPos;
    if (p[0] == 0x00)
      break; // End of list
    if (p[0] == 0xE5)
      continue; // Deleted

    // Garbage Filter
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

  qDebug() << "[DIAG] Parsed" << entries.size()
           << "clean entries. Mode:" << (int)m_geoMode;
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

    // FAT12 Entry Calculation
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
      break; // End or unallocated
    if (next == current || chain.size() > 1440)
      break; // Loop safety

    current = next;
  }

  return chain;
}

std::vector<Atari::DirEntry>
Atari::AtariDiskEngine::readSubDirectory(uint16_t startCluster) const {
  std::vector<DirEntry> entries;
  uint32_t offset = clusterOffset(startCluster);

  qDebug() << "[ENGINE] Reading Sub-Directory at Cluster" << startCluster
           << "Offset" << Qt::hex << offset;

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

  qDebug() << "[TRACE-START] File:" << toQString(entry.getFilename())
           << "Size:" << fileSize;

  if (fileSize == 0 || m_image.empty())
    return {};

  // Standard Atari ST limit (2MB) - adjust if you handle larger HD images later
  if (fileSize > 4 * 1024 * 1024) {
    qDebug() << "[TRACE-ERROR] File size suspiciously large:" << fileSize;
    return {};
  }

  std::vector<uint8_t> data;
  data.reserve(fileSize);

  auto chain = getClusterChain(startCluster);

  // GEOMETRY LOGIC:
  // Standard BPB = 2 sectors per cluster (1024 bytes).
  // HatariGuess (Compact) = 1 sector per cluster (512 bytes).
  uint32_t spc = 2;
  if (m_geoMode == GeometryMode::HatariGuess) {
    spc = 1;
    qDebug() << "[TRACE-GEO] Mode is HatariGuess -> Using 1 sector per cluster";
  } else {
    spc = 2;
    qDebug()
        << "[TRACE-GEO] Mode is BPB/Standard -> Using 2 sectors per cluster";
  }

  qDebug() << "[TRACE-CHAIN] Cluster Chain length:" << chain.size()
           << "clusters.";

  for (size_t cIdx = 0; cIdx < chain.size(); ++cIdx) {
    uint16_t cluster = chain[cIdx];
    uint32_t clusterBase = clusterOffset(cluster);

    qDebug() << "[TRACE-CLUSTER] Chain Pos:" << cIdx << "Cluster ID:" << cluster
             << "Offset:" << hex << clusterBase;

    for (uint32_t s = 0; s < spc; ++s) {
      uint32_t sectorOffset = clusterBase + (s * SECTOR_SIZE);
      uint32_t currentSize = data.size();
      uint32_t remaining = fileSize - currentSize;
      uint32_t toRead = std::min((uint32_t)SECTOR_SIZE, remaining);

      if (toRead > 0) {
        if (sectorOffset + toRead <= m_image.size()) {
          const uint8_t *ptr = m_image.data() + sectorOffset;
          data.insert(data.end(), ptr, ptr + toRead);

          qDebug() << "  [TRACE-SECTOR] Cluster:" << dec << cluster
                   << "Sec:" << s << "Phys:" << hex << sectorOffset
                   << "Read:" << dec << toRead << "Total:" << data.size();
        } else {
          qDebug() << "  [TRACE-ERROR] Phys offset" << hex << sectorOffset
                   << "out of bounds!";
          break;
        }
      }

      if (data.size() >= fileSize)
        break;
    }
    if (data.size() >= fileSize)
      break;
  }

  qDebug() << "[TRACE-END] Final read size:" << data.size() << "/" << fileSize;
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

void Atari::AtariDiskEngine::createNew720KImage() {
  const uint32_t DISK_720K_SIZE = 737280;
  m_image.assign(DISK_720K_SIZE, 0); // Fill with zeros

  // Setup BPB for 720KB (9 sectors, 2 sides, 80 tracks)
  uint8_t *b = m_image.data();

  b[0x00] = 0xEB;
  b[0x01] = 0x34;
  b[0x02] = 0x90;                    // Jump
  std::memcpy(b + 3, "GEMINI  ", 8); // OEM Name

  b[0x0B] = 0x00;
  b[0x0C] = 0x02; // 512 Bytes per sector
  b[0x0D] = 0x02; // 2 Sectors per cluster
  b[0x0E] = 0x01;
  b[0x0F] = 0x00; // 1 Reserved sector (Boot)
  b[0x10] = 0x02; // 2 FATs
  b[0x11] = 0x70;
  b[0x12] = 0x00; // 112 Max Root Entries
  b[0x13] = 0xA0;
  b[0x14] = 0x05; // 1440 Total sectors (0x05A0)
  b[0x15] = 0xF9; // Media descriptor (3.5" DD)
  b[0x16] = 0x05;
  b[0x17] = 0x00; // 5 Sectors per FAT

  // Checksum the bootsector (Atari TOS requirement for 'executable' boot)
  uint16_t sum = 0;
  for (int i = 0; i < 510; i += 2) {
    sum += (b[i] << 8) | b[i + 1];
  }
  uint16_t checksum = 0x1234 - sum;
  b[510] = (checksum >> 8) & 0xFF;
  b[511] = checksum & 0xFF;

  m_geoMode = GeometryMode::BPB;
  m_internalOffset = 0;
  qDebug() << "[ENGINE] New 720KB Disk Created in RAM.";
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
  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  QByteArray fileData = file.readAll();

  // Safety check: Don't inject more than the disk can hold
  if (fileData.size() > 700 * 1024)
    return false;

  QFileInfo info(localPath);
  QString baseName = info.baseName().toUpper().left(8).leftJustified(8, ' ');
  QString ext = info.suffix().toUpper().left(3).leftJustified(3, ' ');

  // 1. Root Directory is at Sector 11 (Standard for our New 720K)
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

  // 2. Data starts at Cluster 2 (Sector 18)
  uint16_t startCluster = 2;
  uint32_t clustersNeeded =
      (fileData.size() + 1023) / 1024; // 2 sectors per cluster

  // 3. FAT12 WRITER - Corrected bit-masking
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
  // Mirror to FAT2 (Sector 6)
  std::memcpy(&m_image[6 * SECTOR_SIZE], &m_image[1 * SECTOR_SIZE],
              5 * SECTOR_SIZE);

  // 4. Directory Entry
  uint8_t *entryPtr = &m_image[rootOffset + (entryIndex * 32)];
  std::memcpy(entryPtr, baseName.toStdString().c_str(), 8);
  std::memcpy(entryPtr + 8, ext.toStdString().c_str(), 3);
  entryPtr[11] = 0x20; // Archive
  writeLE16(entryPtr + 26, startCluster);
  writeLE32(entryPtr + 28, fileData.size());

  // 5. Data Copy
  uint32_t physOffset = (18 * SECTOR_SIZE); // Force Sector 18 for Cluster 2
  if (physOffset + fileData.size() <= m_image.size()) {
    std::memcpy(&m_image[physOffset], fileData.data(), fileData.size());
  }

  qDebug() << "[ENGINE] Injected" << baseName << "at Cluster 2 (Sector 18)";
  return true;
}

} // namespace Atari
