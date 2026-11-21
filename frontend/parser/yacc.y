%skeleton "lalr1.cc"
%require "3.2"

%define api.namespace { FE }
%define api.parser.class { YaccParser }
%define api.token.constructor
%define api.value.type variant
%define parse.assert
%defines

%code requires
{
    #include <memory>
    #include <string>
    #include <sstream>
    #include <frontend/ast/ast_defs.h>
    #include <frontend/ast/ast.h>
    #include <frontend/ast/stmt.h>
    #include <frontend/ast/expr.h>
    #include <frontend/ast/decl.h>
    #include <frontend/symbol/symbol_entry.h>

    namespace FE
    {
        class Parser;
        class Scanner;
    }
}

%code top
{
    #include <iostream>

    #include <frontend/parser/parser.h>
    #include <frontend/parser/location.hh>
    #include <frontend/parser/scanner.h>
    #include <frontend/parser/yacc.h>

    using namespace FE;
    using namespace FE::AST;

    static YaccParser::symbol_type yylex(Scanner& scanner, Parser &parser)
    {
        (void)parser;
        return scanner.nextToken(); 
    }

    extern size_t errCnt;
}

%lex-param { FE::Scanner& scanner }
%lex-param { FE::Parser& parser }
%parse-param { FE::Scanner& scanner }
%parse-param { FE::Parser& parser }

%locations

%define parse.error verbose
%define api.token.prefix {TOKEN_}

// 从这开始定义你需要用到的 token
// 对于一些需要 "值" 的 token，可以在前面加上 <type> 来指定值的类型
// 例如，%token <int> INT_CONST 定义了一个名为 INT_CONST
%token <int> INT_CONST
%token <long long> LL_CONST
%token <float> FLOAT_CONST


%token <std::string> STR_CONST ERR_TOKEN SLASH_COMMENT

%token <std::string> IDENT 

%token INT FLOAT VOID TENSOR

%token IF ELSE FOR WHILE CONTINUE BREAK SWITCH CASE GOTO DO RETURN CONST
%token SEMICOLON COMMA LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE
%token END

// "("                 { RETT(LPAREN, loc) }
// ")"                 { RETT(RPAREN, loc) }
// "["                 { RETT(LBRACKET, loc) }
// "]"                 { RETT(RBRACKET, loc) }
// "{"                 { RETT(LBRACE, loc) }
// "}"                 { RETT(RBRACE, loc) }

//查阅资料可知token声明可以设置别名改善可读性
%token PLUS    "+"
%token MINUS   "-"
%token STAR    "*"
%token SLASH   "/"
%token PERCENT "%"

%token ASSIGN  "="

%token LT      "<"
%token GT      ">"
%token LE      "<="
%token GE      ">="
%token EQ      "=="
%token NE      "!="

%token NOT     "!"
%token AND     "&&"
%token OR      "||"
%token AT      "@"


%nterm <FE::AST::Operator> UNARY_OP
%nterm <FE::AST::Type*> TYPE
%nterm <FE::AST::InitDecl*> INITIALIZER
%nterm <std::vector<FE::AST::InitDecl*>*> INITIALIZER_LIST
%nterm <FE::AST::VarDeclarator*> VAR_DECLARATOR
%nterm <std::vector<FE::AST::VarDeclarator*>*> VAR_DECLARATOR_LIST
%nterm <FE::AST::VarDeclaration*> VAR_DECLARATION
%nterm <FE::AST::ParamDeclarator*> PARAM_DECLARATOR
%nterm <std::vector<FE::AST::ParamDeclarator*>*> PARAM_DECLARATOR_LIST

%nterm <FE::AST::ExprNode*> LITERAL_EXPR
%nterm <FE::AST::ExprNode*> BASIC_EXPR
%nterm <FE::AST::ExprNode*> FUNC_CALL_EXPR
%nterm <FE::AST::ExprNode*> UNARY_EXPR
%nterm <FE::AST::ExprNode*> MULDIV_EXPR
%nterm <FE::AST::ExprNode*> ADDSUB_EXPR
%nterm <FE::AST::ExprNode*> RELATIONAL_EXPR
%nterm <FE::AST::ExprNode*> EQUALITY_EXPR
%nterm <FE::AST::ExprNode*> LOGICAL_AND_EXPR
%nterm <FE::AST::ExprNode*> LOGICAL_OR_EXPR
%nterm <FE::AST::ExprNode*> ASSIGN_EXPR
%nterm <FE::AST::ExprNode*> NOCOMMA_EXPR
%nterm <FE::AST::ExprNode*> EXPR
%nterm <std::vector<FE::AST::ExprNode*>*> EXPR_LIST

%nterm <FE::AST::ExprNode*> ARRAY_DIMENSION_EXPR
%nterm <std::vector<FE::AST::ExprNode*>*> ARRAY_DIMENSION_EXPR_LIST
%nterm <FE::AST::ExprNode*> LEFT_VAL_EXPR

%nterm <FE::AST::StmtNode*> EXPR_STMT
%nterm <FE::AST::StmtNode*> VAR_DECL_STMT
%nterm <FE::AST::StmtNode*> BLOCK_STMT
%nterm <FE::AST::StmtNode*> FUNC_DECL_STMT
%nterm <FE::AST::StmtNode*> RETURN_STMT
%nterm <FE::AST::StmtNode*> WHILE_STMT
%nterm <FE::AST::StmtNode*> IF_STMT
%nterm <FE::AST::StmtNode*> BREAK_STMT
%nterm <FE::AST::StmtNode*> CONTINUE_STMT
%nterm <FE::AST::StmtNode*> FOR_STMT
%nterm <FE::AST::StmtNode*> FUNC_BODY
%nterm <FE::AST::StmtNode*> STMT

%nterm <std::vector<FE::AST::StmtNode*>*> STMT_LIST
%nterm <FE::AST::Root*> PROGRAM

%start PROGRAM

//THEN和ELSE用于处理if和else的移进-规约冲突
%precedence THEN
%precedence ELSE
// token 定义结束

%%

/*
语法分析：补全TODO(Lab2)处的文法规则及处理函数。
如果你不打算实现float、array这些进阶要求，可将对应部分删去。
*/

//语法树匹配从这里开始
PROGRAM:
    STMT_LIST {
        $$ = new Root($1);
        parser.ast = $$;
    }
    | PROGRAM END {
        YYACCEPT;
    }
    ;

STMT_LIST:
    STMT {
        $$ = new std::vector<StmtNode*>();
        if ($1) $$->push_back($1);
    }
    | STMT_LIST STMT {
        $$ = $1;
        if ($2) $$->push_back($2);
    }
    ;

STMT:
    EXPR_STMT {
        $$ = $1;
    }
    | VAR_DECL_STMT {
        $$ = $1;
    }
    | FUNC_DECL_STMT {
        $$ = $1;
    }
    | FOR_STMT {
        $$ = $1;
    }
    | IF_STMT {
        $$ = $1;
    }
    | CONTINUE_STMT {
        $$ = $1;
    }
    | SEMICOLON {
        $$ = nullptr;
    }
    | SLASH_COMMENT {
        $$ = nullptr;
    }
    //TODO(Lab2)：考虑更多语句类型

    //查stmt.h
    | RETURN_STMT {
        $$ = $1;
    }
    | WHILE_STMT {
        $$ = $1;
    }
    | BREAK_STMT {
        $$ = $1;
    }
    | BLOCK_STMT {
        $$ = $1;
    }

    ;

CONTINUE_STMT:
    CONTINUE SEMICOLON {
        $$ = new ContinueStmt(@1.begin.line, @1.begin.column);
    }
    ;

EXPR_STMT:
    EXPR SEMICOLON {
        $$ = new ExprStmt($1, @1.begin.line, @1.begin.column);
    }
    ;

RETURN_STMT:
    RETURN SEMICOLON {
        $$ = new ReturnStmt(nullptr, @1.begin.line, @1.begin.column);
    }
    | RETURN EXPR SEMICOLON {
        $$ = new ReturnStmt($2, @1.begin.line, @1.begin.column);
    }
    ;

VAR_DECLARATION:
    TYPE VAR_DECLARATOR_LIST {
        $$ = new VarDeclaration($1, $2, false, @1.begin.line, @1.begin.column);
    }
    | CONST TYPE VAR_DECLARATOR_LIST {
        $$ = new VarDeclaration($2, $3, true, @1.begin.line, @1.begin.column);
    }
    ;

VAR_DECL_STMT:
    /* TODO(Lab2): Implement variable declaration statement rule */
    //VarDeclStmt(VarDeclaration* decl, int line_num = -1, int col_num = -1) : StmtNode(line_num, col_num), decl(decl)
    VAR_DECLARATION SEMICOLON {
        $$ = new VarDeclStmt($1, @1.begin.line, @1.begin.column);
    }
    ;

FUNC_BODY:
    LBRACE RBRACE {
        $$ = nullptr;
    }
    | LBRACE STMT_LIST RBRACE {
        if (!$2 || $2->empty())
        {
            $$ = nullptr;
            delete $2;
        }
        else if ($2->size() == 1)
        {
            $$ = (*$2)[0];
            delete $2;
        }
        else $$ = new BlockStmt($2, @1.begin.line, @1.begin.column);
    }
    ;

FUNC_DECL_STMT:
    TYPE IDENT LPAREN PARAM_DECLARATOR_LIST RPAREN FUNC_BODY {
        Entry* entry = Entry::getEntry($2);
        $$ = new FuncDeclStmt($1, entry, $4, $6, @1.begin.line, @1.begin.column);
    }
    ;

FOR_STMT:
    FOR LPAREN VAR_DECLARATION SEMICOLON EXPR SEMICOLON EXPR RPAREN STMT {
        VarDeclStmt* initStmt = new VarDeclStmt($3, @3.begin.line, @3.begin.column);
        $$ = new ForStmt(initStmt, $5, $7, $9, @1.begin.line, @1.begin.column);
    }
    | FOR LPAREN EXPR SEMICOLON EXPR SEMICOLON EXPR RPAREN STMT {
        StmtNode* initStmt = new ExprStmt($3, $3->line_num, $3->col_num);
        $$ = new ForStmt(initStmt, $5, $7, $9, @1.begin.line, @1.begin.column);
    }
    ;

IF_STMT:
    /* TODO(Lab2): Implement if statement rule */
    IF LPAREN EXPR RPAREN STMT %prec THEN {
        // 没有 else 分支，第三个参数传 nullptr
        $$ = new IfStmt($3, $5, nullptr, @1.begin.line, @1.begin.column);
    }
    | IF LPAREN EXPR RPAREN STMT ELSE STMT {
        // 有 else 分支，$5 是 then，$7 是 else
        $$ = new IfStmt($3, $5, $7, @1.begin.line, @1.begin.column);
    }
    ;

//TODO(Lab2)：按照你补充的语句类型，实现这些语句的处理
//补充类型如下：
// | RETURN_STMT {
//         $$ = $1;
//     }
//     | WHILE_STMT {
//         $$ = $1;
//     }
//     | BREAK_STMT {
//         $$ = $1;
//     }
//     | BLOCK_STMT {
//         $$ = $1;
//     }

WHILE_STMT:
    WHILE LPAREN EXPR RPAREN STMT {
        $$ = new WhileStmt($3, $5, @1.begin.line, @1.begin.column);
    }
    ;

BLOCK_STMT:
    LBRACE RBRACE {
        $$ = new BlockStmt(new std::vector<StmtNode*>(), @1.begin.line, @1.begin.column);
    }
    | LBRACE STMT_LIST RBRACE {
        $$ = new BlockStmt($2, @1.begin.line, @1.begin.column);
    }
    ;

BREAK_STMT:
    BREAK SEMICOLON {
        $$ = new BreakStmt(@1.begin.line, @1.begin.column);
    }
    ;

PARAM_DECLARATOR:
    TYPE IDENT {
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | TYPE IDENT LBRACKET RBRACKET {
        std::vector<ExprNode*>* dim = new std::vector<ExprNode*>();
        dim->emplace_back(new LiteralExpr(-1, @3.begin.line, @3.begin.column));
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2)：考虑函数形参更多情况
    //情况1是形参参数名，情况2是数组退化指针，
    //情况3是多维数组第一维退化

    //多维数组第一维在sysy里的写法要求只有空方括号：（来自文档）当 FuncFParam 为数组定义时，其第一维的长度省去（用方括号[ ]表示），而
    //后面的各维则需要用表达式指明长度，长度是整型常量。
    //但是测试里有，所以扩展这一条解析规则
    | TYPE IDENT ARRAY_DIMENSION_EXPR_LIST {
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, $3, @1.begin.line, @1.begin.column);
    }

    // 这里需要用到之前数组参数的解析部分
    | TYPE IDENT LBRACKET RBRACKET ARRAY_DIMENSION_EXPR_LIST {
        std::vector<ExprNode*>* dim = new std::vector<ExprNode*>();
        dim->emplace_back(new LiteralExpr(-1, @3.begin.line, @3.begin.column));
        dim->insert(dim->end(), $5->begin(), $5->end());
        delete $5;

        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }

    ;

PARAM_DECLARATOR_LIST:
    /* empty */ {
        $$ = new std::vector<ParamDeclarator*>();
    }
    //TODO(Lab2)：考虑函数形参列表的构成情况
    | PARAM_DECLARATOR {
        $$ = new std::vector<ParamDeclarator*>();
        $$->push_back($1);
    }
    | PARAM_DECLARATOR_LIST COMMA PARAM_DECLARATOR {
        $$ = $1;
        $$->push_back($3);
    }
    ;

VAR_DECLARATOR:
    // 单独声明左值；左值=初始化值；
    //TODO(Lab2)：完成变量声明符的处理
    //VarDeclarator(ExprNode* lval, InitDecl* init = nullptr, int line_num = -1, int col_num = -1)
    //        : DeclNode(line_num, col_num), lval(lval), init(init)
    LEFT_VAL_EXPR {
        $$ = new VarDeclarator($1, nullptr, @1.begin.line, @1.begin.column);
    }
    | LEFT_VAL_EXPR ASSIGN INITIALIZER {
        $$ = new VarDeclarator($1, $3, @1.begin.line, @1.begin.column);
    }
    ;

VAR_DECLARATOR_LIST:
    VAR_DECLARATOR {
        $$ = new std::vector<VarDeclarator*>();
        $$->push_back($1);
    }
    | VAR_DECLARATOR_LIST COMMA VAR_DECLARATOR {
        $$ = $1;
        $$->push_back($3);
    }
    ;

    //变量初始化 decl.h
INITIALIZER:
    /* TODO(Lab2): Implement variable initializer rule */
    NOCOMMA_EXPR {
        $$ = new Initializer($1, @1.begin.line, @1.begin.column);
    }
    | LBRACE RBRACE {
        $$ = new InitializerList(new std::vector<InitDecl*>(), @1.begin.line, @1.begin.column);
    }
    | LBRACE INITIALIZER_LIST RBRACE {
        $$ = new InitializerList($2, @1.begin.line, @1.begin.column);
    }
    ;

INITIALIZER_LIST:
    INITIALIZER {
        $$ = new std::vector<InitDecl*>();
        $$->push_back($1);
    }
    | INITIALIZER_LIST COMMA INITIALIZER {
        $$ = $1;
        $$->push_back($3);
    }
    ;

ASSIGN_EXPR:
    // TODO(Lab2): 完成赋值表达式的处理
    LEFT_VAL_EXPR ASSIGN NOCOMMA_EXPR {
        $$ = new BinaryExpr(Operator::ASSIGN, $1, $3, @1.begin.line, @1.begin.column);
    }
    ;

EXPR_LIST:
    NOCOMMA_EXPR {
        $$ = new std::vector<ExprNode*>();
        $$->push_back($1);
    }
    | EXPR_LIST COMMA NOCOMMA_EXPR {
        $$ = $1;
        $$->push_back($3);
    }
    ;

EXPR:
    NOCOMMA_EXPR {
        $$ = $1;
    }
    | EXPR COMMA NOCOMMA_EXPR {
        if ($1->isCommaExpr()) {
            CommaExpr* ce = static_cast<CommaExpr*>($1);
            ce->exprs->push_back($3);
            $$ = ce;
        } else {
            auto vec = new std::vector<ExprNode*>();
            vec->push_back($1);
            vec->push_back($3);
            $$ = new CommaExpr(vec, $1->line_num, $1->col_num);
        }
    }
    ;

NOCOMMA_EXPR:
    LOGICAL_OR_EXPR {
        $$ = $1;
    }
    | ASSIGN_EXPR {
        $$ = $1;
    }
    ;

LOGICAL_OR_EXPR:
    /* TODO(Lab2): Implement logical OR expression rule */
    LOGICAL_AND_EXPR {  $$ = $1;    }
    | LOGICAL_OR_EXPR OR LOGICAL_AND_EXPR {
        $$ = new BinaryExpr(Operator::OR, $1, $3, @1.begin.line, @1.begin.column);
    }
    ;

LOGICAL_AND_EXPR:
    /* TODO(Lab2): Implement logical AND expression rule */
    EQUALITY_EXPR { $$ = $1;    }
    | LOGICAL_AND_EXPR AND EQUALITY_EXPR {
        $$ = new BinaryExpr(Operator::AND, $1, $3, @1.begin.line, @1.begin.column);
    }
    ;

EQUALITY_EXPR:
    /* TODO(Lab2): Implement equality expression rule */
    RELATIONAL_EXPR { $$ = $1;    }
    | EQUALITY_EXPR EQ RELATIONAL_EXPR {
        $$ = new BinaryExpr(Operator::EQ, $1, $3, @1.begin.line, @1.begin.column);
    }
    | EQUALITY_EXPR NE RELATIONAL_EXPR {
        $$ = new BinaryExpr(Operator::NEQ, $1, $3, @1.begin.line, @1.begin.column);
    }

    ;

RELATIONAL_EXPR:
    /* TODO(Lab2): Implement relational expression rule */
    ADDSUB_EXPR { $$ = $1; }
    | RELATIONAL_EXPR LT ADDSUB_EXPR {
        $$ = new BinaryExpr(Operator::LT, $1, $3, @1.begin.line, @1.begin.column);
    }
    | RELATIONAL_EXPR LE ADDSUB_EXPR {
        $$ = new BinaryExpr(Operator::LE, $1, $3, @1.begin.line, @1.begin.column);
    }
    | RELATIONAL_EXPR GT ADDSUB_EXPR {
        $$ = new BinaryExpr(Operator::GT, $1, $3, @1.begin.line, @1.begin.column);
    }
    | RELATIONAL_EXPR GE ADDSUB_EXPR {
        $$ = new BinaryExpr(Operator::GE, $1, $3, @1.begin.line, @1.begin.column);
    }
    ;

ADDSUB_EXPR:
    /* TODO(Lab2): Implement addition and subtraction expression rule */
    MULDIV_EXPR {   $$ = $1;}
    | ADDSUB_EXPR PLUS MULDIV_EXPR {
        $$ = new BinaryExpr(Operator::ADD, $1, $3, @1.begin.line, @1.begin.column);
    }
    | ADDSUB_EXPR MINUS MULDIV_EXPR {
        $$ = new BinaryExpr(Operator::SUB, $1, $3, @1.begin.line, @1.begin.column);
    }
    ;

MULDIV_EXPR:
    /* TODO(Lab2): Implement multiplication and division expression rule */
    UNARY_EXPR {    $$ = $1;}
    | MULDIV_EXPR STAR UNARY_EXPR {
        $$ = new BinaryExpr(Operator::MUL, $1, $3, @1.begin.line, @1.begin.column);
    }
    | MULDIV_EXPR SLASH UNARY_EXPR {
        $$ = new BinaryExpr(Operator::DIV, $1, $3, @1.begin.line, @1.begin.column);
    }
    | MULDIV_EXPR PERCENT UNARY_EXPR {
        $$ = new BinaryExpr(Operator::MOD, $1, $3, @1.begin.line, @1.begin.column);
    }
    | MULDIV_EXPR AT UNARY_EXPR {
        $$ = new BinaryExpr(Operator::MATMUL, $1, $3, @1.begin.line, @1.begin.column);
    }
    ;

UNARY_EXPR:
    BASIC_EXPR {
        $$ = $1;
    }
    | UNARY_OP UNARY_EXPR {
        $$ = new UnaryExpr($1, $2, $2->line_num, $2->col_num);
    }
    ;

BASIC_EXPR:
    LITERAL_EXPR {
        $$ = $1;
    }
    | LEFT_VAL_EXPR {
        $$ = $1;
    }
    | LPAREN EXPR RPAREN {
        $$ = $2;
    }
    | FUNC_CALL_EXPR {
        $$ = $1;
    }
    ;

FUNC_CALL_EXPR:
    IDENT LPAREN RPAREN {
        std::string funcName = $1;
        if (funcName != "starttime" && funcName != "stoptime")
        {
            Entry* entry = Entry::getEntry(funcName);
            $$ = new CallExpr(entry, nullptr, @1.begin.line, @1.begin.column);
        }
        else
        {    
            funcName = "_sysy_" + funcName;
            std::vector<ExprNode*>* args = new std::vector<ExprNode*>();
            args->emplace_back(new LiteralExpr(static_cast<int>(@1.begin.line), @1.begin.line, @1.begin.column));
            $$ = new CallExpr(Entry::getEntry(funcName), args, @1.begin.line, @1.begin.column);
        }
    }
    | IDENT LPAREN EXPR_LIST RPAREN {
        Entry* entry = Entry::getEntry($1);
        $$ = new CallExpr(entry, $3, @1.begin.line, @1.begin.column);
    }
    ;

    //数组维度声明的处理
ARRAY_DIMENSION_EXPR:
    LBRACKET NOCOMMA_EXPR RBRACKET {
        $$ = $2;
    }
    ;

ARRAY_DIMENSION_EXPR_LIST:
    /* TODO(Lab2): Implement variable dimension rule */
    ARRAY_DIMENSION_EXPR {
        $$ = new std::vector<ExprNode*>();
        $$->push_back($1);
    }
    | ARRAY_DIMENSION_EXPR_LIST ARRAY_DIMENSION_EXPR {
        $$ = $1;
        $$->push_back($2);
    }
    ;

LEFT_VAL_EXPR:
    IDENT {
        Entry* entry = Entry::getEntry($1);
        $$ = new LeftValExpr(entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | IDENT ARRAY_DIMENSION_EXPR_LIST {
        Entry* entry = Entry::getEntry($1);
        $$ = new LeftValExpr(entry, $2, @1.begin.line, @1.begin.column);
    }
    ;

LITERAL_EXPR:
    INT_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2): 处理更多字面量
    | LL_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    | FLOAT_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    ;

TYPE:
    // TODO(Lab2): 完成类型的处理
    //相关定义在interface/ast_defs.h
    INT {
        $$ = TypeFactory::getBasicType(Type_t::INT);
    }
    | FLOAT{
        $$ = TypeFactory::getBasicType(Type_t::FLOAT);
    }
    | VOID{
        $$ = TypeFactory::getBasicType(Type_t::VOID);
    }
    | TENSOR INT {
        $$ = TypeFactory::getTensorType(TypeFactory::getBasicType(Type_t::INT));
    }
    | TENSOR FLOAT {
        $$ = TypeFactory::getTensorType(TypeFactory::getBasicType(Type_t::FLOAT));
    }
    ;

UNARY_OP:
    // TODO(Lab2): 完成一元运算符的处理
    PLUS {
        $$ = Operator::ADD;
    }
    | MINUS {
        $$ = Operator::SUB;
    }
    | NOT {
        $$ = Operator::NOT;
    }
    ;

%%

void FE::YaccParser::error(const FE::location& location, const std::string& message)
{
    std::cerr << "msg: " << message << ", error happened at: " << location << std::endl;
}
