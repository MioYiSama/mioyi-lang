module;

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#include <llvm/ADT/SmallString.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

export module mioyi.compiler;

import mioyi.compiler.backend;
import mioyi.compiler.frontend;
import mioyi.linker;
import mioyi.options;
import mioyi.parser;

export int compile(const CompilerOptions &options) {
  std::ifstream inputFile;
  std::istream *source = &std::cin;
  if (!options.input.empty()) {
    inputFile.open(options.input);
    if (!inputFile) {
      llvm::errs() << "error: cannot open input file '" << options.input
                   << "'\n";
      return 1;
    }
    source = &inputFile;
  }
  std::ostringstream buffer;
  buffer << source->rdbuf();

  auto ast = parseSource(buffer.str());
  if (!ast) return 1;

  llvm::LLVMContext context;
  auto module = generateIR(*ast, context,
                           options.input.empty() ? "stdin" : options.input);
  if (!module) return 1;

  if (options.emitLLVM) {
    if (!prepareNativeIR(*module, options.optimization)) return 1;
    if (options.output == "-") {
      module->print(llvm::outs(), nullptr);
      return 0;
    }
    std::error_code error;
    llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
    if (error) {
      llvm::errs() << "error: cannot open output file '" << options.output
                   << "': " << error.message() << '\n';
      return 1;
    }
    module->print(output, nullptr);
    return 0;
  }

  if (options.compileOnly) {
    if (options.output == "-")
      return emitNativeObject(*module, options.optimization, llvm::outs()) ? 0
                                                                          : 1;
    std::error_code error;
    llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
    if (error) {
      llvm::errs() << "error: cannot open output file '" << options.output
                   << "': " << error.message() << '\n';
      return 1;
    }
    return emitNativeObject(*module, options.optimization, output) ? 0 : 1;
  }

  if (options.output == "-") {
    llvm::errs() << "error: an executable cannot be written to stdout\n";
    return 1;
  }

  int objectFD = -1;
  llvm::SmallString<128> objectPath;
  const std::error_code tempError = llvm::sys::fs::createTemporaryFile(
      "mioyi-lang",
#ifdef _WIN32
      "obj",
#else
      "o",
#endif
      objectFD, objectPath);
  if (tempError) {
    llvm::errs() << "error: cannot create temporary object file: "
                 << tempError.message() << '\n';
    return 1;
  }
  {
    llvm::raw_fd_ostream objectOutput(objectFD, true);
    if (!emitNativeObject(*module, options.optimization, objectOutput)) {
      llvm::sys::fs::remove(objectPath);
      return 1;
    }
  }
  const bool linked = linkExecutable(objectPath.str(), options.output);
  llvm::sys::fs::remove(objectPath);
  return linked ? 0 : 1;
}
