module;

#include <expected>
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

import mioyi.codegen;
import mioyi.linker;
import mioyi.optimizer;
import mioyi.parser;
import mioyi.transformer;

export namespace mioyi::compiler {

class Options {
public:
  std::string input;
  std::string output;
  std::string positionalOutput;
  unsigned optimization = 2;
  bool compileOnly = false;
  bool emitLLVM = false;
};

using Result = std::expected<void, std::string>;

Result compile(Options options) {
  std::ifstream inputFile;
  std::istream *source = &std::cin;
  if (!options.input.empty()) {
    inputFile.open(options.input);
    if (!inputFile)
      return std::unexpected("cannot open input file '" + options.input + "'");
    source = &inputFile;
  }
  std::ostringstream buffer;
  buffer << source->rdbuf();

  auto ast = mioyi::parser::parseSource(buffer.str());
  if (!ast)
    return std::unexpected("failed to parse Mioyi source");

  llvm::LLVMContext context;
  auto module = mioyi::transformer::generateIR(
      *ast, context, options.input.empty() ? "stdin" : options.input);
  if (!module)
    return std::unexpected("failed to transform AST into LLVM IR");

  if (options.emitLLVM) {
    if (!mioyi::optimizer::prepareNativeIR(*module, options.optimization))
      return std::unexpected("failed to optimize LLVM IR");
    if (options.output == "-") {
      module->print(llvm::outs(), nullptr);
      return {};
    }
    std::error_code error;
    llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
    if (error)
      return std::unexpected("cannot open output file '" + options.output +
                             "': " + error.message());
    module->print(output, nullptr);
    return {};
  }

  if (!mioyi::optimizer::prepareNativeIR(*module, options.optimization))
    return std::unexpected("failed to optimize LLVM IR");

  if (options.compileOnly) {
    if (options.output == "-")
      return mioyi::codegen::emitNativeObject(*module, options.optimization,
                                              llvm::outs())
                 ? Result{}
                 : std::unexpected("failed to generate object file");
    std::error_code error;
    llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
    if (error)
      return std::unexpected("cannot open output file '" + options.output +
                             "': " + error.message());
    return mioyi::codegen::emitNativeObject(*module, options.optimization,
                                            output)
               ? Result{}
               : std::unexpected("failed to generate object file");
  }

  if (options.output == "-")
    return std::unexpected("an executable cannot be written to stdout");

  int objectFD = -1;
  llvm::SmallString<128> objectPath;
  const std::error_code tempError =
      llvm::sys::fs::createTemporaryFile("mioyi-lang",
#ifdef _WIN32
                                         "obj",
#else
                                         "o",
#endif
                                         objectFD, objectPath);
  if (tempError)
    return std::unexpected("cannot create temporary object file: " +
                           tempError.message());
  {
    llvm::raw_fd_ostream objectOutput(objectFD, true);
    if (!mioyi::codegen::emitNativeObject(*module, options.optimization,
                                          objectOutput)) {
      llvm::sys::fs::remove(objectPath);
      return std::unexpected("failed to generate object file");
    }
  }
  const bool linked =
      mioyi::linker::linkExecutable(objectPath.str(), options.output);
  llvm::sys::fs::remove(objectPath);
  if (!linked)
    return std::unexpected("failed to link executable");
  return {};
}

} // namespace mioyi::compiler
