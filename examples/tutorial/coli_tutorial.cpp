#include <isl/set.h>
#include <isl/union_map.h>
#include <isl/union_set.h>
#include <isl/ast_build.h>
#include <isl/schedule.h>
#include <isl/schedule_node.h>

#include <coli/debug.h>
#include <coli/core.h>

#include <String.h>
#include <Halide.h>


int main(int argc, char **argv)
{
	// Allocate an isl context.  This isl context will be used by the
	// isl library which is a part of coli.
	isl_ctx *ctx = isl_ctx_alloc();

	// Declare a library.  A library is composed of a set of functions.
	coli::library lib("library0");

	// Declare a function in the library lib.
	coli::function fct("function0", &lib);

	// Declare the computations of the function fct.
	// To declare a computation, you need to provide:
	// (1) a Halide expression that represents the computation,
	// (2) an isl set representing the iteration space of the computation, and
	// (3) an isl context (which will be used by the ISL library calls).
	coli::computation computation0(ctx, Halide::Expr((uint8_t) 3), "{S0[i,j]: 0<=i<=1000 and 0<=j<=1000}", &fct);
	coli::computation computation1(ctx, Halide::Expr((uint8_t) 5), "{S1[i,j]: 0<=i<=1023 and 0<=j<=1023}", &fct);
	coli::computation computation2(ctx, Halide::Expr((uint8_t) 7), "{S2[i,j]: 0<=i<=1023 and 0<=j<=1023}", &fct);

	// Create a memory buffer (2 dimensional).
	coli::buffer buf0("buf0", 2, {10,10}, Halide::Int(8), NULL, &fct);

	// Add the buffer as an argument to the function fct.
	fct.add_argument(buf0);

	// Map the computations to the buffers (i.e. where each computation
	// should be stored in the buffer).
	computation0.SetAccess("{S0[i,j]->buf0[i, j]}");
	computation1.SetAccess("{S1[i,j]->buf0[0, 0]}");
	computation2.SetAccess("{S2[i,j]->buf0[i, j]}");

	// Set the schedule of each computation.
	computation0.Tile(0,1,32,32);
	computation1.Schedule("{S1[i,j]->[2,i1,j1,i2,j3,j4]: i1=floor(i/32) and j1=floor(j/32) and i2=i and j3=floor(j/4) and j4=j%4 and 0<=i<=1023 and 0<=j<=1023}");
	computation2.Split(0, 32);
	computation2.Split(2, 32);
	computation2.Interchange(1, 2);
	lib.tag_parallel_dimension("S0", 1);
//	lib.tag_vector_dimension("S1", 5);

	isl_union_map *schedule_map = lib.get_schedule_map();

	// Create time space IR
	isl_union_set *time_space_representaion =
		coli::create_time_space_representation(isl_union_set_copy(lib.get_iteration_spaces()), isl_union_map_copy(schedule_map));

	// Generate code
	isl_ast_build *ast_build = isl_ast_build_alloc(ctx);
	isl_options_set_ast_build_atomic_upper_bound(ctx, 1);
	ast_build = isl_ast_build_set_after_each_for(ast_build, &coli::for_halide_code_generator_after_for, NULL);
	ast_build = isl_ast_build_set_at_each_domain(ast_build, &coli::stmt_halide_code_generator, NULL);
	isl_ast_node *program = isl_ast_build_node_from_schedule_map(ast_build, isl_union_map_copy(schedule_map));
	isl_ast_build_free(ast_build);

	if (DEBUG)
		coli::isl_ast_node_dump_c_code(ctx, program);

	std::vector<std::string> generated_stmts, iterators;
	Halide::Internal::Stmt ss = coli::generate_Halide_stmt_from_isl_node(lib, program, 0, generated_stmts, iterators);
	fct.halide_stmt = &ss; 

	// Dump IRs
	lib.dump_ISIR();
	lib.dump_schedule();
	IF_DEBUG(coli::str_dump("\n\nTime Space IR:\n")); IF_DEBUG(isl_union_set_dump(time_space_representaion)); IF_DEBUG(coli::str_dump("\n\n"));
	coli::halide_IR_dump(*(fct.halide_stmt));


	// Generate an object file from the library lib. 
	lib.halide_gen_obj("LLVM_generated_code.o");

	return 0;
}