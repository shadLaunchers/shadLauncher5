// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/types.h"

struct User {
    s32 user_id = -1;
    std::string user_name = "";
    u32 user_color = 1;
    int player_index = 0; // 1-4
    bool logged_in = false;
    std::string device_guid = "";
    std::string device_serial = "";
    std::string device_path = "";
    // Carried, not used here. The emulator's copy of this struct has them
    // (src/core/user_manager.h, "kept ... so that users.json stays
    // interchangeable with shadPS4's") and writes them into users.json. A
    // field missing from the serializer below is not merely unread -- it is
    // dropped on the next Save(), so leaving these out meant every launcher
    // save quietly reset the ShadNet account and the NP profile of every user.
    std::string shadnet_npid = "";
    std::string shadnet_password = "";
    std::string shadnet_token = "";
    std::string shadnet_email = "";
    bool shadnet_enabled = false;
    std::string np_country = "us";               // ISO 3166-1 alpha-2
    std::string np_language = "en";              // ISO 639-1
    u8 np_age = 30;                              // 0..127
    std::string np_date_of_birth = "1994-01-01"; // ISO 8601 "YYYY-MM-DD"
};

struct Users {
    std::vector<User> user{};
    std::string commit_hash{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(User, user_id, user_color, user_name, player_index,
                                                device_guid, device_serial, device_path,
                                                shadnet_npid, shadnet_password, shadnet_token,
                                                shadnet_email, shadnet_enabled, np_country,
                                                np_language, np_age, np_date_of_birth)
// WITH_DEFAULT, like the emulator's: the plain macro throws when a key is
// missing, so a users.json without "commit_hash" -- one the emulator is
// perfectly happy to read -- would fail to load here.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Users, user, commit_hash)

using LoggedInUsers = std::array<User*, 4>;

class UserManager {
public:
    UserManager() = default;

    bool AddUser(const User& user);
    bool RemoveUser(s32 user_id);
    bool RenameUser(s32 user_id, const std::string& new_name);
    User* GetUserByID(s32 user_id);
    User* GetUserByPlayerIndex(s32 index);
    const std::vector<User>& GetAllUsers() const;
    Users CreateDefaultUsers();
    bool SetDefaultUser(u32 user_id);
    User GetDefaultUser();
    void SetControllerPort(u32 user_id, int port);
    // Forgets which physical device drives this user's port, so the port is
    // filled in plug order again. The emulator writes the pin (see the
    // device_* fields above); nothing else here ever sets one, and without
    // this a pad pinned to the wrong user could only be undone by editing
    // users.json by hand.
    void ClearPinnedDevice(u32 user_id);
    std::vector<User> GetValidUsers() const;
    LoggedInUsers GetLoggedInUsers() const;

    Users& GetUsers() {
        return m_users;
    }
    const Users& GetUsers() const {
        return m_users;
    }

    bool Save() const;

private:
    Users m_users;
    LoggedInUsers logged_in_users{};
};
