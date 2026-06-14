# C++ Real-time Multiplayer Game Server

C++17 기반 실시간 멀티플레이어 게임 서버 프로젝트입니다.

현재는 Docker, CMake, 설정 파일 로딩 구조를 구성하고 있으며, 이후 `epoll` 기반 non-blocking TCP 서버, 세션 관리, 패킷 처리, 게임룸 시스템, Tick 기반 게임 루프 등을 구현할 예정입니다.

## Goals

- C++ 기반 TCP 서버 구현
- Linux `epoll` 기반 이벤트 처리
- non-blocking socket 통신
- 클라이언트 세션 관리
- 패킷 구조 설계 및 파싱
- 게임 룸 생성 / 입장 / 퇴장
- Tick 기반 게임 상태 업데이트
- Docker 기반 빌드 및 실행 환경 구성

## Tech Stack

- Language: C++17
- Build: CMake
- Network: POSIX Socket, epoll
- Platform: Ubuntu 24.04
- Environment: Docker, Docker Compose

## Project Structure

```bash
game-server
├── CMakeLists.txt
├── Dockerfile
├── README.md
├── config
│   └── config.ini
├── docker-compose.yml
├── docs
└── src
    ├── config
    │   ├── Config.cpp
    │   └── Config.hpp
    ├── main.cpp
    └── utils
        ├── StringUtils.cpp
        └── StringUtils.hpp
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

## Current Status

- [x] Dockerfile 작성
- [x] CMakeLists.txt 작성
- [x] 설정 파일 로딩 구조 구현
- [ ] epoll 기반 non-blocking TCP 서버 구현
- [ ] 클라이언트 세션 관리
- [ ] 패킷 구조 설계 및 파싱
- [ ] 게임 룸 시스템 구현
- [ ] Tick 기반 게임 루프 구현
- [ ] 유닛 테스트 작성
- [ ] 문서화 및 코드 정리

## Roadmap

1. **Basic Server Setup**
- Load server configuration from `config.ini`
- Initialize TCP server socket
- Bind and listen on configured host and port

2. **epoll Event Loop**
- Create epoll instance
- Register server socket for incoming connections
- Accept multiple clients
- Handle client read and write events

3. **Session Management**
- Create `Session` class to manage individual client connections
- Store client socket fds
- Manage receive and send buffers
- Handle client disconnections

4. **Packet**
- Define packet structure (header + payload)
- Parse incoming packets
- Add basic packet types:
    - Login
    - Chat
    - Move
    - Attack

5. **Game Room**
- Create and join rooms
- Broadcast messages to room members
- Manage room state

6. **Game Loop**
- Add server tick loop
- Update game state at fixed intervals
- Broadcast synchronized game state to clients