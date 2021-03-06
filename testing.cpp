#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include "boost/variant.hpp"

using namespace std;
using boost::variant;

// -------------------- data structures
enum class Kind : char {
    Include,        // meta processing
    Begin, Cat, Cons, Car, Cdr, List, Let,  // primitive procs
    Define = 'd', Lambda = 'l', Number = '#', Name = 'n', Expr = 'e', Proc = 'p', False = 'f', True = 't', Cond = 'c', Else = ',', End = '.', Empty = ' ',    // special cases
    Quote = '\'', Lp = '(', Rp = ')', And = '&', Not = '!', Or = '|', 
    Mul = '*', Add = '+', Sub = '-', Div = '/', Less = '<', Equal = '=', Greater = '>',  // primitive operators
    Comment = ';'
};

map<string, Kind> keywords {{"define", Kind::Define}, {"lambda", Kind::Lambda}, {"cond", Kind::Cond},
    {"cons", Kind::Cons}, {"car", Kind::Car}, {"cdr", Kind::Cdr}, {"list", Kind::List}, {"else", Kind::Else},
    {"empty?", Kind::Empty}, {"and", Kind::And}, {"or", Kind::Or}, {"not", Kind::Or}, {"cat", Kind::Cat},
    {"include", Kind::Include}, {"begin", Kind::Begin}, {"let", Kind::Let}};

class Cell;
class Env;

using List = vector<Cell>;

struct Proc {
    List params;    
    List body;
    Env* env;
};

using Data = variant<string, double, Proc*, List>;  // could make List into List*, but then introduce more management issues and indirection

struct Cell {
    Kind kind;
    Data data;

    // constructors
    Cell() : kind{Kind::End} {} // need default for vector storage
    Cell(Kind k) : kind{k} {}
    Cell(const double n) : kind{Kind::Number}, data{n} {}
    Cell(const string& s) : kind{Kind::Name}, data{s} {}
    Cell(const char* s) : kind{Kind::Name}, data{s} {}
    Cell(Proc* p) : kind{Kind::Proc}, data{p} {}
    Cell(List l) : kind{Kind::Expr}, data{l} {}
    explicit Cell(bool b) : kind{b? Kind::True : Kind::False} {}

    // copy and move constructors
    Cell(const Cell&) = default;
    Cell& operator=(const Cell&) = default;
    Cell(Cell&&) = default;
    Cell& operator=(Cell&&) = default;

    ~Cell() = default;

    // conversion operators
    operator bool() { return kind != Kind::False; }
};

class Env {
private:
    using Env_map = map<string, Cell>;
    Env_map env;
    Env* outer;
public:
    // constructors
    Env() : outer{nullptr} {}
    Env(Env* o) : outer{o} {}
    Env(const List& params, const List& args, Env* o) : outer{o} {
        auto a = args.begin();
        for (auto p = params.begin(); p != params.end(); ++p, ++a)
            env[boost::get<string>(p->data)] = *a++;    
    }

    Env_map& findframe(string n) {
        if (env.find(n) != env.end())
            return env;
        else if (outer != nullptr) 
            return outer->findframe(n);
        throw runtime_error("Unbound variable");
    }

    Cell& lookup(string n) {
        return findframe(n)[n];
    }

    Cell& operator[](string n) { // access for assignment
        return env[n];
    }

    // copying and moving
    Env(const Env&) = default;
    Env& operator=(const Env&) = default;

    Env(Env&&) = default;
    Env& operator=(Env&&) = default;
    ~Env() = default;
};

constexpr int max_capacity = 10000; // max of 10000 variables and procedures
Env e0;
vector<Env> envs; 
vector<Proc> procs;

// ------------------- stream
class Cell_stream {
public:
    Cell_stream(istream& instream_ref) : ip{&instream_ref} {}
    Cell_stream(istream* instream_pt)  : ip{instream_pt}, owns{instream_pt} {}

    Cell get();    // get and return next cell
    const Cell& current() { return ct; } // most recently get cell
    bool eof() { return ip->eof(); }
    bool base() { return old.size() == 0; }
    void reset() { if (owns.back() == ip) delete owns.back(); ip = old.back(); old.pop_back(); }
    void ignoreln() { cout << "Ignoring line\n"; ip->ignore(9001, '\n'); }

    void set_input(istream& instream_ref) { old.push_back(ip); ip = &instream_ref; }
    void set_input(istream* instream_pt) { old.push_back(ip); ip = instream_pt; owns.push_back(ip); }

private:
    // void close() { if (owns) delete ip; }
    istream* ip;    // input stream pointer
    vector<istream*> old;  // for switching between input streams through include
    vector<istream*> owns;
    Cell ct {Kind::End};   // current token, default value in case of misuse
};

Cell_stream cs {cin};

Cell Cell_stream::get() {
    // get 1 char, decide what kind of cell is incoming,
    // appropriately get more char then return Cell
    char c = 0;

    do {  // skip all whitespace including newline
        if(!ip->get(c)) return ct = {Kind::End};  // no char can be get from ip
    } while (isspace(c));

    switch (c) {
        case '!':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case '-':
        case ';':
        case '/':
        case '<':
        case '=':
        case '>':
        case '|':
            return ct = {static_cast<Kind>(c)}; // primitive operators
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            ip->putback(c);
            double temp;
            *ip >> temp;
            return ct = {temp};
        }
        case 'a':
        case 'c':
        case 'd':
        case 'e':
        case 'i':
        case 'l':
        case 'n':
        case 'o':{  // potential primitives
            ip->putback(c);
            string temp;
            *ip >> temp;
            while (temp.back() == ')') {    // greedy reading of string adjacent to )
                temp.pop_back();
                ip->putback(')');
            }
            if (keywords.count(temp)) ct.kind = keywords[temp];
            else { ct.kind = Kind::Name; ct.data = temp; }
            return ct;
        }
        default: {    // name
            ip->putback(c);
            string temp;
            *ip >> temp;
            while (temp.back() == ')') {    // greedy reading of string adjacent to )
                temp.pop_back();
                ip->putback(')');
            }
            ct.data = temp;
            ct.kind = Kind::Name;
            return ct;
        }
    }
}

// ------------------- operators
template <typename T, typename Iter>
const T& get(Iter p) {
    return boost::get<T>(p->data);
}

class print_visitor : public boost::static_visitor<> {
    string end {" "};
public:
    print_visitor() {}
    print_visitor(string e) : end{e} {}
    void operator()(const string& str) const {
        cout << str << end;
    }
    void operator()(const double num) const {
        cout << num << end;
    }
    void operator()(const Proc* proc) const {
        cout << "proc" << end;
    }
    void operator()(const List list) const {
        cout << '(';
        if (list.size() > 0) {
            auto p = list.begin();
            if(p->kind != Kind::Number && p->kind != Kind::Name && p->kind != Kind::Expr) cout << static_cast<char>(p->kind);    // primitive
            for (;p + 1 != list.end(); ++p) 
                boost::apply_visitor(print_visitor(), p->data);
            boost::apply_visitor(print_visitor(""), p->data);
        }
        
        cout << ')' << end;
    }
};

void print(const Cell& cell) {
    if(cell.kind != Kind::Number && cell.kind != Kind::Name && cell.kind != Kind::Expr) cout << static_cast<char>(cell.kind);    // primitive
    boost::apply_visitor(print_visitor(), cell.data);
}

ostream& operator<<(ostream& os, const Cell& c) {
    print(c);
    return os;
}


// ---------------- parser
namespace Parser {
    Env* bind(const List& params, const List& args, Env* env);
}
List expr(bool getfirst);    // parses an expression without evaluating it, returning it as the lstval inside a cell
Cell eval(const List& expr, Env* env);     // delayed evaluation of expression given back by expr()
Cell apply(const Cell& c, const List& args);           // applies a procedure to return a value
List evlist(const List& expr, Env* env);
Cell apply_prim(const Cell& prim, const List& args);

List expr(bool getfirst) {   // returns an unevaluated expression from stream
    List res;
    while (getfirst && cs.get().kind == Kind::Comment) { cout << "Comment at start of line\n"; cs.ignoreln(); }  // eat either first ( or ;
    // expr ... (expr) ...) starts with first lp eaten
    while (true) {
        cs.get();
        switch (cs.current().kind) {
            case Kind::Lp: {    // start of another expression
                res.push_back(expr(false));  // construct with List, kind is expr and data stored in lstval
                // after geting in an ( expression ' ' <-- expecting rp
                if (cs.current().kind != Kind::Rp) throw runtime_error("')' expected");
                break;
            }
            case Kind::End:
            case Kind::Rp: return res;  // for initial expr call, all nested expr calls will exit through first case
            case Kind::Comment: 
                cout << "Comment encountered\n";
                cs.ignoreln(); 
                break; 
            default: res.push_back(cs.current()); break;   // anything else just push back as is
        }
    }
}

Cell eval(const List& expr, Env* env) {
    for (auto& cell : expr)
        print(cell);
    cout << endl;
    for (auto p = expr.begin(); p != expr.end(); ++p) {
        switch (p->kind) {
            case Kind::Include: 
                cout << "Including file: " << get<string>(++p) << endl;
                cs.set_input(new ifstream{get<string>(p)}); 
                return {Kind::Include};
            case Kind::Number: cout << "number encountered: " << get<double>(p) << endl; return *p;
            // return next expression unevaluated, (quote expr)
            case Kind::Quote: 
                if (p + 1 == expr.end()) throw runtime_error("Quote expects 1 arg");
                return *++p;  
            case Kind::Begin:       // (begin a b c d ... return)
                cout << "Beginning sequence\n";
                evlist({++p, expr.end() - 1}, env);
                return eval({expr.back()}, env);    
            case Kind::Lambda: {    // (lambda (params) (body))
                if (p + 2 >= expr.end()) throw runtime_error("Malformed lambda expression");
                auto params = get<List>(++p);
                auto body = get<List>(++p);
                procs.push_back({params, body, env});    // introduce onto heap
                return {&procs.back()};
            }
            // introduce cell to environment (define name expr)
            case Kind::Define: {
                if (p + 2 >= expr.end()) throw runtime_error("Malformed define expression");
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
                else throw runtime_error("Unfamiliar form to define");
            }
            // (... (expr) ...) parentheses encloses expression (as parsed by expr())
            case Kind::Expr: { 
                auto res = evlist(get<List>(p), env); 
                if (res.size() == 1) return {res[0]}; // single element
                return {res};
            }
            case Kind::Let: {
                if (p + 2 >= expr.end()) throw runtime_error("Let expects a list of definitions and a body");
                auto localvars = get<List>(++p); // ((name val) (name val) ...)
                cout << "Creating temp env for local vars of let (eval)\n";
                Env localenv {env};
                for (auto& pair : localvars) {// add to local env
                    string name {boost::get<string>((boost::get<List>(pair.data)[0]).data)};
                    cout << "Name: " << name;
                    auto val = eval({boost::get<List>(pair.data)[1]}, env);
                    cout << "  Value: " << val << '\n';
                    localenv[name] = val;
                }
                // envs.push_back(localenv);
                // evaluate rest of expression inside new env
                if ((++p)->kind == Kind::Expr) {
                    auto body = get<List>(p);
                    return eval(body, &localenv);
                }
                else return eval({*p}, &localenv);
            }
            // (cond ((pred) (expr)) ((pred) (expr)) ...(else expr)) expect list of pred-expr pairs
            case Kind::Cond: {
                while (++p != expr.end()) {
                    const List& clause = get<List>(p);
                    if (clause[0].kind == Kind::Else) {
                        if (p + 1 == expr.end()) return eval({clause[1]}, env);
                        else throw runtime_error("Else clause not at end of condition");
                    }
                    if (eval({clause[0]}, env)) return eval({clause[1]}, env);
                }
            }
            // primitive procedures
            case Kind::Add: case Kind::Sub: case Kind::Mul: case Kind::Div: case Kind::Less: case Kind::Greater: case Kind::Equal: 
            case Kind::Cat: case Kind::Cons: case Kind::Car: case Kind::Cdr: case Kind::List: case Kind::And: case Kind::Or: case Kind::Not:
            case Kind::Empty:
                             { cout << "calling apply prim, proc: " << static_cast<char>(p->kind);
                                if (p + 1 == expr.end()) throw runtime_error("Primitives take at least one argument");
                                 cout << endl;
                                 auto prim = *p;
                                 auto argstart = ++p;
                                 cout << "primitive access successful\n";
                                 List arg_unevaled = {argstart, expr.end()};
                                 cout << "argument length: " << arg_unevaled.size() << endl;
                                 cout << "------------ unevaledargs -------------\n";
                                 for (auto& cell : arg_unevaled)
                                     print(cell);
                                 cout << endl;
                                 cout << "----------- endunevaledargs -----------\n";
                                 auto args = evlist({argstart, expr.end()}, env);
                                 cout << "------------ args -------------\n";
                                 for (auto& cell : args)
                                     print(cell);
                                 cout << endl;
                                 cout << "----------- endargs -----------\n";
                                 cout << "evlist successful\n";
                                 return apply_prim(prim, args); }
            case Kind::Name: {  // lexer cannot distinguish between varname and procname, have to evaluate against environment
                Cell x = env->lookup(get<string>(p));
                if (p != expr.begin()) cout << "Not at start of expression\n";
                if (x.kind != Kind::Proc) return x;    // don't evaluate procedures in the middle of an expression
                cout << "User defined proc (eval): " << get<string>(p) << endl;
                // return apply(x, evlist({++p, expr.end()}, env));    // user defined proc
                List args;
                while (++p != expr.end()) {  // evaluate as many arguments locally as possible
                    if (p->kind == Kind::Number) args.push_back(*p);
                    else if (p->kind == Kind::Quote) args.push_back(*++p);
                    else if (p->kind == Kind::Name) args.push_back(env->lookup(get<string>(p)));
                    else {
                        List addargs = evlist({p, expr.end()}, env); // evlist any remaining expressions
                        args.insert(args.end(), addargs.begin(), addargs.end());
                        break;
                    }
                }
                return apply(x, args);    // user defined proc
            }
            default: throw runtime_error("Unmatched cell in eval");
        }
    }
    return {};
}

List evlist(const List& expr, Env* env) {
    cout << "In evlist with expr size: " << expr.size() << endl;
    for (auto& cell : expr)
        print(cell);
    cout << endl;
    List res;   // instead of returning right away, push back into res then return res
    for (auto p = expr.begin(); p != expr.end(); ++p) {
        switch (p->kind) {
            case Kind::Include: 
                cout << "Including file\n";
                cs.set_input(new ifstream{get<string>(++p)}); 
                return {};
            case Kind::Number: res.push_back(*p); break;
            // return next expression unevaluated, (quote expr)
            case Kind::Quote: 
                if (p + 1 == expr.end()) throw runtime_error("Quote expects 1 arg");
                res.push_back(*++p); break;  
            case Kind::Begin:       // (begin a b c d ... return)
                cout << "Beginning sequence (evlist)\n";
                evlist({++p, expr.end() - 1}, env);
                res.push_back(eval({expr.back()}, env));
                return res;
            case Kind::Lambda: {    // (lambda (params) (body))
                if (p + 2 >= expr.end()) throw runtime_error("Malformed lambda expression");
                assert(p + 2 != expr.end()); 
                auto params = get<List>(++p);
                auto body = get<List>(++p);
                procs.push_back({params, body, env});    // introduce onto heap
                res.push_back({&procs.back()});
                break;
            }
            // introduce cell to environment (define name expr)
            case Kind::Define: {
                if (p + 2 >= expr.end()) throw runtime_error("Malformed define expression");
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
                else throw runtime_error("Unfamiliar form to define");
            }
            // (... (expr) ...) parentheses encloses expression (as parsed by expr())
            case Kind::Expr: {
                auto r = evlist(get<List>(p), env); 
                if (r.size() == 1) res.push_back({r[0]}); // single element result
                else res.push_back({r});
                break;
            }
            case Kind::Let: {
                if (p + 2 >= expr.end()) throw runtime_error("Let expects a list of definitions and a body");
                auto localvars = get<List>(++p); // ((name val) (name val) ...)
                cout << "Creating temp env for local vars of let (evlist)\n";
                Env localenv {env};
                for (auto& pair : localvars) {// add to local env
                    string name {boost::get<string>((boost::get<List>(pair.data)[0]).data)};
                    cout << "Name: " << name;
                    auto val = eval({boost::get<List>(pair.data)[1]}, env);
                    cout << "  Value: " << val << '\n';
                    localenv[name] = val;
                }
                // evaluate rest of expression inside new env
                if ((++p)->kind == Kind::Expr) {
                    auto body = get<List>(p);
                    res.push_back(eval(body, &localenv));
                }
                else res.push_back(eval({*p}, &localenv));
                return res;   
            }
            // (cond ((pred) (expr)) ((pred) (expr)) ...) expect list of pred-expr pairs
            case Kind::Cond: {
                while (++p != expr.end()) {
                    const List& clause = get<List>(p);
                    if (clause[0].kind == Kind::Else) {
                        if (p + 1 == expr.end()) { res.push_back(eval({clause[1]}, env)); return res; }
                        else throw runtime_error("Else clause not at end of condition");
                    }
                    if (eval({clause[0]}, env)) { res.push_back(eval({clause[1]}, env)); break; }
                }
                break;
            }
            // primitive procedures
            case Kind::Add: case Kind::Sub: case Kind::Mul: case Kind::Div: case Kind::Less: case Kind::Greater: case Kind::Equal: 
            case Kind::Cat: case Kind::Cons: case Kind::Car: case Kind::Cdr: case Kind::List: case Kind::And: case Kind::Or: case Kind::Not:
            case Kind::Empty: 
            {
                if (p + 1 == expr.end()) throw runtime_error("Primitives take at least one argument");
                auto prim = *p;
                auto argstart = ++p;
                                 cout << "primitive access successful\n";
                                 List arg_unevaled = {argstart, expr.end()};
                                 cout << "argument length: " << arg_unevaled.size() << endl;
                                 cout << "------------ unevaledargs (evlist) -------------\n";
                                 for (auto& cell : arg_unevaled)
                                     print(cell);
                                 cout << endl;
                                 cout << "----------- endunevaledargs (evlist) -----------\n";
                auto args = evlist({argstart, expr.end()}, env);
                                 cout << "------------ args (evlist) -------------\n";
                                 for (auto& cell : args)
                                     print(cell);
                                 cout << endl;
                                 cout << "----------- endargs (evlist) -----------\n";
                res.push_back(apply_prim(prim, args));
                return res; // finished reading entire expression
            }
            case Kind::Name: {  // lexer cannot distinguish between varname and procname, have to evaluate against environment
                Cell x = env->lookup(get<string>(p));
                if (x.kind != Kind::Proc) { res.push_back(x); break; }
                else { 
                    cout << "User defined proc (evlist): " << get<string>(p) << endl;
                    List args;
                    while (++p != expr.end()) {  // evaluate as many arguments locally as possible
                        if (p->kind == Kind::Number) args.push_back(*p);
                        else if (p->kind == Kind::Quote) args.push_back(*++p);
                        else if (p->kind == Kind::Name) args.push_back(env->lookup(get<string>(p)));
                        else {
                            List addargs = evlist({p, expr.end()}, env); // evlist any remaining expressions
                            args.insert(args.end(), addargs.begin(), addargs.end());
                            break;
                        }
                    }
                    res.push_back(apply(x, args)); return res; }   // user defined proc
            }
            default: throw runtime_error("Unmatched in evlist"); 
        }
    }
    return res;
}

Cell apply(const Cell& c, const List& args) {  // expect fully evaluated args
    cout << "Inside apply with arg length: " << args.size() << endl;
                                 cout << "------------ args (apply) -------------\n";
                                 for (auto& cell : args)
                                     print(cell);
                                 cout << endl;
                                 cout << "----------- endargs (apply) -----------\n";

    const Proc& proc = *boost::get<Proc*>(c.data);
    Env* newenv = Parser::bind(proc.params, args, proc.env);
    cout << "Arguments bound to new environment\n";
    for (auto& cell : proc.params)
        cout << get<string>(cell.data) << ": " << newenv->lookup(get<string>(cell.data)) << endl;
    return eval(proc.body, newenv);
}

namespace Parser {
    Env* bind(const List& params, const List& args, Env* env) {
        Env newenv {env};
        if (params.size() != args.size()) throw runtime_error("# of args provided and expected mismatch");
        auto q = args.begin();
        for (auto p = params.begin(); p != params.end(); ++p, ++q) {
            cout << "Binding " << get<string>(p) << " to " << *q << endl;
            newenv[get<string>(p)] = *q;
        }

        envs.push_back(newenv);  // store on the heap to allow reference and pointer
        return &envs.back();
    }
}

// primitive procedures
class less_visitor : public boost::static_visitor<bool> {
    // first elements stored, second elements taken as operand
    string str;
    double num;
    List list;
    Proc* proc;
public:
    less_visitor(const string& s) : str{s} {}
    less_visitor(const double d) : num{d} {}
    less_visitor(Proc* const p) : proc(p) {}
    less_visitor(const List& l) : list{l} {}
    bool operator()(const string& s) const { return str < s; }
    bool operator()(const double n) const { return num < n; }
    bool operator()(Proc* const p) const { return (*proc).body < (*p).body; }
    bool operator()(const List& l) const { return list < l; }
};

class equal_visitor : public boost::static_visitor<bool> {
    string str;
    double num;
    List list;
    Proc* proc;
public:
    equal_visitor(const string& s) : str{s} {}
    equal_visitor(const double d) : num{d} {}
    equal_visitor(Proc* const p) : proc(p) {}
    equal_visitor(const List& l) : list{l} {}
    bool operator()(const string& s) const { return str == s; }
    bool operator()(const double n) const { return num == n; }
    bool operator()(Proc* const p) const { return proc == p; }
    bool operator()(const List& l) const { return list == l; }
};

bool operator<(const Cell& a, const Cell& b) {
    if (a.kind == Kind::Number)
        return boost::apply_visitor(less_visitor(boost::get<double>(a.data)), b.data);
    return boost::apply_visitor(less_visitor(boost::get<string>(a.data)), b.data);
}
bool operator==(const Cell& a, const Cell& b) {
     if (a.kind == Kind::Number)
        return boost::apply_visitor(equal_visitor(boost::get<double>(a.data)), b.data);
    return boost::apply_visitor(equal_visitor(boost::get<string>(a.data)), b.data);
}

Cell apply_prim(const Cell& prim, const List& args) {
    cout << "inside apply prim: " << static_cast<char>(prim.kind) << endl;
                                 cout << "------------ args (apply_prim) -------------\n";
                                 for (auto& cell : args)
                                     print(cell);
                                 cout << endl;
                                 cout << "----------- endargs (apply_prim) -----------\n";
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
        case Kind::Empty: {
            if (args[0].kind == Kind::Expr) {
                cout << "List size in checking if empty: " << get<List>(args.begin()).size() << endl;
                return Cell{get<List>(args.begin()).size() == 0};
            }
            return Cell{Kind::False};
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
        case Kind::Car: {
            if (args[0].kind != Kind::Expr) return args[0];
            return boost::get<List>(args[0].data)[0]; // args is a list of one cell which holds a list itself
        }
        case Kind::Cdr: { 
            if (args[0].kind != Kind::Expr) return {List {}};
            auto list = boost::get<List>(args[0].data); 
            if (list.size() == 1) return {List {}};
            else if (list.size() == 2) return list[1];
            return {List{list.begin() + 1, list.end()}}; 
        }
        default: throw runtime_error("Mismatoh in apply_prim");
    }
}

void alloc_env() {
    envs.reserve(max_capacity * 4); // reserve to preserve pointers
    procs.reserve(max_capacity);
    envs.push_back(e0);
}

void start(bool print_res) {
    while (true) {
        if (print_res) cout << "> ";
        try {
            auto res = eval(expr(true), &e0);
            if (print_res)
                cout << res << '\n';    
            if (res.kind == Kind::End || cs.eof()) { cout << "At end of stream, reseting\n"; cs.reset(); if (cs.base()) print_res =  true; }
        }
        catch (exception& e) {
            cout << "Bad expression: " << e.what() << endl;
        }
    }
}

int main(int argc, char* argv[]) {
    bool print_res {false};
    switch (argc) {
        case 1:
            print_res = true;
            break;
        case 2:
            cs.set_input(new ifstream{argv[1]});
            break;
        case 3: {
            cs.set_input(new ifstream{argv[1]});
            string option {argv[2]};
            if (option == "-p" || option == "-print") print_res = true;
            break;
        }
        default:
            throw runtime_error("too many arguments");
            return 1;
    }
    alloc_env();
    start(print_res);
    /* expr testing
    cs.get();   // eat first '('
    List l = expr();
    print({l});
    cout << endl;
    */

    /* eval testing
    List v {Kind::Define, "x", List{Kind::Lambda, List{"x", "y"}, List{Kind::Add, "x", "y"}}};
    Cell c = eval(v, &e0);
    cout << static_cast<char>(c.kind) << c << endl;

    List v2 {"x", 50, 60};
    cout << "------------- second expr ---------------\n";
    Cell c2 = eval(v2, &e0);
    cout << c2 << endl;
    */


    /* print and Cell testing
    List x {"inner1", "inner2", 6.9};
    List v {"first", "second", "third", "fourth", 5.5, x}; // need brace for char* because 2 conversions required to construct from string
    List z {"this", "list", "has", "only", "strings"};
    cout << v;

    Cell justdouble {0.5};
    Cell justname {"name"};
    
    cout << "sizes: \n";
    cout << "Cell: " << sizeof(Cell) << endl;   // 32 with union, 48 without
    cout << "List: " << sizeof(List) << endl;
    cout << "List*: " << sizeof(List*) << endl;
    cout << "Data: " << sizeof(Data) << endl;
    cout << "Proc: " << sizeof(Proc) << endl;
    cout << "just double: " << sizeof(justdouble) << endl;
    cout << "just name: " << sizeof(justname) << endl;
    */

}
