#include "acclorite/system/process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <system_error>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace acclorite::system {

ProcessResult run_capture_stdout(
    const std::vector<std::string>& arguments,
    const std::size_t max_output_bytes
) {
    if (arguments.empty()) {
        return {};
    }

    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        throw std::system_error(errno, std::generic_category(), "pipe");
    }

    posix_spawn_file_actions_t actions;
    int rc = ::posix_spawn_file_actions_init(&actions);
    if (rc != 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        throw std::system_error(rc, std::generic_category(), "posix_spawn_file_actions_init");
    }

    const auto cleanup_actions = [&]() {
        ::posix_spawn_file_actions_destroy(&actions);
    };

    rc = ::posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    if (rc == 0) {
        rc = ::posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    }
    if (rc == 0) {
        rc = ::posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
    }
    if (rc == 0) {
        rc = ::posix_spawn_file_actions_addopen(
            &actions,
            STDERR_FILENO,
            "/dev/null",
            O_WRONLY,
            0
        );
    }
    if (rc != 0) {
        cleanup_actions();
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        throw std::system_error(rc, std::generic_category(), "posix_spawn_file_actions");
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    rc = ::posix_spawnp(
        &pid,
        argv[0],
        &actions,
        nullptr,
        argv.data(),
        environ
    );
    cleanup_actions();
    ::close(pipe_fds[1]);

    if (rc != 0) {
        ::close(pipe_fds[0]);
        ProcessResult result;
        result.exit_code = (rc == ENOENT) ? 127 : 126;
        return result;
    }

    ProcessResult result;
    std::array<char, 8192> buffer{};

    while (result.stdout_text.size() < max_output_bytes) {
        const std::size_t remaining = max_output_bytes - result.stdout_text.size();
        const std::size_t request = std::min(buffer.size(), remaining);
        const ssize_t bytes = ::read(pipe_fds[0], buffer.data(), request);

        if (bytes > 0) {
            result.stdout_text.append(buffer.data(), static_cast<std::size_t>(bytes));
            continue;
        }

        if (bytes < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    ::close(pipe_fds[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        throw std::system_error(errno, std::generic_category(), "waitpid");
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }

    return result;
}

} // namespace acclorite::system
