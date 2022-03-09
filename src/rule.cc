// Copyright 2015 Google Inc. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// +build ignore

#include "rule.h"

#include "dep.h"
#include "eval.h"
#include "expr.h"
#include "fileutil.h"
#include "log.h"
#include "parser.h"
#include "stringprintf.h"
#include "strutil.h"
#include "symtab.h"

Rule::Rule() : is_double_colon(false), is_suffix_rule(false), parsed_inputs(false), cmd_lineno(0) {}

void Rule::ParseInputs(const StringPiece& inputs_str) {
  bool is_order_only = false;
  for (auto const& input : WordScanner(inputs_str)) {
    if (input == "|") {
      is_order_only = true;
      continue;
    }
    vector<string> input_files;
    if (!g_flags.generate_ninja) {
      // TODO: Support in ninja mode
      input_files = Glob(input.as_string().c_str());
    }
    if (input_files.size() == 0) {
      input_files.push_back(input.as_string());
    }
    for (string input_file : input_files) {
      Symbol input_sym = Intern(TrimLeadingCurdir(input_file));
      (is_order_only ? order_only_inputs : inputs).push_back(input_sym);
    }
  }
  parsed_inputs = true;
}

void Rule::ParseInputs(Evaluator* ev) {
  if (parsed_inputs) {
    return;
  }
  if (second_expansion) {
    Value* val = ParseExpr(&loc, prereq_string);
    prereq_string = val->Eval(ev);
  }
  ParseInputs(prereq_string);
}

void Rule::ParsePrerequisites(const StringPiece& line,
                              size_t separator_pos,
                              const RuleStmt* rule_stmt) {
  // line is either
  //    prerequisites [ ; command ]
  // or
  //    target-prerequisites : prereq-patterns [ ; command ]
  // First, separate command. At this point separator_pos should point to ';'
  // unless null.
  prereq_string = string(line.data(), line.size());
  if (separator_pos != string::npos &&
      rule_stmt->sep != RuleStmt::SEP_SEMICOLON) {
    CHECK(line[separator_pos] == ';');
    // TODO: Maybe better to avoid Intern here?
    cmds.push_back(Value::NewLiteral(
        Intern(TrimLeftSpace(line.substr(separator_pos + 1))).str()));
    prereq_string = prereq_string.substr(0, separator_pos);
  }

  if ((separator_pos = prereq_string.find(':')) == string::npos) {
    // Simple prerequisites
    return;
  }

  // Static pattern rule.
  if (!output_patterns.empty()) {
    ERROR_LOC(loc, "*** mixed implicit and normal rules: deprecated syntax");
  }

  // Empty static patterns should not produce rules, but need to eat the
  // commands So return a rule with no outputs nor output_patterns
  if (outputs.empty()) {
    return;
  }

  string target_prereq = prereq_string.substr(0, separator_pos);

  for (StringPiece target_pattern : WordScanner(target_prereq)) {
    target_pattern = TrimLeadingCurdir(target_pattern);
    for (Symbol target : outputs) {
      if (!Pattern(target_pattern).Match(target.str())) {
        WARN_LOC(loc, "target `%s' doesn't match the target pattern",
                 target.c_str());
      }
    }
    output_patterns.push_back(Intern(target_pattern));
  }

  prereq_string = prereq_string.substr(separator_pos + 1);

  if (output_patterns.empty()) {
    ERROR_LOC(loc, "*** missing target pattern.");
  }
  if (output_patterns.size() > 1) {
    ERROR_LOC(loc, "*** multiple target patterns.");
  }
  if (!IsPatternRule(output_patterns[0].str())) {
    ERROR_LOC(loc, "*** target pattern contains no '%%'.");
  }
}

shared_ptr<Rule> Rule::ApplyPattern(Pattern pat, Symbol output) const {
  shared_ptr<Rule> new_rule = make_shared<Rule>(*this);
  string buf;
  pat.AppendSubst(output.str(), prereq_string, &buf);
  new_rule->prereq_string = buf;
  new_rule->parsed_inputs = false;
  return new_rule;
}

string Rule::DebugString() const {
  vector<string> v;
  v.push_back(StringPrintf("outputs=[%s]", JoinSymbols(outputs, ",").c_str()));
  v.push_back(StringPrintf("inputs=[%s]", JoinSymbols(inputs, ",").c_str()));
  if (!order_only_inputs.empty()) {
    v.push_back(StringPrintf("order_only_inputs=[%s]",
                             JoinSymbols(order_only_inputs, ",").c_str()));
  }
  if (!output_patterns.empty()) {
    v.push_back(StringPrintf("output_patterns=[%s]",
                             JoinSymbols(output_patterns, ",").c_str()));
  }
  if (is_double_colon)
    v.push_back("is_double_colon");
  if (is_suffix_rule)
    v.push_back("is_suffix_rule");
  if (!cmds.empty()) {
    v.push_back(StringPrintf("cmds=[%s]", JoinValues(cmds, ",").c_str()));
  }
  return JoinStrings(v, " ");
}
