#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include <CLI/CLI.hpp>

import mioyi.builder;
import mioyi.codegen;
import mioyi.compiler;
import mioyi.formatter;
import mioyi.linker;
import mioyi.linter;
import mioyi.ls;
import mioyi.manager;
import mioyi.optimizer;
import mioyi.parser;
import mioyi.pm;
import mioyi.transformer;

namespace {

void addCompileOptions(CLI::App &command, mioyi::compiler::Options &options) {
  command.add_option("input", options.input,
                     "Input .mioyi source (stdin if omitted)");
  auto *legacyOutput =
      command.add_option("output", options.positionalOutput,
                         "Output file (legacy positional form)");
  auto *output = command.add_option("-o", options.output, "Output file");
  output->excludes(legacyOutput);
  command.add_flag("-c", options.compileOnly,
                   "Emit an object file without linking");
  command.add_flag("-S,--emit-llvm", options.emitLLVM, "Emit LLVM IR");
  command.add_option("-O", options.optimization, "Optimization level")
      ->check(CLI::Range(0u, 3u))
      ->default_str("2");
}

void finalizeCompileOptions(mioyi::compiler::Options &options) {
  if (!options.positionalOutput.empty())
    options.output = options.positionalOutput;
  if (!options.output.empty() &&
      std::filesystem::path(options.output).extension() == ".ll")
    options.emitLLVM = true;
  if (!options.output.empty())
    return;
  if (options.emitLLVM) {
    options.output = "-";
  } else if (options.compileOnly) {
    if (options.input.empty()) {
      options.output = "a.o";
    } else {
      auto path = std::filesystem::path(options.input);
      path.replace_extension(".o");
      options.output = path.string();
    }
  } else if (options.input.empty()) {
    options.output =
#ifdef _WIN32
        "a.exe";
#else
        "a.out";
#endif
  } else {
    auto path = std::filesystem::path(options.input);
#ifdef _WIN32
    path.replace_extension(".exe");
#else
    path.replace_extension();
    if (path == std::filesystem::path(options.input))
      path += ".out";
#endif
    options.output = path.string();
  }
}

void finalizeParseOptions(mioyi::parser::Options &options) {
  if (options.output.empty())
    options.output = "-";
  if (!options.format.empty())
    return;
  const auto extension = std::filesystem::path(options.output).extension();
  options.format =
      extension == ".yaml" || extension == ".yml" ? "yaml" : "json";
}

void finalizeTransformOptions(mioyi::transformer::Options &options) {
  if (options.output.empty())
    options.output = "-";
  if (!options.format.empty())
    return;
  const auto extension = std::filesystem::path(options.input).extension();
  options.format =
      extension == ".yaml" || extension == ".yml" ? "yaml" : "json";
}

void finalizeCodegenOptions(mioyi::codegen::Options &options) {
  if (!options.output.empty())
    return;
  if (options.input.empty()) {
    options.output = "a.o";
    return;
  }
  auto path = std::filesystem::path(options.input);
  path.replace_extension(".o");
  options.output = path.string();
}

void finalizeLinkOptions(mioyi::linker::Options &options) {
  if (!options.output.empty())
    return;
#ifdef _WIN32
  options.output = "a.exe";
#else
  options.output = "a.out";
#endif
}

int finish(auto result) {
  if (result)
    return 0;
  if (!result.error().empty())
    std::cerr << "error: " << result.error() << '\n';
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  CLI::App app{"Mioyi language toolchain"};

  mioyi::compiler::Options rootCompileOptions;
  addCompileOptions(app, rootCompileOptions);

  mioyi::compiler::Options compileOptions;
  auto *compile = app.add_subcommand("compile", "Compile and link a program");
  addCompileOptions(*compile, compileOptions);

  mioyi::parser::Options parseOptions;
  auto *parse = app.add_subcommand("parse", "Serialize the parsed AST");
  parse->alias("ast");
  parse->add_option("input", parseOptions.input,
                    "Input .mioyi source (stdin if omitted)");
  parse->add_option("-o,--output", parseOptions.output,
                    "Output file (stdout if omitted)");
  parse->add_option("-f,--format", parseOptions.format, "Output format")
      ->check(CLI::IsMember({"json", "yaml"}));

  mioyi::transformer::Options transformOptions;
  auto *transform =
      app.add_subcommand("transform", "Transform an AST into LLVM IR");
  transform->add_option("input", transformOptions.input,
                        "Input AST (stdin if omitted)");
  transform->add_option("-o,--output", transformOptions.output,
                        "Output LLVM IR (stdout if omitted)");
  transform
      ->add_option("-f,--format", transformOptions.format, "Input AST format")
      ->check(CLI::IsMember({"json", "yaml"}));

  mioyi::optimizer::Options optimizeOptions;
  auto *optimize = app.add_subcommand("optimize", "Optimize LLVM IR");
  optimize->add_option("input", optimizeOptions.input,
                       "Input LLVM IR (stdin if omitted)");
  optimize->add_option("-o,--output", optimizeOptions.output,
                       "Output LLVM IR (stdout if omitted)");
  optimize->add_option("-O", optimizeOptions.optimization, "Optimization level")
      ->check(CLI::Range(0u, 3u))
      ->default_str("2");

  mioyi::codegen::Options codegenOptions;
  auto *codegen =
      app.add_subcommand("codegen", "Generate an object file from LLVM IR");
  codegen->add_option("input", codegenOptions.input,
                      "Input LLVM IR (stdin if omitted)");
  codegen->add_option("-o,--output", codegenOptions.output,
                      "Output object file");
  codegen
      ->add_option("-O", codegenOptions.optimization,
                   "Code generation optimization level")
      ->check(CLI::Range(0u, 3u))
      ->default_str("2");

  mioyi::linker::Options linkOptions;
  auto *link =
      app.add_subcommand("link", "Link object files into an executable");
  link->add_option("input", linkOptions.inputs, "Input object files")
      ->required()
      ->expected(-1);
  link->add_option("-o,--output", linkOptions.output, "Output executable");

  mioyi::pm::Options packageOptions;
  auto *add = app.add_subcommand("add", "Add a package dependency");
  add->add_option("package", packageOptions.package, "Package to add")
      ->required();
  auto *remove = app.add_subcommand("remove", "Remove a package dependency");
  remove->add_option("package", packageOptions.package, "Package to remove")
      ->required();
  auto *update = app.add_subcommand("update", "Update package dependencies");
  update->add_option("package", packageOptions.package,
                     "Package to update (all if omitted)");

  mioyi::builder::Options buildOptions;
  auto *build = app.add_subcommand("build", "Build the current project");
  mioyi::linter::Options lintOptions;
  auto *lint = app.add_subcommand("lint", "Lint source files");
  mioyi::formatter::Options formatOptions;
  auto *format = app.add_subcommand("fmt", "Format source files");
  mioyi::ls::Options languageServerOptions;
  auto *languageServer = app.add_subcommand("ls", "Start the language server");

  mioyi::manager::Options managerOptions;
  auto *manage = app.add_subcommand("manage", "Manage the Mioyi toolchain");
  auto *install = manage->add_subcommand("install", "Install the toolchain");
  auto *uninstall =
      manage->add_subcommand("uninstall", "Uninstall the toolchain");
  auto *upgrade = manage->add_subcommand("upgrade", "Upgrade the toolchain");
  manage->require_subcommand(1);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    return app.exit(error);
  }

  if (*parse) {
    finalizeParseOptions(parseOptions);
    return finish(mioyi::parser::parse(std::move(parseOptions)));
  }

  if (*compile) {
    finalizeCompileOptions(compileOptions);
    return finish(mioyi::compiler::compile(std::move(compileOptions)));
  }

  if (*transform) {
    finalizeTransformOptions(transformOptions);
    return finish(mioyi::transformer::transform(std::move(transformOptions)));
  }

  if (*optimize) {
    if (optimizeOptions.output.empty())
      optimizeOptions.output = "-";
    return finish(mioyi::optimizer::optimize(std::move(optimizeOptions)));
  }

  if (*codegen) {
    finalizeCodegenOptions(codegenOptions);
    return finish(mioyi::codegen::codegen(std::move(codegenOptions)));
  }

  if (*link) {
    finalizeLinkOptions(linkOptions);
    return finish(mioyi::linker::link(std::move(linkOptions)));
  }

  if (*add || *remove || *update) {
    packageOptions.command = *add      ? mioyi::pm::Command::Add
                             : *remove ? mioyi::pm::Command::Remove
                                       : mioyi::pm::Command::Update;
    return finish(mioyi::pm::pm(std::move(packageOptions)));
  }

  if (*build)
    return finish(mioyi::builder::build(std::move(buildOptions)));

  if (*lint)
    return finish(mioyi::linter::lint(std::move(lintOptions)));

  if (*format)
    return finish(mioyi::formatter::format(std::move(formatOptions)));

  if (*languageServer)
    return finish(mioyi::ls::ls(std::move(languageServerOptions)));

  if (*install || *uninstall || *upgrade) {
    managerOptions.command = *install     ? mioyi::manager::Command::Install
                             : *uninstall ? mioyi::manager::Command::Uninstall
                                          : mioyi::manager::Command::Upgrade;
    return finish(mioyi::manager::manage(std::move(managerOptions)));
  }

  finalizeCompileOptions(rootCompileOptions);
  return finish(mioyi::compiler::compile(std::move(rootCompileOptions)));
}
