#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct User {
    std::uint64_t id;
    std::string handle;
    std::string login_id;
    std::string password;
};