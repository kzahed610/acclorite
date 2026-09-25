#pragma once

#include "acclorite/core/actionable.hpp"

namespace acclorite {

// Merge independently verified grammar while preserving provider priority.
// Existing facts are authoritative when equally strong; lower-priority facts
// may add non-overlapping syntax but never overwrite a conflicting proven fact.
void merge_command_grammar(CommandGrammar& target, const CommandGrammar& incoming);
void merge_subcommand_grammar(SubcommandSpec& target, const SubcommandSpec& incoming);

} // namespace acclorite
