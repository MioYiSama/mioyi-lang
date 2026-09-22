module;

#include <expected>
#include <print>
#include <string>

export module mioyi.builder;

export namespace mioyi::builder {

class Options {};

using Result = std::expected<void, std::string>;

Result build(Options) {
  std::println("Hello, builder!");
  return {};
}

} // namespace mioyi::builder
