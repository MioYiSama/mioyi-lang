module;

#include <expected>
#include <print>
#include <string>

export module mioyi.ls;

export namespace mioyi::ls {

class Options {};

using Result = std::expected<void, std::string>;

Result ls(Options) {
  std::println("Hello, ls!");
  return {};
}

} // namespace mioyi::ls
