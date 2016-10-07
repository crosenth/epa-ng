#pragma once

#include <vector>
#include <unordered_map>

void to_difficulty(std::vector<double>& perstage_avg);
std::vector<unsigned int> solve(unsigned int stages, unsigned int nodes, 
            std::vector<double> difficulty_per_stage);
void assign(std::vector<unsigned int>& nodes_per_stage, 
            std::unordered_map<int, std::unordered_map<int, int>>& rank_assignm,
            int* local_stage);