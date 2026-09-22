module;

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>

export module mioyi.linker;

export namespace mioyi::linker {

class Options {
public:
  std::vector<std::string> inputs;
  std::string output;
};

using Result = std::expected<void, std::string>;

bool linkExecutable(const std::vector<std::string> &objectPaths,
                    std::string_view outputPath) {
  // The demo runtime uses the host C ABI for printf/putchar. Invoking the same
  // compiler driver that built mioyi supplies the platform CRT and libc while
  // still leaving parsing, IR generation, optimization and object emission in
  // this compiler. A self-contained runtime can replace this at a later stage.
  const std::string compiler = MIOYI_CXX_COMPILER;
  const std::string output(outputPath);
  llvm::SmallVector<llvm::StringRef, 8> arguments;
  arguments.push_back(compiler);
  for (const auto &object : objectPaths)
    arguments.push_back(object);
  arguments.push_back("-o");
  arguments.push_back(output);
  std::string error;
  bool failedToExecute = false;
  const int result = llvm::sys::ExecuteAndWait(
      compiler, arguments, std::nullopt, {}, 0, 0, &error, &failedToExecute);
  if (result == 0)
    return true;
  llvm::sys::fs::remove(output);
  if (failedToExecute)
    llvm::errs() << "error: cannot run host linker driver '" << compiler
                 << "': " << error << '\n';
  else
    llvm::errs() << "error: host linker driver exited with status " << result
                 << '\n';
  return false;
}

bool linkExecutable(std::string_view objectPath, std::string_view outputPath) {
  const std::vector<std::string> objectPaths = {std::string(objectPath)};
  return linkExecutable(objectPaths, outputPath);
}

Result link(Options options) {
  if (options.inputs.empty())
    return std::unexpected("at least one object file is required");
  if (!linkExecutable(options.inputs, options.output))
    return std::unexpected("failed to link executable");
  return {};
}

} // namespace mioyi::linker
