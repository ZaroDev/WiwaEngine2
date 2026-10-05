#include <pch.h>
#include "log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace wiwa
{
	std::unordered_map<LogLevel, LogFunction> Log::s_LogMap =
	{
		{LogLevel::Trace, Log::log_trace},
		{LogLevel::Info, Log::log_info},
		{LogLevel::Warning, Log::log_warning},
		{LogLevel::Error, Log::log_error},
		{LogLevel::Critical, Log::log_critical}
	};

	void Log::init()
	{
		std::vector<spdlog::sink_ptr> logSinks;
		logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("wiwa.log", true));

		logSinks[0]->set_pattern("%^[%T] %n: %v%$");
		logSinks[1]->set_pattern("[%T] [%l] %n: %v");

		s_Logger = std::make_shared<spdlog::logger>("Wiwa", begin(logSinks), end(logSinks));
		spdlog::register_logger(s_Logger);
		s_Logger->set_level(spdlog::level::trace);
		s_Logger->flush_on(spdlog::level::trace);
	}
	void Log::log(LogLevel level, const std::string& title, const std::string& message)
	{
		s_LogMap[level](title, message);
	}
	void Log::log_trace(const std::string& title, const std::string& message)
	{
		s_Logger->trace("{} - {}", title, message);
		on_log_event(LogLevel::Trace);
	}
	void Log::log_info(const std::string& title, const std::string& message)
	{
		s_Logger->info("{} - {}", title, message);
		on_log_event(LogLevel::Info);
	}
	void Log::log_warning(const std::string& title, const std::string& message)
	{
		s_Logger->warn("{} - {}", title, message);
		on_log_event(LogLevel::Warning);
	}
	void Log::log_error(const std::string& title, const std::string& message)
	{
		s_Logger->error("{} - {}", title, message);
		on_log_event(LogLevel::Error);
	}
	void Log::log_critical(const std::string& title, const std::string& message)
	{
		s_Logger->critical("{} - {}", title, message);
		on_log_event(LogLevel::Critical);
	}
	void Log::on_log_event(LogLevel level)
	{
		if (s_Callback)
		{
			const std::vector<spdlog::sink_ptr>& sinks = s_Logger.get()->sinks();
			s_Callback(level, std::string(sinks[0].get()->get_last()));
		}
	}
}