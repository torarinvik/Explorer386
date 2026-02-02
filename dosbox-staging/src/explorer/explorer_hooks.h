// SPDX-License-Identifier: MIT
// Explorer hooks for DOSBox CPU core integration
// Include this file from core_normal.cpp and other CPU cores

#ifndef EXPLORER_HOOKS_H
#define EXPLORER_HOOKS_H

#ifdef EXPLORER_ENABLED

#include "explorer/explorer.h"
#include "explorer/explorer_data.h"
#include "explorer/explorer_trace.h"

// =============================================================================
// Hook Macros for CPU Cores
// =============================================================================

// Call at the start of each instruction (before decode)
// phys_pc should be the physical address of the instruction
#define EXPLORER_PRE_INSTRUCTION(phys_pc) \
    do { \
        if (Explorer::InstrumentationEnabled()) { \
            Explorer::GetInstrumenter().PreInstruction(phys_pc); \
        } \
    } while(0)

// Call after instruction completes
// Provide the physical PC of the instruction start and the next PC.
#define EXPLORER_POST_INSTRUCTION(prev_phys, next_phys) \
    do { \
        if (Explorer::InstrumentationEnabled()) { \
            Explorer::GetInstrumenter().PostInstruction((prev_phys), (next_phys)); \
            if (Explorer::IsTraceEnabled()) { \
                Explorer::RecordInstruction((prev_phys), 0, 0); \
            } \
        } \
    } while(0)

// Call on memory read (from paging or memory access functions)
#define EXPLORER_NOTE_READ(phys_addr, size, pc_phys) \
    do { \
        if (Explorer::DataTrackingEnabled()) { \
            Explorer::GetDataCollector().NoteRead(phys_addr, size, pc_phys); \
        } \
    } while(0)

// Call on memory write
#define EXPLORER_NOTE_WRITE(phys_addr, size, pc_phys) \
    do { \
        if (Explorer::DataTrackingEnabled()) { \
            Explorer::GetDataCollector().NoteWrite(phys_addr, size, pc_phys); \
        } \
    } while(0)

// Call on interrupt
#define EXPLORER_NOTE_INTERRUPT(num) \
    do { \
        if (Explorer::InstrumentationEnabled()) { \
            Explorer::NoteInterrupt(static_cast<uint8_t>(num)); \
        } \
    } while(0)

// Call on port I/O
#define EXPLORER_NOTE_PORT_IN(port, value) \
    do { \
        if (Explorer::InstrumentationEnabled()) { \
            Explorer::NotePortIn(static_cast<uint16_t>(port), static_cast<uint16_t>(value)); \
        } \
    } while(0)

#define EXPLORER_NOTE_PORT_OUT(port, value) \
    do { \
        if (Explorer::InstrumentationEnabled()) { \
            Explorer::NotePortOut(static_cast<uint16_t>(port), static_cast<uint16_t>(value)); \
        } \
    } while(0)

// =============================================================================
// Convenience for physical address computation
// =============================================================================

// For real mode: seg << 4 + offset
// For protected mode: need to use paging translation
// DOSBox already has SegPhys() which gives segment physical base

// Helper to get physical PC from segment:offset
// In protected mode with paging, this needs proper translation
inline uint32_t Explorer_GetPhysicalPC(uint16_t cs_base_high, uint32_t eip) {
    // For now, simple linear address (works for real mode and flat protected)
    // Full paging translation would need LinToPhys() or similar
    return (static_cast<uint32_t>(cs_base_high) << 16) + eip;
}

#else // !EXPLORER_ENABLED

// No-op stubs when Explorer is disabled
#define EXPLORER_PRE_INSTRUCTION(phys_pc) ((void)0)
#define EXPLORER_POST_INSTRUCTION(prev_phys, next_phys) ((void)0)
#define EXPLORER_NOTE_READ(phys_addr, size, pc_phys) ((void)0)
#define EXPLORER_NOTE_WRITE(phys_addr, size, pc_phys) ((void)0)
#define EXPLORER_NOTE_INTERRUPT(num) ((void)0)
#define EXPLORER_NOTE_PORT_IN(port, value) ((void)0)
#define EXPLORER_NOTE_PORT_OUT(port, value) ((void)0)

#endif // EXPLORER_ENABLED

#endif // EXPLORER_HOOKS_H
