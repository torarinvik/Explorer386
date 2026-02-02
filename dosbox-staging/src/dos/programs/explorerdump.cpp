// SPDX-License-Identifier: GPL-2.0-or-later

#include "explorerdump.h"

#ifdef EXPLORER_ENABLED
#include "explorer/explorer_api.h"
#include "explorer/explorer_log.h"
#include "explorer/explorer_cpu.h"
#include "explorer/explorer_memory.h"
#include "explorer/explorer_trace.h"
#endif

#include <string>
#include <sstream>
#include <iomanip>

void EXPLORERDUMP::Run(void)
{
#ifdef EXPLORER_ENABLED
	if (HelpRequested()) {
		WriteOut(MSG_Get("PROGRAM_EXPDUMP_HELP_LONG"));
		return;
	}

	if (!Explorer::IsInitialized()) {
		WriteOut("Explorer is not initialized. Set EXPLORER_ENABLE=1 before starting DOSBox.\n");
		return;
	}

	// Check for subcommands
	std::string subcmd;
	if (cmd->FindCommand(1, temp_line)) {
		subcmd = temp_line;
		// Convert to lowercase
		for (auto& c : subcmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	if (subcmd == "regs" || subcmd == "registers" || subcmd == "cpu") {
		// Dump CPU registers
		auto regs = Explorer::GetRegisters();
		WriteOut("CPU Registers:\n");
		WriteOut("  EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X\n", 
		         regs.eax, regs.ebx, regs.ecx, regs.edx);
		WriteOut("  ESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X\n",
		         regs.esi, regs.edi, regs.ebp, regs.esp);
		WriteOut("  EIP=%08X  EFLAGS=%08X [%s]\n",
		         regs.eip, regs.eflags, Explorer::FormatFlags().c_str());
		WriteOut("  CS=%04X  DS=%04X  ES=%04X  FS=%04X  GS=%04X  SS=%04X\n",
		         regs.cs, regs.ds, regs.es, regs.fs, regs.gs, regs.ss);
		WriteOut("  Mode: %s  CPL=%d\n",
		         regs.protected_mode ? (regs.v86_mode ? "V86" : "Protected") : "Real",
		         regs.cpl);
		return;
	}

	if (subcmd == "mem" || subcmd == "memory") {
		// Dump memory info
		WriteOut("Memory Info:\n");
		WriteOut("  Base: %p\n", Explorer::GetMemoryBase());
		WriteOut("  Size: %zu bytes (%zu KB)\n", 
		         Explorer::GetMemorySize(), 
		         Explorer::GetMemorySize() / 1024);
		
		// VRAM info
		auto vram = Explorer::GetVRAMInfo();
		WriteOut("VRAM Info:\n");
		WriteOut("  Mode: %02Xh (%s)\n", vram.mode, vram.text_mode ? "text" : "graphics");
		WriteOut("  Size: %zu bytes\n", vram.size);
		if (vram.text_mode) {
			WriteOut("  Text: %dx%d\n", vram.text_cols, vram.text_rows);
		} else {
			WriteOut("  Resolution: %dx%d @ %d bpp\n", vram.width, vram.height, vram.bpp);
		}
		return;
	}

	if (subcmd == "trace") {
		// Dump recent trace
		WriteOut("Recent Instruction Trace (last 16):\n");
		auto trace = Explorer::GetTraceVector(16);
		for (const auto& entry : trace) {
			WriteOut("  %08X: ", entry.phys_pc);
			for (int i = 0; i < std::min<int>(entry.len, 8); i++) {
				WriteOut("%02X ", entry.bytes[i]);
			}
			WriteOut("\n");
		}
		WriteOut("Total traced: %llu\n", Explorer::GetTracedInstructionCount());
		return;
	}

	if (subcmd == "screen") {
		// Dump text screen
		std::string screen = Explorer::GetTextScreenString();
		WriteOut("Text Screen:\n%s\n", screen.c_str());
		return;
	}

	// Default: print status summary
	WriteOut_NoParsing(Explorer::GetStatusString());

	// Optional host-side dump file path
	if (cmd->GetCount() > 1) {
		if (cmd->FindCommand(2, temp_line)) {
			const std::string path = temp_line;
			if (Explorer::DumpState(path)) {
				WriteOut("Wrote Explorer state to '%s'\n", path.c_str());
			} else {
				WriteOut("Failed to write Explorer state to '%s'\n", path.c_str());
			}
		}
	}

	// Also append a snapshot to the Explorer log (if enabled)
	Explorer::Log_DumpNow("command");
#else
	WriteOut("Explorer support not compiled in this build.\n");
#endif
}

void EXPLORERDUMP::AddMessages()
{
	MSG_Add("PROGRAM_EXPDUMP_HELP_LONG",
	        "Dump Explorer instrumentation status.\\n"
	        "\\n"
	        "Usage:\\n"
	        "  [color=light-green]expdump[reset]            - Show status summary\\n"
	        "  [color=light-green]expdump[reset] [color=light-cyan]regs[reset]        - Show CPU registers\\n"
	        "  [color=light-green]expdump[reset] [color=light-cyan]mem[reset]         - Show memory info\\n"
	        "  [color=light-green]expdump[reset] [color=light-cyan]trace[reset]       - Show recent instructions\\n"
	        "  [color=light-green]expdump[reset] [color=light-cyan]screen[reset]      - Dump text screen\\n"
	        "  [color=light-green]expdump[reset] [color=light-cyan]HOST_PATH[reset]   - Save state to host file\\n"
	        "\\n"
	        "Notes:\\n"
	        "  - Set EXPLORER_ENABLE=1 before starting DOSBox to enable Explorer.\\n");
}
