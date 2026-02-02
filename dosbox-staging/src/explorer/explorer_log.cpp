// SPDX-License-Identifier: MIT
// Explorer lightweight host-side logging

#include "explorer_log.h"

#include "explorer.h"
#include "explorer_data.h"
#include "explorer_trace.h"
#include "explorer_memory.h"

#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Explorer {

static bool g_log_enabled = false;
static std::string g_log_path;
static std::ofstream g_log;
static uint64_t g_log_every_instructions = 1'000'000;
static uint64_t g_last_logged_instructions = 0;

static bool env_truthy(const char* s)
{
	if (!s) {
		return false;
	}
	std::string v{s};
	for (auto& c : v) {
		c = static_cast<char>(::tolower(c));
	}
	return (v == "1" || v == "true" || v == "yes" || v == "on");
}

static uint64_t env_u64(const char* s, const uint64_t fallback)
{
	if (!s || !*s) {
		return fallback;
	}
	char* end = nullptr;
	errno = 0;
	const auto v = std::strtoull(s, &end, 10);
	if (errno != 0 || end == s) {
		return fallback;
	}
	return static_cast<uint64_t>(v);
}

static std::string now_string()
{
	using clock = std::chrono::system_clock;
	const auto t = clock::to_time_t(clock::now());
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	std::ostringstream ss;
	ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
	return ss.str();
}

static void write_snapshot_line(const char* reason)
{
	if (!g_log_enabled || !g_log.is_open()) {
		return;
	}

	auto& inst = GetInstrumenter();
	auto& data = GetDataCollector();

	g_log << now_string();
	if (reason && *reason) {
		g_log << " [" << reason << "]";
	}

	g_log << " inst=" << inst.GetInstructionCount();
	g_log << " pc=0x" << std::hex << inst.GetCurrentPC() << std::dec;
	g_log << " run_new_cov=" << inst.GetRunNewBits();
	g_log << " global_new_cov=" << inst.GetGlobalNewBits();
	g_log << " data_accesses=" << data.GetTotalAccesses();
	g_log << " data_run_new=" << data.GetRunNewBits();
	g_log << " data_global_new=" << data.GetGlobalNewBits();
	g_log << " stalled=" << (inst.IsStalled() ? 1 : 0);
	g_log << " stop=" << (inst.ShouldStop() ? StopReasonName(inst.GetStopReason()) : "no");
	
	// Include trace count
	g_log << " traced=" << GetTracedInstructionCount();
	
	// Include memory info
	g_log << " mem_base=" << (void*)GetMemoryBase();
	g_log << " mem_size=" << GetMemorySize();
	
	g_log << "\n";

	g_log.flush();
}

void Log_InitFromEnv()
{
	if (g_log_enabled) {
		return;
	}

	const auto* log_env = std::getenv("EXPLORER_LOG");
	if (!log_env) {
		return;
	}

	std::string path = log_env;
	if (path.empty() || env_truthy(log_env)) {
		path = "explorer.log";
	}

	g_log_every_instructions = env_u64(std::getenv("EXPLORER_LOG_EVERY"), 1'000'000);
	if (g_log_every_instructions == 0) {
		g_log_every_instructions = 1'000'000;
	}

	g_log.open(path, std::ios::out | std::ios::app);
	if (!g_log.is_open()) {
		return;
	}

	g_log_enabled = true;
	g_log_path = path;
	g_last_logged_instructions = 0;

	g_log << "--- Explorer log started: " << now_string() << " ---\n";
	write_snapshot_line("init");
}

void Log_DumpNow(const char* reason)
{
	write_snapshot_line(reason ? reason : "dump");
}

void Log_OnTick(const uint64_t instruction_count)
{
	if (!g_log_enabled || !g_log.is_open()) {
		return;
	}
	if (instruction_count < g_last_logged_instructions) {
		g_last_logged_instructions = instruction_count;
		return;
	}
	if (instruction_count - g_last_logged_instructions >= g_log_every_instructions) {
		g_last_logged_instructions = instruction_count;
		write_snapshot_line("tick");
	}
}

void Log_Shutdown()
{
	if (!g_log_enabled) {
		return;
	}

	write_snapshot_line("shutdown");
	g_log << "--- Explorer log ended: " << now_string() << " ---\n";
	g_log.flush();
	g_log.close();
	g_log_enabled = false;
	g_log_path.clear();
}

void Log_Event(const char* event_type, const char* message)
{
	if (!g_log_enabled || !g_log.is_open()) {
		return;
	}

	auto& inst = GetInstrumenter();
	
	g_log << now_string();
	g_log << " [" << (event_type ? event_type : "event") << "]";
	g_log << " inst=" << inst.GetInstructionCount();
	if (message && *message) {
		g_log << " " << message;
	}
	g_log << "\n";
	g_log.flush();
}

bool Log_IsEnabled()
{
	return g_log_enabled;
}

std::string Log_GetPath()
{
	return g_log_path;
}

} // namespace Explorer
