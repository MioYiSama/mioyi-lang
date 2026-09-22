module;

#include <expected>
#include <print>
#include <string>

export module mioyi.pm;

export namespace mioyi::pm {

enum class Command { Add, Remove, Update };

class Options {
public:
  Command command = Command::Add;
  std::string package;
};

using Result = std::expected<void, std::string>;

Result pm(Options) {
  std::println("Hello, pm!");
  return {};
}

} // namespace mioyi::pm
