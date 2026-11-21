#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>
#include <numeric>
#include <functional>
#include <algorithm>

namespace ME
{
    namespace
    {
        int totalOf(const std::vector<int>& dims)
        {
            long long p = 1;
            for (int d : dims) p *= d;
            return static_cast<int>(p);
        }

        int suffixProduct(const std::vector<int>& dims, size_t from)
        {
            int p = 1;
            for (size_t i = from; i < dims.size(); ++i) p *= dims[i];
            return p;
        }

        int alignUp(int v, int a)
        {
            if (a <= 1) return v;
            return ((v + a - 1) / a) * a;
        }
    }  // namespace

    void ASTCodeGen::visit(FE::AST::Initializer& node, Module* m)
    {
        (void)m;
        ERROR("Initializer should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::InitializerList& node, Module* m)
    {
        (void)m;
        ERROR("InitializerList should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::VarDeclarator& node, Module* m)
    {
        (void)m;
        ERROR("VarDeclarator should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::ParamDeclarator& node, Module* m)
    {
        (void)m;
        ERROR("ParamDeclarator should not appear here, at line %d", node.line_num);
    }

    void ASTCodeGen::emitZeroInit(size_t baseReg, DataType elem, int total)
    {
        if (total <= 0) return;
        std::vector<std::pair<DataType, Operand*>> args;
        args.push_back({DataType::PTR, getRegOperand(baseReg)});
        args.push_back({DataType::I8, getImmeI32Operand(0)});
        args.push_back({DataType::I32, getImmeI32Operand(total * 4)});
        args.push_back({DataType::I1, getImmeI32Operand(0)});
        insert(createCallInst(DataType::VOID, "llvm.memset.p0.i32", args));
    }

    void ASTCodeGen::emitArrayInit(FE::AST::InitDecl* init, const std::vector<int>& dims, DataType elem,
        size_t baseReg, size_t& cursor, size_t boundary, Module* m)
    {
        if (!init) return;
        int total = totalOf(dims);
        if (total <= 0) return;

        auto stride = [&](size_t d) -> int {
            if (d >= dims.size()) return 1;
            return suffixProduct(dims, d);
        };

        std::function<void(FE::AST::InitDecl*, size_t)> fill = [&](FE::AST::InitDecl* node, size_t desiredDepth) {
            if (!node) return;
            if (cursor >= boundary)
            {
                ERROR("Too many initializers in IR generation");
            }

            if (auto* scalar = dynamic_cast<FE::AST::Initializer*>(node))
            {
                size_t reg = genExpr(*scalar->init_val, m);
                DataType have = convert(scalar->init_val->attr.val.value.type);
                if (have != elem)
                {
                    auto conv = createTypeConvertInst(have, elem, reg);
                    for (auto& inst : conv) insert(inst);
                    reg = getMaxReg();
                }
                size_t ptr = emitElementPtrImme(baseReg, elem, static_cast<int>(cursor));
                insert(createStoreInst(elem, reg, getRegOperand(ptr)));
                ++cursor;
                return;
            }

            auto* list = dynamic_cast<FE::AST::InitializerList*>(node);
            if (!list || !list->init_list) return;

            if (desiredDepth >= dims.size())
            {
                for (FE::AST::InitDecl* child : *list->init_list)
                {
                    if (!child) continue;
                    fill(child, dims.size());
                    return;
                }
                return;
            }

            size_t depth = desiredDepth;
            while (depth + 1 < dims.size() && cursor % static_cast<size_t>(stride(depth)) != 0) ++depth;

            int cap   = stride(depth);
            int start = static_cast<int>(cursor);
            int limit = std::min(static_cast<int>(boundary), start + cap);

            for (FE::AST::InitDecl* child : *list->init_list)
            {
                if (!child) continue;
                bool isList = dynamic_cast<FE::AST::InitializerList*>(child) != nullptr;
                fill(child, depth + 1);
                if (isList)
                {
                    int childStride = (depth + 1 < dims.size()) ? stride(depth + 1) : 1;
                    cursor = static_cast<size_t>(alignUp(static_cast<int>(cursor), childStride));
                }
                if (static_cast<int>(cursor) > limit) cursor = static_cast<size_t>(limit);
            }
            // 一个初始化列表总是初始化整个子数组；即使列表为空，
            // 也要跳过该子数组的全部元素。
            cursor = static_cast<size_t>(start + cap);
            if (static_cast<int>(cursor) > limit) cursor = static_cast<size_t>(limit);
        };

        fill(init, 0);
    }

    void ASTCodeGen::visit(FE::AST::VarDeclaration& node, Module* m)
    {
        if (!node.decls) return;
        for (FE::AST::VarDeclarator* d : *node.decls)
        {
            if (!d) continue;
            auto* lv = dynamic_cast<FE::AST::LeftValExpr*>(d->lval);
            if (!lv || !lv->entry) continue;

            FE::AST::VarAttr attr = d->varAttr;
            if (attr.type == FE::AST::voidType)
            {
                attr.type     = node.type;
                attr.isTensor = node.type && node.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
                if (attr.isTensor) attr.type = static_cast<FE::AST::TensorType*>(node.type)->elem;
            }

            DataType dt = convert(attr.type);
            size_t   ptrReg = newEntryAlloca(dt, attr.arrayDims);
            name2reg.addSymbol(lv->entry, ptrReg);
            reg2attr[ptrReg] = attr;

            size_t baseElem = materializeElementPtr(getRegOperand(ptrReg), attr, false);
            int    total    = totalOf(attr.arrayDims);

            if (attr.arrayDims.empty())
            {
                if (d->init)
                {
                    if (auto* init = dynamic_cast<FE::AST::Initializer*>(d->init))
                    {
                        size_t   reg  = genExpr(*init->init_val, m);
                        DataType have = convert(init->init_val->attr.val.value.type);
                        if (have != dt)
                        {
                            auto conv = createTypeConvertInst(have, dt, reg);
                            for (auto& inst : conv) insert(inst);
                            reg = getMaxReg();
                        }
                        insert(createStoreInst(dt, reg, getRegOperand(ptrReg)));
                    }
                }
                continue;
            }

            if (d->init)
            {
                if (auto* init = dynamic_cast<FE::AST::Initializer*>(d->init))
                {
                    // tensor 表达式初始化
                    size_t src = genExpr(*init->init_val, m);
                    emitTensorAssign(baseElem, src, dt, total);
                }
                else
                {
                    emitZeroInit(baseElem, dt, total);
                    size_t cursor = 0;
                    emitArrayInit(d->init, attr.arrayDims, dt, baseElem, cursor, static_cast<size_t>(total), m);
                }
            }
        }
    }
}  // namespace ME
