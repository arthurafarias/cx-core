// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

// Running child processes: spawn-and-wait, capture, and finding an executable
// on PATH. POSIX only (fork/execv/waitpid/poll).
//
// run_captured reads stdout and stderr *together*, with poll(2). Reading one
// to the end and then the other deadlocks as soon as the child fills the pipe
// nobody is reading: it blocks in write(), never exits, and the parent waits
// for an end-of-file that cannot come.

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cx::core::process {

// A shell-style status: the child's exit code, or 128 + the signal that killed it.
inline int exit_status(int wait_status) {
  return WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128 + WTERMSIG(wait_status);
}

struct captured {
  int status = 0;
  std::string out;
  std::string err;
};

namespace detail {

inline std::vector<char *> argv_of(std::vector<std::string> &owned) {
  std::vector<char *> argv;
  argv.reserve(owned.size() + 1);
  for (auto &argument : owned) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  return argv;
}

inline int wait_for(pid_t child) {
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      throw std::runtime_error("cannot wait for child process");
    }
  }
  return exit_status(status);
}

// 126 is the shell's "found but could not be executed".
[[noreturn]] inline void exec_or_die(const std::string &binary, std::vector<char *> &argv) {
  ::execv(binary.c_str(), argv.data());
  std::perror(binary.c_str());
  ::_exit(126);
}

} // namespace detail

// Runs `binary` with `arguments` (argv[0] included, as execv takes it), the
// child inheriting this process's stdio, and returns its exit_status().
inline int run(const std::string &binary, std::vector<std::string> arguments) {
  std::vector<char *> argv = detail::argv_of(arguments);
  std::fflush(nullptr); // or the child's output lands before what we had buffered
  const pid_t child = ::fork();
  if (child < 0) {
    throw std::runtime_error("cannot fork");
  }
  if (child == 0) {
    detail::exec_or_die(binary, argv);
  }
  return detail::wait_for(child);
}

// As run(), with the child's stdout and stderr collected instead of inherited.
inline captured run_captured(const std::string &binary, std::vector<std::string> arguments) {
  std::vector<char *> argv = detail::argv_of(arguments);
  std::array<int, 2> out_pipe{}, err_pipe{};
  if (::pipe2(out_pipe.data(), O_CLOEXEC) != 0) {
    throw std::runtime_error("cannot create pipe");
  }
  if (::pipe2(err_pipe.data(), O_CLOEXEC) != 0) {
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    throw std::runtime_error("cannot create pipe");
  }

  std::fflush(nullptr);
  const pid_t child = ::fork();
  if (child < 0) {
    for (int fd : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) ::close(fd);
    throw std::runtime_error("cannot fork");
  }
  if (child == 0) {
    ::dup2(out_pipe[1], STDOUT_FILENO); // dup2 clears O_CLOEXEC on the copy
    ::dup2(err_pipe[1], STDERR_FILENO);
    detail::exec_or_die(binary, argv);
  }
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);

  captured result;
  std::array<pollfd, 2> watched{pollfd{out_pipe[0], POLLIN, 0}, pollfd{err_pipe[0], POLLIN, 0}};
  std::array<std::string *, 2> sinks{&result.out, &result.err};
  std::size_t open = watched.size();
  std::array<char, 65536> chunk{};
  while (open > 0) {
    if (::poll(watched.data(), watched.size(), -1) < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (std::size_t i = 0; i < watched.size(); ++i) {
      if (watched[i].fd < 0 || (watched[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;
      const ssize_t got = ::read(watched[i].fd, chunk.data(), chunk.size());
      if (got > 0) {
        sinks[i]->append(chunk.data(), static_cast<std::size_t>(got));
      } else if (got == 0 || errno != EINTR) {
        ::close(watched[i].fd);
        watched[i].fd = -1; // poll() ignores negative descriptors
        --open;
      }
    }
  }
  result.status = detail::wait_for(child);
  return result;
}

// A regular file this process may execute.
inline bool executable(const std::filesystem::path &file) {
  std::error_code ignored;
  return std::filesystem::is_regular_file(file, ignored) && ::access(file.c_str(), X_OK) == 0;
}

// Where the running binary lives; empty when /proc/self/exe cannot be read.
inline std::filesystem::path own_directory() {
  std::error_code failure;
  const auto self = std::filesystem::read_symlink("/proc/self/exe", failure);
  return failure ? std::filesystem::path{} : self.parent_path();
}

// Where a program looks for the executables that belong to it: beside its own
// binary first, then every PATH entry. A build tree and an install prefix both
// work without PATH naming either - the sibling-executable dispatch
// agenticx-ncortex is built on, which this came from.
inline std::vector<std::filesystem::path> search_directories() {
  std::vector<std::filesystem::path> directories;
  if (const auto own = own_directory(); !own.empty()) {
    directories.push_back(own);
  }
  if (const char *path = std::getenv("PATH")) {
    std::string_view rest = path;
    while (true) {
      const std::size_t colon = rest.find(':');
      if (const std::string_view entry = rest.substr(0, colon); !entry.empty()) {
        directories.emplace_back(entry);
      }
      if (colon == std::string_view::npos) {
        break;
      }
      rest.remove_prefix(colon + 1);
    }
  }
  return directories;
}

// The first directory of search_directories() holding an executable `name`;
// empty when none does. `name` is used as given: a caller taking it from
// untrusted input checks it names no path first.
inline std::filesystem::path find_sibling(std::string_view name) {
  for (const auto &directory : search_directories()) {
    if (executable(directory / name)) {
      return directory / name;
    }
  }
  return {};
}

// The first executable regular file named `name` on PATH, as a shell would
// resolve it; a name containing '/' is checked as given.
inline std::optional<std::filesystem::path> which(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (name.find('/') != std::string_view::npos) {
    return executable(name) ? std::optional<std::filesystem::path>(name) : std::nullopt;
  }
  const char *path = std::getenv("PATH");
  std::string_view rest = path != nullptr ? path : "";
  while (true) {
    const std::size_t colon = rest.find(':');
    const std::string_view directory = rest.substr(0, colon);
    const std::filesystem::path candidate = std::filesystem::path(directory.empty() ? "." : directory) / name;
    if (executable(candidate)) {
      return candidate;
    }
    if (colon == std::string_view::npos) {
      return std::nullopt;
    }
    rest.remove_prefix(colon + 1);
  }
}

} // namespace cx::core::process
