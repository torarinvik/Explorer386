// SPDX-License-Identifier: MIT
// Explorer instruction trace implementation

#include "explorer_trace.h"

#ifdef EXPLORER_ENABLED

#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>
#include <mutex>

#include "hardware/memory.h"
#include "cpu/cpu.h"

namespace Explorer {

// =============================================================================
// Static State
// =============================================================================

static constexpr size_t DEFAULT_TRACE_DEPTH = 4096;
static constexpr size_t MAX_TRACE_DEPTH = 1024 * 1024;  // 1M entries max

static std::vector<TraceInstruction> s_trace_buffer;
static size_t s_trace_head = 0;  // Next write position
static size_t s_trace_count = 0; // Number of valid entries
static size_t s_trace_max_depth = DEFAULT_TRACE_DEPTH;

static bool s_trace_enabled = true;
static bool s_trace_paused = false;

static uint64_t s_total_recorded = 0;
static uint64_t s_total_dropped = 0;
static uint64_t s_interrupts_seen = 0;

// Filters
static bool s_filter_pc_enabled = false;
static uint32_t s_filter_pc_start = 0;
static uint32_t s_filter_pc_end = 0;
static bool s_filter_interrupts_only = false;
static bool s_filter_calls_only = false;

// =============================================================================
// Configuration
// =============================================================================

void SetTraceDepth(size_t max_entries)
{
    max_entries = std::min(max_entries, MAX_TRACE_DEPTH);
    if (max_entries == 0) max_entries = DEFAULT_TRACE_DEPTH;
    
    s_trace_buffer.resize(max_entries);
    s_trace_max_depth = max_entries;
    
    // Reset buffer state
    s_trace_head = 0;
    s_trace_count = 0;
}

size_t GetTraceDepth()
{
    return s_trace_max_depth;
}

void SetTraceEnabled(bool enabled)
{
    s_trace_enabled = enabled;
    if (enabled && s_trace_buffer.empty()) {
        s_trace_buffer.resize(s_trace_max_depth);
    }
}

bool IsTraceEnabled()
{
    return s_trace_enabled;
}

// =============================================================================
// Trace Recording
// =============================================================================

void RecordInstruction(uint32_t phys_pc, uint16_t cs, uint16_t ip)
{
    if (!s_trace_enabled || s_trace_paused) {
        s_total_dropped++;
        return;
    }
    
    // Apply PC filter
    if (s_filter_pc_enabled) {
        if (phys_pc < s_filter_pc_start || phys_pc >= s_filter_pc_end) {
            s_total_dropped++;
            return;
        }
    }
    
    // Ensure buffer is allocated
    if (s_trace_buffer.empty()) {
        s_trace_buffer.resize(s_trace_max_depth);
    }
    
    TraceInstruction& entry = s_trace_buffer[s_trace_head];
    
    entry.phys_pc = phys_pc;
    entry.cs = cs;
    entry.ip = ip;
    entry.timestamp = s_total_recorded;
    entry.is_interrupt = 0;
    entry.is_call = 0;
    entry.is_ret = 0;
    entry.is_jump = 0;
    entry.int_number = 0;
    
    // Read instruction bytes from memory
    entry.len = 0;
    uint8_t* mem = GetMemBase();
    if (mem && phys_pc < MEM_TotalPages() * 4096) {
        // Read up to 16 bytes (max x86 instruction length)
        size_t available = (MEM_TotalPages() * 4096) - phys_pc;
        size_t to_read = std::min<size_t>(16, available);
        memcpy(entry.bytes, mem + phys_pc, to_read);
        entry.len = static_cast<uint8_t>(to_read);
        
        // Simple opcode analysis for flags
        if (entry.len > 0) {
            uint8_t op = entry.bytes[0];
            // CALL opcodes
            if (op == 0xE8 || op == 0x9A || op == 0xFF) {
                entry.is_call = 1;
            }
            // RET opcodes
            if (op == 0xC3 || op == 0xCB || op == 0xC2 || op == 0xCA || op == 0xCF) {
                entry.is_ret = 1;
            }
            // Jump opcodes (simplified)
            if ((op >= 0x70 && op <= 0x7F) || op == 0xEB || op == 0xE9 || op == 0xEA) {
                entry.is_jump = 1;
            }
        }
    }
    
    // Apply call-only filter
    if (s_filter_calls_only && !entry.is_call && !entry.is_ret) {
        s_total_dropped++;
        return;
    }
    
    // Advance ring buffer
    s_trace_head = (s_trace_head + 1) % s_trace_max_depth;
    if (s_trace_count < s_trace_max_depth) {
        s_trace_count++;
    }
    
    s_total_recorded++;
}

void RecordInterrupt(uint8_t int_num, uint32_t return_pc)
{
    if (!s_trace_enabled || s_trace_paused) {
        s_total_dropped++;
        return;
    }
    
    // Ensure buffer is allocated
    if (s_trace_buffer.empty()) {
        s_trace_buffer.resize(s_trace_max_depth);
    }
    
    TraceInstruction& entry = s_trace_buffer[s_trace_head];
    
    entry.phys_pc = return_pc;
    entry.cs = SegValue(cs);
    entry.ip = static_cast<uint16_t>(reg_eip);
    entry.timestamp = s_total_recorded;
    entry.is_interrupt = 1;
    entry.is_call = 0;
    entry.is_ret = 0;
    entry.is_jump = 0;
    entry.int_number = int_num;
    entry.len = 0;  // No instruction bytes for interrupt marker
    
    // Advance ring buffer
    s_trace_head = (s_trace_head + 1) % s_trace_max_depth;
    if (s_trace_count < s_trace_max_depth) {
        s_trace_count++;
    }
    
    s_total_recorded++;
    s_interrupts_seen++;
}

// =============================================================================
// Trace Access
// =============================================================================

size_t GetTraceWindow(TraceInstruction* buffer, size_t count)
{
    if (!buffer || count == 0 || s_trace_count == 0) return 0;
    
    count = std::min(count, s_trace_count);
    
    // Read from most recent backwards
    for (size_t i = 0; i < count; i++) {
        size_t idx = (s_trace_head + s_trace_max_depth - 1 - i) % s_trace_max_depth;
        buffer[i] = s_trace_buffer[idx];
    }
    
    return count;
}

std::vector<TraceInstruction> GetTraceVector(size_t count)
{
    count = std::min(count, s_trace_count);
    std::vector<TraceInstruction> result(count);
    GetTraceWindow(result.data(), count);
    return result;
}

uint64_t GetTracedInstructionCount()
{
    return s_total_recorded;
}

void ClearTrace()
{
    s_trace_head = 0;
    s_trace_count = 0;
    // Don't reset total counters - those are cumulative
}

// =============================================================================
// Trace Formatting
// =============================================================================

std::string FormatTraceEntry(const TraceInstruction& entry)
{
    std::ostringstream ss;
    
    // Address
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << std::setw(4) << entry.cs << ':' << std::setw(4) << entry.ip;
    ss << " (0x" << std::setw(5) << entry.phys_pc << ")  ";
    
    // Event type marker
    if (entry.is_interrupt) {
        ss << "[INT " << std::setw(2) << static_cast<int>(entry.int_number) << "] ";
    } else {
        // Bytes
        for (size_t i = 0; i < entry.len && i < 8; i++) {
            ss << std::setw(2) << static_cast<int>(entry.bytes[i]) << ' ';
        }
        // Pad to align
        for (size_t i = entry.len; i < 8; i++) {
            ss << "   ";
        }
        
        // Flags
        if (entry.is_call) ss << "[CALL] ";
        if (entry.is_ret) ss << "[RET] ";
        if (entry.is_jump) ss << "[JMP] ";
    }
    
    // Disassembly (if available)
    std::string disasm = DisassembleBytes(entry.bytes, entry.len, entry.phys_pc);
    if (!disasm.empty()) {
        ss << " ; " << disasm;
    }
    
    return ss.str();
}

std::string FormatTraceWindow(size_t count)
{
    std::vector<TraceInstruction> entries = GetTraceVector(count);
    std::ostringstream ss;
    
    ss << "=== Instruction Trace (most recent first) ===\n";
    ss << "Total recorded: " << s_total_recorded << "\n\n";
    
    for (size_t i = 0; i < entries.size(); i++) {
        ss << std::setw(4) << i << ": " << FormatTraceEntry(entries[i]) << "\n";
    }
    
    return ss.str();
}

std::string DisassembleBytes(const uint8_t* bytes, size_t len, uint32_t address)
{
    // Simple disassembly for common instructions
    // A full disassembler would be quite large; this covers basics
    
    if (!bytes || len == 0) return "";
    
    uint8_t op = bytes[0];
    
    // Just return hex for now - a proper disassembler would be much larger
    // In a real implementation, we'd integrate udis86, capstone, or zydis
    
    std::ostringstream ss;
    
    switch (op) {
        case 0x90: return "NOP";
        case 0xCC: return "INT 3";
        case 0xCD: 
            if (len >= 2) {
                ss << "INT 0x" << std::hex << static_cast<int>(bytes[1]);
                return ss.str();
            }
            break;
        case 0xC3: return "RET";
        case 0xCB: return "RETF";
        case 0xCF: return "IRET";
        case 0xF4: return "HLT";
        case 0xFA: return "CLI";
        case 0xFB: return "STI";
        case 0xFC: return "CLD";
        case 0xFD: return "STD";
        case 0xE8: return "CALL near";
        case 0x9A: return "CALL far";
        case 0xE9: return "JMP near";
        case 0xEA: return "JMP far";
        case 0xEB: return "JMP short";
        default: break;
    }
    
    // Conditional jumps
    if (op >= 0x70 && op <= 0x7F) {
        static const char* jcc_names[] = {
            "JO", "JNO", "JB", "JAE", "JE", "JNE", "JBE", "JA",
            "JS", "JNS", "JP", "JNP", "JL", "JGE", "JLE", "JG"
        };
        return jcc_names[op - 0x70];
    }
    
    return "";  // Unknown - no disassembly
}

// =============================================================================
// Trace Filters
// =============================================================================

void SetTraceFilterPCRange(uint32_t start, uint32_t end)
{
    s_filter_pc_enabled = true;
    s_filter_pc_start = start;
    s_filter_pc_end = end;
}

void ClearTraceFilterPCRange()
{
    s_filter_pc_enabled = false;
}

void SetTraceOnInterruptsOnly(bool enabled)
{
    s_filter_interrupts_only = enabled;
}

void SetTraceOnCallsOnly(bool enabled)
{
    s_filter_calls_only = enabled;
}

void PauseTrace()
{
    s_trace_paused = true;
}

void ResumeTrace()
{
    s_trace_paused = false;
}

// =============================================================================
// Statistics
// =============================================================================

TraceStats GetTraceStats()
{
    TraceStats stats = {};
    stats.total_recorded = s_total_recorded;
    stats.total_dropped = s_total_dropped;
    stats.interrupts_seen = s_interrupts_seen;
    stats.current_depth = s_trace_count;
    stats.max_depth = s_trace_max_depth;
    stats.recording = s_trace_enabled;
    stats.paused = s_trace_paused;
    return stats;
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
