#include "ast_printer.h"
#include <frontend/symbol/symbol_entry.h>

namespace FE::AST
{
    void ASTPrinter::emitPrefix(std::ostream& os) const
    {
        if (lastStack.empty()) return;
        for (size_t i = 0; i + 1 < lastStack.size(); ++i) { os << (lastStack[i] ? "    " : "|   "); }
        os << (lastStack.back() ? "`-- " : "|-- ");
    }

    void ASTPrinter::emitHeader(std::ostream& os, const std::string& text) const
    {
        emitPrefix(os);
        os << text << "\n";
    }

    std::string ASTPrinter::typeSuffix(const NodeAttr& attr) const
    {
        if (!attr.typeChecked || !attr.val.value.type) return "";
        if (attr.val.value.type == voidType && !attr.val.value.isTensor) return "";
        std::string text = attr.val.value.type->toString();
        if (attr.val.value.isTensor)
        {
            text = "tensor " + text;
        }
        if (!attr.val.value.dims.empty())
        {
            text += "[";
            for (size_t i = 0; i < attr.val.value.dims.size(); ++i)
            {
                if (i) text += "][";
                text += std::to_string(attr.val.value.dims[i]);
            }
            text += "]";
        }
        if (attr.val.isConstexpr) text += " const";
        return " : " + text;
    }

    void ASTPrinter::pushLast(bool isLast) { lastStack.push_back(isLast); }
    void ASTPrinter::popLast()
    {
        if (!lastStack.empty()) lastStack.pop_back();
    }

    void ASTPrinter::withChild(bool isLast, const std::function<void()>& fn)
    {
        pushLast(isLast);
        fn();
        popLast();
    }

    void ASTPrinter::visit(Root& node, std::ostream* os)
    {

        lastStack.clear();
        *os << "ASTree\n";

        auto* stmts = node.getStmts();
        if (!stmts) return;

        size_t cnt = stmts->size();
        for (size_t i = 0; i < cnt; ++i)
        {
            if (!(*stmts)[i]) continue;
            withChild(i + 1 == cnt, [&]() { apply(*this, *(*stmts)[i], os); });
        }
    }
}  // namespace FE::AST
