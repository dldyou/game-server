#include "UserManager.hpp"

// TODO
bool UserManager::verifyPassword(
    std::string_view password,
    std::string_view passwordHash
) const {

}

bool UserManager::load(const std::string& path) {

}

bool UserManager::createUser(User user) {

}

AuthenticationResult UserManager::authenticate(
    std::string_view loginId,
    std::string_view password
) const {

}