#include "utils/StringUtils.hpp"

namespace string_utils {
    std::string ltrim(const std::string& str) {
        const std::size_t start_pos = str.find_first_not_of(" \t\r\n");
        if (start_pos == std::string::npos) {
            return "";
        }
        return str.substr(start_pos);
    }

    std::string rtrim(const std::string& str) {
        const std::size_t end_pos = str.find_last_not_of(" \t\r\n");
        if (end_pos == std::string::npos) {
            return "";
        }
        return str.substr(0, end_pos + 1);
    }

    std::string trim(const std::string& str) {
        return rtrim(ltrim(str));
    }
}