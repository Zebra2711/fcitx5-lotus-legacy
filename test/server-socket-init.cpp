// SPDX-FileCopyrightText: 2026 LotusInputMethod contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A server socket that fails bind()/listen() must be reported, never used
// deaf. Exercises the wrappers with real syscalls: a second server must be
// refused while the first is listening and listen() on an invalid fd must fail.
#include "common-fd-guard.h"
#include "lotus-server-socket.h"
#include "lotus-unix-socket.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    std::string testSocketPath() {
        const auto path = std::filesystem::temp_directory_path() / ("lotus-server-socket-init-" + std::to_string(getpid()) + ".socket");
        return path.string();
    }

} // namespace

int runServerSocketInit() {
    UnixSocket address = makeUnixSocket(testSocketPath());

    FdGuard    first(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    check("create socket", first.isValid());
    if (!first.isValid()) {
        return 1;
    }
    check("bind fresh address", bindServerSocket(first.get(), address, "test socket"));
    check("listen on bound socket", listenServerSocket(first.get(), "test socket"));

    struct stat socketStat{};
    check("stat bound socket", stat(address.path.c_str(), &socketStat) == 0);
    check("bound path is a socket", S_ISSOCK(socketStat.st_mode));
    check("socket is owner-only", (socketStat.st_mode & 0777) == 0600);

    UnixSocket duplicate = makeUnixSocket(address.path);
    FdGuard    second(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    check("create second socket", second.isValid());
    check("rebind a live address fails", !bindServerSocket(second.get(), duplicate, "test socket"));

    check("listen on invalid fd fails", !listenServerSocket(-1, "test socket"));

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("server socket init: all checks passed\n");
    return 0;
}

int main() {
    return runServerSocketInit();
}
