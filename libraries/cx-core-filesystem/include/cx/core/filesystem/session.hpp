#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cx::core::filesystem {
// Per-session paths: a TCP client's cd must never change another client's
// working directory or the daemon's process-wide cwd.
class session {
public:
  explicit session(std::filesystem::path cwd = std::filesystem::current_path())
      : cwd_(std::filesystem::canonical(cwd)) {}
  const std::filesystem::path &cwd() const { return cwd_; }
  std::filesystem::path resolve(const std::string &path) const {
    const std::filesystem::path p(path);
    return (p.is_absolute() ? p : cwd_ / p).lexically_normal();
  }
  void cd(const std::string &path) {
    auto p = std::filesystem::canonical(resolve(path));
    if (!std::filesystem::is_directory(p)) throw std::runtime_error("not a directory: " + p.string());
    cwd_ = std::move(p);
  }
  std::vector<std::string> list(const std::string &path = ".") const {
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(resolve(path)))
      names.push_back(entry.path().filename().string() + (entry.is_directory() ? "/" : ""));
    std::sort(names.begin(), names.end());
    return names;
  }
  template<class Sink> void read_lines(const std::string &path, Sink sink) const {
    std::ifstream in(resolve(path));
    if (!in) throw std::runtime_error("cannot read: " + path);
    for (std::string line; std::getline(in, line);) sink(line);
    if (in.bad()) throw std::runtime_error("read failed: " + path);
  }
private:
  std::filesystem::path cwd_;
};
}
