#ifndef __FRONTEND_SYMBOL_SYMBOL_TABLE_H__
#define __FRONTEND_SYMBOL_SYMBOL_TABLE_H__

#include <frontend/symbol/isymbol_table.h>
#include <list>
#include <map>

namespace FE::Sym
{
    class SymTable : public iSymTable<SymTable>
    {
        friend iSymTable<SymTable>;

        struct Scope
        {
            std::map<Entry*, FE::AST::VarAttr> symbols;
            Scope*                             parent;
            int                                depth;

            Scope(Scope* p = nullptr, int d = 0) : symbols(), parent(p), depth(d) {}
        };

        // 用 list 保存所有作用域，退出作用域时不销毁节点，
        // 保证 AST/IR 持有的 VarAttr* 在语义分析结束后仍然有效。
        std::list<Scope> scopes;
        Scope*           current;

        void reset_impl();

        void              addSymbol_impl(Entry* entry, FE::AST::VarAttr& attr);
        FE::AST::VarAttr* getSymbol_impl(Entry* entry);
        void              enterScope_impl();
        void              exitScope_impl();

        bool isGlobalScope_impl();
        int  getScopeDepth_impl();

      public:
        SymTable() : scopes(), current(nullptr) { reset_impl(); }

        // 只在当前作用域查找，用于重定义检查
        bool hasSymbolInCurrentScope(Entry* entry)
        {
            return current && current->symbols.find(entry) != current->symbols.end();
        }

        // 当前作用域中的全部符号（用于调试/打印）
        const std::map<Entry*, FE::AST::VarAttr>* currentScopeSymbols() const
        {
            return current ? &current->symbols : nullptr;
        }
    };
}  // namespace FE::Sym

#endif  // __FRONTEND_SYMBOL_SYMBOL_TABLE_H__
