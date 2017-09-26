#pragma once

#include "seq/MSA_Stream.hpp"
#include "util/Options.hpp"
#include "tree/Tree.hpp"
#include "core/Model.hpp"

#include <string>

void pipeline_place(Tree& tree,
                    const std::string& query_file,
                    const std::string& outdir,
                    const Options& options,
                    const std::string& invocation);

void simple_mpi(Tree& tree,
                const std::string& query_file,
                const std::string& outdir,
                const Options& options,
                const std::string& invocation);
