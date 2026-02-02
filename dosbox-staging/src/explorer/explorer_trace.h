// SPDX-License-Identifier: MIT
// Explorer instruction trace API
// Provides access to recently executed instructions for debugging

#ifndef EXPLORER_TRACE_H
#define EXPLORER_TRACE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#ifdef EXPLORER_ENABLED

namespace Explorer {

// =============================================================================
// Trace Entry Structure
// =============================================================================

struct TraceInstruction {
    uint32_t phys_pc;          // Physical address (CS:IP linearized)
    uint16_t cs;               // Code segment
    uint16_t ip;               // Instruction pointer
    uint8_t  bytes[16];        // Raw instruction bytes
    uint8_t  len;              // Instruction length
    uint64_t timestamp;        // Instruction counter when executed
    
    // Event flags
    uint8_t  is_interrupt : 1; // This was an interrupt entry
    uint8_t  is_call : 1;      // This was a CALL instruction
    uint8_t  is_ret : 1;       // This was a RET instruction
    uint8_t  is_jump : 1;      // This was a jump instruction
    uint8_t  reserved : 4;
    
    // For interrupts
    uint8_t  int_number;       // Interrupt number (if is_interrupt)
};

// =============================================================================
// Trace Configuration
// =============================================================================

// Set trace ring buffer size (number of instructions to keep)
void SetTraceDepth(size_t max_entries);

// Get current trace depth setting
size_t GetTraceDepth();

// Enable/disable trace recording
void SetTraceEnabled(bool enabled);
bool IsTraceEnabled();

// =============================================================================
// Trace Access
// =============================================================================

// Get recent trace entries (most recent first)
// Returns number of entries written to buffer
size_t GetTraceWindow(TraceInstruction* buffer, size_t count);

// Get trace as vector
std::vector<TraceInstruction> GetTraceVector(size_t count);

// Get total instructions traced (may wrap)
uint64_t GetTracedInstructionCount();

// Clear the trace buffer
void ClearTrace();

// =============================================================================
// Trace Formatting
// =============================================================================

// Format a single trace entry as string
// Format: "CS:IP  bytes  [disasm]"
std::string FormatTraceEntry(const TraceInstruction& entry);

// Format entire trace window as string
std::string FormatTraceWindow(size_t count);

// Get disassembly string for instruction bytes
// Returns empty string if disassembly not available
std::string DisassembleBytes(const uint8_t* bytes, size_t len, uint32_t address);

// =============================================================================
// Trace Recording (called from instruction hooks)
// =============================================================================

// Record an executed instruction
// Called from EXPLORER_POST_INSTRUCTION hook
void RecordInstruction(uint32_t phys_pc, uint16_t cs, uint16_t ip);

// Record an interrupt
void RecordInterrupt(uint8_t int_num, uint32_t return_pc);

// =============================================================================
// Trace Triggers / Filters
// =============================================================================

// Only record trace when PC is in range
void SetTraceFilterPCRange(uint32_t start, uint32_t end);
void ClearTraceFilterPCRange();

// Only record trace on specific events
void SetTraceOnInterruptsOnly(bool enabled);
void SetTraceOnCallsOnly(bool enabled);

// Pause trace recording temporarily
void PauseTrace();
void ResumeTrace();

// =============================================================================
// Trace Statistics
// =============================================================================

struct TraceStats {
    uint64_t total_recorded;     // Total instructions recorded
    uint64_t total_dropped;      // Dropped due to filter or full buffer
    uint64_t interrupts_seen;    // Number of interrupts recorded
    size_t   current_depth;      // Current number of entries in buffer
    size_t   max_depth;          // Maximum buffer size
    bool     recording;          // Currently recording
    bool     paused;             // Temporarily paused
};

TraceStats GetTraceStats();

} // namespace Explorer

#else // !EXPLORER_ENABLED

namespace Explorer {

struct TraceInstruction {
    uint32_t phys_pc; uint16_t cs, ip;
    uint8_t bytes[16], len; uint64_t timestamp;
    uint8_t is_interrupt:1, is_call:1, is_ret:1, is_jump:1, reserved:4;
    uint8_t int_number;
};

inline void SetTraceDepth(size_t) {}
inline size_t GetTraceDepth() { return 0; }
inline void SetTraceEnabled(bool) {}
inline bool IsTraceEnabled() { return false; }

inline size_t GetTraceWindow(TraceInstruction*, size_t) { return 0; }
inline std::vector<TraceInstruction> GetTraceVector(size_t) { return {}; }
inline uint64_t GetTracedInstructionCount() { return 0; }
inline void ClearTrace() {}

inline std::string FormatTraceEntry(const TraceInstruction&) { return ""; }
inline std::string FormatTraceWindow(size_t) { return ""; }
inline std::string DisassembleBytes(const uint8_t*, size_t, uint32_t) { return ""; }

inline void RecordInstruction(uint32_t, uint16_t, uint16_t) {}
inline void RecordInterrupt(uint8_t, uint32_t) {}

inline void SetTraceFilterPCRange(uint32_t, uint32_t) {}
inline void ClearTraceFilterPCRange() {}
inline void SetTraceOnInterruptsOnly(bool) {}
inline void SetTraceOnCallsOnly(bool) {}
inline void PauseTrace() {}
inline void ResumeTrace() {}

struct TraceStats { uint64_t total_recorded, total_dropped, interrupts_seen;
                    size_t current_depth, max_depth; bool recording, paused; };
inline TraceStats GetTraceStats() { return {}; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_TRACE_H
