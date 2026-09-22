module;

#include <expected>
#include <print>
#include <string>

export module mioyi.formatter;

export namespace mioyi::formatter {

class Options {};

using Result = std::expected<void, std::string>;

Result format(Options) {
  std::println("Hello, formatter!");
  return {};
}

} // namespace mioyi::formatter
