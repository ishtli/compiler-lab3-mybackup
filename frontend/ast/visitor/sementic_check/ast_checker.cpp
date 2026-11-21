#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>

namespace FE::AST
{
    namespace
    {
        int product(const std::vector<int>& dims)
        {
            if (dims.empty()) return 1;
            long long p = 1;
            for (int d : dims)
            {
                if (d <= 0) return 0;
                p *= d;
                if (p > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
            }
            return static_cast<int>(p);
        }

        int suffixProduct(const std::vector<int>& dims, size_t from)
        {
            long long p = 1;
            for (size_t i = from; i < dims.size(); ++i)
            {
                if (dims[i] <= 0) return 0;
                p *= dims[i];
                if (p > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
            }
            return static_cast<int>(p);
        }

        int alignUp(int value, int alignment)
        {
            if (alignment <= 1) return value;
            return ((value + alignment - 1) / alignment) * alignment;
        }

        VarValue zeroValue(Type* elem)
        {
            if (elem && elem->getBaseType() == Type_t::FLOAT) return VarValue(0.0f);
            return VarValue(0);
        }

        VarValue convertValue(const VarValue& v, Type* elem)
        {
            if (!elem) return v;
            if (elem->getBaseType() == Type_t::FLOAT)
            {
                if (v.type == floatType) return v;
                if (v.type == intType || v.type == boolType || v.type == llType) return VarValue(v.getFloat());
                return VarValue(0.0f);
            }
            if (elem->getBaseType() == Type_t::INT || elem->getBaseType() == Type_t::LL)
            {
                if (v.type == intType) return v;
                if (v.type == floatType || v.type == boolType || v.type == llType) return VarValue(v.getInt());
                return VarValue(0);
            }
            return v;
        }
    }  // namespace

    void ASTChecker::reportError(const std::string& msg, const Node& node)
    {
        reportErrorAt(msg, node.line_num);
    }

    void ASTChecker::reportErrorAt(const std::string& msg, int line)
    {
        if (line >= 0) errors.push_back(msg + " at line " + std::to_string(line));
        else errors.push_back(msg);
    }

    bool ASTChecker::evalConstInt(ExprNode* expr, int& value)
    {
        if (!expr || !expr->attr.val.isConstexpr) return false;
        const VarValue& v = expr->attr.val.value;
        if (!isScalarValue(v) || v.type != intType) return false;
        value = v.getInt();
        return true;
    }

    Type* ASTChecker::elementType(Type* t) const
    {
        if (!t) return voidType;
        if (t->getTypeGroup() == TypeGroup::TENSOR) return static_cast<TensorType*>(t)->elem;
        return t;
    }

    bool ASTChecker::isTensorType(Type* t) const
    {
        return t && t->getTypeGroup() == TypeGroup::TENSOR;
    }

    bool ASTChecker::isScalarValue(const VarValue& v) const
    {
        return v.type && v.type->getTypeGroup() == TypeGroup::BASIC && !v.isTensor && !v.isArray && v.dims.empty();
    }

    bool ASTChecker::isArithmeticScalar(const VarValue& v) const
    {
        if (!isScalarValue(v)) return false;
        Type_t k = v.type->getBaseType();
        return k == Type_t::INT || k == Type_t::FLOAT || k == Type_t::BOOL || k == Type_t::LL;
    }

    bool ASTChecker::isCondScalar(const VarValue& v) const
    {
        return isArithmeticScalar(v);
    }

    VarAttr* ASTChecker::lookupSymbol(FE::Sym::Entry* entry)
    {
        if (!entry) return nullptr;
        return symTable.getSymbol(entry);
    }

    bool ASTChecker::checkAssignable(
        Type* dstType, bool dstTensor, const std::vector<int>& dstDims, ExprNode& src, const std::string& context)
    {
        const VarValue& v = src.attr.val.value;
        if (!v.type || v.type == voidType)
        {
            reportError("void value cannot be used in " + context, src);
            return false;
        }

        Type* dstElem = elementType(dstType);
        if (dstTensor)
        {
            if (!v.isTensor || v.type->getBaseType() != dstElem->getBaseType() || v.dims != dstDims)
            {
                reportError("tensor type/shape mismatch in " + context, src);
                return false;
            }
            return true;
        }

        if (v.isTensor || v.isArray || !v.dims.empty())
        {
            reportError("array/tensor value cannot be used as scalar in " + context, src);
            return false;
        }

        Type_t dk = dstElem->getBaseType();
        Type_t sk = v.type->getBaseType();
        if (dk != Type_t::INT && dk != Type_t::FLOAT)
        {
            reportError("invalid assignment target type in " + context, src);
            return false;
        }
        if (sk != Type_t::INT && sk != Type_t::FLOAT && sk != Type_t::BOOL && sk != Type_t::LL)
        {
            reportError("invalid scalar value in " + context, src);
            return false;
        }
        return true;
    }

    bool ASTChecker::checkAssignable(
        const VarAttr& dst, ExprNode& src, const std::string& context)
    {
        return checkAssignable(dst.type, dst.isTensor, dst.arrayDims, src, context);
    }

    void ASTChecker::registerFunctionSignature(FuncDeclStmt& node)
    {
        FE::Sym::Entry* entry = node.entry;
        if (!entry) return;

        auto it = funcDecls.find(entry);
        if (it != funcDecls.end())
        {
            // funcDecls 中已有同名函数（包括 SysY 运行时库函数）
            reportError("redefinition of function '" + entry->getName() + "'", node);
            return;
        }

        funcDecls[entry] = &node;

        VarAttr funcAttr;
        funcAttr.type       = elementType(node.retType);
        funcAttr.isTensor   = isTensorType(node.retType);
        funcAttr.isFunction = true;
        funcAttr.isGlobal   = true;
        funcAttr.scopeLevel = 0;

        if (symTable.hasSymbolInCurrentScope(entry))
        {
            reportError("redefinition of symbol '" + entry->getName() + "'", node);
        }
        else
        {
            symTable.addSymbol(entry, funcAttr);
        }

        node.attr.val.value.type     = funcAttr.type;
        node.attr.val.value.isTensor = funcAttr.isTensor;
        node.attr.val.value.isArray  = false;
        node.attr.val.value.dims.clear();

        if (node.retType->getTypeGroup() == TypeGroup::TENSOR && !node.retDimsKnown)
        {
            node.retIsTensor = true;
        }

        std::set<std::string> paramNames;
        if (node.params)
        {
            for (ParamDeclarator* p : *node.params)
            {
                if (!p || !p->entry) continue;
                const std::string& name = p->entry->getName();
                if (!paramNames.insert(name).second)
                {
                    reportError("duplicate parameter '" + name + "'", *p);
                }

                Type* elem = elementType(p->type);
                p->varAttr              = VarAttr();
                p->varAttr.type         = elem;
                p->varAttr.isTensor     = isTensorType(p->type);
                p->varAttr.isParam      = true;
                p->varAttr.scopeLevel   = 1;

                p->attr.val.value.type     = elem;
                p->attr.val.value.isTensor = p->varAttr.isTensor;
                p->attr.val.value.isArray  = false;
                p->attr.val.value.dims.clear();

                if (p->dims && !p->dims->empty())
                {
                    // 普通数组沿用 Lab2 的兼容规则，张量首维必须由实参提供。
                    if (p->varAttr.isTensor)
                    {
                        auto* first = dynamic_cast<LiteralExpr*>(p->dims->front());
                        if (!first || first->literal.type != intType || first->literal.getInt() != -1)
                            reportError("tensor parameter first dimension must be []", *p);
                    }
                    std::vector<int> dims;
                    dims.push_back(-1);
                    for (size_t i = 0; i < p->dims->size(); ++i)
                    {
                        ExprNode* dimExpr = (*p->dims)[i];
                        if (!dimExpr) continue;
                        bool ok = apply(*this, *dimExpr);
                        int  val = 0;
                        if (!ok || !evalConstInt(dimExpr, val))
                        {
                            reportError("array parameter dimension must be a constant integer", *dimExpr);
                            val = 1;
                        }
                        if (i == 0)
                        {
                            // 第一维允许为 -1（[]），也允许写具体大小；统一按指针处理
                            continue;
                        }
                        if (val <= 0)
                        {
                            reportError("array parameter dimension must be positive", *dimExpr);
                            val = 1;
                        }
                        dims.push_back(val);
                    }
                    p->varAttr.arrayDims = dims;
                    p->varAttr.isPointer = true;
                    p->attr.val.value.dims     = dims;
                    p->attr.val.value.isArray  = true;
                    p->attr.val.value.isTensor = p->varAttr.isTensor;
                }
                else
                {
                    if (p->varAttr.isTensor)
                        reportError("tensor parameter must have at least one dimension", *p);
                    p->varAttr.isPointer = false;
                }
            }
        }
    }

    bool ASTChecker::registerGlobalDeclaration(VarDeclStmt& node)
    {
        VarDeclaration* vd = node.decl;
        if (!vd) return true;

        Type* elem    = elementType(vd->type);
        bool  isTens  = isTensorType(vd->type);
        bool  ok      = true;

        if (elem == voidType)
        {
            reportError("variable cannot have void type", *vd);
            return false;
        }
        if (!vd->decls) return true;
        for (VarDeclarator* d : *vd->decls)
        {
            if (!d) continue;
            LeftValExpr* lv = dynamic_cast<LeftValExpr*>(d->lval);
            if (!lv || !lv->entry)
            {
                reportError("invalid variable declarator", *d);
                ok = false;
                continue;
            }

            if (symTable.hasSymbolInCurrentScope(lv->entry) ||
                glbSymbols.find(lv->entry) != glbSymbols.end())
            {
                reportError("redefinition of symbol '" + lv->entry->getName() + "'", *d);
                ok = false;
                continue;
            }

            std::vector<int> dims;
            if (lv->indices)
            {
                for (ExprNode* idx : *lv->indices)
                {
                    if (!idx) continue;
                    bool applied = apply(*this, *idx);
                    int  val     = 0;
                    if (!applied || !evalConstInt(idx, val))
                    {
                        reportError("array dimension must be a constant integer", *idx);
                        val = 1;
                        ok  = false;
                    }
                    if (val <= 0)
                    {
                        reportError("array dimension must be positive", *idx);
                        val = 1;
                        ok  = false;
                    }
                    dims.push_back(val);
                }
            }

            if (isTens && dims.empty())
            {
                reportError("tensor declaration must specify dimensions", *d);
                ok = false;
                continue;
            }

            VarAttr attr;
            attr.type        = elem;
            attr.isConstDecl = vd->isConstDecl;
            attr.isTensor    = isTens;
            attr.isGlobal    = true;
            attr.scopeLevel  = 0;
            attr.arrayDims   = dims;

            symTable.addSymbol(lv->entry, attr);
            glbSymbols[lv->entry] = attr;
            d->varAttr            = attr;

            lv->attr.val.value.type     = elem;
            lv->attr.val.value.isTensor = isTens;
            lv->attr.val.value.isArray  = !dims.empty();
            lv->attr.val.value.dims     = dims;

            if (!checkVarInit(*d, attr, true)) ok = false;

            // checkVarInit 可能填写 constValue/initList，需要同步回符号表
            symTable.addSymbol(lv->entry, attr);
            d->varAttr            = attr;
            glbSymbols[lv->entry] = attr;
        }
        return ok;
    }

    bool ASTChecker::checkVarInit(VarDeclarator& decl, VarAttr& attr, bool isGlobal)
    {
        if (!decl.init)
        {
            if (attr.isConstDecl)
            {
                reportError("const variable must be initialized", decl);
                return false;
            }
            return true;
        }

        Type*            elem = elementType(attr.type);
        std::vector<int> dims = attr.arrayDims;

        if (dims.empty())
        {
            Initializer* init = dynamic_cast<Initializer*>(decl.init);
            if (!init)
            {
                reportError("scalar variable cannot be initialized with an initializer list", decl);
                return false;
            }
            if (!apply(*this, *init)) return false;
            if (!checkAssignable(attr, *init->init_val, "initializer")) return false;
            if (isGlobal && !init->init_val->attr.val.isConstexpr)
            {
                reportError("global variable initializer must be a constant expression", *init->init_val);
                return false;
            }
            if (attr.isConstDecl && !init->init_val->attr.val.isConstexpr)
            {
                reportError("const variable initializer must be a constant expression", *init->init_val);
                return false;
            }
            attr.constValue = convertValue(init->init_val->attr.val.value, elem);
            attr.isInitialized = true;
            return true;
        }

        // tensor 允许用同形状的 tensor 表达式初始化（例如 tensor int c[4] = a + b * 2;）
        if (attr.isTensor)
        {
            if (Initializer* exprInit = dynamic_cast<Initializer*>(decl.init))
            {
                if (!apply(*this, *exprInit)) return false;
                if (!checkAssignable(attr, *exprInit->init_val, "tensor initializer")) return false;
                if (isGlobal && !exprInit->init_val->attr.val.isConstexpr)
                {
                    reportError("global tensor initializer must be a constant expression", *exprInit->init_val);
                    return false;
                }
                attr.isInitialized = true;
                attr.initList.clear();
                return true;
            }
        }

        InitializerList* init = dynamic_cast<InitializerList*>(decl.init);
        if (!init)
        {
            reportError("array/tensor variable must be initialized with an initializer list", decl);
            return false;
        }

        int total = product(dims);
        if (total <= 0)
        {
            reportError("invalid array/tensor size", decl);
            return false;
        }

        // 只记录显式初始化的前缀；末尾缺省元素由常量求值和 IR 打印按零处理。
        attr.initList.clear();

        size_t cursor = 0;
        flattenInitializer(decl.init, dims, elem, attr, cursor, isGlobal, decl);
        if (static_cast<int>(cursor) > total)
        {
            reportError("too many initializers", decl);
            return false;
        }
        attr.isInitialized = true;
        return true;
    }

    void ASTChecker::flattenInitializer(InitDecl* init, const std::vector<int>& dims, Type* elem, VarAttr& attr,
        size_t& cursor, bool isGlobal, const Node& loc)
    {
        if (!init) return;
        int total = product(dims);
        if (total <= 0) return;
        if (cursor >= static_cast<size_t>(total))
        {
            reportError("too many initializers", loc);
            cursor = static_cast<size_t>(total);
            return;
        }

        auto stride = [&](size_t d) -> int {
            if (d >= dims.size()) return 1;
            return suffixProduct(dims, d);
        };

        std::function<void(InitDecl*, size_t)> fill = [&](InitDecl* node, size_t desiredDepth) {
            if (!node) return;
            if (cursor >= static_cast<size_t>(total))
            {
                reportError("too many initializers", loc);
                cursor = static_cast<size_t>(total);
                return;
            }

            if (Initializer* scalar = dynamic_cast<Initializer*>(node))
            {
                apply(*this, *scalar);
                ExprNode* val = scalar->init_val;
                if (!val) return;
                const VarValue& v = val->attr.val.value;
                if (!v.type || v.type == voidType)
                {
                    reportError("void value cannot be used as an initializer", *val);
                    return;
                }
                if (!isArithmeticScalar(v))
                {
                    reportError("initializer element must be a scalar", *val);
                    return;
                }
                if (isGlobal && !val->attr.val.isConstexpr)
                {
                    reportError("global initializer element must be a constant expression", *val);
                }
                if (attr.isConstDecl && !val->attr.val.isConstexpr)
                {
                    reportError("const initializer element must be a constant expression", *val);
                }
                if (isGlobal || attr.isConstDecl)
                {
                    if (attr.initList.size() <= cursor) attr.initList.resize(cursor + 1, zeroValue(elem));
                    attr.initList[cursor] = convertValue(v, elem);
                }
                ++cursor;
                return;
            }

            InitializerList* list = dynamic_cast<InitializerList*>(node);
            if (!list || !list->init_list)
            {
                reportError("invalid initializer", loc);
                return;
            }

            if (desiredDepth >= dims.size())
            {
                // 过量嵌套：找到第一个可用于初始化的标量
                for (InitDecl* child : *list->init_list)
                {
                    if (!child) continue;
                    fill(child, dims.size());
                    return;
                }
                return;
            }

            size_t depth = desiredDepth;
            // 如果当前位置没有对齐到 desiredDepth 的子数组边界，
            // 说明前面的扁平初始化已经进入了更深一层的子数组。
            while (depth + 1 < dims.size() && cursor % static_cast<size_t>(stride(depth)) != 0)
            {
                ++depth;
            }
            if (depth >= dims.size())
            {
                for (InitDecl* child : *list->init_list)
                {
                    if (!child) continue;
                    fill(child, dims.size());
                    return;
                }
                return;
            }

            int cap   = stride(depth);
            int start = static_cast<int>(cursor);
            int limit = std::min(total, start + cap);

            for (InitDecl* child : *list->init_list)
            {
                if (!child) continue;
                bool isList = dynamic_cast<InitializerList*>(child) != nullptr;
                fill(child, depth + 1);
                if (isList)
                {
                    int childStride = (depth + 1 < dims.size()) ? stride(depth + 1) : 1;
                    cursor          = static_cast<size_t>(alignUp(static_cast<int>(cursor), childStride));
                }
                if (static_cast<int>(cursor) > limit)
                {
                    reportError("too many initializers for sub-array", *child);
                    cursor = static_cast<size_t>(limit);
                    break;
                }
            }
            // 空列表也要占用整个子数组
            cursor = static_cast<size_t>(start + cap);
            if (static_cast<int>(cursor) > total) cursor = static_cast<size_t>(total);
        };

        fill(init, 0);
    }

    bool ASTChecker::visit(Root& node)
    {
        symTable.reset();
        glbSymbols.clear();
        errors.clear();
        mainExists = false;
        for (const auto& [entry, fd] : funcDecls)
        {
            VarAttr attr(elementType(fd->retType), false, 0);
            attr.isFunction = true;
            attr.isGlobal = true;
            symTable.addSymbol(entry, attr);
        }

        if (node.getStmts())
        {
            for (StmtNode* stmt : *node.getStmts())
            {
                if (!stmt) continue;
                if (auto* fn = dynamic_cast<FuncDeclStmt*>(stmt))
                {
                    registerFunctionSignature(*fn);
                    if (fn->entry && fn->entry->getName() == "main")
                    {
                        mainExists = true;
                        if (fn->retType != intType || (fn->params && !fn->params->empty()))
                            reportError("main must have signature int main()", *fn);
                    }
                }
                else if (auto* decl = dynamic_cast<VarDeclStmt*>(stmt))
                {
                    registerGlobalDeclaration(*decl);
                }
                else
                {
                    reportError("statement is not allowed at global scope", *stmt);
                }
            }
        }

        if (node.getStmts())
        {
            for (StmtNode* stmt : *node.getStmts())
            {
                if (auto* fn = dynamic_cast<FuncDeclStmt*>(stmt)) apply(*this, *fn);
            }
        }
        if (!mainExists) reportErrorAt("main function is not defined", -1);
        return errors.empty();
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        (void)node;
        // 形参在 registerFunctionSignature / FuncDeclStmt 中统一处理
        return true;
    }

    bool ASTChecker::visit(VarDeclarator& node)
    {
        (void)node;
        return true;
    }

    void ASTChecker::checkBranch(StmtNode* stmt)
    {
        if (!stmt) return;
        symTable.enterScope();
        apply(*this, *stmt);
        symTable.exitScope();
    }

    void ASTChecker::libFuncRegister()
    {
        // 示例实现：注册 SysY 标准库函数到 funcDecls 中
        // 这样在语义检查时可以识别并检查对库函数的调用
        // 包括：getint, getch, getarray, getfloat, getfarray,
        //      putint, putch, putarray, putfloat, putfarray,
        //      _sysy_starttime, _sysy_stoptime
        using SymEnt = FE::Sym::Entry;

        static SymEnt* getint   = SymEnt::getEntry("getint");
        static SymEnt* getch    = SymEnt::getEntry("getch");
        static SymEnt* getarray = SymEnt::getEntry("getarray");
        static SymEnt* getfloat = SymEnt::getEntry("getfloat");
        static SymEnt* getfarray = SymEnt::getEntry("getfarray");
        static SymEnt* putint   = SymEnt::getEntry("putint");
        static SymEnt* putch    = SymEnt::getEntry("putch");
        static SymEnt* putarray = SymEnt::getEntry("putarray");
        static SymEnt* putfloat = SymEnt::getEntry("putfloat");
        static SymEnt* putfarray = SymEnt::getEntry("putfarray");
        static SymEnt* _sysy_starttime = SymEnt::getEntry("_sysy_starttime");
        static SymEnt* _sysy_stoptime  = SymEnt::getEntry("_sysy_stoptime");

        funcDecls[getint] = new FuncDeclStmt(intType, getint, nullptr);
        funcDecls[getch]  = new FuncDeclStmt(intType, getch, nullptr);
        funcDecls[getfloat] = new FuncDeclStmt(floatType, getfloat, nullptr);

        auto getarray_params = new std::vector<ParamDeclarator*>();
        auto getarray_param  = new ParamDeclarator(TypeFactory::getPtrType(intType), SymEnt::getEntry("a"));
        getarray_param->attr.val.value.type = TypeFactory::getPtrType(intType);
        getarray_params->push_back(getarray_param);
        funcDecls[getarray] = new FuncDeclStmt(intType, getarray, getarray_params);

        auto getfarray_params = new std::vector<ParamDeclarator*>();
        auto getfarray_param  = new ParamDeclarator(TypeFactory::getPtrType(floatType), SymEnt::getEntry("a"));
        getfarray_param->attr.val.value.type = TypeFactory::getPtrType(floatType);
        getfarray_params->push_back(getfarray_param);
        funcDecls[getfarray] = new FuncDeclStmt(intType, getfarray, getfarray_params);

        auto putint_params                = new std::vector<ParamDeclarator*>();
        auto putint_param                 = new ParamDeclarator(intType, SymEnt::getEntry("a"));
        putint_param->attr.val.value.type = intType;
        putint_params->push_back(putint_param);
        funcDecls[putint] = new FuncDeclStmt(voidType, putint, putint_params);

        auto putch_params                = new std::vector<ParamDeclarator*>();
        auto putch_param                 = new ParamDeclarator(intType, SymEnt::getEntry("a"));
        putch_param->attr.val.value.type = intType;
        putch_params->push_back(putch_param);
        funcDecls[putch] = new FuncDeclStmt(voidType, putch, putch_params);

        auto putarray_params                 = new std::vector<ParamDeclarator*>();
        auto putarray_param1                 = new ParamDeclarator(intType, SymEnt::getEntry("n"));
        putarray_param1->attr.val.value.type = intType;
        auto putarray_param2 = new ParamDeclarator(TypeFactory::getPtrType(intType), SymEnt::getEntry("a"));
        putarray_param2->attr.val.value.type = TypeFactory::getPtrType(intType);
        putarray_params->push_back(putarray_param1);
        putarray_params->push_back(putarray_param2);
        funcDecls[putarray] = new FuncDeclStmt(voidType, putarray, putarray_params);

        auto putfloat_params                = new std::vector<ParamDeclarator*>();
        auto putfloat_param                 = new ParamDeclarator(floatType, SymEnt::getEntry("a"));
        putfloat_param->attr.val.value.type = floatType;
        putfloat_params->push_back(putfloat_param);
        funcDecls[putfloat] = new FuncDeclStmt(voidType, putfloat, putfloat_params);

        auto putfarray_params                 = new std::vector<ParamDeclarator*>();
        auto putfarray_param1                 = new ParamDeclarator(intType, SymEnt::getEntry("n"));
        putfarray_param1->attr.val.value.type = intType;
        auto putfarray_param2 = new ParamDeclarator(TypeFactory::getPtrType(floatType), SymEnt::getEntry("a"));
        putfarray_param2->attr.val.value.type = TypeFactory::getPtrType(floatType);
        putfarray_params->push_back(putfarray_param1);
        putfarray_params->push_back(putfarray_param2);
        funcDecls[putfarray] = new FuncDeclStmt(voidType, putfarray, putfarray_params);

        auto starttime_params                = new std::vector<ParamDeclarator*>();
        auto starttime_param                 = new ParamDeclarator(intType, SymEnt::getEntry("lineno"));
        starttime_param->attr.val.value.type = intType;
        starttime_params->push_back(starttime_param);
        funcDecls[_sysy_starttime] = new FuncDeclStmt(voidType, _sysy_starttime, starttime_params);

        auto stoptime_params                = new std::vector<ParamDeclarator*>();
        auto stoptime_param                 = new ParamDeclarator(intType, SymEnt::getEntry("lineno"));
        stoptime_param->attr.val.value.type = intType;
        stoptime_params->push_back(stoptime_param);
        funcDecls[_sysy_stoptime] = new FuncDeclStmt(voidType, _sysy_stoptime, stoptime_params);
    }
}  // namespace FE::AST
