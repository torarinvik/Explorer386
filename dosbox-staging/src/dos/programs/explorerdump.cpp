// SPDX-License-Identifier: GPL-2.0-or-later

#include "explorerdump.h"

#ifdef EXPLORER_ENABLED
#include "explorer/explorer_api.h"
#include "explorer/explorer_log.h"
#endif

#include <string>

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

	// Print current status to DOS stdout
	WriteOut_NoParsing(Explorer::GetStatusString());

	// Optional host-side dump file path
	if (cmd->GetCount() > 0) {
		if (cmd->FindCommand(1, temp_line)) {
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
	        "  [color=light-green]expdump[reset]\\n"
	        "  [color=light-green]expdump[reset] [color=light-cyan]HOST_PATH[reset]\\n"
	        "  [color=light-green]xdump[reset]\\n"
	        "\\n"
	        "Notes:\\n"
	        "  - Prints a status summary to the DOS console.\\n"
	        "  - If HOST_PATH is provided, writes a binary dump of the coverage map\\n"
	        "    and basic stats to that host path.\\n"
	        "  - Set EXPLORER_ENABLE=1 before starting DOSBox to enable Explorer.\\n");
}
