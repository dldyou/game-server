#pragma once

#include <cstdint>

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t size;
    uint16_t type;
    uint32_t sequence;
};
#pragma pack(pop);

enum PacketType {
    CS2_PING = 1,
    S2C_PONG = 2,

    C2S_LOGIN = 100,
    S2C_LOGIN_RESULT = 101,

    C2S_CREATE_ROOM = 200,
    C2S_JOIN_ROOM = 201,
    C2S_LEAVE_ROOM = 202,
    S2C_ROOM_CREATED = 203,
    S2C_ROOM_JOINED = 204,
    S2C_ROOM_LEFT = 205,
    S2C_ROOM_STATE = 206,

    C2S_CHAT = 300,
    S2C_CHAT = 201,

    C2S_MOVE = 400,
    C2S_ATTACK = 401,
    S2C_SNAPSHOT = 402,
    S2C_PLAYER_MOVED = 403,
    S2C_ATTACK = 404,

    S2C_ERROR = 900,
};