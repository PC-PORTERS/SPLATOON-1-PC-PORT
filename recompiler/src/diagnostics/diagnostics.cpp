#include "diagnostics.hpp"
#include "rebrewu/logger.hpp"

namespace rebrewu::diagnostics {

void DiagEngine::error(std::string msg) {
    ++m_error_count;
    m_entries.push_back({Severity::Error, std::move(msg)});
    if (m_stderr_echo) RBREW_LOG_ERROR("diag", "%s", m_entries.back().message.c_str());
}

void DiagEngine::warning(std::string msg) {
    ++m_warning_count;
    m_entries.push_back({Severity::Warning, std::move(msg)});
    if (m_stderr_echo) RBREW_LOG_WARN("diag", "%s", m_entries.back().message.c_str());
}

void DiagEngine::note(std::string msg) {
    m_entries.push_back({Severity::Note, std::move(msg)});
    if (m_stderr_echo) RBREW_LOG_INFO("diag", "%s", m_entries.back().message.c_str());
}

} // namespace rebrewu::diagnostics
