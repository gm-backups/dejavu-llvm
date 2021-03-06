#include <dejavu/linker/linker.h>
#include <dejavu/linker/game.h>

#include <dejavu/compiler/lexer.h>
#include <dejavu/compiler/parser.h>
#include <dejavu/compiler/codegen.h>
#include <dejavu/system/buffer.h>

#include <llvm/PassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/DataLayout.h>

#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/FileSystem.h>

#include <sstream>
#include <algorithm>
#include <memory>

using namespace llvm;

static Module *load_module(const char *filename, LLVMContext &context) {
	std::unique_ptr<MemoryBuffer> file = std::move(MemoryBuffer::getFile(filename).get());
	return getLazyBitcodeModule(std::move(file), context).get();
}

linker::linker(
	const char *output, game &g, error_stream &e,
	const std::string &triple, LLVMContext &context
) : context(context), runtime(load_module("runtime.bc", context)),
	output(output), source(g), errors(e), compiler(*runtime, errors) {
	verifyModule(*runtime);
}

bool linker::build(const char *target, bool debug) {
	errors.progress(20, "compiling libraries");
	build_libraries();

	errors.progress(30, "compiling scripts");
	build_scripts();

	errors.progress(40, "compiling objects");
	build_objects();

	if (errors.count() > 0) return false;

	Module &game = compiler.get_module();
	{
		SmallString<80> str;
		raw_svector_ostream error(str);
		if (verifyModule(game, &error)) {
			game.dump();
			errors.error(error.str());
			return false;
		}
	}

	if (!debug) {
		PassManager pm;
		PassManagerBuilder pmb;
		pmb.populateModulePassManager(pm);
		pm.run(game);
	}

	{
		std::error_code error;
		std::ostringstream f; f << output << "/objects.bc";
		tool_output_file out(f.str(), error, sys::fs::F_None);
		if (error) {
			errors.error(error.message());
			return false;
		}

		WriteBitcodeToFile(&game, out.os());
		out.keep();
	}

	errors.progress(60, "linking runtime");
	link(target, debug);

	return errors.count() == 0;
}

static void diagnostic_handler(const DiagnosticInfo &DI) {
	fprintf(stderr, "AUGHERASER\n");
}

bool linker::link(const char *target, bool debug) {
	std::ostringstream f; f << output << "/objects.bc";
	std::unique_ptr<Module> objects(load_module(f.str().c_str(), context));

	std::unique_ptr<Module> game = std::make_unique<Module>("game", context);
	Linker L(game.get(), &diagnostic_handler);
	if (L.linkInModule(objects.get()) || L.linkInModule(runtime.get()))
		errors.error("failed to link with runtime");

	if (!debug) {
		PassManager pm;
		PassManagerBuilder pmb;
		pmb.OptLevel = 3;
		pmb.populateLTOPassManager(pm);
		pm.run(*game);
	}

	std::error_code error;
	tool_output_file out(target, error, sys::fs::F_None);
	if (error) {
		errors.error(error.message());
		return false;
	}

	WriteBitcodeToFile(game.get(), out.os());
	out.keep();

	return true;
}

void linker::build_libraries() {
	for (unsigned int i = 0; i < source.nactions; i++) {
		if (source.actions[i].exec != action_type::exec_code)
			continue;

		std::ostringstream name;
		name << "action_lib";
		if (source.actions[i].parent > -1) name << source.actions[i].parent;
		name << "_" << source.actions[i].id;

		size_t nargs = source.actions[i].nargs;
		if (source.actions[i].relative) nargs++;
		add_function(
			strlen(source.actions[i].code), source.actions[i].code,
			name.str().c_str(), nargs, false
		);
	}
}

void linker::build_scripts() {
	// first pass so the code generator knows which functions are scripts
	for (unsigned int i = 0; i < source.nscripts; i++) {
		compiler.register_script(std::string(source.scripts[i].name));
	}

	for (unsigned int i = 0; i < source.nscripts; i++) {
		add_function(
			strlen(source.scripts[i].code), source.scripts[i].code,
			source.scripts[i].name, 0, true
		);
	}
}

std::ostream &operator <<(std::ostream &out, const argument &arg);
void linker::build_objects() {
	for (unsigned int i = 0; i < source.nobjects; i++) {
		object &obj = source.objects[i];
		// todo: output object properties

		for (unsigned int e = 0; e < obj.nevents; e++) {
			event &evt = obj.events[e];
			// todo: output event data

			// todo: move this into compiler
			std::ostringstream code;
			for (unsigned int a = 0; a < evt.nactions; a++) {
				action &act = evt.actions[a];

				// todo: don't do this by unparsing and reparsing
				switch (act.type->kind) {
				case action_type::act_begin: code << "{\n"; break;
				case action_type::act_end: code << "}\n"; break;
				case action_type::act_else: code << "else\n"; break;
				case action_type::act_exit: code << "exit\n"; break;

				case action_type::act_repeat:
					code << "repeat (" << act.args[0].val << ")\n";
					break;
				case action_type::act_variable:
					code
						<< act.args[0].val
						<< (act.relative ? " += " : " = ")
						<< act.args[1].val << "\n";
					break;

				case action_type::act_code: {
					std::ostringstream s;
					s << obj.name << "_" << evt.main_id << "_" << evt.sub_id << "_" << a;
					add_function(strlen(act.args[0].val), act.args[0].val, s.str(), 0, false);

					code << s.str() << "()\n";
					break;
				}

				case action_type::act_normal: {
					if (act.type->exec == action_type::exec_none) break;

					if (act.target != action::self) code << "with (" << act.target << ")";
					if (act.type->question) code << "if (";
					if (act.inv) code << '!';

					if (act.type->exec == action_type::exec_code) {
						code << "action_lib";
						if (act.type->parent > -1) code << act.type->parent;
						code << "_" << act.type->id;
					}
					else
						code << act.type->code;

					code << '(';
					unsigned int n = 0;
					for (; n < act.nargs; n++) {
						if (n != 0) code << ", ";
						code << act.args[n];
					}
					if (act.type->relative) {
						code << ", " << act.relative;
					}
					code << ')';

					if (act.type->question) code << ')';

					code << '\n';
					break;
				}

				default: /* do nothing */;
				}
			}

			std::ostringstream s;
			s << obj.name << "_" << evt.main_id << "_" << evt.sub_id;
			std::string c = code.str();
			add_function(c.size(), c.c_str(), s.str(), 0, false);
		}
	}
}

std::ostream &operator <<(std::ostream &out, const argument &arg) {
	switch (arg.kind) {
	case argument::arg_expr:
		return out << arg.val;

	case argument::arg_both:
		if (arg.val[0] == '"' || arg.val[0] == '\'') return out << arg.val;

	// fall through
	case argument::arg_string: {
		std::string val(arg.val);
		while (val.find('"') != std::string::npos) {
			val.replace(val.find('"'), 1, "\"+'\"'+\"");
		}
		return out << '"' << val << '"';
	}

	case argument::arg_bool:
		return out << (arg.val[0] == '0');

	case argument::arg_menu:
		return out << arg.val;

	case argument::arg_color:
		return out << '$' << arg.val;

	default:
		return out << arg.resource;
	}
}

void linker::add_function(
	size_t length, const char *data,
	const std::string &name, int args, bool var
) {
	buffer code(length, data);
	token_stream tokens(code);

	arena allocator;
	parser parser(tokens, allocator, errors);
	errors.set_context(name);

	node *program = parser.getprogram();
	if (errors.count() > 0) return;

	compiler.add_function(program, name.c_str(), args, var);
}
