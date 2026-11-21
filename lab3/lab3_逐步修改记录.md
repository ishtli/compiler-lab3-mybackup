# Lab3 逐步修改记录

这份笔记以 `lab3stepthru` 的 16 笔实验提交为顺序，从小组完成 Lab2 的 `14fda0b` 走到 Lab3 验收的 `82ee726`。代码和原有张量样例来自小组实现 `9aa67f8`，分支上的提交把它拆成可验证的小步，并补了数组初始化和张量边界。下面的提问是可复用的学习模板，回答对应这条分支的实际代码与测试。

## 动手前，先和 AI 做两轮对话

### 第一轮先问怎么读项目、怎么找到修改点

> 我刚做完 Lab2，只知道 lexer 把字符变成 token，yacc 把 token 变成 AST。请带我读这个仓库，告诉我从哪个入口开始、每层看哪个文件、我该追哪几个变量。先讲怎么找和怎么想，不给我完整实现。

**AI 回答**

先选一段你能手算的 SysY 程序。用 `int main() { return 1 + 2; }` 就够了。先想清楚谁识别 `+`，谁创建二元表达式，谁判断结果是 `int`，谁把它变成 IR。每读一个文件都回到这条链，遇到暂时用不到的后端类就先记下名字。

| 从哪里看 | 找什么 | 看完后试着复述什么 |
| --- | --- | --- |
| `main.cpp` | `-parser`、`-ast`、`-llvm` 三个出口 | 每个出口之前已经走过哪些阶段 |
| `frontend/parser/lexer.l` 和 `frontend/parser/yacc.y` | token、产生式、构造 AST 的动作 | `1 + 2` 的两个孩子来自哪里 |
| `frontend/ast/` 和 `interfaces/frontend/ast/ast_defs.h` | 表达式节点、位置、类型属性 | AST 存了哪些语法事实，哪些属性要等语义分析填 |
| `frontend/symbol/` 和 `frontend/ast/visitor/sementic_check/` | 作用域、`ASTChecker::visit` | 同一个名字如何查到正确的声明 |
| `middleend/visitor/codegen/` 和 `middleend/module/` | `ASTCodeGen`、基本块、指令 | 变量的地址是什么，读取它得到什么值 |

路径里的 `sementic_check` 是仓库的现有拼写。跟踪时先用 `rg -n 'ASTChecker|ASTCodeGen|IRPrinter' main.cpp` 找调用，再跟一个节点的 `visit`。读到变量声明时，先在纸上写下“我要检查什么、成功后要存什么、之后哪一层需要读取它”。比如声明 `int a[2][3];` 时，语法树里已经有两个维度表达式，语义分析还要把它们确定为 `[2,3]`。代码生成只需要使用已确认的维度求地址。

写代码前先给自己留一个通过样例和一个报错样例。也让 AI 先描述这个接口的输入、输出和失败条件。说清楚这些，再比较当前提交与前一笔的 diff。一次观察一个节点，比直接从几百行改动里猜主线容易。

### 第二轮问完整流程和阶段之间交什么

> 我找到了 parser、符号表和两个 visitor。请用一个变量声明和一次 return，把 Lab3-1、Lab3-2 串起来。解释 AST、符号属性、基本块各自负责什么，什么时候用哪个命令验证。再告诉我张量是在这条流程上增加了什么。

**AI 回答**

以 `int main() { int x = 1 + 2; return x; }` 为例。解析器建立声明、二元表达式和返回语句。`ASTChecker` 遍历这些节点，用符号表确认 `return x` 指向哪个变量，并在表达式上记录 `int` 和可在编译期求值的属性。通过检查后，`ASTCodeGen` 拿到全局符号和函数声明，再为函数建入口块、给 `x` 分配地址、写入表达式结果、读取 `x` 并生成 `ret i32`。`IRPrinter` 最后把模块打印成 LLVM IR。

~~~mermaid
flowchart LR
    Src["SysY 源文件"] --> Parse["Lexer 和 Parser"]
    Parse --> Ast["AST"]
    Ast --> Check["ASTChecker 与符号表"]
    Check --> Typed["带类型、常量和维度信息的 AST"]
    Typed --> Gen["ASTCodeGen"]
    Gen --> Module["Module、Function、Block 和 Instruction"]
    Module --> Out["LLVM IR"]
~~~

| 想确认什么 | 先用哪个出口 | 怎么读结果 |
| --- | --- | --- |
| 文法和树形是否正确 | `-parser` | 看 AST 谁是父节点、谁是孩子 |
| 类型和名字是否正确 | `-ast` | 成功时看类型标注，失败时看语义诊断 |
| 生成了哪些地址和控制流 | `-llvm -O0` | 看 `alloca`、`load`、`store`、`br`、`ret`，再用 `llvm-as` 验证 |
| 指令组合起来是否正确 | 链接 SysY 运行库并运行 | 比对标准输出和退出码 |

`isConstexpr` 表示语义上已知结果，维度和全局初值会用到它。`-O0` 的 IR 仍可能保留普通表达式的算术指令。读 `1 + 2` 时，`-ast` 检查“编译器知道结果是常量”，`-llvm` 检查“生成的程序确实算对”。

张量沿用这条主线。前端增加 `tensor` 和 `@`，再在类型属性里保存元素类型和形状。中端把张量降成连续元素缓冲区。形状告诉代码生成循环边界和行步长，`[]` 形参的未知首维由调用方额外传入。

**每一关怎么复盘**

先读课程的 Lab3 实验说明和张量要求，再用仓库的 [README-Original.md](../README-Original.md) 对照原始接口。在仓库根目录构建一次，在想研究的提交上运行对应的检查名。`git show` 可以只看那步改动，不需要切换正在写代码的工作区。早期提交的脚本只包含当时已经实现的检查。

~~~bash
make
git show afdc0b4 --stat
git show afdc0b4 -- frontend/parser/yacc.y
python3 scripts/lab3_step_test.py parser
~~~

## 01 清理重复的 return 规则

[提交 afdc0b4](https://github.com/YE-REM/compiler-lab3/commit/afdc0b4)

**动手前怎么问 AI**

> Bison 报 `reduce/reduce conflicts`，还说后面两条 `RETURN_STMT` 没用。先教我读警告和查文法。怎么判断是两条规则在竞争，还是我把运算符优先级设错了？

**AI 回答**

先按警告指向的产生式查文件里有几份 `RETURN_STMT`。合并留下了两份相同的规则，两份都能把 `RETURN SEMICOLON` 或 `RETURN EXPR SEMICOLON` 归约成 `RETURN_STMT`。读到分号以后，Bison 对同一个后续 token 可能有两种完全相同的归约选择。它通常选择靠前的规则，所以后面那份两条分支被报告为无用。

保留一份完整规则，让 `return;` 的 `ReturnStmt` 没有返回值，并让 `return expr;` 的节点保留表达式孩子。删除重复块后用 `-Werror=conflicts-rr` 和 `-Werror=conflicts-sr` 做严格检查，再用 `-parser` 看两种 `ReturnStmt` 都出现。这一步先保证语义访问器看到的树稳定。

**这一关怎么验**

`python3 scripts/lab3_step_test.py parser`。脚本要求 Bison 零冲突，并检查一份程序同时包含空返回和带值返回。

## 02 实现符号表的作用域

[提交 6ac1aec](https://github.com/YE-REM/compiler-lab3/commit/6ac1aec)

**动手前怎么问 AI**

> 给我两个同名变量，外层是 `int x`，内层是 `float x`。带我在纸上走一次进入块、查询、离开块。判重和普通查找应该用几个接口？

**AI 回答**

先追 `frontend/symbol/symbol_table.cpp` 的四个操作。`reset` 建全局作用域。`enterScope` 建一个指向外层的新节点。`getSymbol` 从当前节点往父节点找，先命中内层 `x`。`exitScope` 把当前节点退回到父节点，此后查到的是外层 `x`。每一层用自己的映射保存 `Entry* → VarAttr`。

判重只看当前层，所以 `hasSymbolInCurrentScope` 和 `getSymbol` 用途不同。内层还没有声明 `x` 时，查找能看到外层，判重仍允许在内层新声明。当前实现用 `std::list` 保存作用域节点，退出时移动当前指针，已经保存的节点和其中的属性地址不会因为退出而立刻失效。

**这一关怎么验**

`python3 scripts/lab3_step_test.py symbols`。测试同层、父层、遮蔽、退出和重置。你可以先猜每次查询会拿到哪个类型，再看断言。

## 03 登记编译单元、main 和运行库

[提交 aa72885](https://github.com/YE-REM/compiler-lab3/commit/aa72885)

**动手前怎么问 AI**

> 我已经能查变量。为什么还要单独登记函数签名？请对照 `ASTChecker::visit(Root&)`，告诉我在读函数体前要准备什么，以及怎么独立跑语义分析。

**AI 回答**

函数调用需要先知道返回类型、形参数量和每个形参的类型。`ASTChecker` 构造时用 `libFuncRegister` 登记 `getint`、`putint` 等 SysY 库函数。访问根节点时把这些名字放入全局符号表，再扫描顶层函数定义。这样检查函数体时可以从函数表找到签名，运行库名字也能和用户定义采用同样的重名检查。

在这一关，顶层还检查是否出现 `int main()`、它是否没有参数，以及是否混进了全局赋值之类的普通语句。`main.cpp` 新增 `-ast` 出口，先运行语义访问器。成功时打印 AST，失败时输出诊断并返回非零退出码。类型标注在第 06 步进一步接上。

**这一关怎么验**

`python3 scripts/lab3_step_test.py root`。测正常入口、无 `main`、错误签名、全局语句和运行库重名。

## 04 检查声明、常量维度与嵌套初值

[提交 9b5f13c](https://github.com/YE-REM/compiler-lab3/commit/9b5f13c)

**动手前怎么问 AI**

> Parser 已经能读 `int a[n];`。请帮我分清什么时候只是“读出了维度表达式”，什么时候才能说“数组有合法大小”。顺便手推一下 `int a[2][2] = {{1,2,3},{4}};` 为什么报错。

**AI 回答**

从 `registerGlobalDeclaration` 和 `decl_checker.cpp` 中的 `visit(VarDeclaration&)` 开始。声明里保存的是 AST 表达式。先访问一个维度表达式，再用 `evalConstInt` 要求类型为 `int` 且可在编译期求值，最后检查结果大于零。`const int n = 2; int a[n];` 可以得到具体维度，普通变量 `n`、`1.5` 和 `0` 分别违反常量性、类型和正值要求。通过后把形状保存在 `VarAttr::arrayDims`。

检查初值时，外层列表负责整个数组，进入子列表后要单独限制当前子数组的容量。`a[2][2]` 的每一行只能容纳两个元素。第一行的第三个初值会越过这一行，即使全部显式元素数量刚好是四，也应报错。`checkVarInit` 和 `flattenInitializer` 的关键变量是当前位置 `cursor`、当前层的步长和本层边界。先画出行的起点，再读递归，容易发现边界为何需要逐层检查。`const` 还要检查是否有合格的初值，`void` 不能作为变量类型。

**这一关怎么验**

`python3 scripts/lab3_step_test.py declarations`。这一步先用全局声明把合法常量维度和各类负例分开验证。

## 05 推导表达式、折叠常量、检查左值和调用

[提交 cc760a9](https://github.com/YE-REM/compiler-lab3/commit/cc760a9)

**动手前怎么问 AI**

> 请按“查名字 → 求类型 → 记录常量 → 校验目标”带我读 `expr_checker.cpp`。拿 `a / (2 - 2)` 和 `f(int a[][2])` 收到 `int b[3][4]` 举例。我要看错误在哪个节点出现。

**AI 回答**

访问变量先查表，再区分标量、数组元素和仍带剩余维度的数组值。变量能作左值，仍可能因 `const` 而不能赋值。访问运算节点时先检查子节点，从下往上做类型推导。`2 - 2` 折叠出编译期的零，所以即使 `a` 是运行时读到的，除法节点仍能发现分母确定为零。

调用表达式从登记表找到函数，再按位置核对数量、元素类型和数组形状。对于 `[][2]`，首维属于普通数组指针传参的退化部分，尾维 `2` 决定下一行的步长。把尾维为 `4` 的实参交给它，函数内同一个下标会算出不同的地址，因此在调用检查处报尾部形状不匹配。此步也接通了函数体内的直线语句，可以用真正的局部变量和运行库调用验证。

**这一关怎么验**

`python3 scripts/lab3_step_test.py expressions`。关注常量数组长度、隐式转换、未声明名字、除零、错形状和 const 赋值。

## 06 检查语句、循环和返回并打印类型

[提交 2a14d4c](https://github.com/YE-REM/compiler-lab3/commit/2a14d4c)

**动手前怎么问 AI**

> 我能判断一个表达式是否合法了。访问 `if`、`while`、`return` 时，还需要从外面带进来哪些状态？怎么确认报错发生在语义阶段？

**AI 回答**

看 `stmt_checker.cpp`。块和分支要维护作用域，函数体要记录当前返回类型，循环内要记录嵌套深度。`if (putint(1))` 的调用能独立成为语句，但它返回 `void`，无法作为条件。`break` 和 `continue` 要求当前循环深度大于零。`return` 要对照当前函数的返回类型，检查有无返回表达式，以及表达式是否能赋给该返回类型。

用 `-ast` 看 `1 + 2` 时，这一关会输出类似 `BinaryExpr + : int const` 的标注。用非法样例时，看是否有 `Semantic check failed` 和具体错误。单凭非零退出码无法分辨语法错误、语义错误和进程崩溃。

**这一关怎么验**

`python3 scripts/lab3_step_test.py semantic`。核对 18 个课程语义负例，再看合法 AST 的类型与常量信息。

## 07 把标量和函数调用翻译成 LLVM IR

[提交 b9ea4ec](https://github.com/YE-REM/compiler-lab3/commit/b9ea4ec)

**动手前怎么问 AI**

> 请带我用一个局部变量、一次调用和一次 return 画出代码生成顺序。为什么变量地址和写入变量的值要分开？我应该在 IR 里找哪些指令验证？

**AI 回答**

先从 `ASTCodeGen::visit(Root&)` 跟到函数和入口基本块。局部变量的 `alloca` 产生地址。初始化或赋值把右边计算的值用 `store` 写入地址。表达式读同一个变量时，代码生成再用 `load` 得到要参加运算的值。下面这个小程序里的 `x` 出现了两种用途。

~~~c
int twice(int p) { return p * 2; }
int main() { int x = 3; x = twice(x); return x; }
~~~

`twice(x)` 先读 `x`，把得到的值作为 `call` 实参。赋值号左边的 `x` 则提供该写入的地址。最后读回 `x`，产生 `ret i32`，预期退出码是 `6`。读 `expr_codegen.cpp` 时分开跟 `genLValuePtr` 和 `genExpr`，这样等做到数组时，同一个接口还可以分别表示元素地址和元素值。局部 `alloca` 放在函数入口，并通过基本块内的指令完成真正的初始化与使用。

**这一关怎么验**

`python3 scripts/lab3_step_test.py scalar_ir`。生成 IR，经 `llvm-as` 校验并链接运行库，核对整数运算、调用和布尔值转换的结果。

## 08 生成条件、循环与短路控制流

[提交 7271b53](https://github.com/YE-REM/compiler-lab3/commit/7271b53)

**动手前怎么问 AI**

> 我能翻译直线程序了。请先画出 while 的条件块、循环体、出口和回边，再告诉我 `break`、`continue` 和短路表达式怎么接到这些块上。

**AI 回答**

生成 `while` 时先建条件块、循环体和出口。条件为真跳到循环体，执行完回条件块。条件为假跳到出口。进入循环时保存“继续跳到哪里、退出跳到哪里”，嵌套循环只读当前栈顶。`while` 的 `continue` 回条件块，`for` 的 `continue` 先去步进块。`break` 到当前循环的出口。

对 `a && b`，先计算 `a`，只有真分支进入右操作数块。对 `a || b`，只有假分支进入右操作数块。两个路径在末尾合流，当前实现通过一个 `i1` 临时槽传递结果。最容易证明短路的方式是让右侧调用会打印内容的函数。如果 `0 && bump()` 或 `1 || bump()` 打印了内容，跳转结构就有问题。再逐块检查最后一条是否为 `br` 或 `ret`，避免生成未封闭的基本块。

**这一关怎么验**

`python3 scripts/lab3_step_test.py basic`。先验短路副作用和循环跳转，再跑 Basic 100 例。

## 09 计算普通多维数组地址并处理浮点

[提交 e6cfd4d](https://github.com/YE-REM/compiler-lab3/commit/e6cfd4d)

**动手前怎么问 AI**

> 对 `int a[2][3]`，请分别追 `a`、`a[1]`、`a[1][2]`。我该什么时候生成元素地址，什么时候读取元素，传入数组形参以后还剩哪些维度？

**AI 回答**

数组按行主序存储。`a[1][2]` 跳过一整行再移过两个元素，元素偏移为 `1 × 3 + 2 = 5`。`genIndexOffset` 用每个下标乘以后续维度的乘积，再把各项相加。`getelementptr` 按得到的元素偏移求地址，它本身不读取内容。读取 `a[1][2]` 才用 `load i32`，写则用 `store i32`。

`a[1]` 还剩一维，是这一行的起点，可以传给 `int row(int x[])`。整个二维 `a` 可以传给尾维匹配的二维形参。`materializeElementPtr` 负责把全局或局部分配的聚合对象变成元素基址，数组形参进来时已有元素指针。前面检查过尾维，代码生成就能用 `3` 作为每行步长。浮点数组也用同样的地址计算，但根据元素类型生成 `float` 的读写和算术，调用或赋值需要时才插入类型转换。

**这一关怎么验**

`python3 scripts/lab3_step_test.py arrays`。测二维数组传参、传某一行以及浮点数组元素的读写和隐式转换。

## 10 完成普通数组初始化并修复大数组编译内存问题

[提交 8e3016c](https://github.com/YE-REM/compiler-lab3/commit/8e3016c)

**动手前怎么问 AI**

> 我可以往数组元素写值了。请先手推 `{{1},{2}}` 如何补零，再帮我解释 `74_json.sy` 为什么会在编译时耗尽内存。怎样用小而明确的测试确认修复？

**AI 回答**

在 `int a[2][2] = {{1},{2}};` 里，第一个子列表只给第一行的首元素赋值，第一行末尾补零，第二个子列表从第二行开始。预期内存是 `[1,0,2,0]`。第 04 步已经做了递归对齐和容量检查，这一步把它接到运行时局部数组存储与全局 IR 常量上。局部数组列表初始化先生成 `llvm.memset` 清零，再按对齐后的偏移写入显式值。

`74_json.sy` 包含 `int buffer[50000000] = {};`。旧实现为了表达默认零，在语义阶段按总元素数构造 `initList`，一份空列表就引入五千万个记录。修改后只保存到最后一个显式初值为止的前缀，空列表不分配这些值。`initArrayGlb` 在输出时用 `zeroinitializer` 表达完全缺省的数组或子数组，常量数组下标求值也能把合法范围内的缺省元素读成零。这仍然允许 IR 声明一段很大的数组，关键是编译器无需先把其所有零值物化。

~~~llvm
@buffer = global [50000000 x i32] zeroinitializer
~~~

**这一关怎么验**

`python3 scripts/lab3_step_test.py advanced`。脚本检查五千万元素的零初始化 IR 是否紧凑，回归嵌套对齐，再跑 Advanced 100 例，含 `74_json.sy`。

## 11 识别 tensor 类型和 @ 的优先级

[提交 cb21a50](https://github.com/YE-REM/compiler-lab3/commit/cb21a50)

**动手前怎么问 AI**

> 我打算先让 Parser 认识 `tensor int a[2][3];` 和 `a+a@a`。请指给我看 lexer、yacc 和 AST 要各增加什么，我怎么只看 AST 就知道优先级写对了？

**AI 回答**

先分清两件事。`tensor int` 是声明时的类型，`@` 是二元运算符。`lexer.l` 把 `tensor` 识别为 `TENSOR`，把 `@` 识别为 `AT`。`yacc.y` 在 `TYPE` 接收 `TENSOR INT` 和 `TENSOR FLOAT`，通过 `TypeFactory::getTensorType` 建立以 `int` 或 `float` 为元素的类型。声明的方括号依旧解析成维度表达式列表。

`AT` 加到乘除模所在的 `MULDIV_EXPR`。规约时构造 `BinaryExpr(Operator::MATMUL, …)`。因此 `a + a @ a` 的根节点是 `+`，右孩子才是 `@`。这个阶段先核对树形，矩阵形状是否合法由后续语义检查决定。

~~~text
BinaryExpr +
├── a
└── BinaryExpr @
    ├── a
    └── a
~~~

`TensorType` 描述元素类型，形状属于具体的变量和表达式。例如同一个 `tensor int` 类型对象可以用在长度为 `3` 的向量和形状为 `2×3` 的矩阵上。判断赋值或运算是否合法时，还要看维度数据。这里的类型和运算符枚举在前面的语义提交里已随着共用 AST 属性接入，词法和文法到这一步才对源程序开放。

**这一关怎么验**

`python3 scripts/lab3_step_test.py tensor_parser`。确认 `tensor float` 类型和 `+` 下的 `@`，并再次跑严格的 Bison 检查。

## 12 检查张量类型、秩、维度与形状

[提交 415e2a9](https://github.com/YE-REM/compiler-lab3/commit/415e2a9)

**动手前怎么问 AI**

> AST 能读张量了。请先列判定规则，再带我对着 `VarAttr` 和 `ExprValue` 找信息。声明、部分下标、赋值、标量提升、参数和 `@` 各看哪些字段？我希望用反例知道是谁拒绝的。

**AI 回答**

沿声明到表达式的顺序看。`VarAttr::arrayDims` 是某个声明的形状，`VarValue::dims` 保存当前表达式的形状。`VarValue` 是 `ExprValue` 中记录具体类型和值的部分。语义访问器把标量元素类型、是否是张量、维度分别记录，代码生成可以复用这些数据。以 `tensor int a[2][3]` 为例，整张量的描述是 `int + tensor + [2,3]`。`a[1][2]` 则是一个标量 `int`，后续算术按标量处理。

| 要判断的代码 | 先核对什么 | 结果 |
| --- | --- | --- |
| `tensor int a[2][3]` | 至少一维，每维是正的 `int` 常量 | 记录秩 2 与形状 `[2,3]` |
| `tensor int a[1.5]` | `evalConstInt` 能否接受 | 浮点维度报错 |
| `a[1]`，其中 `a` 为二维张量 | 下标数是否等于秩 | 不完整的张量下标报错。与第 09 步的普通数组取行规则区分开 |
| `a = b`，二者均为张量 | 元素类型、秩和每一维 | 同形状、同元素类型可赋值 |
| `a + 2`，其中 `a` 为整型张量 | 标量类型是否与元素类型相同 | `2` 用于每个元素，形状沿用 `a` |
| `a + 0.5` 或对浮点张量求余 | 元素类型匹配和运算符限制 | 报语义错误 |
| `if (!a)` 或 `a == b` | 逻辑与比较需要合适的标量值 | 张量不能直接作逻辑或关系运算的操作数 |
| `f(tensor int x[][3])` | 第一维为 `[]`，实参秩及尾维匹配 | 秩是 2，尾维须等于 `3` |

读形参语法时，空的 `[]` 在 AST 用 `LiteralExpr(-1)` 作为占位，语义层把形参记成比如 `[-1,3]`。它代表首维由实参决定。源代码显式写 `[2][3]` 不能替代张量形参的空首维。整张量传递时检查实参元素类型和秩，尾维在编译期就能比较。第 15 步再把实际首维带到 LLVM 函数，处理只有运行时才能确定的相等关系。

**这一关怎么验**

`python3 scripts/lab3_step_test.py tensor_types`。先用 `-parser` 排除语法没接好的问题，再用 `-ast` 对照维数、类型、下标、逻辑与形参的语义诊断。

## 13 生成张量的存储、拷贝和逐元素运算

[提交 6547ca2](https://github.com/YE-REM/compiler-lab3/commit/6547ca2)

**动手前怎么问 AI**

> 前端已把 `a + b * 2 + 10` 标成整型张量。请从元素布局开始，带我追到 LLVM 的基本块和指令。哪些是标量值、哪些是元素指针？标量提升是否要真的构造一个装满 `2` 的张量？

**AI 回答**

先手算。设 `a = [1,2,3,4]`、`b = [5,6,7,8]`，`b * 2` 得 `[10,12,14,16]`，加上 `a` 得 `[11,14,17,20]`，加 `10` 后得到 `[21,24,27,30]`。每个二元节点都产生一个结果形状 `[4]`。如果不知道结果应该是什么，即使 IR 能汇编，也很难确认语义。

1. **先定存储。** 一个 `tensor int a[2][3]` 是六个连续的 `i32` 元素。`a[1][2]` 与普通数组一样走元素偏移 `1 × 3 + 2`。整个 `a` 作为张量值时，中端拿到元素基址。前端留在 AST 上的形状，用来决定遍历多少个元素。
2. **区分标量与张量。** `expr_codegen.cpp` 根据子表达式属性把张量算术分派给 `emitTensorElementwise`。张量操作数提供基址，标量操作数先计算一次得到寄存器中的值。`b * 2` 的循环每次取一个 `b[i]` 并使用同一个标量值 `2`。
3. **为结果开缓冲区。** 这一步的例子形状静态已知，先给结果分配足量元素。循环条件块比较 `i < total`，循环体用 `getelementptr` 找第 `i` 个元素，用 `load` 取输入值。整型乘法用 `mul`，浮点乘法用 `fmul`，再用 `store` 写结果。每次递增 `i` 并回条件块，退出后把结果缓冲区的元素指针交给父表达式。这里的循环由编译器写进 IR，在生成的程序运行时执行。
4. **初始化和赋值拷贝元素。** `emitTensorAssign` 把结果逐元素写到目标张量。`c = d` 保持 `c` 与 `d` 各自的存储，后续写 `c[2]` 不会改动 `d[2]`。`-c` 走 `emitTensorUnary`，元素类型和形状不变。花括号列表初始化则沿用数组的补零和逐元素写入。

读 `tensor_codegen.cpp` 时用下面这张对照表。寄存器编号由编译器分配，可以用指令和它们的依赖关系定位。

| 源程序的一项操作 | 在生成的 IR 中找什么 |
| --- | --- |
| 进入 `b * 2` | 计算标量 `2` 的值，以及 `b` 和结果的元素地址 |
| 比较当前下标与总元素数 | `icmp slt i32` 和 `br i1` |
| 计算第 `i` 项 | `getelementptr`、`load`、`mul` 或 `fmul`、`store` |
| 写回 `tensor int c[4] = ...` | 从临时缓冲区读，向 `c` 的地址写 |

**这一关怎么验**

`python3 scripts/lab3_step_test.py tensor_elements`。覆盖补零、标量加乘、浮点、整型除法与求余、负号、整张量拷贝和单元素赋值。既看 IR 能否汇编，也对照结果和退出码。

## 14 把 @ 矩阵乘法展开成三层循环

[提交 eafdd72](https://github.com/YE-REM/compiler-lab3/commit/eafdd72)

**动手前怎么问 AI**

> `*` 的整张量运算已经是逐元素乘法了。请用一个 `2×3` 和 `3×2` 的例子手算 `@`，再告诉我 `emitTensorMatMul` 的三层循环分别用哪一维，以及怎么从 IR 里发现步长弄反了。

**AI 回答**

先写形状。若 `A` 是 `M×N`，`B` 是 `N×P`，结果 `C` 是 `M×P`。前端在 `expr_checker.cpp` 检查两个操作数是二维张量、元素类型相同、内维匹配，并把结果形状写回 AST。对于两个尺寸静态已知的操作数，内维不匹配在 `-ast` 就能看到。例如 `[2,3] @ [2,2]` 的内维为 `3` 和 `2`，应拒绝。

代码生成时，`i` 遍历结果行，`j` 遍历结果列，`k` 遍历公共内维。每次开始新的 `(i,j)` 时把累加器清零，`k` 循环结束后才写 `C[i][j]`。拿三条偏移公式对照 `getelementptr` 前面的整数运算。

~~~text
A[i][k] -> A_base + (i * N + k) 个元素
B[k][j] -> B_base + (k * P + j) 个元素
C[i][j] -> C_base + (i * P + j) 个元素
~~~

这里用非方阵特别有用。若误把 `B` 的行步长写成 `N`，方阵时它有可能碰巧正确。用 `A = [[1,2,3],[4,5,6]]`，`B = [[7,8],[9,10],[11,12]]`，四个结果应为 `58 64 139 154`。比如第一项 `1×7 + 2×9 + 3×11 = 58`。IR 中应找到三组循环边界和每个结果元素的乘加。浮点路径用 `fmul` 和 `fadd`。

**这一关怎么验**

`python3 scripts/lab3_step_test.py tensor_matmul`。比对非方阵的四个输出、浮点乘法的输出，以及静态内维不匹配的错误。第 15 步的 `tensor_calls` 另验参数首维参与矩阵运算。

## 15 传递形参的实际首维和固定形状的返回结果

[提交 332fe85](https://github.com/YE-REM/compiler-lab3/commit/332fe85)

**动手前怎么问 AI**

> 我有 `tensor int calc(tensor int x[], tensor int y[])`。语义上两个首维都还是 `-1`。请用一段实际 IR 告诉我函数怎么看到长度、何时校验相等、临时数组怎么分配，以及张量返回值最终写到哪里。请同时解释调用方和被调方。

**AI 回答**

先用这个程序贯穿各层。返回的是固定长度的局部张量，两个输入的长度由调用者提供。将这段保存为 `/tmp/lab3-tensor-guide.sy` 即可复跑。

~~~c
tensor int calc(tensor int x[], tensor int y[]) {
    tensor int r[2] = x + y;
    return r;
}

int main() {
    tensor int a[2] = {1, 2};
    tensor int b[2] = {3, 4};
    tensor int c[2] = calc(a, b);
    return c[0] + c[1];
}
~~~

**先在 AST 上标出已知信息**

函数内部知道 `x` 和 `y` 的秩均为 `1`，形状写作 `[-1]`。比较形状时，`compatibleTensorShape` 要求秩和尾维一致，已知的首维也由语义分析比较。遇到 `-1`，保留只在运行时才能确认的相等条件。`r[2]` 的大小固定。返回检查从 `return r` 推断 `FuncDeclStmt::retDims = [2]`。如果函数直接返回长度来源仍是 `-1` 的 `x`，当前实验实现会报告返回形状必须在编译期已知。

**再跟调用约定**

在 `expr_codegen.cpp` 找 `visit(CallExpr&)`。传给每个张量形参的是两项，元素地址和实际首维。`a` 和 `b` 在调用点都已知长度为 `2`，所以传常量 `i32 2`。继续转发 `x[]` 到另一个函数时，则传当前函数接收的那个首维寄存器。元素类型、秩和尾维已通过前端检查，调用方无需把整份形状数组传递过去。

固定形状的张量返回也需要存储空间。调用者按 `retDims` 为结果分配缓冲区，把地址放在普通参数之前。被调函数在 `return` 时把元素拷贝到这个隐藏目标地址，并发出 `ret void`。源码的返回类型仍然是张量。下面是这个例子在 `-O0` 生成的签名和调用行，省略了函数体。两处的每个 `ptr` 都可以按顺序对上“结果、x、y”。

~~~llvm
define void @calc(ptr %reg_1, ptr %reg_2, i32 %reg_3, ptr %reg_4, i32 %reg_5)
; main 中的调用
call void @calc(ptr %reg_20, ptr %reg_16, i32 2, ptr %reg_18, i32 2)
~~~

**最后看循环边界与检查**

`stmt_codegen.cpp` 建函数参数时，给张量指针与它的首维建立关联。中端的 `tensorFirstDims` 从指针寄存器找到对应的首维操作数。`genLValuePtr` 为一个整个张量产生新指针时保留这层关联。这样 `x + y` 可以拿到 `%reg_3` 和 `%reg_5`，计算动态元素数并生成循环。对于 `x[][3]`，元素数是运行时首维乘编译期尾维 `3`。

这个例子里，逐元素相加先比两个形参长度，拷贝结果到 `r[2]` 时还要确认长度等于 `2`。观察到的关键 IR 指令如下。`requireSameTensorExtent` 用比较结果选择继续块或调用 `llvm.trap` 的块。`tensorElementCount` 计算循环上界，临时缓冲区用带元素数的 `alloca` 分配在函数入口，循环体反复使用它。

~~~llvm
; 比较 x 和 y 的首维，动态分配相加的缓冲区
%reg_10 = icmp eq i32 %reg_3, %reg_5
%reg_11 = alloca i32, i32 %reg_3
; 逐元素循环是否还要继续
%reg_14 = icmp slt i32 %reg_13, %reg_3
; 把结果交给固定长度的 r[2] 之前再检查
%reg_23 = icmp eq i32 2, %reg_3
~~~

参数参与矩阵乘法时，用实际首维确定结果行数，并验证右操作数的首维等于左操作数的内维。你可以把第 14 步的 `M` 改成这里拿到的运行时值，再用非方阵亲手走一轮循环。这个例子 `calc` 应写入 `[4,6]`，成功运行后程序退出码为 `10`。

~~~bash
./bin/compiler /tmp/lab3-tensor-guide.sy -llvm -O0 -o /tmp/lab3-tensor-guide.ll
llvm-as-15 /tmp/lab3-tensor-guide.ll -o /dev/null
clang-15 -static -Wno-override-module /tmp/lab3-tensor-guide.ll \
  -L./lib -lsysy_x86 -o /tmp/lab3-tensor-guide
/tmp/lab3-tensor-guide
echo "$?"
~~~

**这一关怎么验**

`python3 scripts/lab3_step_test.py tensor_calls`。检查直接在 `[]` 参数上做逐元素运算、以不同长度调用同一函数、转发参数、动态矩阵乘法、固定形状返回，以及错误的尾维或返回形状。想看实参长度不匹配在运行时怎么处理，再跑 `acceptance`。

## 16 补齐边界并验收全部阶段

[提交 82ee726](https://github.com/YE-REM/compiler-lab3/commit/82ee726)

**动手前怎么问 AI**

> 现在请带我做一遍验收。哪些情况应在语义阶段拒绝，哪些长度冲突只能在运行时看到？请给我全量命令，以及排查第一个失败该看什么。

**AI 回答**

先补一个小的负例。`tensor int a[1] = {{{1,2}}};` 的花括号层级超过张量的秩。旧路径会深入找出一个标量，可能把多余内容跳过。最终版在 `flattenInitializer` 判到过量嵌套时报语义错误。相反，给 `void add_into(tensor int a[], tensor int b[])` 分别传长度为 `2` 和 `3` 的张量时，两边都能各自匹配形参。函数内相加才需要比较它们的实际长度。验收脚本检查生成的程序进入 `llvm.trap` 路径。

用下面的命令重建，再从第一关跑到第十六关。只想回看一关，可以把 `all` 换成那一节给出的检查名。

~~~bash
make clean
make
python3 scripts/lab3_step_test.py all
~~~

| 验收项 | 在 `82ee726` 的结果 |
| --- | --- |
| `parser` 与 `tensor_parser` | Bison 零冲突，两种 return 和张量优先级通过 |
| 课程 `testcase/semant` | 18/18 产生语义诊断 |
| Basic LLVM `-O0` | 100/100 |
| Advanced LLVM `-O0` | 100/100，含 `74_json.sy` |
| 小组 `lab3/tensor_*.sy` | 3 个正例输出正确，1 个负例按预期报错 |
| 补充张量用例 | 通过元素运算、矩阵乘法、动态首维、固定返回以及形状与初始化负例 |

遇到失败就停在能复现它的最小入口。AST 树形不对，先贴 `-parser` 的输出和相关产生式。诊断不对，贴 `-ast` 的错误与最小源程序。IR 无法汇编，贴 `llvm-as` 指向的那条指令与所在基本块。程序能启动却算错，先写出手算结果，再对照偏移、跳转边界和读写顺序。

这时可以这样问 AI。

> 我卡在第 __ 步，当前提交是 __。我用的命令是 __，最小输入和实际输出如下。我预期 __。请先判断故障在哪一层，指出应该读的函数和一条能验证判断的命令，再和我讨论最小改动。
