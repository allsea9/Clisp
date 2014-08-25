#include "parser_impl.h"
#include "environment.h"
#include "error.h"

using namespace std;
using namespace Lexer;
using namespace Environment;
using Error::error;

template <typename T, typename Iter>
const T& get(Iter p) {
    return boost::get<T>(p->data);
}

List Parser::expr() {   // returns an unevaluated expression from stream
    List res;
    // expr ... (expr) ...) starts with first lp eaten
    while (true) {
        cs.get();
        switch (cs.current().kind) {
            case Kind::Lp: {    // start of another expression
                res.push_back(expr());  // construct with List, kind is expr and data stored in lstval
                // after geting in an ( expression ' ' <-- expecting rp
                if (cs.current().kind != Kind::Rp) return {{error("')' expected")}};
                break;
            }
            case Kind::End:
            case Kind::Rp: return res;  // for initial expr call, all nested expr calls will exit through first case
            default: res.push_back(cs.current()); break;   // anything else just push back as is
        }
    }
}

Cell Parser::eval(const List& expr, Env* env) {
    for (auto p = expr.begin(); p != expr.end(); ++p) {
        switch (p->kind) {
            case Kind::Number: return *p;
            // return next expression unevaluated, (quote expr)
            case Kind::Quote: return *++p;  
            case Kind::Lambda: {    // (lambda (params) (body))
                assert(p + 2 != expr.end()); 
                auto params = get<List>(++p);
                auto body = get<List>(++p);
                procs.push_back({params, body, env});    // introduce onto heap
                return {&procs.back()};
            }
            // introduce cell to environment (define name expr)
            case Kind::Define: {
                auto np = ++p;    // cell to be defined
                if (np->kind == Kind::Name) 
                    return (*env)[get<string>(np)] = eval({++p, expr.end()}, env); 
                else if (np->kind == Kind::Expr) {   // (syntactic sugar for defining functions (define (func args) (body))
                    auto declaration = get<List>(np);
                    string name = get<string>(declaration.begin());
                    auto params = List{declaration.begin() + 1, declaration.end()};
                    auto body = get<List>(++p);
                    procs.push_back({params, body, env});
                    return (*env)[name] = {&procs.back()};
                }
                else return {error("Unfamiliar form to define")};
            }
            // (... (expr) ...) parentheses encloses expression (as parsed by expr())
            case Kind::Expr: { 
                auto res = evlist(get<List>(p), env); 
                if (res.size() == 1) return {res[0]}; // single element
                return {res};
            }
            // (cond ((pred) (expr)) ((pred) (expr)) ...(else expr)) expect list of pred-expr pairs
            case Kind::Cond: {
                while (++p != expr.end()) {
                    const List& clause = get<List>(p);
                    if (clause[0].kind == Kind::Else) {
                        if (p + 1 == expr.end()) return eval({clause[1]}, env);
                        else return {error("Else clause not at end of condition")};
                    }
                    if (eval({clause[0]}, env)) return eval({clause[1]}, env);
                }
            }
            // primitive procedures
            case Kind::Add: case Kind::Sub: case Kind::Mul: case Kind::Div: case Kind::Less: case Kind::Greater: case Kind::Equal: 
            case Kind::Cat: case Kind::Cons: case Kind::Car: case Kind::Cdr: case Kind::List: case Kind::And: case Kind::Or: case Kind::Not: {
                auto prim = *p;
                return apply_prim(prim, evlist({++p, expr.end()}, env));
            }
            case Kind::Name: {  // lexer cannot distinguish between varname and procname, have to evaluate against environment
                Cell x = env->lookup(get<string>(p));
                if (x.kind != Kind::Proc) return x;
                List args;  // user defined proc
                while ((p+1)->kind == Kind::Number || (p+1)->kind == Kind::Quote || (p+1)->kind == Kind::Name) { // evaluate as many arguments locally as possible
                    ++p;
                    if (p->kind == Kind::Number) args.push_back(*p);
                    else if (p->kind == Kind::Quote) args.push_back(*++p);
                    else args.push_back(env->lookup(get<string>(p)));
                }
                if (p != expr.end()) { 
                    List addargs = evlist({++p, expr.end()}, env); // evlist any remaining expressions
                    args.insert(args.end(), addargs.begin(), addargs.end());
                }
                return apply(x, args);    
            }
            default: return {error("Unmatched cell in eval")};
        }
    }
    return {};
}

List Parser::evlist(const List& expr, Env* env) {
    List res;   // instead of returning right away, push back into res then return res
    for (auto p = expr.begin(); p != expr.end(); ++p) {
        switch (p->kind) {
            case Kind::Number: res.push_back(*p); break;
            // return next expression unevaluated, (quote expr)
            case Kind::Quote: res.push_back(*++p); break;  
            case Kind::Lambda: {    // (lambda (params) (body))
                assert(p + 2 != expr.end()); 
                auto params = get<List>(++p);
                auto body = get<List>(++p);
                procs.push_back({params, body, env});    // introduce onto heap
                res.push_back({&procs.back()});
                break;
            }
            // introduce cell to environment (define name expr)
            case Kind::Define: {
                auto np = ++p;    // cell to be defined
                if (np->kind == Kind::Name) {
                    res.push_back((*env)[get<string>(np)] = eval({++p, expr.end()}, env)); 
                    return res;
                }
                else if (np->kind == Kind::Expr) {   // (syntactic sugar for defining functions (define (func args) (body))
                    auto declaration = get<List>(np);
                    string name = get<string>(declaration.begin());
                    auto params = List{declaration.begin() + 1, declaration.end()};
                    auto body = get<List>(++p);
                    procs.push_back({params, body, env});
                    res.push_back((*env)[name] = {&procs.back()});
                    return res;
                }
                else return {error("Unfamiliar form to define")};
            }
            // (... (expr) ...) parentheses encloses expression (as parsed by expr())
            case Kind::Expr: {
                auto r = evlist(get<List>(p), env); 
                if (r.size() == 1) res.push_back({r[0]}); // single element result
                else res.push_back({r});
                break;
            }
            // (cond ((pred) (expr)) ((pred) (expr)) ...) expect list of pred-expr pairs
            case Kind::Cond: {
                while (++p != expr.end()) {
                    const List& clause = get<List>(p);
                    if (clause[0].kind == Kind::Else) {
                        if (p + 1 == expr.end()) { res.push_back(eval({clause[1]}, env)); return res; }
                        else return {error("Else clause not at end of condition")};
                    }
                    if (eval({clause[0]}, env)) { res.push_back(eval({clause[1]}, env)); break; }
                }
                break;
            }
            // primitive procedures
            case Kind::Add: case Kind::Sub: case Kind::Mul: case Kind::Div: case Kind::Less: case Kind::Greater: case Kind::Equal: 
            case Kind::Cat: case Kind::Cons: case Kind::Car: case Kind::Cdr: case Kind::List: case Kind::And: case Kind::Or: case Kind::Not: {
                auto prim = *p;
                res.push_back(apply_prim(prim, evlist({++p, expr.end()}, env)));
                return res; // finished reading entire expression
            }
            case Kind::Name: {  // lexer cannot distinguish between varname and procname, have to evaluate against environment
                Cell x = env->lookup(get<string>(p));
                if (x.kind != Kind::Proc) { res.push_back(x); break; }
                List args;
                while ((p+1)->kind == Kind::Number || (p+1)->kind == Kind::Quote || (p+1)->kind == Kind::Name) { // evaluate as many arguments locally as possible
                    ++p;
                    if (p->kind == Kind::Number) args.push_back(*p);
                    else if (p->kind == Kind::Quote) args.push_back(*++p);
                    else args.push_back(env->lookup(get<string>(p)));
                }
                if (p != expr.end()) { 
                    List addargs = evlist({++p, expr.end()}, env); // evlist any remaining expressions
                    args.insert(args.end(), addargs.begin(), addargs.end());
                }
                res.push_back(apply(x, args)); return res;         // user defined proc
            }
            default: error("Unmatched in evlist"); break;
        }
    }
    return res;
}

Cell Parser::apply(const Cell& c, const List& args) {  // expect fully evaluated args
    const Proc& proc = *boost::get<Proc*>(c.data);
    Env* newenv = Parser::bind(proc.params, args, proc.env);
    return eval(proc.body, newenv);
}

Env* Parser::bind(const List& params, const List& args, Env* env) {
    Env newenv {env};
    assert(params.size() == args.size());
    auto q = args.begin();
    for (auto p = params.begin(); p != params.end(); ++p, ++q)
        newenv[get<string>(p)] = *q;

    envs.push_back(newenv);  // store on the heap to allow reference and pointer
    return &envs.back();
}

// primitive procedures
Cell Parser::apply_prim(const Cell& prim, const List& args) {
    switch (prim.kind) {
        case Kind::Add: {   // more efficient to separate addition and concatenation
            double res {get<double>(args.begin())};
            for (auto p = args.begin() + 1; p != args.end(); ++p)
                res += get<double>(p);
            return {res};
        }
        case Kind::Cat: {   // (cat 'str 'str ...)
            string res {get<string>(args.begin())};
            for (auto p = args.begin() + 1; p != args.end(); ++p)
                res += get<string>(p);
            return {res};
        }
        case Kind::Sub: {
            double res {get<double>(args.begin())};
            for (auto p = args.begin() + 1; p != args.end(); ++p)
                res -= get<double>(p);
            return {res};
        }
        case Kind::Mul: {
            double res {get<double>(args.begin())};
            for (auto p = args.begin() + 1; p != args.end(); ++p)
                res *= get<double>(p);
            return {res};
        }
        case Kind::Div: {
            double res {get<double>(args.begin())};
            for (auto p = args.begin() + 1; p != args.end(); ++p)
                res /= get<double>(p);  // uncheckd divide by 0
            return {res};
        }
        case Kind::Less: {
            if (args[0].kind == Kind::Number)
                return Cell{boost::apply_visitor(less_visitor(get<double>(args.begin())), args[1].data)};
            return Cell{boost::apply_visitor(less_visitor(get<string>(args.begin())), args[1].data)};
        }
        case Kind::Equal: {
            if (args[0].kind == Kind::Number)
                return Cell{boost::apply_visitor(equal_visitor(get<double>(args.begin())), args[1].data)};
            return Cell{boost::apply_visitor(equal_visitor(get<string>(args.begin())), args[1].data)};
        }
        case Kind::Greater: {   // for the sake of efficiency not implemented using !< && !=
            if (args[1].kind == Kind::Number)   // a > b == b < a, just use less
                return Cell{boost::apply_visitor(less_visitor(get<double>(args.begin() + 1)), args[0].data)};
            return Cell{boost::apply_visitor(less_visitor(get<string>(args.begin() + 1)), args[0].data)};
        }
        case Kind::And: {
            for (auto& clause : args)
                if(clause.kind == Kind::False) return clause;
            return Cell{Kind::True};
        }
        case Kind::Or: {
            for (auto& clause : args)
                if(clause.kind == Kind::True) return clause;
            return Cell{Kind::False};
        }
        case Kind::Not: return Cell{args[0].kind == Kind::False? Kind::True : Kind::False};  // only expect 1 argument
        case Kind::List:              // same as cons in this implementation, just that cons conventionally expects only 2 args
        case Kind::Cons: return args; // return List of the args
        case Kind::Car: return boost::get<List>(args[0].data)[0]; // args is a list of one cell which holds a list itself
        case Kind::Cdr: {
            if (args[0].kind != Kind::Expr) return {List {}};
            auto list = boost::get<List>(args[0].data); 
            if (list.size() == 1) return {List {}};
            else if (list.size() == 2) return list[1];
            return {List{list.begin() + 1, list.end()}}; 
        }
        default: return error("Mismatch in apply_prim");
    }
}
