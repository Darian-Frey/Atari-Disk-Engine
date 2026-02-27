// =============================================================================
//  AtariDiskEngine.cpp
//  Atari ST Toolkit — Core Disk Parsing Engine (Implementation)
// =============================================================================

#include "AtariDiskEngine.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace Atari {

// =============================================================================
//  DirEntry Implementation
// =============================================================================

std::string DirEntry::getFilename() const {
    auto trimRight = [](const uint8_t* src, int len) -> std::string {
        int end = len;
        while (end > 0 && (src[end - 1] == ' ' || src[end - 1] == 0)) --end;
        return std::string(reinterpret_cast<const char*>(src), static_cast<std::size_t>(end));
    };

    std::string base = trimRight(name, 8);
    std::string extension = trimRight(ext, 3);

    if (extension.empty()) return base;
    return base + "." + extension;
}

// =============================================================================
//  AtariDiskEngine — Construction & Initialization
// =============================================================================

AtariDiskEngine::AtariDiskEngine(std::vector<uint8_t> imageData)
    : m_image(std::move(imageData)) {
    init();
}

AtariDiskEngine::AtariDiskEngine(const uint8_t* data, std::size_t byteCount)
    : m_image(data, data + byteCount) {
    init();
}

void AtariDiskEngine::init() {
    if (m_image.size() < SECTOR_SIZE) {
        throw std::runtime_error("AtariDiskEngine: Image size is smaller than 512 bytes.");
    }
    // Map BPB to the first sector
    m_bpb = reinterpret_cast<const AtariBPB*>(m_image.data());
}

// =============================================================================
//  Core Logic & Geometry
// =============================================================================

const AtariBPB& AtariDiskEngine::bpb() const noexcept {
    return *m_bpb;
}

/*static*/ bool AtariDiskEngine::validateBootChecksum(const uint8_t* sector512) noexcept {
    uint32_t sum = 0;
    for (int i = 0; i < 256; ++i) {
        sum += readLE16(sector512 + (i * 2));
    }
    return (sum & 0xFFFF) == BOOT_CHECKSUM_TARGET;
}

bool AtariDiskEngine::validateBootChecksum() const noexcept {
    return validateBootChecksum(m_image.data());
}

uint32_t AtariDiskEngine::fat1Offset() const noexcept {
    return static_cast<uint32_t>(m_bpb->getReservedSectors()) * SECTOR_SIZE;
}

uint32_t AtariDiskEngine::rootDirOffset() const noexcept {
    return fat1Offset() + (static_cast<uint32_t>(m_bpb->fatCount) * m_bpb->getSectorsPerFat() * SECTOR_SIZE);
}

uint32_t AtariDiskEngine::dataRegionOffset() const noexcept {
    uint32_t rootDirBytes = static_cast<uint32_t>(m_bpb->getRootEntryCount()) * DIRENT_SIZE;
    return rootDirOffset() + rootDirBytes;
}

uint32_t AtariDiskEngine::clusterOffset(uint16_t cluster) const noexcept {
    assert(cluster >= 2);
    uint32_t clusterIndex = static_cast<uint32_t>(cluster) - 2;
    uint32_t bytesPerCluster = static_cast<uint32_t>(m_bpb->sectorsPerCluster) * SECTOR_SIZE;
    return dataRegionOffset() + (clusterIndex * bytesPerCluster);
}

// =============================================================================
//  FAT12 Management
// =============================================================================

uint16_t AtariDiskEngine::getNextCluster(uint16_t currentCluster) const noexcept {
    const uint8_t* fat = m_image.data() + fat1Offset();
    uint32_t byteOffset = (static_cast<uint32_t>(currentCluster) * 3) / 2;

    if (byteOffset + 1 >= (static_cast<uint32_t>(m_bpb->getSectorsPerFat()) * SECTOR_SIZE)) {
        return 0xFFF; // Out of bounds safety
    }

    uint16_t raw = readLE16(fat + byteOffset);

    if (currentCluster & 1) {
        return raw >> 4; // Odd: high 12 bits
    } else {
        return raw & 0x0FFF; // Even: low 12 bits
    }
}

std::vector<uint16_t> AtariDiskEngine::getClusterChain(uint16_t startCluster) const {
    std::vector<uint16_t> chain;
    uint16_t current = startCluster;

    // Safety limit based on total sectors to prevent infinite loops on corrupt images
    const size_t maxClusters = (m_image.size() / SECTOR_SIZE) + 1;

    while (current >= 2 && current < FAT12_EOC_MIN) {
        chain.push_back(current);
        current = getNextCluster(current);
        if (chain.size() > maxClusters) break;
    }
    return chain;
}

// =============================================================================
//  Directory & File Parsing
// =============================================================================

std::vector<DirEntry> AtariDiskEngine::readRootDirectory() const {
    std::vector<DirEntry> entries;
    const uint8_t* ptr = m_image.data() + rootDirOffset();
    uint16_t maxEntries = m_bpb->getRootEntryCount();

    for (uint16_t i = 0; i < maxEntries; ++i) {
        DirEntry entry;
        std::memcpy(&entry, ptr + (i * DIRENT_SIZE), DIRENT_SIZE);
        if (entry.isEndOfDir()) break;
        entries.push_back(entry);
    }
    return entries;
}

std::vector<DirEntry> AtariDiskEngine::readSubDirectory(uint16_t startCluster) const {
    std::vector<DirEntry> entries;
    auto chain = getClusterChain(startCluster);
    uint32_t bytesPerCluster = static_cast<uint32_t>(m_bpb->sectorsPerCluster) * SECTOR_SIZE;

    for (uint16_t cluster : chain) {
        const uint8_t* ptr = m_image.data() + clusterOffset(cluster);
        for (uint32_t i = 0; i < bytesPerCluster / DIRENT_SIZE; ++i) {
            DirEntry entry;
            std::memcpy(&entry, ptr + (i * DIRENT_SIZE), DIRENT_SIZE);
            if (entry.isEndOfDir()) return entries;
            entries.push_back(entry);
        }
    }
    return entries;
}

std::vector<uint8_t> AtariDiskEngine::readFile(const DirEntry& entry) const {
    if (entry.isDirectory()) return {};
    
    std::vector<uint8_t> data;
    uint32_t fileSize = entry.getFileSize();
    data.reserve(fileSize);

    auto chain = getClusterChain(entry.getStartCluster());
    uint32_t bytesPerCluster = static_cast<uint32_t>(m_bpb->sectorsPerCluster) * SECTOR_SIZE;

    for (uint16_t cluster : chain) {
        uint32_t remaining = fileSize - static_cast<uint32_t>(data.size());
        uint32_t toCopy = std::min(remaining, bytesPerCluster);
        const uint8_t* ptr = m_image.data() + clusterOffset(cluster);
        
        data.insert(data.end(), ptr, ptr + toCopy);
        if (data.size() >= fileSize) break;
    }
    return data;
}

} // namespace Atari
