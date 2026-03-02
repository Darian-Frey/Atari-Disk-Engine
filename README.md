# 🕹️ Atari ST Toolkit (Linux Edition)

**"Power Without The Price" — Modern Disk Manipulation for Retro Hardware.**

---

## 💾 Overview

The **Atari ST Toolkit** is a specialized RAM-disk utility designed to bridge the gap between your modern Linux environment and the classic 16-bit Atari ST ecosystem. Built with a focus on speed and accuracy, it allows you to manipulate .ST floppy images with surgical precision.

Whether you are rebuilding a game disk, extracting old code, or managing GEM-based software, this toolkit provides the low-level access required to handle **FAT12** filesystems and **TOS-specific** boot sectors.

---

## 🚀 Key Features

* **RAM-Disk Engine**: Instant disk creation and manipulation in system memory.
* **Double-Sided 720KB Support**: Native handling of the standard Atari DS/DD format.
* **Brute-Force Directory Scanning**: Advanced logic to recover file structures from non-standard "compact" disks (Vectronix style).
* **Dynamic Injection & Deletion**: Add or remove files with automatic FAT chain management.
* **Diagnostic Hex Viewer**: Real-time visualization of raw disk data with sector-aligned mapping.
* **Disk Metadata Profiling**: Deep-scan diagnostics for cluster health and space utilization.

---

## 🛠️ Hardware & Environment

This project is currently optimized and tested for the following environment:

* **Machine**: Dell Latitude-5480
* **OS**: Linux (Ubuntu 22.04+ recommended)
* **Compiler**: GCC (C++17)
* **Toolkit**: Qt 5 Core/Widgets

---

## 📂 Quick Start

### 1. Build from Source

To perform a clean build on your system, run the following in the project root:

make clean

make -j$(nproc)

### 2. Launch the Engine

./bin/AtariDiskEngine

### 3. Standard Workflow

1. **Create**: File > New 720K Disk (Initializes a fresh GEMINI-labeled image).
2. **Inject**: File > Inject File (Bridges a Linux file into the Atari environment).
3. **Inspect**: Click any file in the Tree View to view its raw bytes in the Hex Viewer.
4. **Analyze**: Disk > Disk Information to verify FAT health.
5. **Save**: File > Save As to export your .st image for use in emulators (Hatari) or real hardware.

---

## 📝 Atari Technical Specs (Standard 720KB)

| Attribute | Value |
| :--- | :--- |
| **Sectors per Track** | 9 |
| **Heads** | 2 |
| **Sectors per Cluster** | 2 (1024 bytes) |
| **Directory Start** | Sector 11 |
| **Data Start** | Sector 18 |
| **FS Type** | FAT12 (Atari TOS Variant) |

---

## 🎨 Aesthetic Credits

* **Boot Sector Signature**: "ANTIGRAV"
* **UI Inspiration**: Classic GEM Desktop / Neodesk
* **Philosophy**: Keep it fast, keep it accurate.

---
*Developed for the Atari Community in 2026. Long live the 68000.*
