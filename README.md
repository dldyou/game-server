# C++ Real-time Multiplayer Game Server

C++ 기반 실시간 멀티플레이어 게임 서버 프로젝트입니다.

Docker와 CMake를 활용하여 빌드 및 실행 환경을 구성하고, `epoll` 기반 non-blocking TCP 서버를 구현하여 클라이언트와의 통신을 처리합니다. 

또한, 세션 관리, 패킷 처리, 게임룸 시스템이 포함되며 이후 채팅 기능과 Tick 기반 게임 루프를 구현할 예정입니다.

## Goals

- C++ 기반 TCP 서버 구현
- Linux `epoll` 기반 이벤트 처리
- non-blocking socket 통신
- 클라이언트 세션 관리
- 패킷 구조 설계 및 파싱
- 로그인 및 인증 처리
- 채팅 기능
- 게임 룸 생성 / 입장 / 퇴장
- Tick 기반 게임 상태 업데이트
- Docker 기반 빌드 및 실행 환경 구성

## Current Progress

- [x] Dockerfile 작성
- [x] CMakeLists.txt 작성
- [x] 설정 파일 로딩 구조 구현
- [x] epoll 기반 non-blocking TCP 서버 구현
- [x] 클라이언트 세션 관리
- [x] 패킷 구조 설계 및 파싱
- [x] 로그인 및 인증 처리 구현
- [ ] 채팅 기능 구현
- [ ] 게임 룸 시스템 구현
- [ ] Tick 기반 게임 루프 구현
- [ ] 유닛 테스트 작성
- [ ] 문서화 및 코드 정리

## Tech Stack

- Language: C++23
- Build: CMake
- Network: POSIX Socket, epoll
- Platform: Ubuntu 24.04
- Environment: Docker, Docker Compose

## Project Structure

```bash
game-server/                        # project root
├── config/                         # config files
├── src/                            # source files
│   ├── auth/
│   │   ├── AuthResult.hpp          # Login Result, Authentication
│   │   ├── LoginProtocol.cpp
│   │   └── LoginProtocol.hpp       # protocol associated to Login
│   ├── chat/
│   │   ├── ChatProtocol.cpp        # protocol associated to Chat
│   │   └── ChatProtocol.hpp
│   ├── rooms/
│   │   ├── Room.cpp                # Room object
│   │   ├── Room.hpp
│   │   ├── RoomManager.cpp         # handle Room
│   │   ├── RoomManager.hpp
│   │   ├── RoomProtocol.cpp        # protocol associated to Room
│   │   └── RoomProtocol.hpp
│   ├── users/
│   │   ├── User.hpp                # User object
│   │   ├── UserManager.cpp         # User Manager
│   │   └── UserManager.hpp
│   ├── networks/
│   │   ├── Packet.hpp              # Packet Type & Packet
│   │   ├── PacketParser.cpp        # packet to content
│   │   ├── PacketParser.hpp
│   │   ├── PacketSerializer.cpp    # content to packet
│   │   ├── PacketSerializer.hpp
│   │   ├── Session.cpp
│   │   └── Session.hpp             # client sessions
│   ├── utils/                      # util functions
│   │   ├── StringUtils.cpp         # utils for string
│   │   └── StringUtils.hpp
│   ├── config/
│   │   ├── Config.cpp              # parse config files
│   │   └── Config.hpp
│   ├── Server.cpp                  # TCP server
│   ├── Server.hpp
│   └── main.cpp                    # main app
├── .dockerignore
├── .gitignore
├── build.sh
├── CMakeLists.txt
├── docker-compose.yml
├── Dockerfile
└── tools/
    └── TestClient.cpp              # client for test server

```

## Configuration

게임 서버 설정은 `config/config.ini` 파일에서 관리됩니다. 현재는 서버 호스트, 포트, 최대 클라이언트 수, 버퍼 크기 등의 설정이 포함되어 있습니다.

```ini
[server]
host=0.0.0.0
port=8080
max_clients=1024
buffer_size=4096
```

| **Key** | **Description** |
|---------|-----------------|
| `host` | 서버가 바인딩할 IP 주소 |
| `port` | 서버가 수신 대기할 포트 번호 |
| `max_clients` | 동시에 연결할 수 있는 최대 클라이언트 수 |
| `buffer_size` | 클라이언트로부터 데이터를 읽을 때 사용할 버퍼 크기 |

## Build & Run
### Build with Local CMake

```bash
chmod +x ./build.sh
./build.sh
```

### Run with Local CMake

```bash
# Run the server
./build/game-server
# Run the test client
./build/test-client
```

### Run with Docker Compose

```bash
docker compose up --build
```

### Stop the server

```bash
docker compose down
```

### Run in background

```bash
docker compose up -d --build
```

### View logs

```bash
docker compose logs -f
```

## Packet Structure

packet 구조는 다음과 같이 정의됩니다.

| **Field** | **Size (bytes)** | **Description** |
|-----------|-----------------|-----------------|
| `packet_size` | 2 | 패킷 전체 크기 (헤더 + 페이로드) |
| `packet_type` | 2 | 패킷 타입 (예: 로그인, 채팅, 이동 등) |
| `sequence` | 4 | 패킷 순서 번호 (클라이언트와 서버 간 동기화 용도) |
| `payload` | variable | 실제 데이터 (예: 메시지, 좌표 등) |

패킷의 헤더는 최대 8B이며 패킷의 최대 크기는 16KB로 제한됩니다.

nework packet으로 전송되는 데이터는 앞에 `packet_size`를 붙여서 전송됩니다. `packet_size`는 `packet_type`, `sequence`, `payload`를 포함한 전체 패킷 크기를 나타냅니다.
