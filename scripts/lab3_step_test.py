#!/usr/bin/env python3
"""Run checks for successive Lab3 milestones from a built compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "bin/compiler"


def run(*args, **kwargs):
    return subprocess.run(
        list(map(str, args)), cwd=ROOT, capture_output=True, text=True, timeout=30, **kwargs
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


CHECKS = {"parser": parser, "symbols": symbols}


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
