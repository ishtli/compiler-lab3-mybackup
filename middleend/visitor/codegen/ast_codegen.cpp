#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>
#include <numeric>
#include <limits>

namespace ME
{
    void ASTCodeGen::insert(Instruction* inst)
    {
        if (!inst) return;
        ASSERT(curFunc && "No current function");
        if (!curBlock) curBlock = curFunc->createBlock();
        if (!curBlock->insts.empty() && curBlock->insts.back()->isTerminator())
        {
            // 当前基本块已经被 return/break/continue 终止，后续指令放入新的不可达块
            curBlock = curFunc->createBlock();
        }
        curBlock->insertBack(inst);
    }

    bool ASTCodeGen::currentBlockTerminated() const
    {
        return curBlock && !curBlock->insts.empty() && curBlock->insts.back()->isTerminator();
    }

    void ASTCodeGen::ensureOpenBlock()
    {
        if (!curBlock) curBlock = curFunc->createBlock();
        if (currentBlockTerminated()) curBlock = curFunc->createBlock();
    }

    size_t ASTCodeGen::newEntryAlloca(DataType t, const std::vector<int>& dims)
    {
        ASSERT(curFunc && entryBlock && "No current function/entry block");
        size_t      reg = getNewRegId();
        AllocaInst* a   = createAllocaInst(t, reg, dims);
        entryBlock->insertFront(a);
        return reg;
    }

    size_t ASTCodeGen::genExpr(FE::AST::ExprNode& node, Module* m)
    {
        apply(*this, node, m);
        return getMaxReg();
    }

    size_t ASTCodeGen::toBoolReg(size_t reg, DataType dt)
    {
        if (dt == DataType::I1) return reg;
        if (dt == DataType::I32)
        {
            size_t dest = getNewRegId();
            insert(createIcmpInst_ImmeRight(ICmpOp::NE, reg, 0, dest));
            return dest;
        }
        if (dt == DataType::F32)
        {
            size_t dest = getNewRegId();
            insert(createFcmpInst_ImmeRight(FCmpOp::ONE, reg, 0.0f, dest));
            return dest;
        }
        ERROR("Cannot convert type to bool");
    }

    size_t ASTCodeGen::genBool(FE::AST::ExprNode& node, Module* m)
    {
        size_t reg = genExpr(node, m);
        return toBoolReg(reg, convert(node.attr.val.value.type));
    }

    bool ASTCodeGen::getSymbolAttr(FE::Sym::Entry* entry, FE::AST::VarAttr& attr, bool& isGlobal, Operand*& base)
    {
        if (!entry) return false;
        size_t reg = name2reg.getReg(entry);
        if (reg != static_cast<size_t>(-1))
        {
            auto it = reg2attr.find(reg);
            if (it == reg2attr.end()) return false;
            attr     = it->second;
            isGlobal = false;
            base     = getRegOperand(reg);
            return true;
        }
        auto git = glbSymbols.find(entry);
        if (git != glbSymbols.end())
        {
            attr     = git->second;
            isGlobal = true;
            base     = getGlobalOperand(entry->getName());
            return true;
        }
        return false;
    }

    size_t ASTCodeGen::materializeElementPtr(Operand* base, const FE::AST::VarAttr& attr, bool isGlobal)
    {
        (void)isGlobal;
        DataType elem = convert(attr.type);
        if (attr.isParam)
        {
            // 形参数组/张量的参数已经是指向其首元素的指针
            if (base->getType() == OperandType::REG) return base->getRegNum();
            size_t res = getNewRegId();
            insert(new GEPInst(elem, DataType::I32, base, getRegOperand(res)));
            return res;
        }
        if (attr.arrayDims.empty())
        {
            if (base->getType() == OperandType::REG) return base->getRegNum();
            size_t res = getNewRegId();
            insert(new GEPInst(elem, DataType::I32, base, getRegOperand(res), {}, {getImmeI32Operand(0)}));
            return res;
        }

        // 取数组首元素的地址：getelementptr [d0 x ...], ptr base, i32 0, i32 0, ...
        size_t                  res = getNewRegId();
        std::vector<Operand*>   zeros;
        zeros.reserve(attr.arrayDims.size() + 1);
        for (size_t i = 0; i <= attr.arrayDims.size(); ++i) zeros.push_back(getImmeI32Operand(0));
        insert(new GEPInst(elem, DataType::I32, base, getRegOperand(res), attr.arrayDims, zeros));
        return res;
    }

    size_t ASTCodeGen::emitElementPtr(size_t baseReg, DataType elem, size_t offsetReg)
    {
        size_t res = getNewRegId();
        insert(new GEPInst(elem, DataType::I32, getRegOperand(baseReg), getRegOperand(res), {},
            {getRegOperand(offsetReg)}));
        return res;
    }

    size_t ASTCodeGen::emitElementPtrImme(size_t baseReg, DataType elem, int offset)
    {
        size_t res = getNewRegId();
        insert(new GEPInst(elem, DataType::I32, getRegOperand(baseReg), getRegOperand(res), {},
            {getImmeI32Operand(offset)}));
        return res;
    }

    size_t ASTCodeGen::genIndexOffset(
        const std::vector<int>& dims, const std::vector<size_t>& idxRegs, const std::vector<int>& constVals)
    {
        size_t acc     = 0;
        bool   hasAcc  = false;
        bool   accIsIm = false;
        int    accIm   = 0;

        for (size_t i = 0; i < idxRegs.size(); ++i)
        {
            int stride = 1;
            for (size_t j = i + 1; j < dims.size(); ++j)
            {
                if (dims[j] > 0) stride *= dims[j];
            }

            bool isConst = i < constVals.size() && constVals[i] != std::numeric_limits<int>::min();
            if (isConst)
            {
                int v = constVals[i] * stride;
                if (v == 0) continue;
                if (!hasAcc)
                {
                    acc     = 0;
                    accIm   = v;
                    accIsIm = true;
                    hasAcc  = true;
                }
                else if (accIsIm)
                {
                    accIm += v;
                }
                else
                {
                    size_t r = getNewRegId();
                    insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, v, acc, r));
                    acc     = r;
                    accIsIm = false;
                }
                continue;
            }

            size_t term = idxRegs[i];
            if (stride != 1)
            {
                size_t r = getNewRegId();
                insert(createArithmeticI32Inst_ImmeLeft(Operator::MUL, stride, term, r));
                // 上面的顺序是 stride * term；ArithmeticInst 支持立即数在左侧
                term = r;
            }

            if (!hasAcc)
            {
                acc     = term;
                accIsIm = false;
                hasAcc  = true;
            }
            else if (accIsIm)
            {
                size_t r = getNewRegId();
                insert(createArithmeticI32Inst_ImmeLeft(Operator::ADD, accIm, term, r));
                acc     = r;
                accIsIm = false;
            }
            else
            {
                size_t r = getNewRegId();
                insert(createArithmeticI32Inst(Operator::ADD, acc, term, r));
                acc = r;
            }
        }

        if (!hasAcc)
        {
            size_t r = getNewRegId();
            insert(createArithmeticI32Inst_ImmeAll(Operator::ADD, 0, 0, r));
            return r;
        }
        if (accIsIm)
        {
            size_t r = getNewRegId();
            insert(createArithmeticI32Inst_ImmeAll(Operator::ADD, accIm, 0, r));
            return r;
        }
        return acc;
    }

    size_t ASTCodeGen::genLValuePtr(FE::AST::LeftValExpr& node, Module* m)
    {
        FE::AST::VarAttr attr;
        bool             isGlobal = false;
        Operand*         base     = nullptr;
        if (!getSymbolAttr(node.entry, attr, isGlobal, base))
        {
            ERROR("Unknown variable in IR generation: %s", node.entry ? node.entry->getName().c_str() : "?");
        }

        size_t baseReg = materializeElementPtr(base, attr, isGlobal);
        const auto& dims = attr.arrayDims;
        size_t n = node.indices ? node.indices->size() : 0;
        if (n > dims.size()) ERROR("Too many subscripts in IR generation");

        std::vector<size_t> idxRegs;
        std::vector<int>    constVals;
        if (node.indices)
        {
            for (FE::AST::ExprNode* idx : *node.indices)
            {
                if (!idx) continue;
                size_t reg = genExpr(*idx, m);
                DataType dt = convert(idx->attr.val.value.type);
                if (dt == DataType::I1)
                {
                    size_t z = getNewRegId();
                    insert(createZextInst(reg, z, 1, 32));
                    reg = z;
                    dt  = DataType::I32;
                }
                else if (dt == DataType::F32)
                {
                    size_t c = getNewRegId();
                    insert(createFP2SIInst(reg, c));
                    reg = c;
                    dt  = DataType::I32;
                }
                if (dt != DataType::I32) ERROR("Array subscript is not an integer");
                idxRegs.push_back(reg);
                if (idx->attr.val.isConstexpr)
                {
                    constVals.push_back(idx->attr.val.value.getInt());
                }
                else
                {
                    constVals.push_back(std::numeric_limits<int>::min());
                }
            }
        }

        DataType elem = convert(attr.type);
        if (idxRegs.empty())
        {
            return emitElementPtrImme(baseReg, elem, 0);
        }

        size_t offset = genIndexOffset(dims, idxRegs, constVals);
        return emitElementPtr(baseReg, elem, offset);
    }

    void ASTCodeGen::finishBlockWithDefaultRet(Block* block)
    {
        if (!block) return;
        if (!block->insts.empty() && block->insts.back()->isTerminator()) return;

        if (curRetTensor || curRetType == DataType::VOID)
        {
            block->insertBack(createRetInst());
        }
        else if (curRetType == DataType::F32)
        {
            block->insertBack(createRetInst(0.0f));
        }
        else
        {
            block->insertBack(createRetInst(0));
        }
    }

    void ASTCodeGen::finishFunction()
    {
        if (!curFunc) return;
        for (auto& [label, block] : curFunc->blocks)
        {
            (void)label;
            finishBlockWithDefaultRet(block);
        }
    }

    void ASTCodeGen::libFuncRegister(Module* m)
    {
        auto& decls = m->funcDecls;

        // int getint();
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getint"));

        // int getch();
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getch"));

        // int getarray(int a[]);
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getarray", {DataType::PTR}));

        // float getfloat();
        decls.emplace_back(new FuncDeclInst(DataType::F32, "getfloat"));

        // int getfarray(float a[]);
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getfarray", {DataType::PTR}));

        // void putint(int a);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putint", {DataType::I32}));

        // void putch(int a);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putch", {DataType::I32}));

        // void putarray(int n, int a[]);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putarray", {DataType::I32, DataType::PTR}));

        // void putfloat(float a);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putfloat", {DataType::F32}));

        // void putfarray(int n, float a[]);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putfarray", {DataType::I32, DataType::PTR}));

        // void starttime(int lineno);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "_sysy_starttime", {DataType::I32}));

        // void stoptime(int lineno);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "_sysy_stoptime", {DataType::I32}));

        // llvm memset
        decls.emplace_back(new FuncDeclInst(
            DataType::VOID, "llvm.memset.p0.i32", {DataType::PTR, DataType::I8, DataType::I32, DataType::I1}));
    }

    void ASTCodeGen::handleGlobalVarDecl(FE::AST::VarDeclStmt* decls, Module* m)
    {
        (void)m;
        if (!decls || !decls->decl || !decls->decl->decls) return;
        for (FE::AST::VarDeclarator* d : *decls->decl->decls)
        {
            if (!d) continue;
            auto* lv = dynamic_cast<FE::AST::LeftValExpr*>(d->lval);
            if (!lv || !lv->entry) continue;

            auto it = glbSymbols.find(lv->entry);
            if (it == glbSymbols.end()) continue;
            FE::AST::VarAttr attr = it->second;
            DataType          dt   = convert(attr.type);

            if (attr.arrayDims.empty())
            {
                Operand* init = nullptr;
                if (d->init)
                {
                    if (auto* initExpr = dynamic_cast<FE::AST::Initializer*>(d->init))
                    {
                        if (initExpr->init_val && initExpr->init_val->attr.val.isConstexpr)
                        {
                            FE::AST::VarValue v = initExpr->init_val->attr.val.value;
                            if (dt == DataType::F32)
                                init = getImmeF32Operand(v.getFloat());
                            else
                                init = getImmeI32Operand(v.getInt());
                        }
                    }
                }
                m->globalVars.push_back(new GlbVarDeclInst(dt, lv->entry->getName(), init));
            }
            else
            {
                m->globalVars.push_back(new GlbVarDeclInst(dt, lv->entry->getName(), attr));
            }
        }
    }

    void ASTCodeGen::visit(FE::AST::Root& node, Module* m)
    {
        libFuncRegister(m);
        if (!node.getStmts()) return;

        for (FE::AST::StmtNode* stmt : *node.getStmts())
        {
            if (!stmt) continue;
            if (dynamic_cast<FE::AST::FuncDeclStmt*>(stmt))
            {
                apply(*this, *stmt, m);
            }
            else if (dynamic_cast<FE::AST::VarDeclStmt*>(stmt))
            {
                handleGlobalVarDecl(static_cast<FE::AST::VarDeclStmt*>(stmt), m);
            }
        }
    }

    LoadInst* ASTCodeGen::createLoadInst(DataType t, Operand* ptr, size_t resReg)
    {
        return new LoadInst(t, ptr, getRegOperand(resReg));
    }

    StoreInst* ASTCodeGen::createStoreInst(DataType t, size_t valReg, Operand* ptr)
    {
        return new StoreInst(t, getRegOperand(valReg), ptr);
    }
    StoreInst* ASTCodeGen::createStoreInst(DataType t, Operand* val, Operand* ptr)
    {
        return new StoreInst(t, val, ptr);
    }

    ArithmeticInst* ASTCodeGen::createArithmeticI32Inst(Operator op, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::I32, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticI32Inst_ImmeLeft(Operator op, int lhsVal, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::I32, getImmeI32Operand(lhsVal), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticI32Inst_ImmeAll(Operator op, int lhsVal, int rhsVal, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::I32, getImmeI32Operand(lhsVal), getImmeI32Operand(rhsVal), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticF32Inst(Operator op, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::F32, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticF32Inst_ImmeLeft(
        Operator op, float lhsVal, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::F32, getImmeF32Operand(lhsVal), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticF32Inst_ImmeAll(Operator op, float lhsVal, float rhsVal, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::F32, getImmeF32Operand(lhsVal), getImmeF32Operand(rhsVal), getRegOperand(resReg));
    }

    IcmpInst* ASTCodeGen::createIcmpInst(ICmpOp cond, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new IcmpInst(DataType::I32, cond, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    IcmpInst* ASTCodeGen::createIcmpInst_ImmeRight(ICmpOp cond, size_t lhsReg, int rhsVal, size_t resReg)
    {
        return new IcmpInst(
            DataType::I32, cond, getRegOperand(lhsReg), getImmeI32Operand(rhsVal), getRegOperand(resReg));
    }
    FcmpInst* ASTCodeGen::createFcmpInst(FCmpOp cond, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new FcmpInst(DataType::F32, cond, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    FcmpInst* ASTCodeGen::createFcmpInst_ImmeRight(FCmpOp cond, size_t lhsReg, float rhsVal, size_t resReg)
    {
        return new FcmpInst(
            DataType::F32, cond, getRegOperand(lhsReg), getImmeF32Operand(rhsVal), getRegOperand(resReg));
    }

    FP2SIInst* ASTCodeGen::createFP2SIInst(size_t srcReg, size_t destReg)
    {
        return new FP2SIInst(getRegOperand(srcReg), getRegOperand(destReg));
    }
    SI2FPInst* ASTCodeGen::createSI2FPInst(size_t srcReg, size_t destReg)
    {
        return new SI2FPInst(getRegOperand(srcReg), getRegOperand(destReg));
    }
    ZextInst* ASTCodeGen::createZextInst(size_t srcReg, size_t destReg, size_t srcBits, size_t destBits)
    {
        ASSERT(srcBits == 1 && destBits == 32 && "Currently only support i1 to i32 zext");
        return new ZextInst(DataType::I1, DataType::I32, getRegOperand(srcReg), getRegOperand(destReg));
    }

    GEPInst* ASTCodeGen::createGEP_I32Inst(
        DataType t, Operand* ptr, std::vector<int> dims, std::vector<Operand*> is, size_t resReg)
    {
        return new GEPInst(t, DataType::I32, ptr, getRegOperand(resReg), dims, is);
    }

    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName, CallInst::argList args, size_t resReg)
    {
        return new CallInst(t, funcName, args, getRegOperand(resReg));
    }
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName, CallInst::argList args)
    {
        return new CallInst(t, funcName, args);
    }
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName, size_t resReg)
    {
        return new CallInst(t, funcName, getRegOperand(resReg));
    }
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName) { return new CallInst(t, funcName); }

    RetInst* ASTCodeGen::createRetInst() { return new RetInst(DataType::VOID); }
    RetInst* ASTCodeGen::createRetInst(DataType t, size_t retReg) { return new RetInst(t, getRegOperand(retReg)); }
    RetInst* ASTCodeGen::createRetInst(int val) { return new RetInst(DataType::I32, getImmeI32Operand(val)); }
    RetInst* ASTCodeGen::createRetInst(float val) { return new RetInst(DataType::F32, getImmeF32Operand(val)); }

    BrCondInst* ASTCodeGen::createBranchInst(size_t condReg, size_t trueTar, size_t falseTar)
    {
        return new BrCondInst(getRegOperand(condReg), getLabelOperand(trueTar), getLabelOperand(falseTar));
    }
    BrUncondInst* ASTCodeGen::createBranchInst(size_t tar) { return new BrUncondInst(getLabelOperand(tar)); }

    AllocaInst* ASTCodeGen::createAllocaInst(DataType t, size_t ptrReg)
    {
        return new AllocaInst(t, getRegOperand(ptrReg));
    }
    AllocaInst* ASTCodeGen::createAllocaInst(DataType t, size_t ptrReg, std::vector<int> dims)
    {
        return new AllocaInst(t, getRegOperand(ptrReg), dims);
    }

    std::list<Instruction*> ASTCodeGen::createTypeConvertInst(DataType from, DataType to, size_t srcReg)
    {
        if (from == to) return {};
        ASSERT((from == DataType::I1) || (from == DataType::I32) || (from == DataType::F32));
        ASSERT((to == DataType::I1) || (to == DataType::I32) || (to == DataType::F32));

        std::list<Instruction*> insts;

        switch (from)
        {
            case DataType::I1:
            {
                switch (to)
                {
                    case DataType::I32:
                    {
                        size_t    destReg = getNewRegId();
                        ZextInst* zext    = createZextInst(srcReg, destReg, 1, 32);
                        insts.push_back(zext);
                        break;
                    }
                    case DataType::F32:
                    {
                        size_t    i32Reg = getNewRegId();
                        ZextInst* zext   = createZextInst(srcReg, i32Reg, 1, 32);
                        insts.push_back(zext);
                        size_t f32Reg = getNewRegId();
                        insts.push_back(createSI2FPInst(i32Reg, f32Reg));
                        break;
                    }
                    default: ERROR("Type conversion not supported");
                }
                break;
            }
            case DataType::I32:
            {
                switch (to)
                {
                    case DataType::I1:
                    {
                        size_t    destReg = getNewRegId();
                        IcmpInst* icmp    = createIcmpInst_ImmeRight(ICmpOp::NE, srcReg, 0, destReg);
                        insts.push_back(icmp);
                        break;
                    }
                    case DataType::F32:
                    {
                        size_t destReg = getNewRegId();
                        insts.push_back(createSI2FPInst(srcReg, destReg));
                        break;
                    }
                    default: ERROR("Type conversion not supported");
                }
                break;
            }
            case DataType::F32:
            {
                switch (to)
                {
                    case DataType::I1:
                    {
                        size_t    destReg = getNewRegId();
                        FcmpInst* fcmp    = createFcmpInst_ImmeRight(FCmpOp::ONE, srcReg, 0.0f, destReg);
                        insts.push_back(fcmp);
                        break;
                    }
                    case DataType::I32:
                    {
                        size_t destReg = getNewRegId();
                        insts.push_back(createFP2SIInst(srcReg, destReg));
                        break;
                    }
                    default: ERROR("Type conversion not supported");
                }
                break;
            }
            default: ERROR("Type conversion not supported");
        }

        return insts;
    }
}  // namespace ME
