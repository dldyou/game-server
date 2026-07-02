# C++ Real-time Multiplayer Game Server

C++23 기반 실시간 멀티플레이어 게임 서버 프로젝트입니다. Linux `epoll`, non-blocking socket, `eventfd`를 사용해 TCP 클라이언트 연결을 처리하고, 로그인, 세션, 패킷 파싱, 룸, 채팅, ready/start 흐름을 구현합니다.

현재는 실제 게임 tick loop 이전 단계까지 구현되어 있으며, `RoomStatus::Playing` 전환 시 `GameManager`가 game instance를 생성하는 골격까지 연결되어 있습니다.

## Goals

- C++ 기반 TCP 서버 구현
- Linux `epoll` 기반 이벤트 처리
- non-blocking socket 통신
- 클라이언트 세션 관리
- 패킷 구조 설계, 파싱, 직렬화
- 로그인 및 인증 처리
- 게임 룸 생성 / 목록 / 입장 / 퇴장
- 룸 owner, ready, start 상태 관리
- 룸 단위 채팅 브로드캐스트
- Tick 기반 게임 상태 업데이트
- Docker 기반 빌드 및 실행 환경 구성

## Current Progress

- [x] Dockerfile 및 Docker Compose 구성
- [x] CMake 빌드 구성
- [x] 설정 파일 로딩 구조 구현
- [x] `epoll` 기반 non-blocking TCP 서버 구현
- [x] `eventfd` 기반 서버 wakeup/shutdown 처리
- [x] 클라이언트 세션 관리 및 cleanup 처리
- [x] 패킷 파서 / 직렬화 구현
- [x] 로그인 및 인증 처리 구현
- [x] 게임 룸 생성 / 목록 / 입장 / 퇴장 구현
- [x] 룸 owner / ready / start 상태 구현
- [x] 룸 단위 채팅 구현
- [x] 로컬 테스트용 `test-client` 구현
- [ ] Tick 기반 게임 루프 구현
- [x] `GameManager` 및 게임 인스턴스 골격 구현
- [ ] 자동화 테스트 작성
- [ ] 인증 정보 저장 방식 개선

## Tech Stack

- Language: C++23
- Build: CMake
- Network: POSIX Socket, `epoll`, `eventfd`
- Platform: Linux / Ubuntu 24.04
- Environment: Docker, Docker Compose

## Project Structure

```text
game-server/
├── config/
│   └── config.ini                  # server host, port, max clients, buffer size
├── src/
│   ├── auth/                       # login request/response protocol and auth result
│   ├── chat/                       # chat payload protocol
│   ├── config/                     # config file parser
│   ├── game/                       # game instance and game manager skeleton
│   ├── networks/                   # packet, parser, serializer, session
│   ├── rooms/                      # room state, room manager, room protocol
│   ├── users/                      # in-memory user manager
│   ├── utils/                      # utility functions
│   ├── Server.cpp                  # epoll TCP server
│   ├── Server.hpp
│   └── main.cpp
├── tools/
│   └── TestClient.cpp              # local interactive test client
├── CMakeLists.txt
├── Dockerfile                      # multi-stage server image build
├── docker-compose.yml
├── build.sh
└── README.md
```

## Configuration

Server configuration is loaded from `config/config.ini`.

```ini
[server]
host=0.0.0.0
port=8080
max_clients=1024
buffer_size=4096
```

| Key | Description |
| --- | --- |
| `host` | 서버가 바인딩할 IP 주소 |
| `port` | 서버가 수신 대기할 포트 번호 |
| `max_clients` | 동시에 연결할 수 있는 최대 클라이언트 수 |
| `buffer_size` | 클라이언트 recv 버퍼 크기 |

## Build & Run

### Local Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

### Run Server

```bash
./build/game-server
```

### Run Test Client

```bash
./build/test-client 127.0.0.1 8080
```

Available test users are seeded at server startup:

| Login ID | Password | Handle |
| --- | --- | --- |
| `test` | `password` | `tester` |
| `test2` | `password` | `tester2` |
| `test3` | `password` | `tester3` |
| `test4` | `password` | `tester4` |

Example test-client flow:

```text
login test password
create-room alpha 3
ready
start-game
```

Use another terminal for a second client:

```text
login test2 password
list-rooms
join-room 1
ready
chat hello
```

### Docker Compose

```bash
docker compose up --build
```

```bash
docker compose logs -f
```

```bash
docker compose down
```

The Dockerfile uses a multi-stage build. The runtime image includes only the `game-server` binary and config file; `test-client` is for local development builds.

### Strict Warning Build

```bash
cmake -S . -B build-warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Wconversion'
cmake --build build-warnings --parallel
```

## Test Client Commands

| Command | Description |
| --- | --- |
| `ping [message]` | Send `C2S_PING` |
| `login <id> <password>` | Authenticate session |
| `create-room <name> <max_players>` | Create and enter a room |
| `list-rooms` | Request lobby room list |
| `join-room <room_id>` | Join a waiting room |
| `leave-room` | Leave current room |
| `ready` | Set ready state to true |
| `unready` | Set ready state to false |
| `start-game` | Owner starts the game when all players are ready |
| `chat <message>` | Broadcast chat to players in the same room |
| `send <type> [payload]` | Send an arbitrary packet for manual testing |
| `wait [ms]` | Pause scripted input |

## Packet Structure

All packet fields use network byte order.

| Field | Size | Description |
| --- | --- | --- |
| `packet_size` | 2 bytes | Total packet size, including header and payload |
| `packet_type` | 2 bytes | Packet type |
| `sequence` | 4 bytes | Request/response sequence number |
| `payload` | variable | Packet-specific data |

The packet header is 8 bytes. Maximum packet size is limited to 16 KiB.

## Implemented Protocols

| Packet | Payload |
| --- | --- |
| `C2S_LOGIN` | `[id_length:u16][password_length:u16][id][password]` |
| `S2C_LOGIN_RESULT` | `[result:u16][user_id:u64][handle_length:u16][handle]` |
| `C2S_CREATE_ROOM` | `[name_length:u16][max_players:u16][name]` |
| `C2S_JOIN_ROOM` | `[room_id:u32]` |
| `C2S_LEAVE_ROOM` | empty |
| `C2S_ROOM_LIST` | empty |
| `S2C_ROOM_LIST` | `[room_count:u16] rooms...` |
| `S2C_ROOM_STATE` | room id, status, owner id, room name, max players, players with ready state |
| `C2S_SET_READY` | `[ready:u8]` |
| `C2S_START_GAME` | empty |
| `C2S_CHAT` | `[message_length:u16][message]` |
| `S2C_CHAT` | `[user_id:u64][handle_length:u16][handle][message_length:u16][message]` |

## Current Limitations

- User data is seeded in memory at server startup.
- Passwords are currently compared as plain text and should be replaced with hashing.
- `RoomStatus::Playing` creates a `Game` through `GameManager`, but gameplay tick, input processing, and snapshots are not implemented yet.
- Automated tests are not configured yet; use `test-client` and strict warning builds for smoke validation.