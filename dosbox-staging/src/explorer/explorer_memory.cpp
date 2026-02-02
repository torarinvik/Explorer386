// SPDX-License-Identifier: MIT
// Explorer memory access implementation

#include "explorer_memory.h"

#ifdef EXPLORER_ENABLED

#include <cstring>
#include <algorithm>
#include <sstream>

#include "hardware/memory.h"
#include "hardware/video/vga.h"
#include "ints/int10.h"

namespace Explorer {

// =============================================================================
// Memory Regions
// =============================================================================

static const MemoryRegion s_memory_regions[] = {
    {0x00000, 0x003FF, "IVT"},           // Interrupt Vector Table
    {0x00400, 0x004FF, "BDA"},           // BIOS Data Area
    {0x00500, 0x9FFFF, "Conventional"},  // Conventional memory (to 640KB)
    {0xA0000, 0xAFFFF, "VGA_Graphics"},  // VGA graphics memory
    {0xB0000, 0xB7FFF, "VGA_Mono"},      // VGA monochrome text
    {0xB8000, 0xBFFFF, "VGA_Color"},     // VGA color text
    {0xC0000, 0xC7FFF, "Video_ROM"},     // Video ROM
    {0xC8000, 0xEFFFF, "Adapter_ROM"},   // Adapter ROM/RAM
    {0xF0000, 0xFFFFF, "System_ROM"},    // System ROM (BIOS)
    {0, 0, nullptr}
};

// =============================================================================
// Physical Memory Access
// =============================================================================

size_t ReadPhysicalMemory(uint32_t phys_addr, uint8_t* buffer, size_t len)
{
    if (!buffer || len == 0) return 0;
    
    uint8_t* mem_base = GetMemBase();
    if (!mem_base) return 0;
    
    // Clamp to available memory
    size_t mem_size = MEM_TotalPages() * 4096;
    if (phys_addr >= mem_size) return 0;
    
    size_t available = mem_size - phys_addr;
    size_t to_read = std::min(len, available);
    
    memcpy(buffer, mem_base + phys_addr, to_read);
    return to_read;
}

uint8_t ReadPhysByte(uint32_t addr)
{
    return mem_readb(addr);
}

uint16_t ReadPhysWord(uint32_t addr)
{
    return mem_readw(addr);
}

uint32_t ReadPhysDWord(uint32_t addr)
{
    return mem_readd(addr);
}

uint8_t* GetMemoryBase()
{
    return GetMemBase();
}

size_t GetMemorySize()
{
    return MEM_TotalPages() * 4096;
}

// =============================================================================
// Logical Memory Access
// =============================================================================

size_t ReadLogicalMemory(uint16_t segment, uint16_t offset, uint8_t* buffer, size_t len)
{
    uint32_t phys = (static_cast<uint32_t>(segment) << 4) + offset;
    return ReadPhysicalMemory(phys, buffer, len);
}

uint8_t ReadLogicalByte(uint16_t segment, uint16_t offset)
{
    uint32_t phys = (static_cast<uint32_t>(segment) << 4) + offset;
    return ReadPhysByte(phys);
}

uint16_t ReadLogicalWord(uint16_t segment, uint16_t offset)
{
    uint32_t phys = (static_cast<uint32_t>(segment) << 4) + offset;
    return ReadPhysWord(phys);
}

uint32_t ReadLogicalDWord(uint16_t segment, uint16_t offset)
{
    uint32_t phys = (static_cast<uint32_t>(segment) << 4) + offset;
    return ReadPhysDWord(phys);
}

// =============================================================================
// Memory Region Helpers
// =============================================================================

uint8_t* GetIVTPointer()
{
    uint8_t* base = GetMemBase();
    return base ? base : nullptr;  // IVT starts at 0x000
}

uint8_t* GetBDAPointer()
{
    uint8_t* base = GetMemBase();
    return base ? base + 0x400 : nullptr;
}

uint8_t* GetConventionalMemory()
{
    uint8_t* base = GetMemBase();
    return base ? base + 0x500 : nullptr;  // After IVT and BDA
}

const MemoryRegion* GetMemoryRegions()
{
    return s_memory_regions;
}

size_t GetMemoryRegionCount()
{
    size_t count = 0;
    while (s_memory_regions[count].name) count++;
    return count;
}

// =============================================================================
// VRAM Access
// =============================================================================

VRAMInfo GetVRAMInfo()
{
    VRAMInfo info = {};
    
    info.linear = vga.mem.linear;
    info.size = vga.vmemsize;
    info.mode = CurMode->mode;
    info.width = CurMode->swidth;
    info.height = CurMode->sheight;
    
    // Determine BPP from mode type
    switch (CurMode->type) {
        case M_TEXT:
        case M_HERC_TEXT:
        case M_TANDY_TEXT:
            info.bpp = 0;  // Text mode
            info.text_mode = true;
            info.text_cols = static_cast<uint8_t>(CurMode->twidth);
            info.text_rows = static_cast<uint8_t>(CurMode->theight);
            break;
        case M_CGA2:
        case M_HERC_GFX:
        case M_TANDY2:
            info.bpp = 1;
            break;
        case M_CGA4:
        case M_TANDY4:
            info.bpp = 2;
            break;
        case M_EGA:
        case M_LIN4:
        case M_TANDY16:
        case M_CGA16:
            info.bpp = 4;
            break;
        case M_VGA:
        case M_LIN8:
            info.bpp = 8;
            break;
        case M_LIN15:
            info.bpp = 15;
            break;
        case M_LIN16:
            info.bpp = 16;
            break;
        case M_LIN24:
            info.bpp = 24;
            break;
        case M_LIN32:
            info.bpp = 32;
            break;
        default:
            info.bpp = 8;
            break;
    }
    
    return info;
}

size_t ReadVRAM(uint32_t offset, uint8_t* buffer, size_t len)
{
    if (!buffer || len == 0) return 0;
    
    uint8_t* vram = vga.mem.linear;
    if (!vram) return 0;
    
    size_t vram_size = vga.vmemsize;
    if (offset >= vram_size) return 0;
    
    size_t available = vram_size - offset;
    size_t to_read = std::min(len, available);
    
    memcpy(buffer, vram + offset, to_read);
    return to_read;
}

// =============================================================================
// Text Mode Helpers
// =============================================================================

bool GetTextCell(uint8_t row, uint8_t col, char* ch, uint8_t* attr)
{
    VRAMInfo info = GetVRAMInfo();
    if (!info.text_mode) return false;
    if (row >= info.text_rows || col >= info.text_cols) return false;
    
    // Text mode VRAM starts at B8000 for color, B0000 for mono
    // Each cell is 2 bytes: character + attribute
    uint32_t vram_base = (CurMode->type == M_HERC_TEXT) ? 0xB0000 : 0xB8000;
    uint32_t offset = (row * info.text_cols + col) * 2;
    
    if (ch) *ch = static_cast<char>(ReadPhysByte(vram_base + offset));
    if (attr) *attr = ReadPhysByte(vram_base + offset + 1);
    
    return true;
}

size_t DumpTextScreen(char* buffer, size_t buffer_size)
{
    VRAMInfo info = GetVRAMInfo();
    if (!info.text_mode || !buffer) return 0;
    
    size_t total = static_cast<size_t>(info.text_rows) * info.text_cols;
    size_t to_write = std::min(total, buffer_size);
    
    uint32_t vram_base = (CurMode->type == M_HERC_TEXT) ? 0xB0000 : 0xB8000;
    
    for (size_t i = 0; i < to_write; i++) {
        buffer[i] = static_cast<char>(ReadPhysByte(vram_base + i * 2));
    }
    
    return to_write;
}

std::string GetTextScreenString()
{
    VRAMInfo info = GetVRAMInfo();
    if (!info.text_mode) return "[Not in text mode]";
    
    std::ostringstream ss;
    
    for (uint8_t row = 0; row < info.text_rows; row++) {
        for (uint8_t col = 0; col < info.text_cols; col++) {
            char ch;
            if (GetTextCell(row, col, &ch, nullptr)) {
                // Replace non-printable characters with space
                if (ch < 32 || ch > 126) ch = ' ';
                ss << ch;
            }
        }
        // Trim trailing spaces and add newline
        std::string line = ss.str();
        size_t last_non_space = line.find_last_not_of(' ');
        if (last_non_space != std::string::npos) {
            line = line.substr(0, last_non_space + 1);
        }
        ss.str("");
        ss << line << '\n';
    }
    
    return ss.str();
}

// =============================================================================
// Memory Search
// =============================================================================

uint32_t SearchMemory(uint32_t start, uint32_t end, 
                      const uint8_t* pattern, size_t pattern_len)
{
    if (!pattern || pattern_len == 0 || start >= end) return UINT32_MAX;
    
    uint8_t* mem_base = GetMemBase();
    if (!mem_base) return UINT32_MAX;
    
    size_t mem_size = GetMemorySize();
    if (start >= mem_size) return UINT32_MAX;
    
    end = std::min(end, static_cast<uint32_t>(mem_size));
    
    for (uint32_t addr = start; addr + pattern_len <= end; addr++) {
        if (memcmp(mem_base + addr, pattern, pattern_len) == 0) {
            return addr;
        }
    }
    
    return UINT32_MAX;
}

uint32_t SearchMemoryString(uint32_t start, uint32_t end, const char* str)
{
    if (!str) return UINT32_MAX;
    return SearchMemory(start, end, 
                        reinterpret_cast<const uint8_t*>(str), 
                        strlen(str));
}

// =============================================================================
// Memory Comparison / Hashing
// =============================================================================

uint64_t HashMemoryRegion(uint32_t start, size_t len)
{
    uint8_t* mem_base = GetMemBase();
    if (!mem_base) return 0;
    
    size_t mem_size = GetMemorySize();
    if (start >= mem_size) return 0;
    
    size_t available = mem_size - start;
    len = std::min(len, available);
    
    // FNV-1a hash
    uint64_t hash = 0xcbf29ce484222325ULL;
    const uint8_t* ptr = mem_base + start;
    
    for (size_t i = 0; i < len; i++) {
        hash ^= ptr[i];
        hash *= 0x100000001b3ULL;
    }
    
    return hash;
}

size_t CompareMemoryRegions(uint32_t addr1, uint32_t addr2, size_t len)
{
    uint8_t* mem_base = GetMemBase();
    if (!mem_base) return len;
    
    size_t mem_size = GetMemorySize();
    if (addr1 >= mem_size || addr2 >= mem_size) return len;
    
    size_t available1 = mem_size - addr1;
    size_t available2 = mem_size - addr2;
    len = std::min({len, available1, available2});
    
    size_t diff_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (mem_base[addr1 + i] != mem_base[addr2 + i]) {
            diff_count++;
        }
    }
    
    return diff_count;
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
