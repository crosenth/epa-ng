#include "tree/Tiny_Tree.hpp"

#ifdef __OMP
#include <omp.h>
#endif

#include <numeric>
#include <vector>

#include "core/pll/optimize.hpp"
#include "core/pll/pll_util.hpp"
#include "core/pll/pllhead.hpp"
#include "core/raxml/Model.hpp"
#include "set_manipulators.hpp"
#include "tree/Tree_Numbers.hpp"
#include "tree/tiny_util.hpp"
#include "util/logging.hpp"

/**
 * Calculate the persite log-likelihoods for a given character, defining a
 * full-length sequence of only that character.
 *
 * Used exclusively to build the util/LookupStore, a performance shortcut for
 * pre-placement
 *
 * @param nt     character defining the artificial sequence
 * @param result per-site log-likelihood vector
 */
void Tiny_Tree::get_persite_logl( char const nt,
                                  std::vector< double >& result )
{
  size_t const sites = partition_->sites;
  auto const new_tip = tree_.get()->nodes[ 2 ];
  auto const inner   = new_tip->back;
  result.clear();
  result.resize( sites );
  std::string seq( sites, nt );

  std::vector< unsigned int > param_indices( partition_->rate_cats, 0 );

  auto map = get_char_map( partition_.get() );

  auto err_check = pll_set_tip_states( partition_.get(),
                                       new_tip->clv_index,
                                       map,
                                       seq.c_str() );

  if( err_check == PLL_FAILURE ) {
    throw std::runtime_error{
      std::string( "Set tip states during sites precompution failed! pll_errmsg: " )
      + pll_errmsg
    };
  }

  pll_compute_edge_loglikelihood( partition_.get(),
                                  new_tip->clv_index,
                                  PLL_SCALE_BUFFER_NONE,
                                  inner->clv_index,
                                  inner->scaler_index,
                                  inner->pmatrix_index,
                                  &param_indices[ 0 ],
                                  &result[ 0 ] );
}

Tiny_Tree::Tiny_Tree( pll_unode_t* edge_node,
                      unsigned int const branch_id,
                      Tree& reference_tree,
                      bool const deep_copy_clvs = false )
    : partition_( nullptr, tiny_partition_destroy )
    , tree_( nullptr, utree_destroy )
    , branch_id_( branch_id )
{
  assert( edge_node );
  original_branch_length_ = edge_node->length;

  auto old_proximal = edge_node->back;
  auto old_distal   = edge_node;

  // detect the tip-tip case. In the tip-tip case, the reference tip should
  // always be the DISTAL
  bool tip_tip_case = false;
  if( !old_distal->next ) {
    tip_tip_case = true;
  } else if( !old_proximal->next ) {
    tip_tip_case = true;
    // do the switcheroo
    old_distal   = old_proximal;
    old_proximal = old_distal->back;
  }

  tree_ = std::unique_ptr< pll_utree_t, utree_deleter >(
      make_tiny_tree_structure( old_proximal,
                                old_distal,
                                tip_tip_case ),
      utree_destroy );

  partition_ = std::unique_ptr< pll_partition_t, partition_deleter >(
      make_tiny_partition( reference_tree,
                           tree_.get(),
                           old_proximal,
                           old_distal,
                           tip_tip_case ),
      tiny_partition_destroy );

  // operation for computing the clv toward the new tip (for initialization and logl in non-blo case)
  auto proximal = tree_->nodes[ 0 ];
  auto distal   = tree_->nodes[ 1 ];
  auto inner    = tree_->nodes[ 3 ];

  pll_operation_t op;
  op.parent_clv_index    = inner->clv_index;
  op.child1_clv_index    = distal->clv_index;
  op.child1_scaler_index = distal->scaler_index;
  op.child2_clv_index    = proximal->clv_index;
  op.child2_scaler_index = proximal->scaler_index;
  op.parent_scaler_index = inner->scaler_index;
  op.child1_matrix_index = distal->pmatrix_index;
  op.child2_matrix_index = proximal->pmatrix_index;

  // wether heuristic is used or not, this is the initial branch length configuration
  double branch_lengths[ 3 ]       = { proximal->length, distal->length, inner->length };
  unsigned int matrix_indices[ 3 ] = { proximal->pmatrix_index, distal->pmatrix_index, inner->pmatrix_index };

  // use branch lengths to compute the probability matrices
  std::vector< unsigned int > param_indices( reference_tree.partition()->rate_cats, 0 );
  pll_update_prob_matrices( partition_.get(),
                            &param_indices[ 0 ],
                            matrix_indices,
                            branch_lengths,
                            3 );

  // use update_partials to compute the clv pointing toward the new tip
  pll_update_partials( partition_.get(), &op, 1 );
}

Placement Tiny_Tree::place( Sequence const& s,
                            bool const opt_branches,
                            Options const& options )
{
  assert( partition_ );
  assert( tree_ );

  auto const inner    = tree_->nodes[ 3 ];
  auto const distal   = tree_->nodes[ 1 ];
  auto const proximal = tree_->nodes[ 0 ];
  auto const new_tip  = inner->back;

  auto distal_length  = distal->length;
  auto pendant_length = inner->length;
  double logl         = 0.0;
  std::vector< unsigned int > param_indices( partition_->rate_cats, 0 );

  if( s.sequence().size() != partition_->sites ) {
    throw std::runtime_error{ "Query sequence length not same as reference alignment!" };
  }

  Range range( 0, partition_->sites );

  if( options.premasking ) {
    range = get_valid_range( s.sequence() );
    if( not range ) {
      throw std::runtime_error{ std::string() + "Sequence with header '" + s.header()
                                + "' does not appear to have any non-gap sites!" };
    }
  }

  auto virtual_root = inner;

  // init the new tip with s.sequence(), branch length
  auto err_check = pll_set_tip_states( partition_.get(),
                                       new_tip->clv_index,
                                       get_char_map( partition_.get() ),
                                       s.sequence().c_str() );

  if( err_check == PLL_FAILURE ) {
    throw std::runtime_error{ "Set tip states during placement failed!" };
  }

  if( opt_branches ) {

    if( options.premasking ) {
      logl = call_focused( optimize_branch_triplet, range, partition_.get(), virtual_root, options.sliding_blo );
    } else {
      logl = optimize_branch_triplet( partition_.get(), virtual_root, options.sliding_blo );
    }

    assert( inner->length >= 0 );
    assert( inner->next->length >= 0 );
    assert( inner->next->next->length >= 0 );

    // rescale the distal length, as it has likely changed during optimization
    // done as in raxml
    double const new_total_branch_length = distal->length + proximal->length;
    distal_length                        = ( original_branch_length_ / new_total_branch_length ) * distal->length;
    pendant_length                       = inner->length;

    reset_triplet_lengths( inner,
                           partition_.get(),
                           original_branch_length_ );
  }

  // re-update the partial
  auto child1 = virtual_root->next->back;
  auto child2 = virtual_root->next->next->back;

  pll_operation_t op;
  op.parent_clv_index    = virtual_root->clv_index;
  op.parent_scaler_index = virtual_root->scaler_index;
  op.child1_clv_index    = child1->clv_index;
  op.child1_scaler_index = child1->scaler_index;
  op.child1_matrix_index = child1->pmatrix_index;
  op.child2_clv_index    = child2->clv_index;
  op.child2_scaler_index = child2->scaler_index;
  op.child2_matrix_index = child2->pmatrix_index;

  pll_update_partials( partition_.get(), &op, 1 );

  if( logl == -std::numeric_limits< double >::infinity() ) {
    throw std::runtime_error{
      std::string( "-INF logl at branch " ) + std::to_string( branch_id_ ) + " with sequence " + s.header()
    };
  }

  assert( distal_length <= original_branch_length_ );
  assert( distal_length >= 0.0 );

  return Placement( branch_id_, logl, pendant_length, distal_length );
}
