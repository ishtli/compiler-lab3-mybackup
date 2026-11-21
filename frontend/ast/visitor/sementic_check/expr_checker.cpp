#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    namespace
    {
        int dimsProduct(const std::vector<int>& dims, size_t from)
        {
            long long p = 1;
            for (size_t i = from; i < dims.size(); ++i)
            {
                if (dims[i] <= 0) return 0;
                p *= dims[i];
            }
            return static_cast<int>(p);
        }


    }  // namespace

    bool ASTChecker::visit(LeftValExpr& node)
    {
        node.attr.typeChecked = true;
        VarAttr* attr = lookupSymbol(node.entry);
        if (!attr)
        {
            reportError("use of undeclared identifier '" + (node.entry ? node.entry->getName() : std::string("?")) + "'", node);
            return false;
        }
        if (attr->isFunction)
        {
            reportError("function '" + node.entry->getName() + "' used as a variable", node);
            return false;
        }

        const std::vector<int>& dims = attr->arrayDims;
        size_t n = node.indices ? node.indices->size() : 0;
        if (n > dims.size())
        {
            reportError("too many subscripts for '" + node.entry->getName() + "'", node);
            return false;
        }
        if (attr->isTensor && n != 0 && n != dims.size())
        {
            reportError("tensor subscript count must equal its rank", node);
            return false;
        }

        bool              allConst = true;
        std::vector<int>  idxVals;
        idxVals.reserve(n);
        if (node.indices)
        {
            for (ExprNode* idx : *node.indices)
            {
                if (!idx) continue;
                if (!apply(*this, *idx)) return false;
                const VarValue& iv = idx->attr.val.value;
                if (!isScalarValue(iv) || (iv.type->getBaseType() != Type_t::INT && iv.type->getBaseType() != Type_t::LL &&
                                              iv.type->getBaseType() != Type_t::BOOL))
                {
                    reportError("array subscript must be an integer", *idx);
                    return false;
                }
                int v = 0;
                if (!evalConstInt(idx, v)) allConst = false;
                idxVals.push_back(v);
            }
        }

        VarValue v;
        v.type     = attr->type;
        v.isTensor = false;
        v.isArray  = false;

        if (n == dims.size())
        {
            node.isLval = true;
        }
        else
        {
            node.isLval = false;
            v.isTensor  = attr->isTensor;
            v.isArray   = !attr->isTensor;
            v.dims.assign(dims.begin() + static_cast<std::ptrdiff_t>(n), dims.end());
        }

        node.attr.val.value       = v;
        node.attr.val.isConstexpr = false;

        // 甯搁噺涓嬫爣璁块棶 const 鏁扮粍/鏍囬噺鏃跺仛甯搁噺鎶樺彔
        if (attr->isConstDecl && n == dims.size())
        {
            if (n == 0)
            {
                node.attr.val.value       = attr->constValue;
                node.attr.val.isConstexpr = true;
            }
            else if (allConst && !attr->initList.empty())
            {
                int offset = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    int stride = dimsProduct(dims, i + 1);
                    offset += idxVals[i] * stride;
                }
                if (offset >= 0 && static_cast<size_t>(offset) < attr->initList.size())
                {
                    node.attr.val.value       = attr->initList[static_cast<size_t>(offset)];
                    node.attr.val.isConstexpr = true;
                }
            }
        }

        return true;
    }

    bool ASTChecker::visit(LiteralExpr& node)
    {
        node.attr.typeChecked = true;
        node.attr.val.isConstexpr = true;
        node.attr.val.value       = node.literal;
        if (node.literal.type == llType)
        {
            // 鍓嶇鎶婅秴鍑?int 鑼冨洿鐨勬暣鏁版殏瀛樹负 long long锛汭R 闃舵鎸?32 浣嶅鐞?            node.attr.val.value.type = llType;
        }
        return true;
    }

    bool ASTChecker::visit(UnaryExpr& node)
    {
        // TODO(Lab3-1): 实现一元表达式的语义检查
        // 访问子表达式，检查操作数类型，调用类型推断函数
        (void)node;
        TODO("Lab3-1: Implement UnaryExpr semantic checking");
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        // TODO(Lab3-1): 实现二元表达式的语义检查
        // 访问左右子表达式，检查操作数类型，调用类型推断
        (void)node;
        TODO("Lab3-1: Implement BinaryExpr semantic checking");
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        // TODO(Lab3-1): 实现函数调用表达式的语义检查
        // 检查函数是否存在，访问实参列表，检查参数数量和类型匹配
        (void)node;
        TODO("Lab3-1: Implement CallExpr semantic checking");
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        // TODO(Lab3-1): 实现逗号表达式的语义检查
        // 依序访问各子表达式，结果为最后一个表达式的属性
        (void)node;
        TODO("Lab3-1: Implement CommaExpr semantic checking");
    }
}  // namespace FE::AST
