#include "tiny_util.hpp"

#include "pll_util.hpp"


pll_partition_t * make_tiny_partition(const pll_partition_t * old_partition, const pll_utree_t * tree,
  const pll_utree_t * old_proximal, const pll_utree_t * old_distal, const bool tip_tip_case)
{
  /**
    As we work with PLL_PATTERN_TIP functionality, special care has to be taken in regards to the tree and partition
    structure: PLL assumes that any node with clv index < number of tips is in fact a real tip, that is
    a tip that uses a character array instead of a real clv. Here we need to set up the tree/partition to fool pll:
    the tips that actually contain CLVs copied over from the reference tree have their index set to greater than
    number of tips. This results in a acceptable amount of wasted memory that is never used (num_sites * bytes
    * number of clv-tips)
  */
  unsigned int num_clv_tips = 2; // tip_inner case: both reference nodes are inner nodes
  if (tip_tip_case)
    num_clv_tips = 1; // one for the "proximal" clv tip

  auto distal = tree->next->back;
  auto proximal = tree->next->next->back;

  pll_partition_t * tiny = pll_partition_create(
    3, // tips
    1 + num_clv_tips, // extra clv's
    old_partition->states, old_partition->sites,
    0, // number of mixture models
    old_partition->rate_matrices,
    3, // number of prob. matrices (one per possible unique branch length)
    old_partition->rate_cats,
    3, // number of scale buffers (one per possible inner node)
    pll_map_nt,
    old_partition->attributes);

  assert(tiny);

  unsigned int i;
  free(tiny->rates);
  tiny->rates = old_partition->rates;
  if (tiny->subst_params)
    for (i = 0; i < tiny->rate_matrices; ++i)
      pll_aligned_free(tiny->subst_params[i]);
  free(tiny->subst_params);
  tiny->subst_params = old_partition->subst_params;
  if (tiny->frequencies)
    for (i = 0; i < tiny->rate_matrices; ++i)
      pll_aligned_free(tiny->frequencies[i]);
  free(tiny->frequencies);
  tiny->frequencies = old_partition->frequencies;
  if (tiny->eigenvecs)
    for (i = 0; i < tiny->rate_matrices; ++i)
      pll_aligned_free(tiny->eigenvecs[i]);
  free(tiny->eigenvecs);
  tiny->eigenvecs = old_partition->eigenvecs;
  if (tiny->inv_eigenvecs)
    for (i = 0; i < tiny->rate_matrices; ++i)
      pll_aligned_free(tiny->inv_eigenvecs[i]);
  free(tiny->inv_eigenvecs);
  tiny->inv_eigenvecs = old_partition->inv_eigenvecs;
  if (tiny->eigenvals)
    for (i = 0; i < tiny->rate_matrices; ++i)
      pll_aligned_free(tiny->eigenvals[i]);
  free(tiny->eigenvals);
  tiny->eigenvals = old_partition->eigenvals;
  if (tiny->prop_invar)
    free(tiny->prop_invar);
  tiny->prop_invar = old_partition->prop_invar;
  free(tiny->eigen_decomp_valid);
  tiny->eigen_decomp_valid = old_partition->eigen_decomp_valid;
  if (tiny->pattern_weights)
    free(tiny->pattern_weights);
  tiny->pattern_weights = old_partition->pattern_weights;

  // shalow/deep copy tip_tip_pattern specific things
  // shallow
  if (tiny->lh_statepair)
    free(tiny->lh_statepair);
  if (tiny->charmap)
    free(tiny->charmap);
  if (tiny->revmap)
    free(tiny->revmap);
  tiny->lh_statepair = old_partition->lh_statepair;
  tiny->charmap = old_partition->charmap;
  tiny->revmap = old_partition->revmap;

  // deep
  tiny->maxstates = old_partition->maxstates;
  tiny->log2_maxstates = old_partition->log2_maxstates;
  tiny->log2_rates = old_partition->log2_rates;
  tiny->log2_states = old_partition->log2_states;

  assert(old_partition->clv[old_proximal->clv_index] != NULL);
  assert(old_partition->clv[old_distal->clv_index] || old_partition->tipchars[old_distal->clv_index]);

  unsigned int clv_size = sizeof(double) * old_partition->sites * old_partition->rate_cats * old_partition->states_padded;

  // deep copy clv's
  memcpy(tiny->clv[proximal->clv_index],
    old_partition->clv[old_proximal->clv_index],
    clv_size);

  if(tip_tip_case)
    memcpy(tiny->tipchars[distal->clv_index],
      old_partition->tipchars[old_distal->clv_index],
      sizeof(char) * old_partition->sites );
  else
    memcpy(tiny->clv[distal->clv_index],
      old_partition->clv[old_distal->clv_index],
      clv_size);

  // deep copy scalers
  if (old_proximal->scaler_index != PLL_SCALE_BUFFER_NONE)
    memcpy(tiny->scale_buffer[proximal->scaler_index],
      old_partition->scale_buffer[old_proximal->scaler_index],
      sizeof(unsigned int) * old_partition->sites);
  if (old_distal->scaler_index != PLL_SCALE_BUFFER_NONE)
    memcpy(tiny->scale_buffer[distal->scaler_index],
      old_partition->scale_buffer[old_distal->scaler_index],
      sizeof(unsigned int) * old_partition->sites);

  return tiny;
}

void destroy_tiny_partition(pll_partition_t * partition)
{
  // unset shallow copied things
  partition->rates = nullptr;
  partition->subst_params = nullptr;
  partition->frequencies = nullptr;
  partition->eigenvecs = nullptr;
  partition->inv_eigenvecs = nullptr;
  partition->eigenvals = nullptr;
  partition->prop_invar = nullptr;
  partition->eigen_decomp_valid = nullptr;
  partition->pattern_weights = nullptr;

  partition->lh_statepair = nullptr;
  partition->charmap = nullptr;
  partition->revmap = nullptr;

  pll_partition_destroy(partition);
}

pll_utree_t * make_tiny_tree_structure(const pll_utree_t * old_proximal, const pll_utree_t * old_distal,
  const bool tip_tip_case)
{
  const unsigned int inner_scaler_index = 1;
  const unsigned int proximal_scaler_index = 0;
  const unsigned int distal_scaler_index = 2;

  /**
    As we work with PLL_PATTERN_TIP functionality, special care has to be taken in regards to the tree and partition
    structure: PLL assumes that any node with clv index < number of tips is in fact a real tip, that is
    a tip that uses a character array instead of a real clv. Here we need to set up the tree/partition to fool pll:
    the tips that actually contain CLVs copied over from the reference tree have their index set to greater than
    number of tips. This results in a acceptable amount of wasted memory that is never used (num_sites * bytes
    * number of clv-tips)
  */
  // if tip-inner case
  unsigned int distal_clv_index = 5;
  const unsigned int proximal_clv_index = 4;
  const unsigned int inner_clv_index = 3;
  const unsigned int new_tip_clv_index = 1;

  if (tip_tip_case)
    distal_clv_index = 2;

  pll_utree_t * inner = (pll_utree_t *) calloc(1,sizeof(pll_utree_t));
  inner->next = (pll_utree_t *) calloc(1,sizeof(pll_utree_t));
  inner->next->next = (pll_utree_t *) calloc(1,sizeof(pll_utree_t));
  inner->next->next->next = inner;

  pll_utree_t * new_tip = (pll_utree_t *) calloc(1,sizeof(pll_utree_t));
  pll_utree_t * proximal = (pll_utree_t *) calloc(1,sizeof(pll_utree_t));
  pll_utree_t * distal = (pll_utree_t *) calloc(1,sizeof(pll_utree_t));

  // connect the nodes to each other
  inner->back = new_tip;
  new_tip->back = inner;
  inner->next->back = distal;
  distal->back = inner->next;
  inner->next->next->back = proximal;
  proximal->back = inner->next->next;

  // set up clv indices
  inner->clv_index = inner_clv_index;
  inner->next->clv_index = inner_clv_index;
  inner->next->next->clv_index = inner_clv_index;
  proximal->clv_index = proximal_clv_index;
  distal->clv_index = distal_clv_index;
  new_tip->clv_index = new_tip_clv_index;

  // set up scaler indices
  new_tip->scaler_index = PLL_SCALE_BUFFER_NONE;
  inner->scaler_index = inner_scaler_index;
  inner->next->scaler_index = inner_scaler_index;
  inner->next->next->scaler_index = inner_scaler_index;
  proximal->scaler_index = (old_proximal->scaler_index == PLL_SCALE_BUFFER_NONE) ?
    PLL_SCALE_BUFFER_NONE : proximal_scaler_index;
  distal->scaler_index = (old_distal->scaler_index == PLL_SCALE_BUFFER_NONE) ?
    PLL_SCALE_BUFFER_NONE : distal_scaler_index;


  reset_triplet_lengths(inner, nullptr, old_distal->length);

  return inner;
}