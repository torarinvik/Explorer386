// SPDX-License-Identifier: MIT
// Explorer CPU register access implementation

#include "explorer_cpu.h"

#ifdef EXPLORER_ENABLED

#include <cstring>
#include <sstream>

#include "cpu/cpu.h"

namespace Explorer {

// =============================================================================
// Register Names Table
// =============================================================================

static const char* s_register_names[] = {
    "eax", "ebx", "ecx", "edx",
    "esi", "edi", "ebp", "esp",
    "eip", "eflags",
    "ax", "bx", "cx", "dx",
    "si", "di", "bp", "sp",
    "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh",
    "cs", "ds", "es", "fs", "gs", "ss",
    nullptr
};

// =============================================================================
// Implementation
// =============================================================================

CPURegisters GetRegisters()
{
    CPURegisters regs = {};
    
    // 32-bit general purpose
    regs.eax = reg_eax;
    regs.ebx = reg_ebx;
    regs.ecx = reg_ecx;
    regs.edx = reg_edx;
    regs.esi = reg_esi;
    regs.edi = reg_edi;
    regs.ebp = reg_ebp;
    regs.esp = reg_esp;
    
    // Instruction pointer
    regs.eip = reg_eip;
    
    // Flags
    regs.eflags = reg_flags;
    
    // Segment registers
    regs.cs = SegValue(cs);
    regs.ds = SegValue(ds);
    regs.es = SegValue(es);
    regs.fs = SegValue(fs);
    regs.gs = SegValue(gs);
    regs.ss = SegValue(ss);
    
    // Segment bases
    regs.cs_base = SegPhys(cs);
    regs.ds_base = SegPhys(ds);
    regs.es_base = SegPhys(es);
    regs.fs_base = SegPhys(fs);
    regs.gs_base = SegPhys(gs);
    regs.ss_base = SegPhys(ss);
    
    // Physical IP
    regs.physical_ip = SegPhys(cs) + reg_eip;
    
    // CPU mode
    regs.protected_mode = cpu.pmode;
    regs.v86_mode = GETFLAG(VM) != 0;
    regs.cpl = cpu.cpl;
    
    return regs;
}

uint32_t GetRegister(const char* name)
{
    if (!name) return 0;
    
    // 32-bit registers
    if (strcasecmp(name, "eax") == 0) return reg_eax;
    if (strcasecmp(name, "ebx") == 0) return reg_ebx;
    if (strcasecmp(name, "ecx") == 0) return reg_ecx;
    if (strcasecmp(name, "edx") == 0) return reg_edx;
    if (strcasecmp(name, "esi") == 0) return reg_esi;
    if (strcasecmp(name, "edi") == 0) return reg_edi;
    if (strcasecmp(name, "ebp") == 0) return reg_ebp;
    if (strcasecmp(name, "esp") == 0) return reg_esp;
    if (strcasecmp(name, "eip") == 0) return reg_eip;
    if (strcasecmp(name, "eflags") == 0) return reg_flags;
    
    // 16-bit registers
    if (strcasecmp(name, "ax") == 0) return reg_ax;
    if (strcasecmp(name, "bx") == 0) return reg_bx;
    if (strcasecmp(name, "cx") == 0) return reg_cx;
    if (strcasecmp(name, "dx") == 0) return reg_dx;
    if (strcasecmp(name, "si") == 0) return reg_si;
    if (strcasecmp(name, "di") == 0) return reg_di;
    if (strcasecmp(name, "bp") == 0) return reg_bp;
    if (strcasecmp(name, "sp") == 0) return reg_sp;
    if (strcasecmp(name, "ip") == 0) return reg_ip;
    
    // 8-bit registers
    if (strcasecmp(name, "al") == 0) return reg_al;
    if (strcasecmp(name, "ah") == 0) return reg_ah;
    if (strcasecmp(name, "bl") == 0) return reg_bl;
    if (strcasecmp(name, "bh") == 0) return reg_bh;
    if (strcasecmp(name, "cl") == 0) return reg_cl;
    if (strcasecmp(name, "ch") == 0) return reg_ch;
    if (strcasecmp(name, "dl") == 0) return reg_dl;
    if (strcasecmp(name, "dh") == 0) return reg_dh;
    
    // Segment registers
    if (strcasecmp(name, "cs") == 0) return SegValue(cs);
    if (strcasecmp(name, "ds") == 0) return SegValue(ds);
    if (strcasecmp(name, "es") == 0) return SegValue(es);
    if (strcasecmp(name, "fs") == 0) return SegValue(fs);
    if (strcasecmp(name, "gs") == 0) return SegValue(gs);
    if (strcasecmp(name, "ss") == 0) return SegValue(ss);
    
    return 0;
}

uint32_t GetSegmentBase(const char* segment_name)
{
    if (!segment_name) return 0;
    
    if (strcasecmp(segment_name, "cs") == 0) return SegPhys(cs);
    if (strcasecmp(segment_name, "ds") == 0) return SegPhys(ds);
    if (strcasecmp(segment_name, "es") == 0) return SegPhys(es);
    if (strcasecmp(segment_name, "fs") == 0) return SegPhys(fs);
    if (strcasecmp(segment_name, "gs") == 0) return SegPhys(gs);
    if (strcasecmp(segment_name, "ss") == 0) return SegPhys(ss);
    
    return 0;
}

uint32_t SegmentToPhysical(uint16_t segment, uint16_t offset)
{
    // In real mode, physical = segment * 16 + offset
    // In protected mode, this would need to look up the descriptor
    // For simplicity, we do real-mode calculation here
    // DOSBox handles protected mode internally via SegPhys macros
    return (static_cast<uint32_t>(segment) << 4) + offset;
}

// =============================================================================
// Flag Accessors
// =============================================================================

bool GetCarryFlag()     { return GETFLAG(CF) != 0; }
bool GetZeroFlag()      { return GETFLAG(ZF) != 0; }
bool GetSignFlag()      { return GETFLAG(SF) != 0; }
bool GetOverflowFlag()  { return GETFLAG(OF) != 0; }
bool GetParityFlag()    { return GETFLAG(PF) != 0; }
bool GetAuxCarryFlag()  { return GETFLAG(AF) != 0; }
bool GetDirectionFlag() { return GETFLAG(DF) != 0; }
bool GetInterruptFlag() { return GETFLAG(IF) != 0; }
bool GetTrapFlag()      { return GETFLAG(TF) != 0; }

std::string FormatFlags()
{
    std::ostringstream ss;
    ss << (GetCarryFlag() ? 'C' : '-');
    ss << (GetZeroFlag() ? 'Z' : '-');
    ss << (GetSignFlag() ? 'S' : '-');
    ss << (GetOverflowFlag() ? 'O' : '-');
    ss << (GetParityFlag() ? 'P' : '-');
    ss << (GetAuxCarryFlag() ? 'A' : '-');
    ss << (GetDirectionFlag() ? 'D' : '-');
    ss << (GetInterruptFlag() ? 'I' : '-');
    return ss.str();
}

// =============================================================================
// Register Names
// =============================================================================

const char** GetRegisterNames()
{
    return s_register_names;
}

size_t GetRegisterCount()
{
    size_t count = 0;
    while (s_register_names[count]) count++;
    return count;
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
