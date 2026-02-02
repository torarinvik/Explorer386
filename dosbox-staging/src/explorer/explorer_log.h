// SPDX-License-Identifier: MIT
// Explorer lightweight host-side logging

#ifndef EXPLORER_LOG_H
#define EXPLORER_LOG_H

#include <cstdint>
#include <string>

namespace Explorer {

// Initializes logging from environment variables.
//
// Supported variables:
//   EXPLORER_LOG          If set, enables logging. If set to "1" or empty,
//                         logs to "explorer.log" in the current working dir.
//                         Otherwise, treated as a file path.
//   EXPLORER_LOG_EVERY    Instruction interval between periodic log lines.
//                         Defaults to 1000000.
void Log_InitFromEnv();

// Writes one snapshot line immediately (if enabled).
void Log_DumpNow(const char* reason = nullptr);

// Called from the instrumentation tick path.
void Log_OnTick(uint64_t instruction_count);

// Flushes and closes the log.
void Log_Shutdown();

// Logs a custom event message.
void Log_Event(const char* event_type, const char* message);

bool Log_IsEnabled();
std::string Log_GetPath();

} // namespace Explorer

#endif // EXPLORER_LOG_H
