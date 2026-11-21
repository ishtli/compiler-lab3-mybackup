#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::ExprStmt& node, Module* m)
    {
        if (!node.expr) return;
        apply(*this, *node.expr, m);
    }

    void ASTCodeGen::visit(FE::AST::FuncDeclStmt& node, Module* m)
    {
        DataType funcRet = convert(node.retType);
        FuncDefInst* fd  = new FuncDefInst(funcRet, node.entry->getName());
        Function*    func = new Function(fd);

        curFunc      = func;
        curRetType   = funcRet;
        curSretReg   = 0;
        entryBlock   = func->createBlock();
        curBlock     = entryBlock;
        name2reg.enterScope();

        FuncDefInst::argList argRegs;
        std::vector<size_t>   argRegIds;
        std::vector<bool>     argIsPtr;

        if (node.params)
        {
            for (FE::AST::ParamDeclarator* p : *node.params)
            {
                if (!p) continue;
                FE::AST::VarAttr pa = p->varAttr;
                if (!pa.type || pa.type == FE::AST::voidType)
                {
                    pa.type = p->type;
                    if (pa.type && pa.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
                        pa.type = static_cast<FE::AST::TensorType*>(pa.type)->elem;
                    pa.isTensor = p->type && p->type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
                    pa.isPointer = (p->type && p->type->getTypeGroup() == FE::AST::TypeGroup::POINTER) ||
                                   (p->dims && !p->dims->empty());
                }
                DataType dt    = pa.isPointer ? DataType::PTR : convert(pa.type);
                size_t   regId = getNewRegId();
                argRegs.push_back({dt, getRegOperand(regId)});
                argRegIds.push_back(regId);
                argIsPtr.push_back(pa.isPointer);
            }
        }
        fd->argRegs = argRegs;

        if (node.params)
        {
            for (size_t i = 0; i < node.params->size(); ++i)
            {
                FE::AST::ParamDeclarator* p = (*node.params)[i];
                if (!p) continue;
                FE::AST::VarAttr pa = p->varAttr;
                if (!pa.type || pa.type == FE::AST::voidType)
                {
                    pa.type = p->type;
                    if (pa.type && pa.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
                        pa.type = static_cast<FE::AST::TensorType*>(pa.type)->elem;
                    pa.isTensor = p->type && p->type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
                    pa.isPointer = (p->type && p->type->getTypeGroup() == FE::AST::TypeGroup::POINTER) ||
                                   (p->dims && !p->dims->empty());
                }
                pa.isParam = true;

                size_t argReg = argRegIds[i];
                if (argIsPtr[i])
                {
                    name2reg.addSymbol(p->entry, argReg);
                    reg2attr[argReg] = pa;
                }
                else
                {
                    DataType dt  = convert(pa.type);
                    size_t   ptr = newEntryAlloca(dt);
                    insert(createStoreInst(dt, argReg, getRegOperand(ptr)));
                    name2reg.addSymbol(p->entry, ptr);
                    reg2attr[ptr] = pa;
                }
            }
        }

        if (node.body) apply(*this, *node.body, m);

        finishFunction();

        name2reg.exitScope();
        m->functions.push_back(func);

        curFunc      = nullptr;
        curBlock     = nullptr;
        entryBlock   = nullptr;
        curRetType   = DataType::VOID;
        curRetTensor = false;
        curSretReg   = 0;
    }

    void ASTCodeGen::visit(FE::AST::VarDeclStmt& node, Module* m)
    {
        if (!node.decl) return;
        apply(*this, *node.decl, m);
    }

    void ASTCodeGen::visit(FE::AST::BlockStmt& node, Module* m)
    {
        if (!node.stmts) return;
        name2reg.enterScope();
        for (FE::AST::StmtNode* stmt : *node.stmts)
        {
            if (!stmt) continue;
            apply(*this, *stmt, m);
        }
        name2reg.exitScope();
    }

    void ASTCodeGen::visit(FE::AST::ReturnStmt& node, Module* m)
    {
        if (!node.retExpr)
        {
            insert(createRetInst());
            return;
        }

        size_t   reg  = genExpr(*node.retExpr, m);
        DataType have = convert(node.retExpr->attr.val.value.type);
        if (have != curRetType)
        {
            auto conv = createTypeConvertInst(have, curRetType, reg);
            for (auto& inst : conv) insert(inst);
            reg = getMaxReg();
        }
        insert(createRetInst(curRetType, reg));
    }

    void ASTCodeGen::visit(FE::AST::WhileStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 while 循环 IR（条件块、循环体与结束块、循环标签）
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement WhileStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::IfStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 if/else IR（then/else/end 基本块与条件分支）
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement IfStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::BreakStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 break 的无条件跳转至循环结束块
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement BreakStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::ContinueStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 continue 的无条件跳转至循环步进/条件块
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement ContinueStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::ForStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 for 循环 IR（init/cond/body/step 基本块与循环标签）
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement ForStmt IR generation");
    }
}  // namespace ME
