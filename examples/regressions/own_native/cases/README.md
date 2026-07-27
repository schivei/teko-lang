# `examples/regressions/own_native/cases/` — the cross-target case files

`own_native.tkp` declares `source = "src"`, so `cases/` is outside this project's build and invisible
to it. The three cross-target scenarios of `own_native.tkr` name a file here with `Given source`
instead of running off the project build, because each is the ONLY row of its target: a project build
for a target nothing else uses buys nothing, and a one-line program is the honest minimum for "the
emitter produces a well-formed object for that target".

The files carry **no leading comment block**, and that is load-bearing, not an oversight: the runner
synthesizes a scratch project from a case file by splitting it into DECLARATIONS and STATEMENTS
(`split_snippet_decls_and_stmts`), and a file that declares nothing is written to `main.tks`
verbatim — where a leading `/** … */` with no declaration after it is a parse error (C7.9). Each
case's documentation therefore lives beside its `Scenario` in `own_native.tkr`, next to the
`Given source` line that names it.
