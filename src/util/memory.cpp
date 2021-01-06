#include "util/memory.hpp"

#include "genesis/utils/io/input_stream.hpp"

#include <iomanip>
#include <mutex>

#include "util/maps.hpp"
#include "core/pll/pllhead.hpp"
#include "core/pll/epa_pll_util.hpp"
#include "core/raxml/Model.hpp"
#include "core/Work.hpp"
#include "seq/MSA_Info.hpp"
#include "sample/Placement.hpp"
#include "util/Options.hpp"
#include "util/logging.hpp"

static constexpr char SPACER[] = "  \t";

static size_t lookuptable_footprint( size_t const branches,
                                     size_t const states,
                                     size_t const sites )
{
  size_t size = 0;

  // char_to_posish_ size is static:
  size += 128 * sizeof( size_t );

  // class currently saves mutexes to handle parallel access:
  size += branches * sizeof( std::mutex );

  // the actual lookuptable is, per branch, a matrix of all possible per site likelihoods (pslh) for a given number of states
  // however the number of states includes ambiguity chars:
  auto const effective_states = ( states == 4 ) ? NT_MAP_SIZE : AA_MAP_SIZE;
  auto const pslh_vector      = sites * sizeof( double );

  size += branches * pslh_vector * effective_states;

  return size;
}

static unsigned int simd_autodetect()
{
  if( PLL_STAT( avx2_present ) )
    return PLL_ATTRIB_ARCH_AVX2;
  else if( PLL_STAT( avx_present ) )
    return PLL_ATTRIB_ARCH_AVX;
  else if( PLL_STAT( sse3_present ) )
    return PLL_ATTRIB_ARCH_SSE;
  else
    return PLL_ATTRIB_ARCH_CPU;
}

struct partition_breakdown {
  size_t total      = 0ul;
  size_t clv        = 0ul;
  size_t clv_buffer = 0ul;
};

static partition_breakdown partition_footprint( raxml::Model const& model,
                                                Tree_Numbers const& nums,
                                                int const num_sites )
{
  partition_breakdown pb;
  size_t size = 0;

  /*
		We make a fake partition, such that we can leave it to pll to figure out cruicial size values like
		states_padded, which we then use in our own calculation.
		Purposefully reusing the function that is used in the Tree constructor here.
	 */

  Tree_Numbers fake_nums;
  fake_nums.tip_nodes   = 1;
  fake_nums.inner_nodes = 1;
  fake_nums.branches    = 1;

  auto attributes = simd_autodetect();
  // TODO this should behave more like the actual partition, i.e. figure out
  // when and when not we use tipchars
  attributes |= PLL_ATTRIB_PATTERN_TIP;
  auto partition
      = pll_partition_create( fake_nums.tip_nodes,
                              fake_nums.inner_nodes,
                              model.num_states(),
                              num_sites,
                              1,
                              fake_nums.branches,
                              model.num_ratecats(),
                              ( fake_nums.inner_nodes ) + fake_nums.tip_nodes,
                              attributes );

  size_t const sites_alloc = partition->asc_additional_sites + partition->sites;

  // eigendecomposition valid
  size += partition->rate_matrices * sizeof( int );

  // number of clv's depends on wether we use tipchars
  size_t num_clvs = nums.inner_nodes * 3;
  if( not( partition->attributes & PLL_ATTRIB_PATTERN_TIP ) ) {
    num_clvs += nums.tip_nodes;
  } else {
    /* reference "create_charmap" in pll.c for this part */
    // account for the charmap
    size += PLL_ASCII_SIZE * sizeof( unsigned char );
    // account fort tipmap
    size += PLL_ASCII_SIZE * sizeof( pll_state_t );

    // ttlookup
    size += size_of_ttlookup( partition ) * sizeof( double );

    // account for the tipchars
    size_t const tipchars_buffer = nums.tip_nodes
            * sites_alloc
            * sizeof( unsigned char )
        + nums.tip_nodes * sizeof( unsigned char* ); // account for top level array
    size += tipchars_buffer;

    LOG_DBG << "\t\t" << format_byte_num( tipchars_buffer ) << SPACER
            << "tipchars array";
  }

  size_t const per_clv = sites_alloc * partition->states_padded
      * partition->rate_cats * sizeof( double ) 
      + sizeof( double* ); // account for top level array

  pb.clv = per_clv;

  size_t const clv_buffer = num_clvs * per_clv;
  pb.clv_buffer = clv_buffer;

  // hypothetical size with log(n) optimization
  size_t const log_clv_buffer
      = ceil( log2( nums.tip_nodes ) + 2 ) * per_clv; // account for top level array


  LOG_DBG << "\t\t" << format_byte_num( clv_buffer ) << SPACER << "CLV Buffer"
          << " (with log(n) opt: " << format_byte_num( log_clv_buffer ) << ")";

  size += clv_buffer;

  // p matrices, they are allocated in a memory saving way (consult pll.c)
  size_t const displacement = ( partition->states_padded - partition->states )
      * ( partition->states_padded )
      * sizeof( double );

  size_t const pmat_buffer = partition->prob_matrices
          * partition->states
          * partition->states_padded
          * partition->rate_cats
          * sizeof( double )
      + displacement
      + partition->prob_matrices * sizeof( double* ); // account for top level array
  size += pmat_buffer;

  // eigenvectors
  size_t const eigen_buffer = partition->rate_matrices
          * partition->states
          * partition->states_padded
          * sizeof( double )
      + partition->rate_matrices * sizeof( double* ); // account for top level array
  size += eigen_buffer;

  // invariant eigenvecs
  size_t const inv_eigen_buffer = partition->rate_matrices
          * partition->states
          * partition->states_padded
          * sizeof( double )
      + partition->rate_matrices * sizeof( double* ); // account for top level array
  size += inv_eigen_buffer;

  // eigenvalues
  size_t const eigenval_buffer = partition->rate_matrices
          * partition->states_padded
          * sizeof( double )
      + partition->rate_matrices * sizeof( double* ); // account for top level array
  size += eigenval_buffer;

  // substitution parameters
  size_t const sub_param_buffer = partition->rate_matrices
          * ( ( partition->states * partition->states - partition->states ) / 2 ) // :thinking:
          * sizeof( double )
      + partition->rate_matrices * sizeof( double* ); // account for top level array
  size += sub_param_buffer;

  // frequencies
  size_t const freq_buffer = partition->rate_matrices
          * partition->states_padded
          * sizeof( double )
      + partition->rate_matrices * sizeof( double* ); // account for top level array
  size += freq_buffer;

  // rates
  size += partition->rate_cats * sizeof( double );
  // rate weights
  size += partition->rate_cats * sizeof( double );
  // proportion of invariant sites
  size += partition->rate_matrices * sizeof( double );
  // site weights
  size += sites_alloc * sizeof( unsigned int );

  // scale buffers
  size_t const scaler_size = ( partition->attributes & PLL_ATTRIB_RATE_SCALERS )
      ? sites_alloc * partition->rate_cats
      : sites_alloc;

  size_t const scaler_buffer
      = partition->scale_buffers * scaler_size * sizeof( unsigned int )
      + partition->scale_buffers
          * sizeof( unsigned int* ); // account for top level array
  size += scaler_buffer;

  LOG_DBG << "\t\t" << format_byte_num( scaler_buffer ) << SPACER
            << "scalers array";

  pll_partition_destroy( partition );

  pb.total = size;
  return pb;
}

static size_t msa_footprint( MSA_Info const& info, Options const& options )
{
  size_t size = 0;

  auto const sites = options.premasking ? info.nongap_count() : info.sites();

  // the sequences themselves
  size += info.sequences() * sites * sizeof(char);

  // some guess about the average size of sequence labels
  size += info.sequences() * 50 * sizeof(char);

  return size;
}

static size_t sample_footprint( size_t const chunk_size,
                                size_t const num_branches,
                                bool const slim )
{
  size_t size = 0;

  size_t const placement_size = slim ? sizeof(Preplacement) : sizeof(Placement);

  // the sequences themselves
  size += chunk_size * num_branches * placement_size;

  return size;
}

static size_t all_work_footprint( Tree_Numbers const& nums,
                                  MSA_Info const& qry_info,
                                  Options const& options )
{
  // the intermediate all-work object only gets created if prescoring is disabled
  if( options.prescoring ) {
    return 0;
  } else {
    return nums.branches * sizeof( Work::key_type )
        * std::min( qry_info.sequences(), options.chunk_size )
        * sizeof( Work::value_type );
  }
}

Memory_Footprint::Memory_Footprint( MSA_Info const& ref_info,
                                    MSA_Info const& qry_info,
                                    raxml::Model const& model,
                                    Options const& options )
{
  if( options.repeats ) {
    LOG_ERR << "Cannot accurately calculate memory footprint when using "
               "siterepeats! Aborting.";
    std::exit( EXIT_FAILURE );
  }

  auto const tree_nums = Tree_Numbers( ref_info.sequences() );

  // figure out the true size of the ref alignment
  assert( ref_info.sites() == qry_info.sites() );
  assert( ref_info.gap_mask().size() == qry_info.gap_mask().size() );
  auto const num_sites
      = options.premasking ? ref_info.nongap_count() : ref_info.sites();

  LOG_DBG << "Memory footprint breakdown:";

  auto pb    = partition_footprint( model, tree_nums, num_sites );
  partition_ = pb.total;
  perclv_    = pb.clv;
  clvbuffer_ = pb.clv_buffer;
  maxnumclv_ = tree_nums.inner_nodes * 3
      + ( options.repeats ? tree_nums.tip_nodes : 0 );
  logn_ = ceil( log2( tree_nums.tip_nodes ) + 2 );
  LOG_DBG << "\t" << format_byte_num( partition_ ) << SPACER
          << "Partition Total";

  if( options.prescoring ) {
    lookup_ = lookuptable_footprint(
        tree_nums.branches, model.num_states(), num_sites );
    LOG_DBG << "\t" << format_byte_num( lookup_ ) << SPACER
            << "Preplacement Lookup";

    presample_ = sample_footprint(
        std::min( options.chunk_size, qry_info.sequences() ),
        tree_nums.branches,
        true );
    LOG_DBG << "\t" << format_byte_num( presample_ ) << SPACER
            << "Preplacement Sample";
  }

  refmsa_ = msa_footprint( ref_info, options );
  LOG_DBG << "\t" << format_byte_num( refmsa_ ) << SPACER << "Reference MSA";

  // account for the overhead induced by the tinytree-encapsulated partitions,
  // which are one per thread

  // size of the fasta input stream buffers
  // currently only one: the query MSA_Stream
  qsistream_ = genesis::utils::InputStream::BlockLength * 3;
  LOG_DBG << "\t" << format_byte_num( qsistream_ ) << SPACER
          << "Query MSA Inputstream";

  allwork_ = all_work_footprint( tree_nums, qry_info, options );
  if( allwork_ ) {
    LOG_DBG << "\t" << format_byte_num( allwork_ ) << SPACER
            << "all-work object";
  }

  LOG_INFO << "Estimated memory footprint: "
         << format_byte_num( total() );

  LOG_INFO << "Total available memory: " << format_byte_num( get_max_memory() );
}

Memory_Config::Memory_Config( Memsave_Option const& memsave_opt,
                              Memory_Footprint const& footprint,
                              pll_utree_t* tree )
{
  //
  // figure out what the mode means for the config
  //
  if( footprint ) {
    switch( memsave_opt.mode ) {
    case Memsave_Option::Mode::kCustom:
      std::runtime_error { "Custom memsave mode not implemented yet." };
      break;
    case Memsave_Option::Mode::kOff:
      break;
    case Memsave_Option::Mode::kAuto:
      // only create a valid memory config if we absolutely need it
      if( footprint.total() > memsave_opt.memory_constraint * 0.95 ) {
        init( memsave_opt.memory_constraint, footprint, tree );
      }
      break;
    case Memsave_Option::Mode::kFull:
      init( footprint.minimum(), footprint, tree );
      break;
    default:
      std::runtime_error { "Wrong mode!" };
    }
  }
}

void Memory_Config::init( size_t const constraint,
                          Memory_Footprint const& footprint,
                          pll_utree_t* tree )
{
  //
  // figure out the budget by setting the respective config values
  //
  auto const maxmem = get_max_memory();

  if( constraint > maxmem ) {
    LOG_WARN << "Specified memory limit of " << format_byte_num( constraint )
             << " exceeds the determined system wide maximum of "
             << format_byte_num( maxmem )
             << ". Continuing with the specified limit!";
  }

  auto const minmem = footprint.minimum();
  if( constraint < minmem ) {
    LOG_ERR << "Specified memory limit of " << format_byte_num( constraint )
            << " is below the minimum required value (for this input) of "
            << format_byte_num( minmem ) << ". Aborting!";
    std::exit( EXIT_FAILURE );
  }

  LOG_DBG << "Limiting the memory footprint to "
          << format_byte_num( constraint );

  // how much above the minimum can we play with?
  // (this includes the logn + 2 clv slots)
  auto budget = constraint - minmem;

  LOG_DBG1 << "Minimum possible RSS: "
          << format_byte_num( minmem );

  LOG_DBG1 << "RSS budget: "
          << format_byte_num( budget );

  // if we can afford it, use the preplacement lookup
  if( footprint.lookup() and ( footprint.lookup() < budget ) ) {
    budget -= footprint.lookup();
    preplace_lookup_enabled = true;
    LOG_DBG1 << "To lookuptable: "
        << format_byte_num( footprint.lookup() );
  } else {
    LOG_DBG1 << "Cannot afford lookuptable.";
    preplace_lookup_enabled = false;
  }

  auto const per_clv = footprint.clv();

  // figure out how many more clv slots we can afford
  size_t extra_clv_slots = floor( static_cast< double >( budget ) / per_clv );

  // but have no more than the theoretical maximum
  clv_slots = std::min( footprint.logn_clvs() + extra_clv_slots,
                        footprint.maximum_required_clvs() );

  LOG_DBG1 << "Will allocate clv slots: "
        << clv_slots << " / " << footprint.maximum_required_clvs()
        << " (" << format_byte_num( clv_slots * per_clv ) << ")";

  //
  // allocate the needed structures, if we do need them for the memsave mode
  //
  structs = Logn_Structures( tree );
}

std::string format_byte_num( double size )
{
  constexpr std::array< char const*, 6 > magnitude = { { "", "KiB", "MiB", "GiB", "TiB", "PiB" } };

  size_t lvl = 0;
  while( size > 1024 ) {
    size /= 1024.0;
    lvl++;
  }

  std::ostringstream ret;

  ret << std::setprecision( 1 ) << std::fixed;
  ret << size;
  ret << magnitude[ lvl ];

  return ret.str();
}

std::string format_byte_num( size_t size )
{
  return format_byte_num( static_cast< double >( size ) );
}

size_t slurm_memstring_to_bytes( std::string memstr )
{
  assert( not memstr.empty() );
  auto length       = memstr.size() - 1; // length without suffix
  char const suffix = toupper( memstr.at( length ) );

  size_t mult = 1;
  switch( suffix ) {
  case 'K':
    mult = 1024;
    break;
  case 'M':
    mult = std::pow( 1024, 2 );
    break;
  case 'G':
    mult = std::pow( 1024, 3 );
    break;
  case 'T':
    mult = std::pow( 1024, 4 );
    break;
  default:
    // no good prefix: assume default: M
    mult = std::pow( 1024, 2 );
    // but also adjust the length
    length = memstr.size();
  }

  return std::stod( memstr.substr( 0, length ) ) * mult;
}

size_t memstring_to_byte( std::string s )
{
  return slurm_memstring_to_bytes( s );
}

#include "util/get_memory_size.hpp"
#include <cstdlib>
size_t get_max_memory()
{
  size_t maxmem = 0ul;

  // first check the total available memory
  maxmem = getMemorySize();

  // next, try to identify extra limitations

  // SLURM might have set some maximum per process, useful for not exceeding
  // total node memory when there are multiple processes per node
  if( char const* slurm_mem = std::getenv( "SLURM_MEM_PER_NODE" ) ) {
    size_t const slurm_maxbytes = slurm_memstring_to_bytes( slurm_mem );
    maxmem                      = std::min( slurm_maxbytes, maxmem );
  }

  return maxmem;
}
