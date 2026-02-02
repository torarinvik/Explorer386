// SPDX-License-Identifier: MIT
// Explorer CPU register access API
// Provides read access to CPU registers for observation/debugging

#ifndef EXPLORER_CPU_H
#define EXPLORER_CPU_H

#include <cstdint>
#include <string>

#ifdef EXPLORER_ENABLED

namespace Explorer {

// =============================================================================
// CPU Register Snapshot
// =============================================================================

struct CPURegisters {
    // 32-bit general purpose registers
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    
    // Instruction pointer
    uint32_t eip;
    
    // Flags register
    uint32_t eflags;
    
    // Segment registers (16-bit values)
    uint16_t cs, ds, es, fs, gs, ss;
    
    // Segment base addresses (linear addresses)
    uint32_t cs_base, ds_base, es_base, fs_base, gs_base, ss_base;
    
    // Current physical address (CS:IP linearized)
    uint32_t physical_ip;
    
    // CPU mode info
    bool protected_mode;
    bool v86_mode;
    uint8_t cpl;  // Current privilege level
};

// =============================================================================
// Register Access API
// =============================================================================

// Get a snapshot of all CPU registers
CPURegisters GetRegisters();

// Get individual registers by name (case-insensitive)
// Returns 0 if name not recognized
uint32_t GetRegister(const char* name);

// Get segment base address
uint32_t GetSegmentBase(const char* segment_name);

// Compute physical address from segment:offset
uint32_t SegmentToPhysical(uint16_t segment, uint16_t offset);

// =============================================================================
// Flag Helpers
// =============================================================================

// Individual flag accessors
bool GetCarryFlag();
bool GetZeroFlag();
bool GetSignFlag();
bool GetOverflowFlag();
bool GetParityFlag();
bool GetAuxCarryFlag();
bool GetDirectionFlag();
bool GetInterruptFlag();
bool GetTrapFlag();

// Format flags as string (e.g., "CZ--O---")
std::string FormatFlags();

// =============================================================================
// Register Names
// =============================================================================

// Get list of available register names
const char** GetRegisterNames();
size_t GetRegisterCount();

} // namespace Explorer

#else // !EXPLORER_ENABLED

namespace Explorer {

struct CPURegisters {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
    uint16_t cs, ds, es, fs, gs, ss;
    uint32_t cs_base, ds_base, es_base, fs_base, gs_base, ss_base;
    uint32_t physical_ip;
    bool protected_mode, v86_mode;
    uint8_t cpl;
};

inline CPURegisters GetRegisters() { return {}; }
inline uint32_t GetRegister(const char*) { return 0; }
inline uint32_t GetSegmentBase(const char*) { return 0; }
inline uint32_t SegmentToPhysical(uint16_t seg, uint16_t off) { return (seg << 4) + off; }

inline bool GetCarryFlag() { return false; }
inline bool GetZeroFlag() { return false; }
inline bool GetSignFlag() { return false; }
inline bool GetOverflowFlag() { return false; }
inline bool GetParityFlag() { return false; }
inline bool GetAuxCarryFlag() { return false; }
inline bool GetDirectionFlag() { return false; }
inline bool GetInterruptFlag() { return false; }
inline bool GetTrapFlag() { return false; }
inline std::string FormatFlags() { return "--------"; }

inline const char** GetRegisterNames() { return nullptr; }
inline size_t GetRegisterCount() { return 0; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_CPU_H
