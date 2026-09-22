import mioyi.builder;
import mioyi.codegen;
import mioyi.compiler;
import mioyi.formatter;
import mioyi.ls;
import mioyi.linker;
import mioyi.linter;
import mioyi.manager;
import mioyi.optimizer;
import mioyi.pm;
import mioyi.parser;
import mioyi.transformer;

int main() {
  mioyi::builder::build();
  mioyi::codegen::codegen();
  mioyi::compiler::compile();
  mioyi::formatter::format();
  mioyi::ls::ls();
  mioyi::linker::link();
  mioyi::linter::lint();
  mioyi::manager::manage();
  mioyi::optimizer::optimize();
  mioyi::pm::pm();
  mioyi::parser::parse();
  mioyi::transformer::transform();

  return 0;
}
