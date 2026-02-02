// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_PROGRAM_EXPLORERDUMP_H
#define DOSBOX_PROGRAM_EXPLORERDUMP_H

#include "dos/programs.h"

class EXPLORERDUMP final : public Program {
public:
	EXPLORERDUMP()
	{
		AddMessages();
		help_detail = {HELP_Filter::Common,
		               HELP_Category::Dosbox,
		               HELP_CmdType::Program,
		               "EXPDUMP"};
	}
	void Run(void) override;

private:
	static void AddMessages();
};

#endif // DOSBOX_PROGRAM_EXPLORERDUMP_H
