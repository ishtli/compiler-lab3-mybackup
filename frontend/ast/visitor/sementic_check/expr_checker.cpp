#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>
#include <numeric>
#include <algorithm>
#include <vector>

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

        bool sameTrailingDims(const std::vector<int>& formal, const std::vector<int>& actual)
        {
            size_t first = 0;
            if (!formal.empty() && formal[0] < 0) first = 1;
            size_t n = formal.size() - first;
            if (n == 0) return true;  // 褰㈠弬鍙啓浜?[]锛岄暱搴︾敱瀹炲弬鍐冲畾
            if (actual.size() < n) return false;
            for (size_t i = 0; i < n; ++i)
            {
                if (formal[first + i] != actual[actual.size() - n + i]) return false;
            }
            return true;
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
        node.attr.typeChecked = true;
        if (!node.expr) return true;
        if (!apply(*this, *node.expr)) return false;
        const VarValue& v = node.expr->attr.val.value;

        if (v.type == voidType)
        {
            reportError("void value cannot be used with unary operator '" + toString(node.op) + "'", node);
            return false;
        }

        if (v.isTensor || v.isArray || !v.dims.empty())
        {
            if (node.op != Operator::ADD && node.op != Operator::SUB)
            {
                reportError("operator '" + toString(node.op) + "' is not supported for tensors/arrays", node);
                return false;
            }
            node.attr.val.value          = v;
            node.attr.val.value.type     = v.type;
            node.attr.val.value.isArray  = false;
            node.attr.val.value.isTensor = true;
            node.attr.val.isConstexpr    = false;
            return true;
        }

        if (!isArithmeticScalar(v))
        {
            reportError("invalid operand type for unary operator '" + toString(node.op) + "'", node);
            return false;
        }

        bool      hasError = false;
        ExprValue res      = typeInfer(node.expr->attr.val, node.op, node, hasError);
        node.attr.val      = res;
        node.attr.op       = node.op;
        return !hasError;
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        node.attr.typeChecked = true;
        if (!node.lhs || !node.rhs) return true;

        // 瀵瑰乏娣辩殑闀垮姞娉曢摼鍋氳凯浠ｅ鐞嗭紝閬垮厤瓒呴暱琛ㄨ揪寮忓鑷撮€掑綊鐖嗘爤銆?        if (node.op == Operator::ADD)
        {
            std::vector<ExprNode*>    operands;
            std::vector<BinaryExpr*> chainNodes;
            ExprNode*                 cur = &node;
            while (auto* b = dynamic_cast<BinaryExpr*>(cur))
            {
                if (b->op != Operator::ADD || !b->lhs || !b->rhs) break;
                chainNodes.push_back(b);
                operands.push_back(b->rhs);
                cur = b->lhs;
            }
            if (!operands.empty())
            {
                operands.push_back(cur);
                std::reverse(operands.begin(), operands.end());
                std::reverse(chainNodes.begin(), chainNodes.end());

                ExprValue acc;
                bool      first = true;
                size_t    foldIdx = 0;
                for (ExprNode* e : operands)
                {
                    if (!e) continue;
                    if (!apply(*this, *e)) return false;
                    const VarValue& v = e->attr.val.value;
                    if (v.type == voidType)
                    {
                        reportError("void value cannot be used in binary expression", *e);
                        return false;
                    }
                    if (first)
                    {
                        acc   = e->attr.val;
                        first = false;
                        continue;
                    }

                    if (acc.value.isTensor || v.isTensor)
                    {
                        if (acc.value.isArray || v.isArray)
                        {
                            reportError("array value cannot be used in arithmetic expression", node);
                            return false;
                        }
                        if (acc.value.isTensor && v.isTensor)
                        {
                            if (acc.value.type->getBaseType() != v.type->getBaseType() || acc.value.dims != v.dims)
                            {
                                reportError("tensor shapes/types must match for addition", node);
                                return false;
                            }
                            acc.value.isArray = false;
                            acc.isConstexpr   = false;
                        }
                        else if (acc.value.isTensor)
                        {
                            if (!isArithmeticScalar(v) || acc.value.type->getBaseType() != v.type->getBaseType())
                            {
                                reportError("tensor and scalar base types must match", node);
                                return false;
                            }
                            acc.isConstexpr = false;
                        }
                        else
                        {
                            if (!isArithmeticScalar(acc.value) || v.type->getBaseType() != acc.value.type->getBaseType())
                            {
                                reportError("tensor and scalar base types must match", node);
                                return false;
                            }
                            acc = e->attr.val;
                            acc.value.isArray = false;
                            acc.isConstexpr   = false;
                        }
                    }
                    else
                    {
                        if (!isArithmeticScalar(acc.value) || !isArithmeticScalar(v))
                        {
                            reportError("invalid operand types for operator '+'", node);
                            return false;
                        }
                        bool      hasError = false;
                        ExprValue res      = typeInfer(acc, e->attr.val, Operator::ADD, node, hasError);
                        if (hasError) return false;
                        acc = res;
                    }

                    // 记录左深链中每个中间 BinaryExpr 的类型，IR 生成阶段会依赖它
                    if (foldIdx < chainNodes.size())
                    {
                        chainNodes[foldIdx]->attr.val         = acc;
                        chainNodes[foldIdx]->attr.op          = Operator::ADD;
                        chainNodes[foldIdx]->attr.typeChecked = true;
                    }
                    ++foldIdx;
                }
                node.attr.val         = acc;
                node.attr.op          = Operator::ADD;
                node.attr.typeChecked = true;
                return true;
            }
        }

        if (node.op == Operator::ASSIGN)
        {
            if (!apply(*this, *node.lhs)) return false;
            if (!apply(*this, *node.rhs)) return false;

            LeftValExpr* lv = dynamic_cast<LeftValExpr*>(node.lhs);
            if (!lv)
            {
                reportError("left operand of assignment is not an lvalue", *node.lhs);
                return false;
            }
            if (!lv->isLval && !lv->attr.val.value.isTensor)
            {
                reportError("left operand of assignment is not a scalar lvalue", *node.lhs);
                return false;
            }
            if (lv)
            {
                VarAttr* a = lookupSymbol(lv->entry);
                if (a && a->isConstDecl)
                {
                    reportError("cannot assign to const variable '" + lv->entry->getName() + "'", *node.lhs);
                    return false;
                }
            }

            if (!checkAssignable(node.lhs->attr.val.value.type, node.lhs->attr.val.value.isTensor,
                    node.lhs->attr.val.value.dims, *node.rhs, "assignment"))
            {
                return false;
            }

            node.attr.val               = node.lhs->attr.val;
            node.attr.val.isConstexpr   = false;
            node.attr.val.value.isArray = node.lhs->attr.val.value.isArray;
            return true;
        }

        if (!apply(*this, *node.lhs)) return false;
        if (!apply(*this, *node.rhs)) return false;

        const VarValue& lv = node.lhs->attr.val.value;
        const VarValue& rv = node.rhs->attr.val.value;

        if (lv.type == voidType || rv.type == voidType)
        {
            reportError("void value cannot be used in binary expression", node);
            return false;
        }

        // 寮犻噺/鏁扮粍杩愮畻
        if (lv.isTensor || lv.isArray || rv.isTensor || rv.isArray || !lv.dims.empty() || !rv.dims.empty())
        {
            bool rhsAgg = rv.isTensor || rv.isArray || !rv.dims.empty();

            if (lv.isArray || rv.isArray)
            {
                reportError("array value cannot be used in arithmetic expression", node);
                return false;
            }

            if (!lv.isTensor && !rv.isTensor)
            {
                reportError("aggregate value cannot be used in arithmetic expression", node);
                return false;
            }

            if (node.op == Operator::MATMUL)
            {
                if (!lv.isTensor || !rv.isTensor || lv.dims.size() != 2 || rv.dims.size() != 2)
                {
                    reportError("operator '@' requires two 2-D tensors", node);
                    return false;
                }
                if (lv.type->getBaseType() != rv.type->getBaseType())
                {
                    reportError("tensor base types must match for '@'", node);
                    return false;
                }
                if (lv.dims[1] != rv.dims[0])
                {
                    reportError("tensor shapes are incompatible for '@'", node);
                    return false;
                }
                node.attr.val.value.type     = lv.type;
                node.attr.val.value.isTensor = true;
                node.attr.val.value.isArray  = false;
                node.attr.val.value.dims     = {lv.dims[0], rv.dims[1]};
                node.attr.val.isConstexpr    = false;
                return true;
            }

            if (node.op == Operator::EQ || node.op == Operator::NEQ || node.op == Operator::GT ||
                node.op == Operator::GE || node.op == Operator::LT || node.op == Operator::LE ||
                node.op == Operator::AND || node.op == Operator::OR || node.op == Operator::NOT)
            {
                reportError("relational/logical operators do not support tensors", node);
                return false;
            }

            if (node.op != Operator::ADD && node.op != Operator::SUB && node.op != Operator::MUL &&
                node.op != Operator::DIV && node.op != Operator::MOD)
            {
                reportError("operator '" + toString(node.op) + "' is not supported for tensors", node);
                return false;
            }

            Type* elem = nullptr;
            std::vector<int> shape;
            if (lv.isTensor && rv.isTensor)
            {
                if (lv.type->getBaseType() != rv.type->getBaseType())
                {
                    reportError("tensor base types must match", node);
                    return false;
                }
                if (lv.dims != rv.dims)
                {
                    reportError("tensor shapes must match", node);
                    return false;
                }
                elem  = lv.type;
                shape = lv.dims;
            }
            else if (lv.isTensor)
            {
                if (!isArithmeticScalar(rv) || lv.type->getBaseType() != rv.type->getBaseType())
                {
                    reportError("tensor and scalar base types must match", node);
                    return false;
                }
                elem  = lv.type;
                shape = lv.dims;
            }
            else
            {
                if (!isArithmeticScalar(lv) || rv.type->getBaseType() != lv.type->getBaseType())
                {
                    reportError("scalar and tensor base types must match", node);
                    return false;
                }
                elem  = rv.type;
                shape = rv.dims;
            }

            if (node.op == Operator::MOD && elem->getBaseType() == Type_t::FLOAT)
            {
                reportError("operator '%' is not supported for float tensors", node);
                return false;
            }
            if ((node.op == Operator::DIV || node.op == Operator::MOD) && !rhsAgg && rv.type->getBaseType() == Type_t::INT)
            {
                if (node.rhs->attr.val.isConstexpr && node.rhs->attr.val.value.getInt() == 0)
                {
                    reportError("division by zero", node);
                    return false;
                }
            }

            node.attr.val.value.type     = elem;
            node.attr.val.value.isTensor = true;
            node.attr.val.value.isArray  = false;
            node.attr.val.value.dims     = shape;
            node.attr.val.isConstexpr    = false;
            return true;
        }

        // 鏍囬噺杩愮畻
        if (!isArithmeticScalar(lv) || !isArithmeticScalar(rv))
        {
            reportError("invalid operand types for operator '" + toString(node.op) + "'", node);
            return false;
        }

        if ((node.op == Operator::DIV || node.op == Operator::MOD) && rv.type->getBaseType() == Type_t::INT &&
            node.rhs->attr.val.isConstexpr && node.rhs->attr.val.value.getInt() == 0)
        {
            reportError("division by zero", node);
            return false;
        }
        if (node.op == Operator::MOD &&
            (lv.type->getBaseType() == Type_t::FLOAT || rv.type->getBaseType() == Type_t::FLOAT))
        {
            reportError("operator '%' is not supported for float", node);
            return false;
        }

        bool      hasError = false;
        ExprValue res      = typeInfer(node.lhs->attr.val, node.rhs->attr.val, node.op, node, hasError);
        node.attr.val      = res;
        node.attr.op       = node.op;
        return !hasError;
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        node.attr.typeChecked = true;
        auto it = funcDecls.find(node.func);
        if (it == funcDecls.end())
        {
            reportError("call to undeclared function '" + (node.func ? node.func->getName() : std::string("?")) + "'", node);
            return false;
        }
        FuncDeclStmt* fd = it->second;
        if (!fd) return false;

        size_t argCount = node.args ? node.args->size() : 0;
        size_t parCount = (fd->params ? fd->params->size() : 0);
        if (argCount != parCount)
        {
            reportError("function '" + node.func->getName() + "' expects " + std::to_string(parCount) + " argument(s), but got " +
                            std::to_string(argCount),
                node);
            // 浠嶇劧妫€鏌ュ凡鎻愪緵鐨勫疄鍙傦紝渚夸簬鎶ュ憡鏇村閿欒
        }

        bool ok = true;
        for (size_t i = 0; i < argCount; ++i)
        {
            ExprNode* arg = (*node.args)[i];
            if (!arg) continue;
            if (!apply(*this, *arg)) ok = false;

            if (i >= parCount) continue;
            ParamDeclarator* p = (fd->params ? (*fd->params)[i] : nullptr);
            if (!p) continue;

            VarAttr pAttr = p->varAttr;
            bool    isPtr = pAttr.isPointer;
            Type*   pRaw  = pAttr.type;
            if (!pRaw || pRaw == voidType) pRaw = p->type;
            Type* pElem = elementType(pRaw);
            if (pRaw && pRaw->getTypeGroup() == TypeGroup::POINTER)
            {
                isPtr = true;
                pElem = static_cast<PtrType*>(pRaw)->base;
            }
            bool pTens = pAttr.isTensor || isTensorType(p->type);

            const VarValue& av = arg->attr.val.value;

            if (isPtr)
            {
                if (av.type == voidType || (!av.isArray && !av.isTensor && av.dims.empty()))
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' must be an array/tensor",
                        *arg);
                    ok = false;
                    continue;
                }
                if (av.type->getBaseType() != pElem->getBaseType())
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' has incompatible element type",
                        *arg);
                    ok = false;
                    continue;
                }
                if (pTens && !av.isTensor)
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' must be a tensor",
                        *arg);
                    ok = false;
                    continue;
                }
                if (!pTens && av.isTensor)
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' must be an array",
                        *arg);
                    ok = false;
                    continue;
                }
                if (!sameTrailingDims(pAttr.arrayDims, av.dims))
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' has incompatible shape",
                        *arg);
                    ok = false;
                }
            }
            else
            {
                if (!isArithmeticScalar(av))
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' must be a scalar",
                        *arg);
                    ok = false;
                    continue;
                }
                if (pElem->getBaseType() != av.type->getBaseType() &&
                    !((pElem->getBaseType() == Type_t::FLOAT || pElem->getBaseType() == Type_t::INT) &&
                        (av.type->getBaseType() == Type_t::FLOAT || av.type->getBaseType() == Type_t::INT ||
                            av.type->getBaseType() == Type_t::BOOL || av.type->getBaseType() == Type_t::LL)))
                {
                    reportError("argument " + std::to_string(i + 1) + " of '" + node.func->getName() +
                                    "' has incompatible type",
                        *arg);
                    ok = false;
                }
            }
        }

        Type* elem    = elementType(fd->retType);
        bool  isTens  = isTensorType(fd->retType);
        node.attr.val.value.type     = elem;
        node.attr.val.value.isTensor = isTens;
        node.attr.val.value.isArray  = false;
        node.attr.val.value.dims     = isTens ? fd->retDims : std::vector<int>{};
        node.attr.val.isConstexpr    = false;
        return ok;
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        node.attr.typeChecked = true;
        if (!node.exprs || node.exprs->empty()) return true;
        bool ok = true;
        for (ExprNode* e : *node.exprs)
        {
            if (!e) continue;
            if (!apply(*this, *e)) ok = false;
        }
        node.attr = node.exprs->back()->attr;
        return ok;
    }
}  // namespace FE::AST
