#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>

namespace ME
{
    namespace
    {
        int tensorTotal(const std::vector<int>& dims)
        {
            int p = 1;
            for (int d : dims) p *= d;
            return p;
        }
    }  // namespace

    void ASTCodeGen::emitTensorAssign(size_t dstReg, size_t srcReg, DataType elem, int total)
    {
        if (total <= 0) return;
        size_t ivPtr = newEntryAlloca(DataType::I32);
        insert(createStoreInst(DataType::I32, getImmeI32Operand(0), getRegOperand(ivPtr)));

        Block* condBB = createBlock();
        Block* bodyBB = createBlock();
        Block* endBB  = createBlock();
        insert(createBranchInst(condBB->blockId));

        curBlock = condBB;
        size_t ivReg = getNewRegId();
        insert(createLoadInst(DataType::I32, getRegOperand(ivPtr), ivReg));
        size_t cond = getNewRegId();
        insert(createIcmpInst_ImmeRight(ICmpOp::SLT, ivReg, total, cond));
        insert(createBranchInst(cond, bodyBB->blockId, endBB->blockId));

        curBlock = bodyBB;
        size_t ptrSrc = emitElementPtr(srcReg, elem, ivReg);
        size_t val    = getNewRegId();
        insert(createLoadInst(elem, getRegOperand(ptrSrc), val));
        size_t ptrDst = emitElementPtr(dstReg, elem, ivReg);
        insert(createStoreInst(elem, val, getRegOperand(ptrDst)));
        size_t next = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, 1, ivReg, next));
        insert(createStoreInst(DataType::I32, next, getRegOperand(ivPtr)));
        insert(createBranchInst(condBB->blockId));

        curBlock = endBB;
    }

    void ASTCodeGen::emitTensorUnary(FE::AST::UnaryExpr& node, Module* m)
    {
        const auto& dims = node.attr.val.value.dims;
        int total = tensorTotal(dims);
        if (total <= 0) ERROR("Empty tensor in unary operation");
        DataType elem = convert(node.attr.val.value.type);

        size_t tmpArray = newEntryAlloca(elem, {total});
        FE::AST::VarAttr tmpAttr;
        tmpAttr.type      = node.attr.val.value.type;
        tmpAttr.isTensor  = true;
        tmpAttr.arrayDims = {total};
        size_t tmpBase = materializeElementPtr(getRegOperand(tmpArray), tmpAttr, false);

        size_t src = genExpr(*node.expr, m);
        size_t ivPtr = newEntryAlloca(DataType::I32);
        insert(createStoreInst(DataType::I32, getImmeI32Operand(0), getRegOperand(ivPtr)));

        Block* condBB = createBlock();
        Block* bodyBB = createBlock();
        Block* endBB  = createBlock();
        insert(createBranchInst(condBB->blockId));

        curBlock = condBB;
        size_t ivReg = getNewRegId();
        insert(createLoadInst(DataType::I32, getRegOperand(ivPtr), ivReg));
        size_t cond = getNewRegId();
        insert(createIcmpInst_ImmeRight(ICmpOp::SLT, ivReg, total, cond));
        insert(createBranchInst(cond, bodyBB->blockId, endBB->blockId));

        curBlock = bodyBB;
        size_t pSrc = emitElementPtr(src, elem, ivReg);
        size_t val  = getNewRegId();
        insert(createLoadInst(elem, getRegOperand(pSrc), val));
        size_t res = val;
        if (node.op == FE::AST::Operator::SUB)
        {
            res = getNewRegId();
            if (elem == DataType::F32)
                insert(createArithmeticF32Inst_ImmeLeft(Operator::FSUB, 0.0f, val, res));
            else
                insert(createArithmeticI32Inst_ImmeLeft(Operator::SUB, 0, val, res));
        }
        size_t pDst = emitElementPtr(tmpBase, elem, ivReg);
        insert(createStoreInst(elem, res, getRegOperand(pDst)));

        size_t next = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, 1, ivReg, next));
        insert(createStoreInst(DataType::I32, next, getRegOperand(ivPtr)));
        insert(createBranchInst(condBB->blockId));

        curBlock = endBB;
        emitElementPtrImme(tmpBase, elem, 0);
    }

    void ASTCodeGen::emitTensorElementwise(FE::AST::BinaryExpr& node, Module* m)
    {
        const auto& dims = node.attr.val.value.dims;
        int total = tensorTotal(dims);
        if (total <= 0) ERROR("Empty tensor in elementwise operation");

        DataType elem = convert(node.attr.val.value.type);
        size_t   tmpArray = newEntryAlloca(elem, {total});
        FE::AST::VarAttr tmpAttr;
        tmpAttr.type     = node.attr.val.value.type;
        tmpAttr.isTensor = true;
        tmpAttr.arrayDims = {total};
        size_t tmpBase = materializeElementPtr(getRegOperand(tmpArray), tmpAttr, false);

        bool lhsTensor = node.lhs->attr.val.value.isTensor;
        bool rhsTensor = node.rhs->attr.val.value.isTensor;
        size_t lhsReg = 0, rhsReg = 0;
        size_t scalarL = 0, scalarR = 0;
        if (lhsTensor)
            lhsReg = genExpr(*node.lhs, m);
        else
        {
            scalarL = genExpr(*node.lhs, m);
            DataType st = convert(node.lhs->attr.val.value.type);
            if (st != elem)
            {
                auto conv = createTypeConvertInst(st, elem, scalarL);
                for (auto& inst : conv) insert(inst);
                scalarL = getMaxReg();
            }
        }
        if (rhsTensor)
            rhsReg = genExpr(*node.rhs, m);
        else
        {
            scalarR = genExpr(*node.rhs, m);
            DataType st = convert(node.rhs->attr.val.value.type);
            if (st != elem)
            {
                auto conv = createTypeConvertInst(st, elem, scalarR);
                for (auto& inst : conv) insert(inst);
                scalarR = getMaxReg();
            }
        }

        size_t ivPtr = newEntryAlloca(DataType::I32);
        insert(createStoreInst(DataType::I32, getImmeI32Operand(0), getRegOperand(ivPtr)));

        Block* condBB = createBlock();
        Block* bodyBB = createBlock();
        Block* endBB  = createBlock();
        insert(createBranchInst(condBB->blockId));

        curBlock = condBB;
        size_t ivReg = getNewRegId();
        insert(createLoadInst(DataType::I32, getRegOperand(ivPtr), ivReg));
        size_t cond = getNewRegId();
        insert(createIcmpInst_ImmeRight(ICmpOp::SLT, ivReg, total, cond));
        insert(createBranchInst(cond, bodyBB->blockId, endBB->blockId));

        curBlock = bodyBB;
        size_t lval = scalarL;
        size_t rval = scalarR;
        if (lhsTensor)
        {
            size_t p = emitElementPtr(lhsReg, elem, ivReg);
            lval = getNewRegId();
            insert(createLoadInst(elem, getRegOperand(p), lval));
        }
        if (rhsTensor)
        {
            size_t p = emitElementPtr(rhsReg, elem, ivReg);
            rval = getNewRegId();
            insert(createLoadInst(elem, getRegOperand(p), rval));
        }

        Operator op = Operator::ADD;
        switch (node.op)
        {
            case FE::AST::Operator::ADD: op = elem == DataType::F32 ? Operator::FADD : Operator::ADD; break;
            case FE::AST::Operator::SUB: op = elem == DataType::F32 ? Operator::FSUB : Operator::SUB; break;
            case FE::AST::Operator::MUL: op = elem == DataType::F32 ? Operator::FMUL : Operator::MUL; break;
            case FE::AST::Operator::DIV: op = elem == DataType::F32 ? Operator::FDIV : Operator::DIV; break;
            case FE::AST::Operator::MOD: op = Operator::MOD; break;
            default: ERROR("Unsupported tensor binary operator");
        }

        size_t res = getNewRegId();
        insert(new ArithmeticInst(op, elem, getRegOperand(lval), getRegOperand(rval), getRegOperand(res)));
        size_t dstPtr = emitElementPtr(tmpBase, elem, ivReg);
        insert(createStoreInst(elem, res, getRegOperand(dstPtr)));

        size_t next = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, 1, ivReg, next));
        insert(createStoreInst(DataType::I32, next, getRegOperand(ivPtr)));
        insert(createBranchInst(condBB->blockId));

        curBlock = endBB;
        // 让结果指针成为当前最大寄存器
        emitElementPtrImme(tmpBase, elem, 0);
    }

    void ASTCodeGen::emitTensorMatMul(FE::AST::BinaryExpr& node, Module* m)
    {
        const auto& ld = node.lhs->attr.val.value.dims;
        const auto& rd = node.rhs->attr.val.value.dims;
        if (ld.size() != 2 || rd.size() != 2) ERROR("MatMul requires 2-D tensors");
        int M = ld[0], N = ld[1], P = rd[1];
        DataType elem = convert(node.attr.val.value.type);

        size_t tmpArray = newEntryAlloca(elem, {M * P});
        FE::AST::VarAttr tmpAttr;
        tmpAttr.type      = node.attr.val.value.type;
        tmpAttr.isTensor  = true;
        tmpAttr.arrayDims = {M * P};
        size_t tmpBase = materializeElementPtr(getRegOperand(tmpArray), tmpAttr, false);

        size_t aReg = genExpr(*node.lhs, m);
        size_t bReg = genExpr(*node.rhs, m);

        size_t iPtr = newEntryAlloca(DataType::I32);
        size_t jPtr = newEntryAlloca(DataType::I32);
        size_t kPtr = newEntryAlloca(DataType::I32);
        size_t sPtr = newEntryAlloca(elem);

        insert(createStoreInst(DataType::I32, getImmeI32Operand(0), getRegOperand(iPtr)));
        Block* iCond = createBlock();
        Block* iBody = createBlock();
        Block* iEnd  = createBlock();
        insert(createBranchInst(iCond->blockId));

        curBlock = iCond;
        size_t iReg = getNewRegId();
        insert(createLoadInst(DataType::I32, getRegOperand(iPtr), iReg));
        size_t iCmp = getNewRegId();
        insert(createIcmpInst_ImmeRight(ICmpOp::SLT, iReg, M, iCmp));
        insert(createBranchInst(iCmp, iBody->blockId, iEnd->blockId));

        curBlock = iBody;
        insert(createStoreInst(DataType::I32, getImmeI32Operand(0), getRegOperand(jPtr)));
        Block* jCond = createBlock();
        Block* jBody = createBlock();
        Block* jEnd  = createBlock();
        insert(createBranchInst(jCond->blockId));

        curBlock = jCond;
        size_t jReg = getNewRegId();
        insert(createLoadInst(DataType::I32, getRegOperand(jPtr), jReg));
        size_t jCmp = getNewRegId();
        insert(createIcmpInst_ImmeRight(ICmpOp::SLT, jReg, P, jCmp));
        insert(createBranchInst(jCmp, jBody->blockId, jEnd->blockId));

        curBlock = jBody;
        if (elem == DataType::F32)
            insert(createStoreInst(elem, getImmeF32Operand(0.0f), getRegOperand(sPtr)));
        else
            insert(createStoreInst(elem, getImmeI32Operand(0), getRegOperand(sPtr)));

        insert(createStoreInst(DataType::I32, getImmeI32Operand(0), getRegOperand(kPtr)));
        Block* kCond = createBlock();
        Block* kBody = createBlock();
        Block* kEnd  = createBlock();
        insert(createBranchInst(kCond->blockId));

        curBlock = kCond;
        size_t kReg = getNewRegId();
        insert(createLoadInst(DataType::I32, getRegOperand(kPtr), kReg));
        size_t kCmp = getNewRegId();
        insert(createIcmpInst_ImmeRight(ICmpOp::SLT, kReg, N, kCmp));
        insert(createBranchInst(kCmp, kBody->blockId, kEnd->blockId));

        curBlock = kBody;
        // A[i][k]
        size_t offA = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::MUL, N, iReg, offA));
        size_t offA2 = getNewRegId();
        insert(createArithmeticI32Inst(Operator::ADD, offA, kReg, offA2));
        size_t pA = emitElementPtr(aReg, elem, offA2);
        size_t vA = getNewRegId();
        insert(createLoadInst(elem, getRegOperand(pA), vA));
        // B[k][j]
        size_t offB = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::MUL, P, kReg, offB));
        size_t offB2 = getNewRegId();
        insert(createArithmeticI32Inst(Operator::ADD, offB, jReg, offB2));
        size_t pB = emitElementPtr(bReg, elem, offB2);
        size_t vB = getNewRegId();
        insert(createLoadInst(elem, getRegOperand(pB), vB));
        // sum += A * B
        size_t prod = getNewRegId();
        insert(new ArithmeticInst(elem == DataType::F32 ? Operator::FMUL : Operator::MUL, elem,
            getRegOperand(vA), getRegOperand(vB), getRegOperand(prod)));
        size_t sum = getNewRegId();
        insert(createLoadInst(elem, getRegOperand(sPtr), sum));
        size_t sum2 = getNewRegId();
        insert(new ArithmeticInst(elem == DataType::F32 ? Operator::FADD : Operator::ADD, elem,
            getRegOperand(sum), getRegOperand(prod), getRegOperand(sum2)));
        insert(createStoreInst(elem, sum2, getRegOperand(sPtr)));

        size_t kNext = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, 1, kReg, kNext));
        insert(createStoreInst(DataType::I32, kNext, getRegOperand(kPtr)));
        insert(createBranchInst(kCond->blockId));

        curBlock = kEnd;
        // C[i][j] = sum
        size_t offC = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::MUL, P, iReg, offC));
        size_t offC2 = getNewRegId();
        insert(createArithmeticI32Inst(Operator::ADD, offC, jReg, offC2));
        size_t pC = emitElementPtr(tmpBase, elem, offC2);
        size_t sumFinal = getNewRegId();
        insert(createLoadInst(elem, getRegOperand(sPtr), sumFinal));
        insert(createStoreInst(elem, sumFinal, getRegOperand(pC)));

        size_t jNext = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, 1, jReg, jNext));
        insert(createStoreInst(DataType::I32, jNext, getRegOperand(jPtr)));
        insert(createBranchInst(jCond->blockId));

        curBlock = jEnd;
        size_t iNext = getNewRegId();
        insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, 1, iReg, iNext));
        insert(createStoreInst(DataType::I32, iNext, getRegOperand(iPtr)));
        insert(createBranchInst(iCond->blockId));

        curBlock = iEnd;
        emitElementPtrImme(tmpBase, elem, 0);
    }
}  // namespace ME
