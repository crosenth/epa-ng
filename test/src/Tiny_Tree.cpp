#include "Epatest.hpp"

#include "check_equal.hpp"

#include "core/Lookup_Store.hpp"
#include "core/pll/epa_pll_util.hpp"
#include "core/pll/pll_util.hpp"
#include "core/pll/pllhead.hpp"
#include "core/raxml/Model.hpp"
#include "io/Binary.hpp"
#include "io/file_io.hpp"
#include "sample/Sample.hpp"
#include "sample/functions.hpp"
#include "seq/MSA.hpp"
#include "seq/MSA_Info.hpp"
#include "tree/Tiny_Tree.hpp"
#include "tree/Tree.hpp"
#include "tree/Tree_Numbers.hpp"

#include <limits>
#include <tuple>
#include <vector>

using namespace std;

static void place_( Options const options )
{
  // buildup
  auto msa     = build_MSA_from_file( env->reference_file,
                                  MSA_Info( env->reference_file ),
                                  options.premasking );
  auto queries = build_MSA_from_file(
      env->query_file, MSA_Info( env->query_file ), options.premasking );

  auto ref_tree = Tree( env->tree_file, msa, env->model, options );

  auto root = get_root( ref_tree.tree() );

  // tests
  Tiny_Tree tt( root, 0, ref_tree, ref_tree.memsave() );

  for( auto const& x : queries ) {
    auto place = tt.place( x, not options.prescoring, options );
    auto brlen = root->length;
    EXPECT_NE( place.likelihood(), 0.0 );
    EXPECT_NE( place.likelihood(), std::numeric_limits< double >::infinity() );
    EXPECT_NE( place.likelihood(), -std::numeric_limits< double >::infinity() );
    EXPECT_GT( place.distal_length(), 0.0 );
    EXPECT_GT( brlen, place.distal_length() );
    EXPECT_GT( place.pendant_length(), 0.0 );
  }
  // teardown
}

TEST( Tiny_Tree, place ) { all_combinations( place_ ); }

static void place_from_binary( Options const options )
{
  // skip some configs
  SKIP_CONFIG( options.memsave );


  // setup
  auto tree_file      = env->tree_file;
  auto reference_file = env->reference_file;
  auto msa            = build_MSA_from_file( env->reference_file,
                                  MSA_Info( env->reference_file ),
                                  options.premasking );
  auto queries        = build_MSA_from_file(
      env->query_file, MSA_Info( env->query_file ), options.premasking );

  raxml::Model model;

  Tree original_tree( tree_file, msa, model, options );

  dump_to_binary( original_tree, env->binary_file );
  Tree read_tree( env->binary_file, model, options );
  string invocation( "./this --is -a test" );

  if( options.repeats ) {
    ASSERT_TRUE( original_tree.partition()->attributes
                 & PLL_ATTRIB_SITE_REPEATS );
    ASSERT_TRUE( read_tree.partition()->attributes & PLL_ATTRIB_SITE_REPEATS );
  }

  ASSERT_EQ( original_tree.nums().branches, read_tree.nums().branches );

  vector< pll_unode_t* > original_branches( original_tree.nums().branches );
  vector< pll_unode_t* > read_branches( read_tree.nums().branches );

  auto original_traversed
      = utree_query_branches( original_tree.tree(), &original_branches[ 0 ] );
  auto read_traversed
      = utree_query_branches( read_tree.tree(), &read_branches[ 0 ] );

  ASSERT_EQ( original_traversed, read_traversed );
  ASSERT_EQ( original_traversed, original_tree.nums().branches );

  Sample< Placement > orig_samp;
  Sample< Placement > read_samp;

  // test
  for( size_t i = 0; i < original_traversed; i++ ) {
    Tiny_Tree original_tiny(
        original_branches[ i ], i, original_tree, original_tree.memsave() );
    Tiny_Tree read_tiny(
        read_branches[ i ], i, read_tree, read_tree.memsave() );

    size_t seq_id = 0;
    for( auto& seq : queries ) {
      auto orig_place
          = original_tiny.place( seq, not options.prescoring, options );
      auto read_place = read_tiny.place( seq, not options.prescoring, options );

      ASSERT_DOUBLE_EQ( orig_place.likelihood(), read_place.likelihood() );

      orig_samp.add_placement( seq_id, "", orig_place );
      read_samp.add_placement( seq_id, "", read_place );

      ++seq_id;
    }
  }

  ASSERT_EQ( orig_samp.size(), read_samp.size() );

  check_equal( orig_samp, read_samp );

  // effectively, do the candidate selection
  compute_and_set_lwr( orig_samp );
  compute_and_set_lwr( read_samp );
  check_equal( orig_samp, read_samp );

  discard_by_accumulated_threshold( orig_samp, options.prescoring_threshold );
  discard_by_accumulated_threshold( read_samp, options.prescoring_threshold );
  check_equal( orig_samp, read_samp );

  Work read_work( read_samp );
  Work orig_work( orig_samp );

  // printf("%lu vs %lu\n", orig_work.size(), read_work.size());

  EXPECT_EQ( orig_work.size(), read_work.size() );

  // teardown
}

TEST( Tiny_Tree, place_from_binary )
{
  all_combinations( place_from_binary );
}

static void copy_chaining( Options const options )
{
  auto msa     = build_MSA_from_file( env->reference_file,
                                MSA_Info( env->reference_file ),
                                options.premasking );
  auto queries = build_MSA_from_file(
      env->query_file, MSA_Info( env->query_file ), options.premasking );

  auto ref_tree = Tree( env->tree_file, msa, env->model, options );
  auto root = get_root( ref_tree.tree() );

  Tiny_Tree original( root, 0, ref_tree, true );

  // shallow from tiny tree
  Tiny_Tree shallow( original, false );
  check_equal( original, shallow );
  
  Tiny_Tree deep( original, true );
  check_equal( original, deep );
  
  Tiny_Tree shallow_from_deep( deep, false );
  check_equal( original, shallow_from_deep );

  Tiny_Tree deep_from_shallow( shallow, true );
  check_equal( original, deep_from_shallow );
}

TEST( Tiny_Tree, copy_chaining )
{
  all_combinations( copy_chaining );
}
