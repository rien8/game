# AGENTS.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## Non-negotiable Rules

These rules are non-negotiable. They override any workflow guidance below.

- Do not run `git commit`; wait for the user to decide when to commit.
- On Windows, invoke the terminal via `pwsh`.
- Never use `using namespace` in header files.
- Target C++23. Use its facilities directly; do not add compatibility shims for older standards.
- Reply to the user in Chinese.
- Place generated documentation under `doc/`; put temporary files under `agent-temp/`.
- Do not delete existing comments arbitrarily.
- Use `py -3` as python interpreter.

## C++ Standard

The project standardizes on **C++23**. Write new code directly in C++23 idioms; do not carry over the older forms it replaced:

- Error handling: `std::expected<T, E>`, not hand-rolled error codes paired with out-parameters.
- Formatted output: `std::print` / `std::println` (requires `<print>`), not hand-written `printf` or `std::cout << std::endl`.
- Container work: `std::ranges` algorithms and views (`views::filter`, `views::transform`), not hand-written loops plus `push_back`.
- Compile-time constraints: `if consteval`, `consteval`, `constexpr` instead of runtime checks backed by assert.
- Named tuples: structured bindings on `std::tuple` with `auto [a, b]`, not hand-written structs passed as arguments.
- Strings and Unicode: keep the encoding chain intact when using `std::u8string` / `std::char8_t`; don't let narrow strings convert implicitly.
- Use templates and concepts aggressively.

## Comments Rules

- Comments explain *why* (rationale, constraint, ownership, architectural pattern) — never *what*. Anything derivable from names/types belongs in code, not comments.
- Keep comments sparse and concise; omit comments for self-explanatory code.
- Preserve existing comments that already explain *why*; don't delete or rewrite them without cause.
- Comments keep concise and accurate.

## Before Coding

- State assumptions explicitly; ask when uncertain.
- When multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop and name what's confusing.

## Implementation

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code; no unrequested flexibility or configurability.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## Code Style

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- comments use doxygen '///' style
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.
- When there are tasks involving refactoring or new features, avoid overdesigning unless necessary, and strive for simplicity in the solution
- Abandon the idea of design patterns
- Don't use `shared_ptr` or `unique_ptr`.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## Line Breaking Rules

- No line may exceed 90 characters. Break it yourself; don't let IDE soft-wrap hide the overflow.
- Long function calls or argument lists: break after a comma or an opening parenthesis, then align continuation lines with the parameters or indent them uniformly by 4 spaces.
- Long conditions in if/while/for: break at operators such as `&&` and `||`, putting each operand on its own line.
- Chained calls: break after each `->` and `.()`, instead of packing a long chain onto one line.

## Verification

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
