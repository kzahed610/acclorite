#include "acclorite/system/process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int error = errno;
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        throw std::system_error(error, std::generic_category(), "fork");
    }

    if (pid == 0) {
        ::close(pipe_fds[0]);

        if (::dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        ::close(pipe_fds[1]);

        const int dev_null = ::open("/dev/null", O_WRONLY);
        if (dev_null >= 0) {
            ::dup2(dev_null, STDERR_FILENO);
            ::close(dev_null);
        }

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(argv[0], argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    ::close(pipe_fds[1]);

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
