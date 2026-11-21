#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(ExprStmt& node)
    {
        if (!node.expr) return true;
        return apply(*this, *node.expr);
    }

    bool ASTChecker::visit(FuncDeclStmt& node)
    {
        if (!node.body) return true;

        FuncDeclStmt* oldFunc    = curFuncDecl;
        Type*         oldRetType = curFuncRetType;
        bool          oldIsTens  = curFuncRetIsTensor;
        bool          oldHasRet  = funcHasReturn;

        curFuncDecl        = &node;
        curFuncRetType     = elementType(node.retType);
        curFuncRetIsTensor = isTensorType(node.retType);
        funcHasReturn      = false;
        node.retIsTensor   = curFuncRetIsTensor;

        symTable.enterScope();

        if (node.params)
        {
            for (ParamDeclarator* p : *node.params)
            {
                if (!p || !p->entry) continue;
                VarAttr pa = p->varAttr;
                if (!pa.type) pa.type = elementType(p->type);
                pa.isParam    = true;
                pa.scopeLevel = symTable.getScopeDepth();
                if (symTable.hasSymbolInCurrentScope(p->entry))
                {
                    reportError("duplicate parameter '" + p->entry->getName() + "'", *p);
                }
                else
                {
                    symTable.addSymbol(p->entry, pa);
                }
            }
        }

        bool ok = apply(*this, *node.body);

        symTable.exitScope();

        curFuncDecl        = oldFunc;
        curFuncRetType     = oldRetType;
        curFuncRetIsTensor = oldIsTens;
        funcHasReturn      = oldHasRet;
        return ok;
    }

    bool ASTChecker::visit(VarDeclStmt& node)
    {
        if (!node.decl) return true;
        if (symTable.isGlobalScope())
        {
            // 全局变量已在 Root 的预扫描阶段检查
            return true;
        }
        return apply(*this, *node.decl);
    }

    bool ASTChecker::visit(BlockStmt& node)
    {
        symTable.enterScope();
        bool ok = true;
        if (node.stmts)
        {
            for (StmtNode* stmt : *node.stmts)
            {
                if (!stmt) continue;
                ok &= apply(*this, *stmt);
            }
        }
        symTable.exitScope();
        return ok;
    }

    bool ASTChecker::visit(ReturnStmt& node)
    {
        funcHasReturn = true;
        if (!curFuncDecl)
        {
            reportError("return statement outside function", node);
            return false;
        }

        bool retVoid = curFuncRetType == voidType && !curFuncRetIsTensor;
        if (retVoid)
        {
            if (node.retExpr)
            {
                reportError("void function should not return a value", node);
                apply(*this, *node.retExpr);
                return false;
            }
            return true;
        }

        if (!node.retExpr)
        {
            reportError("non-void function should return a value", node);
            return false;
        }

        if (!apply(*this, *node.retExpr)) return false;

        const VarValue& v = node.retExpr->attr.val.value;
        if (curFuncRetIsTensor)
        {
            if (!v.isTensor)
            {
                reportError("tensor-returning function must return a tensor", *node.retExpr);
                return false;
            }
            if (v.type->getBaseType() != curFuncRetType->getBaseType())
            {
                reportError("returned tensor element type does not match function return type", *node.retExpr);
                return false;
            }
            if (!curFuncDecl->retDimsKnown)
            {
                curFuncDecl->retDims      = v.dims;
                curFuncDecl->retDimsKnown = true;
                curFuncDecl->retIsTensor  = true;
            }
            else if (curFuncDecl->retDims != v.dims)
            {
                reportError("inconsistent tensor return shape", *node.retExpr);
                return false;
            }
            return true;
        }

        return checkAssignable(curFuncRetType, false, {}, *node.retExpr, "return statement");
    }

    bool ASTChecker::visit(WhileStmt& node)
    {
        // TODO(Lab3-1): 实现while循环的语义检查
        // 检查作用域，访问条件表达式，管理循环深度，访问循环体
        (void)node;
        TODO("Lab3-1: Implement WhileStmt semantic checking");
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        // TODO(Lab3-1): 实现if语句的语义检查
        // 检查作用域，访问条件表达式，分别访问then和else分支
        (void)node;
        TODO("Lab3-1: Implement IfStmt semantic checking");
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        // TODO(Lab3-1): 实现break语句的语义检查
        // 检查是否在循环内使用
        (void)node;
        TODO("Lab3-1: Implement BreakStmt semantic checking");
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        // TODO(Lab3-1): 实现continue语句的语义检查
        // 检查是否在循环内使用
        (void)node;
        TODO("Lab3-1: Implement ContinueStmt semantic checking");
    }

    bool ASTChecker::visit(ForStmt& node)
    {
        // TODO(Lab3-1): 实现for循环的语义检查
        // 检查作用域，访问初始化、条件、步进表达式，管理循环深度
        (void)node;
        TODO("Lab3-1: Implement ForStmt semantic checking");
    }
}  // namespace FE::AST
