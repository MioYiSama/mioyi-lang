module;

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

export bool linkExecutable(std::string_view objectPath,
                           std::string_view outputPath) {
  // The demo runtime uses the host C ABI for printf/putchar. Invoking the same
  // compiler driver that built mioyi supplies the platform CRT and libc while
  // still leaving parsing, IR generation, optimization and object emission in
  // this compiler. A self-contained runtime can replace this at a later stage.
  const std::string compiler = MIOYI_CXX_COMPILER;
  const std::string object(objectPath);
  const std::string output(outputPath);
  llvm::SmallVector<llvm::StringRef, 5> arguments = {compiler, object, "-o",
                                                     output};
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
