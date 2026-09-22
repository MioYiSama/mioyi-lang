# mioyi-lang

## 命令行

- 编译器（源代码后缀名 `.mioyi`）：
  - `mioyi-lang compile`：将**源代码**编译成**可执行文件**
  - `mioyi-lang parse`：将**源代码**解析成**AST**并导出为JSON/YAML
  - `mioyi-lang transform`：将**AST**转换成**LLVM IR**
  - `mioyi-lang optimize`：优化**LLVM IR**
  - `mioyi-lang codegen`：从**LLVM IR**生成**目标文件**
  - `mioyi-lang link`：将**目标文件**链接成**可执行文件**
- 工具链（配置文件 `mioyi.toml`）：
  - `mioyi-lang <add|remove|update>`：包管理
  - `mioyi-lang build`：构建工具
  - `mioyi-lang lint`：代码检查工具
  - `mioyi-lang fmt`：格式化工具
  - `mioyi-lang ls`：语言服务器（[LSP](https://microsoft.github.io/language-server-protocol/)）
  - `mioyi-lang manage <install|uninstall|upgrade>`：工具链管理工具

## 注释与语句

```mioyi
// 单行注释
/* 多行注释 */

// 默认按行分割，可选用分号分割
print("")
```

## 变量定义与类型系统

万物皆变量，支持代数数据类型、泛型

```mioyi
var x = 1 // 可变的变量
val y = 1 // 不可变的变量
def z = 1 // 编译期常量
var w: Int = 1 // 显式类型标注
var a = 1, b = 2 // 定义多个变量
var a = b = 2 // 链式定义

// 基础类型
var x: Byte = 0B // 忽略符号
var x: Int = 0 // 或 Int32
var x: Int64 = 0L
var x: UInt = 0U  // 或 UInt32
var x: UInt64 = 0UL

var x: Float = 0.0 // 或 Float32
var x: Float64 = 0.0L

var x: Char = 'x' // 完整 Unicode Codepoint
var x: String = "Hello" // 底层 UTF-8 编码的字节数组

var x: Bool = true // or false
var x: Void = void // 相当于 Unit 类型
var x: Never = @todo() // 所有类型的子类型

// 类型表达式
// 语法为 type <type_expr>
def Int = type Int32;
// 字面量类型
def Red = type 1;

// 复合类型
var array: [Int] = [1, 2, 3]
var map: [Int:Char] = [1: '1', 2: '2', 3: '3'] // 底层HashMap
var struct: (x: Int, y: Int) = (x: 1, y: 2)
var tuple: (Int, Int) = (1, 2)

// 和类型
def Color = type 'Red' | 'Green' | 'Blue'
// 积类型（仅限结构体）
def Point = type (x: Float) & (y: Float)

// 解构赋值
var (x, y) = struct;
var (x1, x2) = tuple;

// 函数
def f = { (x: Int, y: Int) => x + y }

def f = { (x: Int, y: Int) => Int
    return x + y
}

def Func = type (x: Int, y: Int) => Int

// 泛型
def Array<T> = type [T]
def add<T> = { (x: T, y: T) => x + y }

// 接口
def Add<T> = interface {
    add: (rhs: T) => T
}
def Int impl Add<Int> {
    add = { (rhs: Int) => Int
        self + rhs
    }
}
```

## 控制流

```mioyi
if x > y {

} else if x == y && true {

} else {

}

for {}

for x < 10 { // 相当于 while
  x += 1
}

for x in [1, 2, 3] {}

// 模式匹配
match x {
  0 {}
  if x > 0 {} // 自定义逻辑
  is XXX {} //（仅限和类型）
} else {} // 默认分支
```

## 模块系统

```mioyi
// 模块系统
def foo = @import("foo.mioyi") // 支持解构赋值
export def f = {}
```

## 元编程

```mioyi
// @开头为注解，必须为元组/结构体
def @document = (descrption: String);

@document(descrption: "niubi")
var x = 1

@intrinsic // 编译器自动实现
def Int;

@intrinsic
def @import

@intrinsic
def @intrinsic;
```

## Demo 编译器

当前仓库中的 demo 编译器已经打通 `.mioyi` 源码、ANTLR AST、LLVM IR、
本机目标文件和可执行文件。程序入口固定为：

```mioyi
def main = { () => Int
    print("Hello, mioyi")
    return 0
}
```

构建与运行：

```sh
task configure
task build
./build/Debug/mioyi-lang tests/demo.mioyi -o demo
./demo
```

也可以使用 `-S` 输出 LLVM IR，或用 `ast` 子命令输出 JSON/YAML AST。

demo 阶段只提供一个内建标准库函数 `print(value)`。它可以打印当前编译器
能够构造的全部值，包括整数、浮点数、Unicode 字符、字符串、布尔值、
`void` 和嵌套数组。已经实现的语言核心包括：

- `var`、`val`、`def` 及可选的基础类型标注；
- 基础字面量、数组、索引和算术/比较/逻辑表达式；
- 顶层函数、参数、显式返回和末表达式隐式返回；
- `if/else if/else`、三种 `for` 形式、`break`、`continue`；
- 分号可选、AST 导出、LLVM 优化、目标文件与本机可执行文件输出。

以下设计在进入下一阶段前仍需确定，目前编译器会明确拒绝，而不会猜测语义：

- map、tuple、struct 的布局、相等性和可变性规则；
- 和类型/积类型的运行时表示，以及 `match` 的穷尽检查；
- 泛型是单态化还是字典传递，接口实现的重叠与查找规则；
- `def` 的常量求值边界、整数溢出行为和隐式数值转换；
- 模块搜索路径、循环依赖、导出可见性和初始化顺序；
- 注解的保留阶段、宏能力与沙箱边界；
- 内存所有权、引用/指针、数组和字符串的生命周期。

## TODO

- 内存模型、引用、指针
- null
- ABI / C interop（@ffi_import @ffi_export）
- 错误模型

## 鸣谢

- <https://rust-lang.org>
- <https://ziglang.org>
- <https://go.dev>
- <https://kotlinlang.org>
- <https://oxc.rs>
