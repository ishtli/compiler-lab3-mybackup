#include <middleend/visitor/codegen/ast_codegen.h>
#include <algorithm>

namespace ME
{
    namespace
    {
        DataType scalarType(FE::AST::Type* t)
        {
            if (!t) return DataType::UNK;
            if (t->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
                return scalarType(static_cast<FE::AST::TensorType*>(t)->elem);
            if (t->getTypeGroup() == FE::AST::TypeGroup::POINTER) return DataType::PTR;
            if (t->getBaseType() == FE::AST::Type_t::FLOAT) return DataType::F32;
            if (t->getBaseType() == FE::AST::Type_t::VOID) return DataType::VOID;
            if (t->getBaseType() == FE::AST::Type_t::BOOL) return DataType::I1;
            return DataType::I32;
        }
    }  // namespace

    void ASTCodeGen::visit(FE::AST::LeftValExpr& node, Module* m)
    {
        size_t ptr = genLValuePtr(node, m);

        DataType dt = convert(node.attr.val.value.type);
        size_t   r  = getNewRegId();
        insert(createLoadInst(dt, getRegOperand(ptr), r));
    }

    void ASTCodeGen::visit(FE::AST::LiteralExpr& node, Module* m)
    {
        (void)m;
        size_t reg = getNewRegId();
        switch (node.literal.type->getBaseType())
        {
            case FE::AST::Type_t::INT:
            case FE::AST::Type_t::LL:
            {
                int val = node.literal.getInt();
                insert(createArithmeticI32Inst_ImmeAll(Operator::ADD, val, 0, reg));
                break;
            }
            case FE::AST::Type_t::FLOAT:
            {
                float val = node.literal.getFloat();
                insert(createArithmeticF32Inst_ImmeAll(Operator::FADD, val, 0.0f, reg));
                break;
            }
            default: ERROR("Unsupported literal type");
        }
    }

    void ASTCodeGen::visit(FE::AST::UnaryExpr& node, Module* m)
    {
        if (!node.expr) return;
        handleUnaryCalc(*node.expr, node.op, curBlock, m);
    }

    void ASTCodeGen::handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        size_t ptr = genLValuePtr(lhs, m);

        size_t rhsReg = genExpr(rhs, m);
        DataType dst  = convert(lhs.attr.val.value.type);
        DataType src  = convert(rhs.attr.val.value.type);
        if (dst != src)
        {
            auto conv = createTypeConvertInst(src, dst, rhsReg);
            for (auto& inst : conv) insert(inst);
            rhsReg = getMaxReg();
        }
        insert(createStoreInst(dst, rhsReg, getRegOperand(ptr)));
    }

    void ASTCodeGen::visit(FE::AST::BinaryExpr& node, Module* m)
    {
        if (!node.lhs || !node.rhs) return;

        if (node.op == FE::AST::Operator::ASSIGN)
        {
            auto* lhs = dynamic_cast<FE::AST::LeftValExpr*>(node.lhs);
            if (!lhs) ERROR("Assignment lhs is not a left-value expression");
            handleAssign(*lhs, *node.rhs, m);
            return;
        }

        // 长加法链的迭代生成，避免左深 AST 造成递归爆栈。
        if (node.op == FE::AST::Operator::ADD && !node.attr.val.value.isTensor && !node.attr.val.value.isArray)
        {
            std::vector<FE::AST::ExprNode*> operands;
            FE::AST::ExprNode*              cur = &node;
            while (auto* b = dynamic_cast<FE::AST::BinaryExpr*>(cur))
            {
                if (b->op != FE::AST::Operator::ADD || !b->lhs || !b->rhs) break;
                operands.push_back(b->rhs);
                cur = b->lhs;
            }
            if (operands.size() > 1)
            {
                operands.push_back(cur);
                std::reverse(operands.begin(), operands.end());

                size_t   accReg  = genExpr(*operands[0], m);
                DataType accType = convert(operands[0]->attr.val.value.type);
                for (size_t i = 1; i < operands.size(); ++i)
                {
                    size_t   rhsReg  = genExpr(*operands[i], m);
                    DataType rhsType = convert(operands[i]->attr.val.value.type);
                    DataType pType   = (accType == DataType::F32 || rhsType == DataType::F32) ? DataType::F32
                                                                                              : DataType::I32;
                    if (accType != pType)
                    {
                        auto conv = createTypeConvertInst(accType, pType, accReg);
                        for (auto& inst : conv) insert(inst);
                        accReg = getMaxReg();
                    }
                    if (rhsType != pType)
                    {
                        auto conv = createTypeConvertInst(rhsType, pType, rhsReg);
                        for (auto& inst : conv) insert(inst);
                        rhsReg = getMaxReg();
                    }
                    size_t res = getNewRegId();
                    insert(new ArithmeticInst(pType == DataType::F32 ? Operator::FADD : Operator::ADD, pType,
                        getRegOperand(accReg), getRegOperand(rhsReg), getRegOperand(res)));
                    accReg  = res;
                    accType = pType;
                }
                return;
            }
        }

        handleBinaryCalc(*node.lhs, *node.rhs, node.op, curBlock, m);
    }

    void ASTCodeGen::visit(FE::AST::CallExpr& node, Module* m)
    {
        auto it = funcDecls.find(node.func);
        if (it == funcDecls.end()) ERROR("Unknown function in IR generation");
        FE::AST::FuncDeclStmt* fd = it->second;

        std::vector<std::pair<DataType, Operand*>> args;
        size_t argCount = node.args ? node.args->size() : 0;
        for (size_t i = 0; i < argCount; ++i)
        {
            FE::AST::ExprNode* arg = (*node.args)[i];
            if (!arg) continue;

            FE::AST::ParamDeclarator* p = (fd->params && i < fd->params->size()) ? (*fd->params)[i] : nullptr;
            FE::AST::VarAttr           pa;
            bool                       isPtr = false;
            DataType                   want  = DataType::I32;
            if (p)
            {
                pa    = p->varAttr;
                isPtr = pa.isPointer || (p->type && p->type->getTypeGroup() == FE::AST::TypeGroup::POINTER);
                FE::AST::Type* pt = pa.type;
                if (!pt || pt == FE::AST::voidType) pt = p->type;
                want = isPtr ? DataType::PTR : scalarType(pt);
            }

            size_t reg = genExpr(*arg, m);
            if (!isPtr)
            {
                DataType have = scalarType(arg->attr.val.value.type);
                if (have != want)
                {
                    auto conv = createTypeConvertInst(have, want, reg);
                    for (auto& inst : conv) insert(inst);
                    reg = getMaxReg();
                }
            }
            args.emplace_back(isPtr ? DataType::PTR : want, getRegOperand(reg));
        }

        DataType ret = scalarType(fd->retType);
        if (ret == DataType::VOID)
        {
            insert(createCallInst(DataType::VOID, fd->entry->getName(), args));
        }
        else
        {
            size_t res = getNewRegId();
            insert(createCallInst(ret, fd->entry->getName(), args, res));
        }
    }

    void ASTCodeGen::visit(FE::AST::CommaExpr& node, Module* m)
    {
        if (!node.exprs) return;
        for (FE::AST::ExprNode* e : *node.exprs)
        {
            if (e) genExpr(*e, m);
        }
    }
}  // namespace ME
