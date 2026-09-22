module;

#include <expected>
#include <print>
#include <string>

export module mioyi.manager;

export namespace mioyi::manager {

enum class Command { Install, Uninstall, Upgrade };

class Options {
public:
  Command command = Command::Install;
};

using Result = std::expected<void, std::string>;

Result manage(Options) {
  std::println("Hello, manager!");
  return {};
}

} // namespace mioyi::manager
