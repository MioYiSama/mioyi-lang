module;

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

export module mioyi.codegen;

namespace {

std::unique_ptr<llvm::TargetMachine> createNativeTargetMachine(unsigned level) {
  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter()) {
    llvm::errs() << "error: failed to initialize the native LLVM target\n";
    return nullptr;
  }
  llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  std::string targetError;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple, targetError);
  if (!target) {
    llvm::errs() << "error: " << targetError << '\n';
    return nullptr;
  }
  const auto codegenLevel = static_cast<llvm::CodeGenOptLevel>(level);
  const std::string cpu = llvm::sys::getHostCPUName().str();
  auto machine = std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, cpu, "", llvm::TargetOptions{},
                                  std::nullopt, std::nullopt, codegenLevel));
  if (!machine)
    llvm::errs() << "error: failed to create target machine for '"
                 << triple.str() << "'\n";
  return machine;
}

} // namespace

export namespace mioyi::codegen {

class Options {
public:
  std::string input;
  std::string output;
  unsigned optimization = 2;
};

using Result = std::expected<void, std::string>;

bool emitNativeObject(llvm::Module &module, unsigned optimization,
                      llvm::raw_pwrite_stream &output) {
  auto machine = createNativeTargetMachine(optimization);
  if (!machine)
    return false;
  module.setTargetTriple(machine->getTargetTriple());
  module.setDataLayout(machine->createDataLayout());
  if (llvm::verifyModule(module, &llvm::errs()))
    return false;
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, output, nullptr,
                                   llvm::CodeGenFileType::ObjectFile)) {
    llvm::errs() << "error: target does not support object-file emission\n";
    return false;
  }
  passes.run(module);
  return true;
}

Result codegen(Options options) {
  llvm::LLVMContext context;
  llvm::SMDiagnostic diagnostic;
  const std::string input = options.input.empty() ? "-" : options.input;
  auto module = llvm::parseIRFile(input, diagnostic, context);
  if (!module) {
    diagnostic.print("mioyi-lang codegen", llvm::errs());
    return std::unexpected("failed to read LLVM IR");
  }

  if (options.output == "-") {
    if (!emitNativeObject(*module, options.optimization, llvm::outs()))
      return std::unexpected("failed to generate object file");
    return {};
  }
  std::error_code error;
  llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
  if (error)
    return std::unexpected("cannot open output file '" + options.output +
                           "': " + error.message());
  if (!emitNativeObject(*module, options.optimization, output))
    return std::unexpected("failed to generate object file");
  return {};
}

} // namespace mioyi::codegen
