# `examples/regressions/cwd_build/cases/` — the case files `regressor.tkr` names with `Given source`

Every scenario in `regressor.tkr` that is NOT declarative names its snippet as a file here. The files
carry **no leading comment block**, and that is load-bearing, not an oversight: the runner
synthesizes a scratch project from a case file by splitting it into DECLARATIONS and STATEMENTS
(`split_snippet_decls_and_stmts`), and when a file declares nothing it is written to `main.tks`
verbatim — where a leading `/** … */` with no declaration after it is a parse error (C7.9), while a
leading `//` block is classified as a statement chunk and defeats the grouping split. Each case's
documentation therefore lives beside its `Scenario` in `regressor.tkr`, next to the `Given source`
line that names it.

They live under `cwd_build/` rather than in a directory of their own because
`cwd_build.tkp` declares `source = "src"`, so `cases/` is outside its build and invisible to it —
and because a bare directory under `examples/regressions/` with no `.tkp` would break every consumer
that walks that tree expecting one project per directory.
