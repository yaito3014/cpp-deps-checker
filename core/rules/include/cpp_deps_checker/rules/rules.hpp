#ifndef CPP_DEPS_CHECKER_RULES_RULES_HPP
#define CPP_DEPS_CHECKER_RULES_RULES_HPP

#include <cpp_deps_checker/config/config.hpp>
#include <cpp_deps_checker/model/model.hpp>
#include <cpp_deps_checker/ownership/ownership.hpp>

#include <compare>
#include <expected>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace cpp_deps_checker::rules {

// An include directive: the file holding it, the file it includes, and its
// line, if known. Paths are absolute and canonical.
struct include {
  std::filesystem::path includer;
  std::filesystem::path included;
  std::optional<int> line;

  friend bool operator==(include const&, include const&) = default;
  friend auto operator<=>(include const&, include const&) = default;
};

struct diagnostic {
  std::filesystem::path file;  // absolute and canonical
  int line = 0;
  std::string message;

  friend bool operator==(diagnostic const&, diagnostic const&) = default;
};

// A translation unit that did not preprocess.
struct failure {
  std::filesystem::path source;     // absolute and canonical
  std::optional<diagnostic> error;  // the first error the compiler reported
  std::string output;               // everything the compiler wrote

  friend bool operator==(failure const&, failure const&) = default;
};

enum class level { error, warning };

[[nodiscard]] std::string_view to_string(level value) noexcept;

struct violation {
  std::string rule;
  rules::level level = level::error;
  std::string message;
  std::optional<model::location> location;

  friend bool operator==(violation const&, violation const&) = default;
};

// An exception from the config: the targets it names, as ids, in the order of
// its rule's keys (from and to, the target, or a cycle's members).
struct exception {
  std::string rule;
  std::vector<std::string> targets;

  friend bool operator==(exception const&, exception const&) = default;
};

// Every rule's input.
struct context {
  model::project project;
  // Only those whose includer has an owner; unset when they were not collected,
  // so rules 3, 3a, and 4 did not run.
  std::optional<std::vector<include>> includes;
  // Only header set verification translation units; any other failure makes
  // the input unusable.
  std::vector<failure> failures;
  ownership::owners owners;
  std::set<std::string> tests;     // ids of test targets
  std::set<std::string> external;  // ids of targets whose code is not the project's
  // Ids of interface libraries that own no header and only link other targets;
  // for rule 3, linking one counts as linking what it links.
  std::set<std::string> umbrellas;
  // Ids; unset, every production target that no production target links is one.
  std::optional<std::vector<std::string>> composition_roots;
  std::vector<exception> exceptions;

  // Targets the privacy and placement rules apply to, in model order: not
  // external, not tests, and not header set verification targets.
  [[nodiscard]] std::vector<model::target const*> production() const;
};

// A context with no includes or failures, classified with the default test
// patterns.
[[nodiscard]] context make_context(model::project project);

// A context with no includes or failures, configured by settings, with the
// defaults for what it leaves unset. A composition root that names no
// production target is an error.
[[nodiscard]] std::expected<context, std::string> make_context(model::project project, config::config const& settings);

// Every rule's violations, rule by rule.
[[nodiscard]] std::vector<violation> check(context const& ctx);

// Rules 3 and 4 for one include, as the clang-tidy plugin checks it; ctx's own
// includes are not used.
[[nodiscard]] std::vector<violation> check_include(context const& ctx, include const& directive);

}  // namespace cpp_deps_checker::rules

#endif
