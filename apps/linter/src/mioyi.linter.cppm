module;

#include <expected>
#include <print>
#include <string>

export module mioyi.linter;

export namespace mioyi::linter {

class Options {};

using Result = std::expected<void, std::string>;

Result lint(Options) {
  std::println("Hello, linter!");
  return {};
}

} // namespace mioyi::linter
