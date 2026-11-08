#pragma once

#include <string>

namespace crashlog {

// Appends one line to a log file. Exposed for headless tests.
bool WriteLogLine(const std::wstring& path, const std::wstring& line);

// Installs the process-wide unhandled-exception filter. Call once at startup.
void Install();

}  // namespace crashlog
