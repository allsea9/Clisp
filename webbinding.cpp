#include <sstream>
#include "parser.h"
#include "lexer.h"
#include "environment.h"
#include "error.h"
#include "emscripten/bind.h"

using namespace Lexer;
using namespace Parser;
using namespace Environment;

static void init_env() {
	envs.reserve(max_capacity * 4); // reserve to preserve pointers
	procs.reserve(max_capacity);
	envs.push_back(e0);
}

string expr_str(string input) {
	static bool inited {false};
	if (!inited) { init_env(); inited = true; }
	istringstream in(input);
	ostringstream out;
	outstream = &out;
	cs.set_input(in);
	// ts.set_input(nullptr);
	auto res = eval(expr(true), &e0);
	*outstream << res;
	return out.str();
}



EMSCRIPTEN_BINDINGS(my_module) {
	emscripten::function("expr_str", &expr_str);
}