#!/usr/bin/env python3
"""Run checks for successive Lab3 milestones from a built compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "bin/compiler"


def run(*args, timeout=30, **kwargs):
    return subprocess.run(
        list(map(str, args)), cwd=ROOT, capture_output=True, text=True, timeout=timeout, **kwargs
    )


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_case(work, name, source, mode="-llvm"):
    src = work / f"{name}.sy"
    out = work / f"{name}.out"
    src.write_text(source, encoding="utf-8")
    result = run(COMPILER, src, mode, "-O0", "-o", out)
    return result, out


def parser(work):
    grammar = run(
        "bison", "-Werror=conflicts-sr", "-Werror=conflicts-rr",
        "-o", work / "parser.cpp", ROOT / "frontend/parser/yacc.y",
    )
    require(grammar.returncode == 0, grammar.stderr)
    result, out = compile_case(
        work, "return_forms", "void stop() { return; }\nint main() { return 42; }\n", "-parser"
    )
    require(result.returncode == 0, result.stderr)
    require(out.read_text().count("ReturnStmt") == 2, "expected both return forms in AST")


def symbols(work):
    src = work / "symbol_scope.cpp"
    src.write_text(r"""
#include <frontend/symbol/symbol_table.h>
#include <cassert>

int main()
{
    FE::Sym::SymTable table;
    auto* name = FE::Sym::Entry::getEntry("value");
    FE::AST::VarAttr global(FE::AST::intType);
    FE::AST::VarAttr inner(FE::AST::floatType);
    assert(table.isGlobalScope() && table.getScopeDepth() == 0);
    assert(table.getSymbol(name) == nullptr);
    table.addSymbol(name, global);
    auto* outer = table.getSymbol(name);
    table.enterScope();
    assert(!table.hasSymbolInCurrentScope(name) && table.getSymbol(name) == outer);
    table.addSymbol(name, inner);
    assert(table.getSymbol(name)->type == FE::AST::floatType);
    table.exitScope();
    assert(table.getSymbol(name) == outer && table.getScopeDepth() == 0);
    table.reset();
    assert(table.isGlobalScope() && table.getSymbol(name) == nullptr);
}
""", encoding="utf-8")
    binary = work / "symbol_scope"
    objects = (
        "frontend/symbol/symbol_table.o",
        "interfaces/frontend/symbol/symbol_entry.o",
        "interfaces/frontend/ast/ast_defs.o",
    )
    result = run("c++", "-std=c++17", "-I.", "-Iinterfaces", "-Iutils",
                 src, *(ROOT / "obj" / item for item in objects), "-o", binary)
    require(result.returncode == 0, result.stderr)
    result = run(binary)
    require(result.returncode == 0, result.stderr)


def expect_semantic(work, name, source, error=None):
    result, out = compile_case(work, name, source, "-ast")
    if error is None:
        require(result.returncode == 0 and out.stat().st_size, f"{name}: {result.stderr}")
    else:
        require(result.returncode != 0 and "Semantic check failed" in result.stderr
                and error in result.stderr, f"{name}: expected {error!r}, got {result.stderr!r}")
    return out


def root(work):
    expect_semantic(work, "main_ok", "int main(){ return 0; }")
    expect_semantic(work, "missing_main", "int f(){ return 0; }", "main function is not defined")
    expect_semantic(work, "bad_main", "void main(){}", "main must have signature")
    expect_semantic(work, "global_stmt", "int x; x=2; int main(){return 0;}", "at global scope")
    expect_semantic(work, "builtin_reserved", "int getint(){return 0;} int main(){return 0;}",
                    "redefinition of function")


def declarations(work):
    expect_semantic(work, "valid_const_dim", "const int n=2; int a[n]={1}; int main(){return 0;}")
    expect_semantic(work, "float_dim", "int a[1.5]; int main(){return 0;}", "constant integer")
    expect_semantic(work, "nonconst_dim", "int n=2; int a[n]; int main(){return 0;}", "constant integer")
    expect_semantic(work, "zero_dim", "int a[0]; int main(){return 0;}", "must be positive")
    expect_semantic(work, "const_missing_init", "const int n; int main(){return 0;}", "must be initialized")
    expect_semantic(work, "one_row_overflow",
                    "int a[2][2]={{1,2,3},{4}}; int main(){return 0;}", "too many initializers")


def expressions(work):
    expect_semantic(work, "fold_and_call",
                    "const int n=1+2; int a[n]; int main(){float x=2; putint(a[0]+x); return !x;}")
    expect_semantic(work, "divide_zero", "int main(){int a=getint(); return a/(2-2);}", "division by zero")
    expect_semantic(work, "undefined", "int main(){return absent;}", "undeclared identifier")
    expect_semantic(work, "call_shape",
                    "int f(int a[][2]){return a[0][0];} int main(){int a[3][4]; return f(a);}",
                    "incompatible shape")
    expect_semantic(work, "const_assign", "int main(){const int a=1; a=2; return a;}", "const")


def semantic(work):
    cases = sorted((ROOT / "testcase/semant").glob("*.sy"))
    require(len(cases) == 18, f"expected 18 course semantic cases, got {len(cases)}")
    for src in cases:
        result = run(COMPILER, src, "-ast", "-o", work / "course.ast")
        require(result.returncode != 0 and "Semantic check failed" in result.stderr
                and "Error:" in result.stderr, f"{src.name}: {result.stderr!r}")
    out = expect_semantic(work, "typed", "int main(){ while(0){break;} return 1+2; }")
    require("BinaryExpr + : int const" in out.read_text(), "typed AST lacks folded integer")
    expect_semantic(work, "break", "int main(){break; return 0;}", "outside")
    expect_semantic(work, "condition", "int main(){if(putint(1)) return 1; return 0;}", "condition")


def llvm_tool(name):
    import shutil
    found = shutil.which(f"{name}-15") or shutil.which(name)
    require(found is not None, f"missing tool: {name}")
    return found


def expect_run(work, name, source, code=0, stdout=""):
    result, ir = compile_case(work, name, source)
    require(result.returncode == 0, f"{name}: {result.stderr}")
    result = run(llvm_tool("llvm-as"), ir, "-o", "/dev/null")
    require(result.returncode == 0, f"{name}: {result.stderr}")
    binary = work / name
    result = run(llvm_tool("clang"), "-x", "ir", ir, "-Wno-override-module",
                 "-static", "-L" + str(ROOT / "lib"), "-lsysy_x86", "-o", binary)
    require(result.returncode == 0, f"{name}: {result.stderr}")
    result = run(binary)
    require((result.returncode, result.stdout) == (code, stdout),
            f"{name}: exit {result.returncode}, stdout {result.stdout!r}, stderr {result.stderr!r}")
    return ir


def scalar_ir(work):
    expect_run(work, "scalar", """
int g=4;
int f(int x, int y) { int a=x*y; a=a+g; return a/2; }
int main(){ int x=f(6,5); putint(x); putch(10); return x; }
""", 17, "17\n")
    expect_run(work, "scalar_bool", "int main(){int a=5; return a+!a;}", 5)


def run_group(work, group):
    import os
    tools = work / "llvm-tools"
    tools.mkdir(exist_ok=True)
    for name in ("clang", "llvm-as"):
        alias = tools / name
        if not alias.exists():
            alias.symlink_to(llvm_tool(name))
    env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"])
    result = run("python3", ROOT / "test.py", "--group", group,
                 "--stage", "llvm", "--opt", "0", env=env, timeout=240)
    require(result.returncode == 0 and "Passed: 100 / 100" in result.stdout,
            f"{group}: {result.stdout[-14000:]} {result.stderr}")
    print(f"{group} LLVM -O0: 100/100")


def basic(work):
    expect_run(work, "control", """
int bump(){ putint(9); return 1; }
int main(){ int i=0; int s=0;
  while(i<5){ i=i+1; if(i==2) continue; if(i==4) break; s=s+i; }
  if(0&&bump()) return 1; if(1||bump()) return s; return 0;
}""", 4)
    run_group(work, "Basic")


def arrays(work):
    expect_run(work, "array_address", """
int f(int a[][3]) { return a[1][2]; }
int row(int a[]) { return a[1]; }
int main(){int a[2][3]; a[1][2]=7; a[1][1]=9; return f(a)+row(a[1]);}
""", 16)
    expect_run(work, "float_array", """
float f(float a[]) { return a[1]/2; }
int main(){float a[2]; a[1]=3; putfloat(f(a)); putch(10); return f(a)+4;}
""", 5, "0x1.8p+0\n")


def advanced(work):
    ir = expect_run(work, "big_zero", """
int a[50000000]={};
const int b[50000000]={};
const int n=b[49999999]+2;
int main(){ int x[n]; a[5]=3; x[1]=a[5]; return x[1]+b[5]; }
""", 3)
    require(ir.stat().st_size < 20000 and "zeroinitializer" in ir.read_text(),
            "empty initializer expanded instead of emitting compact LLVM zero")
    expect_run(work, "nested_init", """
int a[5][4][3]={{{2,3},6,7},7,8,11};
int main(){int b[5][4][3]={{{2,3},6,7},7,8,11};
return a[0][0][2]+2*a[0][1][0]+3*a[1][0][0]+b[1][0][2];}
""", 44)
    run_group(work, "Advanced")


def tensor_parser(work):
    result, out = compile_case(work, "tensor_parser",
        "tensor float a[2][2]; int main(){ tensor float b[2][2]=a+a@a; return 0; }",
        "-parser")
    require(result.returncode == 0, result.stderr)
    ast = out.read_text()
    require("tensor float" in ast and 0 <= ast.find("BinaryExpr +") < ast.find("BinaryExpr @"),
            f"unexpected tensor grammar or operator precedence: {ast}")
    parser(work)


def tensor_types(work):
    expect_semantic(work, "tensor_shape", """
int main(){ tensor int x[2][2]; tensor int y[4]; x=y; return 0; }
""", "type/shape mismatch")
    expect_semantic(work, "tensor_mixed", """
int main(){ tensor int x[2]; tensor int y[2]=x+0.5; return 0; }
""", "base types must match")
    expect_semantic(work, "tensor_rank", """
int main(){ tensor int x[2][2]; return x[0]; }
""", "subscript count")
    expect_semantic(work, "tensor_condition", """
int main(){ tensor int x[2]; if(!x) return 1; return 0; }
""", "not supported")
    expect_semantic(work, "tensor_float_mod", """
int main(){ tensor float x[2]; tensor float y[2]=x%2.0; return 0; }
""", "not supported for float tensors")
    expect_semantic(work, "tensor_float_dim", """
int main(){ tensor int x[1.5]; return 0; }
""", "constant integer")
    expect_semantic(work, "tensor_fixed_first", """
int f(tensor int x[2][2]){return x[0][0];}
int main(){ return 0; }
""", "first dimension must be []")
    expect_semantic(work, "tensor_scalar_alloc", "tensor int x; int main(){return 0;}",
                    "must specify dimensions")


CHECKS = {"parser": parser, "symbols": symbols, "root": root, "declarations": declarations, "expressions": expressions, "semantic": semantic, "scalar_ir": scalar_ir, "basic": basic, "arrays": arrays, "advanced": advanced, "tensor_parser": tensor_parser, "tensor_types": tensor_types}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("part", nargs="?", default="all", choices=[*CHECKS, "all"])
    args = cli.parse_args()
    parts = list(CHECKS) if args.part == "all" else [args.part]
    with tempfile.TemporaryDirectory(prefix="lab3-step-") as tmp:
        for part in parts:
            CHECKS[part](Path(tmp))
            print(f"{part}: PASS")


if __name__ == "__main__":
    main()
