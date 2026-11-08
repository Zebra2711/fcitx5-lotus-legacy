// SPDX-FileCopyrightText: 2026 LotusInputMethod contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SO_PEERCRED is the kernel-side half of the socket access policy: pathname
// permissions keep other users out, and the server double-checks the UID of
// every accepted peer. This exercises the shared credential helpers with a
// real AF_UNIX socket pair.
#include "common-executable-path.h"
#include "common-peer-credentials.h"

#include <cstdio>
#include <signal.h>
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

} // namespace

int main() {
    int fds[2] = {-1, -1};
    check("create socket pair", socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0);
    if (fds[0] < 0 || fds[1] < 0) {
        return 1;
    }

    const auto credentials = lotusPeerCredentials(fds[0]);
    check("read peer credentials", credentials.has_value());
    if (credentials.has_value()) {
        check("peer uid is the current user", credentials->uid == getuid());
        check("peer pid is this process", credentials->pid == getpid());
    }
    check("reject invalid descriptor", !lotusPeerCredentials(-1).has_value());

    const std::string selfExecutable = lotusPeerExecutablePath(getpid());
    check("resolve own executable", !selfExecutable.empty());
    check("self executable passes the policy", isAllowedExecutablePath(selfExecutable, selfExecutable, ""));
    check("reject another executable", !isAllowedExecutablePath("/nonexistent/lotus-server", selfExecutable, ""));

    const std::string selfCommand = lotusPeerCommandPath(getpid());
    check("resolve own argv[0]", !selfCommand.empty());

    // A service that drops privileges is not dumpable: /proc/<pid>/exe becomes
    // unreadable for same-user processes, but /proc/<pid>/cmdline still works.
    int ready[2] = {-1, -1};
    check("create sync pipe", pipe(ready) == 0);
    if (ready[0] >= 0 && ready[1] >= 0) {
        const pid_t child = fork();
        if (child == 0) {
            close(ready[0]);
            prctl(PR_SET_DUMPABLE, 0);
            const char                  byte    = 'x';
            [[maybe_unused]] const auto written = write(ready[1], &byte, 1);
            pause();
            _exit(0);
        }
        close(ready[1]);
        char readyByte = 0;
        check("wait for non-dumpable peer", child > 0 && read(ready[0], &readyByte, 1) == 1);
        close(ready[0]);
        if (child > 0) {
            check("resolve cmdline of a non-dumpable peer", !lotusPeerCommandPath(child).empty());
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
        }
    }

    close(fds[0]);
    close(fds[1]);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("peer credentials: all checks passed\n");
    return 0;
}
