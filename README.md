# 🕹️ Atari ST Toolkit (Linux Edition)

**"Power Without The Price" — Modern Disk Manipulation for Retro Hardware.**

[![Platform](https://img.shields.io/badge/Platform-Linux-blue.svg)](https://ubuntu.com/)
[![Qt](https://img.shields.io/badge/Framework-Qt5.15-green.svg)](https://www.qt.io/)

---

## 💾 Overview

The **Atari ST Toolkit** is a specialized RAM-disk utility designed to bridge the gap between your modern Linux environment and the classic 16-bit Atari ST ecosystem. Built with a focus on speed and accuracy, it allows you to manipulate `.ST` floppy images with surgical precision.

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

```bash
# Clean and build the toolkit
make clean
make -j$(nproc)

# Launch the engine
./bin/AtariDiskEngine
```
