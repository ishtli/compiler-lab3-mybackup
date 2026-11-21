# NKU Compiler Lab3

本仓库在 Lab1/Lab2 的文法分析与 AST 基础上，完成了 Lab3 的语义分析和 LLVM IR 生成，并实现了进阶的数组、浮点数和 `tensor` 类型。

## 已实现内容

### Lab3-1 语义分析

- 栈式作用域符号表：`enterScope` / `exitScope` / `addSymbol` / `getSymbol`
- 全局变量、局部变量、常量、函数与形参的重复定义检查
- 未声明变量/函数、函数实参与形参数量和类型检查、`main` 函数检查
- `int` / `float` / `bool` 隐式转换与常量折叠
- 除零检查、`void` 值参与表达式检查
- `break` / `continue` 只能出现在循环内
- 数组维度、下标数量、初始化列表（嵌套/展平/混合）检查
- 数组初始化列表的过量初始化、过量嵌套、默认零填充
- `tensor` 的形状检查、逐元素运算、标量提升、矩阵乘法 `@` 检查
- `-ast` 可打印带类型信息的 AST

### Lab3-2 中间代码生成

- 全局变量/常量（含数组、浮点、tensor）的 LLVM 全局定义
- 函数定义、形参、返回值、函数调用
- 局部变量 `alloca`、初始化、作用域与重名遮蔽
- 算术/比较/逻辑表达式、短路求值、赋值、逗号表达式
- `if` / `while` / `for` / `break` / `continue`
- 数组下标、多维数组、数组形参、数组初始化
- 浮点数的常量、变量、运算、比较、隐式转换
- `tensor` 的声明、初始化、下标、赋值、逐元素运算、标量提升、`@` 矩阵乘法
- tensor 形参与 tensor 返回值（返回值使用 sret 约定）

## 构建

```bash
make
```

编译器输出到 `bin/compiler`。

## 使用

```bash
# 词法、语法
./bin/compiler -lexer  -o out.txt input.sy
./bin/compiler -parser -o out.txt input.sy

# 类型检查后的 AST（带类型信息）
./bin/compiler -ast    -o out.ast input.sy

# LLVM IR
./bin/compiler -llvm   -o out.ll  input.sy

# 目标代码（后端）
./bin/compiler -S      -o out.s   input.sy
```

## tensor 语法示例

```c
tensor int a[4] = {1, 2, 3, 4};
tensor int b[4] = {5, 6, 7, 8};

int main()
{
    tensor int c[4] = a + b * 2 + 10;   // 标量提升 + 逐元素运算
    tensor int A[2][2] = {{1, 2}, {3, 4}};
    tensor int B[2][2] = {{5, 6}, {7, 8}};
    tensor int C[2][2] = A @ B;         // 矩阵乘法
    return c[0] + C[0][0];
}
```

tensor 形参的第一维用 `[]` 表示由实参决定，后续维度需为常量；tensor 返回值由函数体中的 `return` 表达式推断形状。

## 测试

```bash
python test.py --group Basic    --stage llvm --opt 0
python test.py --group Advanced --stage llvm --opt 0
```

`testcase/semant` 下的样例用于验证错误检测；`lab3/tensor_*.sy` 是 tensor 的补充测试。

## 参考

原始实验框架与说明见 `README-Original.md`。`lab3/tensor_*.sy` 为本次实现补充的 tensor 测试用例。

## lab3stepthru 分步记录

此分支从小组完成 Lab2 的节点 14fda0b 开始。Lab3 已有实现和原张量样例来自小组提交 9aa67f8。这里按依赖顺序拆分实现，并修正重复返回规则、大数组初始化和张量形参等问题。

构建后可以单独重跑某个阶段，也可以运行所有阶段的检查。

~~~sh
make clean
make
python3 scripts/lab3_step_test.py --help
python3 scripts/lab3_step_test.py all
~~~

| 提交阶段 | 对应检查 |
| --- | --- |
| 01 修复重复返回规则 | parser |
| 02 实现符号表 | symbols |
| 03 登记入口和运行库 | root |
| 04 检查维度、声明和初始化 | declarations |
| 05 检查表达式、左值和实参 | expressions |
| 06 检查语句并打印类型信息 | semantic |
| 07 生成标量和调用的 IR | scalar_ir |
| 08 生成控制流 | basic |
| 09 生成普通数组和浮点 IR | arrays |
| 10 初始化数组并压缩空初值 | advanced |
| 11 识别张量语法 | tensor_parser |
| 12 检查张量类型和形状 | tensor_types |
| 13 生成逐元素运算 | tensor_elements |
| 14 生成矩阵乘法 | tensor_matmul |
| 15 传递实际首维与固定形状返回 | tensor_calls |
| 16 回归小组张量样例及边界 | acceptance |

张量形参的首维必须写成 []。调用时把元素地址和实际首维一并传入，其他维度仍在编译期检查。整张量操作若有只能运行时确定的长度，生成的 LLVM IR 会先比较长度，形状不符时中止该程序。张量函数返回值仍要求形状在编译期已知。

课程功能样例和语义样例沿用仓库原件。自测覆盖 100 个 Basic 和 100 个 Advanced LLVM -O0 用例，以及 18 个语义负例和补充张量样例。
