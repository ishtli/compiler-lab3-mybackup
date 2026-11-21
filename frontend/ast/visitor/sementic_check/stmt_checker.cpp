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
        bool ok = true;
        if (node.cond)
        {
            ok &= apply(*this, *node.cond);
            if (!isCondScalar(node.cond->attr.val.value))
            {
                reportError("while condition must be a scalar expression", *node.cond);
                ok = false;
            }
        }
        ++loopDepth;
        checkBranch(node.body);
        --loopDepth;
        return ok;
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        bool ok = true;
        if (node.cond)
        {
            ok &= apply(*this, *node.cond);
            if (!isCondScalar(node.cond->attr.val.value))
            {
                reportError("if condition must be a scalar expression", *node.cond);
                ok = false;
            }
        }
        checkBranch(node.thenStmt);
        checkBranch(node.elseStmt);
        return ok;
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        if (loopDepth == 0)
        {
            reportError("break statement outside loop", node);
            return false;
        }
        return true;
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        if (loopDepth == 0)
        {
            reportError("continue statement outside loop", node);
            return false;
        }
        return true;
    }

    bool ASTChecker::visit(ForStmt& node)
    {
        symTable.enterScope();
        bool ok = true;
        if (node.init) ok &= apply(*this, *node.init);
        if (node.cond)
        {
            ok &= apply(*this, *node.cond);
            if (!isCondScalar(node.cond->attr.val.value))
            {
                reportError("for condition must be a scalar expression", *node.cond);
                ok = false;
            }
        }
        if (node.step) ok &= apply(*this, *node.step);

        ++loopDepth;
        checkBranch(node.body);
        --loopDepth;
        symTable.exitScope();
        return ok;
    }
}  // namespace FE::AST
