module;

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <lld/Common/Driver.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#if defined(_WIN32)
LLD_HAS_DRIVER(coff)
#elif defined(__APPLE__)
LLD_HAS_DRIVER(macho)
#else
LLD_HAS_DRIVER(elf)
#endif

export module mioyi.linker;

namespace {

std::vector<std::string> linkerArguments(std::string_view objectPath,
                                         std::string_view outputPath) {
#if defined(_WIN32)
  return {"lld-link", std::string(objectPath),
          "/out:" + std::string(outputPath)};
#elif defined(__APPLE__)
  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  const std::string platformVersion =
      triple.getArch() == llvm::Triple::aarch64 ? "11.0" : "10.15";
  return {"ld64.lld",
          "-w",
          "-arch",
          triple.getArchName().str(),
          "-platform_version",
          "macos",
          platformVersion,
          platformVersion,
          std::string(objectPath),
          "-o",
          std::string(outputPath)};
#else
  return {"ld.lld", std::string(objectPath), "-o", std::string(outputPath)};
#endif
}

llvm::ArrayRef<lld::DriverDef> linkerDrivers() {
#if defined(_WIN32)
  static constexpr std::array drivers = {
      lld::DriverDef{lld::WinLink, &lld::coff::link}};
#elif defined(__APPLE__)
  static constexpr std::array drivers = {
      lld::DriverDef{lld::Darwin, &lld::macho::link}};
#else
  static constexpr std::array drivers = {
      lld::DriverDef{lld::Gnu, &lld::elf::link}};
#endif
  return drivers;
}

} // namespace

export bool linkExecutable(std::string_view objectPath,
                           std::string_view outputPath) {
  std::vector<std::string> ownedArguments =
      linkerArguments(objectPath, outputPath);
  std::vector<const char *> arguments;
  arguments.reserve(ownedArguments.size());
  for (const std::string &argument : ownedArguments)
    arguments.push_back(argument.c_str());

  const lld::Result result =
      lld::lldMain(arguments, llvm::outs(), llvm::errs(), linkerDrivers());
  if (result.retCode == 0) return true;

  llvm::sys::fs::remove(llvm::StringRef(outputPath.data(), outputPath.size()));
  if (!result.canRunAgain)
    llvm::errs() << "error: embedded LLD cannot be invoked again after its "
                    "internal failure\n";
  return false;
}
