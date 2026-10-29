#pragma once


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <functional>

#include <string>
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/fmt/fmt.h>
#pragma warning(pop)

#define WI_USE_LOG WI_DEBUG || WI_RELEASE


namespace wiwa
{
	enum LogLevel
	{
		Trace = 0,
		Info,
		Warning,
		Error,
		Critical,
		Last
	};

	typedef std::function<void(LogLevel, std::string)> LogCallback;
	typedef std::function<void(const std::string&, const std::string&)> LogFunction;

	class Log final
	{
	public:
		Log() = delete;
		~Log() = delete;

		static void init();

		static void log(LogLevel level, const std::string& title, const std::string& message);

		static void log_trace(const std::string& title, const std::string& message);

		static void log_info(const std::string& title, const std::string& message);

		static void log_warning(const std::string& title, const std::string& message);

		static void log_error(const std::string& title, const std::string& message);

		static void log_critical(const std::string& title, const std::string& message);

		static void set_log_callback(LogCallback callback) { s_Callback = callback; }

	private:
		static void on_log_event(LogLevel level);

		inline static std::shared_ptr<spdlog::logger> s_Logger;
		inline static LogCallback s_Callback;
		static std::unordered_map<LogLevel, LogFunction> s_LogMap;
	};
}

template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
}

template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
	return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
	return os << glm::to_string(quaternion);
}


#define FMT(...) fmt::format(__VA_ARGS__)

#if WI_USE_LOG

#define WI_LOG(level, x, ...) wiwa::Log::log(LogLevel::level, x, FMT(__VA_ARGS__))
#define WI_F_LOG(level, x, ...) WI_LOG(level, __FUNCTION__, __VA_ARGS__)

#define WI_TRACE(x, ...) wiwa::Log::log_trace(x, FMT(__VA_ARGS__))
#define WI_F_TRACE(...) WI_TRACE(__FUNCTION__, __VA_ARGS__)

#define WI_INFO(x, ...) wiwa::Log::log_info(x, FMT(__VA_ARGS__))
#define WI_F_INFO(...) WI_F_INFO(__FUNCTION__, __VA_ARGS__)

#define WI_WARN(x, ...) wiwa::Log::log_warning(x, FMT(__VA_ARGS__))
#define WI_F_WARN(...) WI_WARN(__FUNCTION__, __VA_ARGS__)

#define WI_WARN(x, ...) wiwa::Log::log_warning(x, FMT(__VA_ARGS__))
#define WI_F_WARN(...) WI_WARN(__FUNCTION__, __VA_ARGS__)

#define WI_ERROR(x, ...) wiwa::Log::log_error(x, FMT(__VA_ARGS__))
#define WI_F_ERROR(...) WI_ERROR(__FUNCTION__, __VA_ARGS__)

#define WI_CRITICAL(x, ...) wiwa::Log::log_critical(x, FMT(__VA_ARGS__))
#define WI_F_CRITICAL(...) WI_CRITICAL(__FUNCTION__, __VA_ARGS__)


#else
#define WI_LOG(level, x, ...) 
#define WI_F_LOG(level, x, ...) 

#define WI_TRACE(x, ...)
#define WI_F_TRACE(...)

#define WI_INFO(x, ...)
#define WI_F_INFO(...) 

#define WI_WARN(x, ...)
#define WI_F_WARN(...) 

#define WI_WARN(x, ...)
#define WI_F_WARN(...) 

#define WI_ERROR(x, ...) 
#define WI_F_ERROR(...)

#define WI_CRITICAL(x, ...) 
#define WI_F_CRITICAL(...)

#endif

