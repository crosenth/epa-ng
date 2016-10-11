#include <gtest/gtest.h>
#include <string>
#include <iostream>

#include "Epatest.hpp"
#include "src/mpihead.hpp"
#include "src/Log.hpp"

Epatest* env;
Log lgr(false);

int main(int argc, char** argv)
{
  env = new Epatest();

  // Set data dir using the program path.
  std::string call = argv[0];
  std::size_t found = call.find_last_of("/\\");
  if (found != std::string::npos) {
      env->data_dir = call.substr(0,found) + "/../data/";
  }

  env->tree_file = std::string(env->data_dir);
  env->tree_file += "ref.tre";
  env->tree_file_rooted = std::string(env->data_dir);
  env->tree_file_rooted += "ref_rooted.tre";
  env->reference_file  = std::string(env->data_dir);
  env->reference_file += "aln.fasta";
  env->query_file  = std::string(env->data_dir);
  env->query_file += "query.fasta";
  env->combined_file  = std::string(env->data_dir);
  env->combined_file += "combined.fasta";
  env->out_dir  = std::string("/tmp/epatest/");
  std::string cmd("mkdir ");
  cmd += env->out_dir.c_str();
  system(cmd.c_str());
  env->binary_file = env->out_dir + "persisted.bin";

  ::testing::InitGoogleTest(&argc, argv);
  MPI_INIT(&argc, &argv);
  ::testing::AddGlobalTestEnvironment(env);
  auto result = RUN_ALL_TESTS();
  MPI_FINALIZE();
  return result;
}
