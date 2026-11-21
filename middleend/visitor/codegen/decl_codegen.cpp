#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>
#include <numeric>
#include <functional>
#include <algorithm>

namespace ME
{
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

            if (d->init) ERROR("Array initializer lowering has not been implemented");
        }
    }
}  // namespace ME
