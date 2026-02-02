// SPDX-License-Identifier: MIT
// Explorer memory access API
// Provides read access to RAM and VRAM for observation/debugging

#ifndef EXPLORER_MEMORY_H
#define EXPLORER_MEMORY_H

#include <cstdint>
#include <cstddef>
#include <vector>

#ifdef EXPLORER_ENABLED

namespace Explorer {

// =============================================================================
// Physical Memory Access
// =============================================================================

// Read bytes from physical address
// Returns number of bytes actually read
size_t ReadPhysicalMemory(uint32_t phys_addr, uint8_t* buffer, size_t len);

// Read single values
uint8_t  ReadPhysByte(uint32_t addr);
uint16_t ReadPhysWord(uint32_t addr);
uint32_t ReadPhysDWord(uint32_t addr);

// Get raw pointer to memory base (for advanced/bulk access)
// WARNING: Use carefully, may become invalid if memory is reallocated
uint8_t* GetMemoryBase();
size_t GetMemorySize();

// =============================================================================
// Logical Memory Access (Segment:Offset)
// =============================================================================

// Read via segment:offset (real mode calculation)
size_t ReadLogicalMemory(uint16_t segment, uint16_t offset, uint8_t* buffer, size_t len);

uint8_t  ReadLogicalByte(uint16_t segment, uint16_t offset);
uint16_t ReadLogicalWord(uint16_t segment, uint16_t offset);
uint32_t ReadLogicalDWord(uint16_t segment, uint16_t offset);

// =============================================================================
// Memory Region Helpers
// =============================================================================

// Get pointer to specific memory regions
uint8_t* GetIVTPointer();          // Interrupt Vector Table: 0x000-0x3FF
uint8_t* GetBDAPointer();          // BIOS Data Area: 0x400-0x4FF
uint8_t* GetConventionalMemory();  // First 640KB

// Memory region info
struct MemoryRegion {
    uint32_t start;
    uint32_t end;
    const char* name;
};

const MemoryRegion* GetMemoryRegions();
size_t GetMemoryRegionCount();

// =============================================================================
// VRAM Access
// =============================================================================

struct VRAMInfo {
    uint8_t* linear;       // Pointer to linear VRAM
    size_t   size;         // Total VRAM size in bytes
    uint32_t mode;         // Current VGA mode number
    uint16_t width;        // Screen width in pixels
    uint16_t height;       // Screen height in pixels
    uint8_t  bpp;          // Bits per pixel
    bool     text_mode;    // True if in text mode
    uint8_t  text_cols;    // Text mode columns
    uint8_t  text_rows;    // Text mode rows
};

// Get VRAM info and pointer
VRAMInfo GetVRAMInfo();

// Read from VRAM
size_t ReadVRAM(uint32_t offset, uint8_t* buffer, size_t len);

// =============================================================================
// Text Mode Helpers
// =============================================================================

// Get character and attribute at text position
// Returns false if not in text mode or position out of range
bool GetTextCell(uint8_t row, uint8_t col, char* ch, uint8_t* attr);

// Dump entire text screen to buffer
// Buffer should be at least rows*cols chars
// Returns number of characters written
size_t DumpTextScreen(char* buffer, size_t buffer_size);

// Get text screen as string with newlines
std::string GetTextScreenString();

// =============================================================================
// Memory Search
// =============================================================================

// Search for byte pattern in memory range
// Returns first match address or UINT32_MAX if not found
uint32_t SearchMemory(uint32_t start, uint32_t end, 
                      const uint8_t* pattern, size_t pattern_len);

// Search for string in memory
uint32_t SearchMemoryString(uint32_t start, uint32_t end, const char* str);

// =============================================================================
// Memory Comparison / Hashing
// =============================================================================

// Compute hash of memory region (for change detection)
uint64_t HashMemoryRegion(uint32_t start, size_t len);

// Compare memory regions, returns number of differing bytes
size_t CompareMemoryRegions(uint32_t addr1, uint32_t addr2, size_t len);

} // namespace Explorer

#else // !EXPLORER_ENABLED

namespace Explorer {

inline size_t ReadPhysicalMemory(uint32_t, uint8_t*, size_t) { return 0; }
inline uint8_t ReadPhysByte(uint32_t) { return 0; }
inline uint16_t ReadPhysWord(uint32_t) { return 0; }
inline uint32_t ReadPhysDWord(uint32_t) { return 0; }
inline uint8_t* GetMemoryBase() { return nullptr; }
inline size_t GetMemorySize() { return 0; }

inline size_t ReadLogicalMemory(uint16_t, uint16_t, uint8_t*, size_t) { return 0; }
inline uint8_t ReadLogicalByte(uint16_t, uint16_t) { return 0; }
inline uint16_t ReadLogicalWord(uint16_t, uint16_t) { return 0; }
inline uint32_t ReadLogicalDWord(uint16_t, uint16_t) { return 0; }

inline uint8_t* GetIVTPointer() { return nullptr; }
inline uint8_t* GetBDAPointer() { return nullptr; }
inline uint8_t* GetConventionalMemory() { return nullptr; }

struct MemoryRegion { uint32_t start, end; const char* name; };
inline const MemoryRegion* GetMemoryRegions() { return nullptr; }
inline size_t GetMemoryRegionCount() { return 0; }

struct VRAMInfo { uint8_t* linear; size_t size; uint32_t mode; 
                  uint16_t width, height; uint8_t bpp; bool text_mode;
                  uint8_t text_cols, text_rows; };
inline VRAMInfo GetVRAMInfo() { return {}; }
inline size_t ReadVRAM(uint32_t, uint8_t*, size_t) { return 0; }

inline bool GetTextCell(uint8_t, uint8_t, char*, uint8_t*) { return false; }
inline size_t DumpTextScreen(char*, size_t) { return 0; }
inline std::string GetTextScreenString() { return ""; }

inline uint32_t SearchMemory(uint32_t, uint32_t, const uint8_t*, size_t) { return UINT32_MAX; }
inline uint32_t SearchMemoryString(uint32_t, uint32_t, const char*) { return UINT32_MAX; }
inline uint64_t HashMemoryRegion(uint32_t, size_t) { return 0; }
inline size_t CompareMemoryRegions(uint32_t, uint32_t, size_t) { return 0; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_MEMORY_H
