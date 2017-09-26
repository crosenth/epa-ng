#include "util/stringify.hpp"

std::string stringify(Model& model)
{
  std::ostringstream output;

  output << "Substitution Matrix Symmetries: " << NEWL;
  output << stringify(model.symmetries()) << NEWL;

  output << "Base Frequencies: " << NEWL;
  output << stringify(model.base_frequencies()) << NEWL;

  output << "Substitution Rates: " << NEWL;
  output << stringify(model.substitution_rates()) << NEWL;

  output << "Alpha: " << model.alpha() << NEWL;

  return output.str();
}