module;

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>

export module mioyi.linker;

namespace {

struct LinkCommand {
  std::string program;
  std::vector<std::string> arguments;
};

std::vector<std::string>
findPrograms(std::initializer_list<llvm::StringRef> names) {
  std::vector<std::string> programs;
  for (const llvm::StringRef name : names) {
    auto program = llvm::sys::findProgramByName(name);
    if (!program) continue;
    if (std::find(programs.begin(), programs.end(), *program) == programs.end())
      programs.push_back(*program);
  }
  return programs;
}

std::vector<LinkCommand> linkerCommands(llvm::StringRef object,
                                        llvm::StringRef output) {
  std::vector<LinkCommand> commands;
#ifdef _WIN32
  for (const auto &driver : findPrograms({"clang-cl", "cl"})) {
    commands.push_back({driver, {driver, "/nologo", object.str(),
                                 "/Fe:" + output.str()}});
  }
  for (const auto &driver : findPrograms({"clang", "gcc", "cc"})) {
    commands.push_back(
        {driver, {driver, object.str(), "-o", output.str()}});
  }
#else
  const auto drivers = findPrograms({"clang", "cc", "gcc", "clang++", "g++"});
#ifdef __APPLE__
  const auto linkers = findPrograms({"ld64.lld", "lld", "ld"});
#else
  const auto linkers =
      findPrograms({"mold", "ld.lld", "lld", "ld.gold", "gold", "ld"});
#endif
  for (const auto &driver : drivers) {
    for (const auto &linker : linkers) {
      commands.push_back(
          {driver, {driver, "-fuse-ld=" + linker, object.str(), "-o",
                    output.str()}});
    }
  }
  for (const auto &driver : drivers)
    commands.push_back({driver, {driver, object.str(), "-o", output.str()}});
#endif
  return commands;
}

} // namespace

export bool linkExecutable(std::string_view objectPath,
                           std::string_view outputPath) {
  const llvm::StringRef object(objectPath.data(), objectPath.size());
  const llvm::StringRef output(outputPath.data(), outputPath.size());
  auto commands = linkerCommands(object, output);
  if (commands.empty()) {
    llvm::errs() << "error: no supported linker or compiler driver was found "
                    "in PATH\n";
    return false;
  }

  for (std::size_t index = 0; index < commands.size(); ++index) {
    auto &command = commands[index];
    std::vector<llvm::StringRef> arguments;
    arguments.reserve(command.arguments.size());
    for (const auto &argument : command.arguments) arguments.push_back(argument);

    const bool showDiagnostics = index + 1 == commands.size();
    std::array<std::optional<llvm::StringRef>, 3> redirects = {
        std::nullopt,
        showDiagnostics ? std::nullopt : std::optional<llvm::StringRef>(""),
        showDiagnostics ? std::nullopt : std::optional<llvm::StringRef>("")};
    std::string error;
    bool executionFailed = false;
    const int result = llvm::sys::ExecuteAndWait(
        command.program, arguments, std::nullopt, redirects, 0, 0, &error,
        &executionFailed);
    if (result == 0) return true;
    llvm::sys::fs::remove(output);
    if (showDiagnostics && !error.empty())
      llvm::errs() << "error: unable to run linker: " << error << '\n';
  }

  llvm::errs() << "error: all available linkers failed\n";
  return false;
}
