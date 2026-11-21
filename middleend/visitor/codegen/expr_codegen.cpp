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

        // 标量访问：dims 为空且不是数组/张量
        bool scalar = !node.attr.val.value.isTensor && !node.attr.val.value.isArray &&
                      node.attr.val.value.dims.empty();

        if (!scalar)
        {
            // 整个数组/张量或子数组的地址，结果就是 genLValuePtr 返回的寄存器
            return;
        }

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
        if (node.attr.val.value.isTensor)
        {
            emitTensorUnary(node, m);
            return;
        }
        handleUnaryCalc(*node.expr, node.op, curBlock, m);
    }

    void ASTCodeGen::handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        size_t ptr = genLValuePtr(lhs, m);

        // 张量整体赋值
        if (lhs.attr.val.value.isTensor && (!lhs.indices || lhs.indices->empty()))
        {
            size_t src = genExpr(rhs, m);
            emitTensorAssign(ptr, src, convert(lhs.attr.val.value.type),
                lhs.attr.val.value.dims, rhs.attr.val.value.dims);
            return;
        }

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

    void ASTCodeGen::handleLogicalAnd(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        (void)node;
        size_t tmpPtr = newEntryAlloca(DataType::I1);
        size_t lhsReg = genBool(lhs, m);
        Block* rhsBB  = createBlock();
        Block* falseBB = createBlock();
        Block* endBB  = createBlock();

        insert(createBranchInst(lhsReg, rhsBB->blockId, falseBB->blockId));
        curBlock = falseBB;
        insert(createStoreInst(DataType::I1, getImmeI32Operand(0), getRegOperand(tmpPtr)));
        insert(createBranchInst(endBB->blockId));

        curBlock      = rhsBB;
        size_t rhsReg = genBool(rhs, m);
        insert(createStoreInst(DataType::I1, rhsReg, getRegOperand(tmpPtr)));
        insert(createBranchInst(endBB->blockId));

        curBlock     = endBB;
        size_t res   = getNewRegId();
        insert(createLoadInst(DataType::I1, getRegOperand(tmpPtr), res));
    }

    void ASTCodeGen::handleLogicalOr(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        (void)node;
        size_t tmpPtr  = newEntryAlloca(DataType::I1);
        size_t lhsReg  = genBool(lhs, m);
        Block* trueBB  = createBlock();
        Block* rhsBB   = createBlock();
        Block* endBB   = createBlock();

        insert(createBranchInst(lhsReg, trueBB->blockId, rhsBB->blockId));
        curBlock = trueBB;
        insert(createStoreInst(DataType::I1, getImmeI32Operand(1), getRegOperand(tmpPtr)));
        insert(createBranchInst(endBB->blockId));

        curBlock      = rhsBB;
        size_t rhsReg = genBool(rhs, m);
        insert(createStoreInst(DataType::I1, rhsReg, getRegOperand(tmpPtr)));
        insert(createBranchInst(endBB->blockId));

        curBlock   = endBB;
        size_t res = getNewRegId();
        insert(createLoadInst(DataType::I1, getRegOperand(tmpPtr), res));
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

        if (node.op == FE::AST::Operator::AND)
        {
            handleLogicalAnd(node, *node.lhs, *node.rhs, m);
            return;
        }
        if (node.op == FE::AST::Operator::OR)
        {
            handleLogicalOr(node, *node.lhs, *node.rhs, m);
            return;
        }
        if (node.op == FE::AST::Operator::MATMUL)
        {
            emitTensorMatMul(node, m);
            return;
        }

        if (node.lhs->attr.val.value.isTensor || node.rhs->attr.val.value.isTensor)
        {
            emitTensorElementwise(node, m);
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
            if (pa.isTensor)
                args.emplace_back(DataType::I32, tensorFirstDim(reg, arg->attr.val.value.dims));
        }

        DataType ret = scalarType(fd->retType);
        if (fd->retType->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
        {
            // tensor 返回值暂时只支持通过 sret 的调用约定，见 stmt_codegen.cpp
            ret = DataType::VOID;
            int total = 1;
            for (int d : fd->retDims) total *= d;
            DataType elem = scalarType(fd->retType);
            size_t bufArray = newEntryAlloca(elem, {total});
            FE::AST::VarAttr ba;
            ba.type      = fd->retType;
            if (ba.type && ba.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
                ba.type = static_cast<FE::AST::TensorType*>(ba.type)->elem;
            ba.isTensor  = true;
            ba.arrayDims = {total};
            size_t bufElem = materializeElementPtr(getRegOperand(bufArray), ba, false);
            args.insert(args.begin(), {DataType::PTR, getRegOperand(bufElem)});
            insert(createCallInst(DataType::VOID, fd->entry->getName(), args));
            return;
        }

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
