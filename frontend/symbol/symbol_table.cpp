#include <frontend/symbol/symbol_table.h>
#include <debug.h>

namespace FE::Sym
{
    void SymTable::reset_impl()
    {
        scopes.clear();
        scopes.emplace_back(nullptr, 0);
        current = &scopes.back();
    }

    void SymTable::enterScope_impl()
    {
        ASSERT(current != nullptr && "SymTable not initialized");
        scopes.emplace_back(current, current->depth + 1);
        current = &scopes.back();
    }

    void SymTable::exitScope_impl()
    {
        ASSERT(current != nullptr && "SymTable not initialized");
        ASSERT(current->parent != nullptr && "Cannot exit global scope");
        // 注意：不删除 scope，只改变当前可见作用域。
        current = current->parent;
    }

    void SymTable::addSymbol_impl(Entry* entry, FE::AST::VarAttr& attr)
    {
        ASSERT(current != nullptr && "SymTable not initialized");
        ASSERT(entry != nullptr && "Cannot add a null symbol");
        current->symbols[entry] = attr;
    }

    FE::AST::VarAttr* SymTable::getSymbol_impl(Entry* entry)
    {
        if (!entry) return nullptr;
        for (Scope* scope = current; scope != nullptr; scope = scope->parent)
        {
            auto it = scope->symbols.find(entry);
            if (it != scope->symbols.end()) return &it->second;
        }
        return nullptr;
    }

    bool SymTable::isGlobalScope_impl()
    {
        return current != nullptr && current->parent == nullptr;
    }

    int SymTable::getScopeDepth_impl()
    {
        return current ? current->depth : -1;
    }
}  // namespace FE::Sym
