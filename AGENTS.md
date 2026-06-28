# Repository Guidelines

## Project Structure & Module Organization

This C++23 Linux game server uses non-blocking sockets, `epoll`, and
`eventfd`.

```text
game-server/                        # project root
├── config/                         # config files
├── src/                            # source files
│   ├── auth/
│   │   ├── AuthResult.hpp          # Login Result, Authentication
│   │   ├── LoginProtocol.cpp
│   │   └── LoginProtocol.hpp       # protocol associated to login
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
├── CMakeLists.txt
├── docker-compose.yml
├── Dockerfile
└── tools/
    └── TestClient.cpp              # client for test server

```

Keep networking responsibilities in `Server` and per-client state in
`Session`. Update `CMakeLists.txt` whenever source files move or are added.
Linux paths are case-sensitive, so preserve filename capitalization.

## Build, Test, and Development Commands

```bash
cmake -S . -B build       # Configure a local build
cmake --build build       # Compile game-server
./build/game-server       # Run locally
docker compose up --build # Build and run in Docker
docker compose logs -f    # Follow container output
```

Test the TCP connection from another terminal:

```bash
printf "hello\n" | nc -w 1 127.0.0.1 8080
```

For C++ changes, also run a strict warning check with `-Wall -Wextra
-Wpedantic -Wconversion`. Do not commit `build/`, CMake caches, binaries, or
logs.

## Coding Style & Naming Conventions

Use four-space indentation and standard C++23 features. Classes and matching
files use PascalCase (`Server`, `Session`); methods and local variables use
camelCase or the existing snake_case style. Prefer `{}` initialization,
`nullptr`, explicit casts, RAII, and length-aware byte handling. Avoid VLAs,
C-style casts, unchecked system-call results, and treating TCP reads as
complete messages.

Preserve fd ownership rules: sockets must be non-blocking, disconnects must
remove the session and close the fd once, and `eventfd` shutdown state must
remain synchronized.

Don't use `using namespace` in headers. 
Avoid linebreak in function declarations when I did not.

## Testing Guidelines

No automated test framework is configured yet. Every change must at least
configure, build, and pass relevant manual smoke tests. Networking changes
should cover connect, receive, disconnect, cleanup, and idle shutdown.
Packet work should test partial headers, partial payloads, multiple packets,
and oversized input. Add tests under `tests/` when introducing a framework,
using descriptive names such as `SessionCleanupTest.cpp`.

## Commit & Pull Request Guidelines

Recent commits use an emoji plus a Conventional Commit-style type:
`✨ feat: add packet parser`, `🎨 refactor: simplify cleanup`, or
`🐛 fix: handle partial reads`. Keep commits focused and imperative.

Create branches as `codex/<short-description>`. Pull requests should explain
the change, motivation, implementation decisions, validation commands, and
remaining risks. Link related issues when available. Open draft PRs by
default, stage only relevant files, and never include local configuration or
unrelated worktree changes.
