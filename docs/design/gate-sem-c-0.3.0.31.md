# O gate de teste sem C — mapa da emissão e plano de porte (0.3.0.31)

> *"vi que o self-test emitiu arquivo C, para testes, mas emitiu. O que eu disse foi ZERO C, isso
> inclui os testes."* — owner, 2026-07-26

O decreto de ZERO C não abre exceção para o harness de teste. Emitir C "só para testar" continua
sendo emitir C. Este documento mapeia **exatamente onde** o caminho de teste emite C hoje, o que
cada sítio produz, e o plano de porte para o caminho nativo.

O linker **fica** (ruling do owner: *"Isso não inclui o linker, pois ainda temos libs a debater até
lá"*). `run_cc`/`build_cc_argv` sobrevivem para LINKAR; o que precisa sumir é a **emissão e
compilação de C**.

---

## 1. O mapa — quem emite C no caminho de teste

### 1.1 Os quatro sítios de escrita de `.c`

Todos em `src/build/project.tks`. Cada um segue o mesmo molde de cinco passos:
emitir texto C → concatenar `tk_emit_meta` → `teko::io::write_file(<stem>.c)` → `run_cc` → `run`.

| # | Função | Linhas | Arquivo escrito | Emissor | Quando dispara |
|---|--------|--------|-----------------|---------|----------------|
| 1 | `run_native_gate` | 2698–2731 (34) | `bin/<name>-tktest.c` | `codegen::tk_emit_c_test(prog, true)` | **SEMPRE** — `teko test .` (via `test_project`:4422) e `teko build` (via `run_gate_native`:2777) |
| 2 | `build_regression_cov_exe` | 4353–4365 (13) | `bin/<name>-regrcov.c` | `codegen::tk_emit_c_cov(prog)` | `teko test .` quando o manifesto tem `[tests] regression` **e** `name == "teko"` — ou seja, no self-test |
| 3 | `run_one_test_cov` | 3753–3772 (20) | `<od>/<stem>.c` (um por `#test`) | `codegen::tk_emit_c_test(filtered, true)` | só sob `--per-test-cov` |
| 4 | `run_analyzer` | 3591–3608 (18) | `bin/<name>-tkanalyze.c` | `codegen::tk_emit_c_test_analyze(prog)` | só sob `--analyzer` |

Os sítios 1 e 2 são os que disparam num `teko test .` seco. Os sítios 3 e 4 são dev-time,
sob flag.

### 1.2 Um quinto `.c`, transitório, no caminho do LINKER

`cc_family_is_clang` (`project.tks`:556–571) escreve `<binary>.ccprobe.c` com
`int main(void){return 0;}`, roda `cc -fsyntax-only` nele e o **remove**. É chamado de
`build_cc_argv`:903 — portanto do `link_object` também, isto é, do caminho que o ruling
PRESERVA. Ele não sobrevive à checagem "nenhum `.c` no diretório ao fim da suíte" porque já se
apagou, mas ele existe: é detecção de família de compilador, não emissão de programa.
Declarado aqui para não voltar em silêncio.

Os `.c` VERSIONADOS que entram na linha de link (`src/runtime/teko_rt.c`, `src/assert/assert.c`,
`build_cc_argv`:923–924) não são emitidos por ninguém — são o runtime semente, e só saem quando o
runtime em Teko existir.

### 1.3 A superfície de emissão em `codegen.tks`

`src/codegen/codegen.tks` tem 10 727 linhas. As entradas que o caminho de teste usa:

| Entrada | Linha | Modo | Papel |
|---------|-------|------|-------|
| `tk_emit_c_test(prog, cov)` | 10210 | `TestCov`/`TestPlain` | TU cujo `main()` chama cada `#test` |
| `tk_emit_c_test_analyze(prog)` | 10238 | `TestAnalyze` | idem, isolando cobertura por teste |
| `tk_emit_c_cov(prog)` | 10229 | `ProgramCov` | o CLI real, instrumentado |
| `tk_emit_c_mode(prog, mode)` | 10246 | — | o corpo comum: tipos, protótipos, funções, `main` |
| `emit_mode_main` | 10394 | — | o ÚNICO ponto que difere por modo |
| `emit_test_main` | 10504 | — | prólogo de cobertura + uma chamada por `#test` |
| `emit_test_call` | 10593 | — | `arena_push` · `print(label)` · `flush` · `cov_enter` · `<test>()` · `cov_leave` · `println("ok")` · `arena_pop` |
| `emit_test_main_analyze` | 10550 | — | idem, com reset+dump por teste |
| `emit_function_mode` / `emit_function_cov` | 10676 / 8165 | — | prólogo `tk_cov_mark(idx)` em cada fn de produção |
| `tk_emit_meta` | 297 | — | metadados (`@(#)` + Info.plist) concatenados ao TU |

O corpo de `emit_test_main` é **pequeno** (30 linhas) e o de `emit_test_call` menor ainda (18). O
que é grande — as 10 mil linhas — é o corpo compartilhado `tk_emit_c_mode`, que o caminho nativo
já substitui inteiro por `teko::lir::lower_program` + `teko::backend::*`.

### 1.4 O que o binário de gate faz hoje (contrato a preservar)

Ordem exata, de `emit_test_main` + `emit_test_call`:

```
main(argc, argv):
    tk_set_args(argc, argv)
    tk_cov_reset(); tk_cov_branch_reset(); tk_cov_branches_on(true)
    tk_cov_line_reset(); tk_cov_lines_on(true)
    para cada #test em ordem de prog.items, com idx = seu índice:
        tk_arena_push()
        tk_print("test <ns::name> ... "); tk_flush_out()
        tk_cov_enter(idx); <símbolo do teste>(); tk_cov_leave()
        tk_println("ok")
        tk_arena_pop()
    tk_cov_branches_on(false); tk_cov_lines_on(false)
    tk_cov_dump(getenv("TEKO_TKCOV"))
    return 0
```

Um assert que falha faz `panic` → saída não-zero → fail-fast (M.1). O pai (`run_native_gate`)
define `TEKO_TKCOV`, roda o filho e faz `coverage::cov_merge(covfile)`.

Além disso, cada função de PRODUÇÃO carrega, sob `TestCov`:
`tk_cov_mark(idx)` na entrada (`emit_function_cov`:8165) e marcas interiores
`tk_cov_line_at(fn, line)` / `tk_cov_branch_at(fn, line, col, outcome)`. São essas marcas que
populam as três métricas que `coverage::{coverage,line_coverage,branch_coverage}_pct` leem.

---

## 2. O que o caminho nativo já tem — e o que lhe falta

### 2.1 A favor

- **A ponte de `extern fn` mira o símbolo C real** (`afdb1fd8`): `call_symbol` →
  `find_extern_symbol` → símbolo declarado; `collect_undefined_x86` deriva o `SHN_UNDEF`; o
  `R_X86_64_PLT32` já é emitido. Chamar `tk_*` do runtime a partir de código nativo funciona.
- **Os sinks de cobertura já são builtins do checker** (`src/checker/scope.tks`:496–536):
  `cov_reset`, `cov_mark`, `cov_branches_on`, `cov_branch_reset`, `cov_enter`, `cov_leave`,
  `cov_branch`, `cov_lines_on`, `cov_line_reset`, `cov_line`, e `arena_push`/`arena_pop`
  (:507–508). Um gate sintetizado em AST tipada pode chamá-los **sem inventar `extern fn`**.
- `teko::coverage::cov_merge` já é `exp extern fn ... = "tk_cov_merge"` — o lado do PAI está pronto.
- `finish_native_object` já escreve `.o` + linka + `.tsym`; o gate reusa isso inteiro.

### 2.2 Contra — os buracos nomeados

**(a) O LIR descarta os `#test`.** `lower_item_function` (`src/lir/lower.tks`:5655):
```
if f.is_test { return LowerItemOut { module = m; loose = loose; lifted = lifted } }
```
Nenhum corpo de teste é lowerado hoje. **Consequência**: nunca se exercitou o backend próprio
sobre os corpos de teste — é risco não medido, e é o primeiro a medir.

**(b) Não há `main` de gate.** `lower_virtual_main` (:5684) sintetiza `main` a partir das
statements soltas. O gate precisa de um `main` que ignore as soltas e chame os testes.

**(c) `call_symbol` (`lower.tks`:1088) só conhece 8 builtins**: `exit`, `panic` e o grupo de io.
Qualquer outro builtin bare honest-stopa com
`"native backend N1: builtin `X` not yet lowered (N2)"`. Os `cov_*` e `arena_*` caem aí.

**(d) Não existe builtin `flush_out`.** `tk_flush_out` existe no runtime (`teko_rt.h`:183), mas
nenhum builtin do checker o expõe. Sem ele o rótulo `test X ... ` fica no buffer quando o teste
aborta — perda de diagnóstico, não de veredito.

**(e) `tk_cov_dump(const char *path)` não tem forma chamável de Teko.** É a única primitiva do
contrato do gate cujo parâmetro é `char*` e não `tk_str`. `tk_cov_merge` recebe `tk_str`
(`teko_rt.h`:524), o irmão `tk_cov_dump` não (:523). Declarar `extern fn dump(p: str) =
"tk_cov_dump"` seria trocadilho de ABI (passar `{ptr,len}` onde se lê `char*`, dependendo de o
`ptr` estar NUL-terminado) — desonesto sob M.3. **Resolver isso exige superfície nova em C**
(um `void tk_cov_dump_s(tk_str)`), o que é decisão do owner. Ver §4.

**(f) O `main` nativo nunca chama `tk_set_args`.** `lower_virtual_main` cria
`new_func("main", 0, [], I32)` — zero parâmetros. Não há uma única ocorrência de `argc`/`argv` em
`src/lir/` ou `src/backend/`. Portanto **todo binário do backend próprio vê `teko::env::args()`
vazio**. Para o binário de gate isso é tolerável (ele não lê argv); para o compilador
auto-hospedado nativo, não é — mas isso é do vagão do backend, não deste porte.

**(g) Não há instrumentação de cobertura no caminho nativo.** Nenhum `cov_mark` de entrada,
nenhuma marca de linha/branch. Sem isso as três métricas ficam em zero, e a política de piso
(hoje AVISO por padrão, `--cov-validation` para falhar) passa a avisar sobre um zero falso — o
que é pior que C sobreviver mais um dia, porque cega a cobertura em vez do gate.

---

## 3. O plano — decomposição em fatias

A ordem é obrigatória: **o caminho novo funciona antes de o velho sair**. Nenhuma fatia deleta
emissão de C; a deleção é o vagão seguinte.

- **F0 — o mapa** (este documento).

- **F1 — medir o backend próprio sobre os corpos de teste.** Lowerar os `#test` como funções
  comuns (um modo em `lower_program`) e emitir o `.o`, sem gate ainda. Entrega: a lista de
  honest-stops que os 1042 corpos de teste provocam no backend próprio. É a medida que decide se
  F2 é uma fatia ou um vagão.

- **F2 — o `main` de gate nativo.** `lower_gate_program(prog, gate_stmts)`: modo em que
  `lower_item_function` NÃO descarta `is_test`, as statements soltas são descartadas, e o
  virtual-main vem das statements sintetizadas. Um novo módulo sintetiza o corpo do gate em AST
  TIPADA (`checker::TExprStmt` sobre `TCall` para os builtins `arena_push`/`print`/`cov_enter`/
  `<teste>`/`cov_leave`/`println`/`arena_pop`). Índices de `prog.items` preservados: o transform
  não insere nem remove itens, então o `idx` de cobertura continua o mesmo do pai.

- **F3 — os builtins que faltam no LIR.** Estender `call_symbol` com a tabela
  `cov_* → tk_cov_*`, `arena_push/pop/commit → tk_arena_*`, e adicionar o builtin `flush_out`
  (checker + LIR + codegen C, para paridade). Sem C novo: todos os símbolos já existem em
  `teko_rt.h`.

- **F4 — veredito sem cobertura.** `run_native_gate` passa a emitir `.o` + `link_object` em vez
  de `.c` + `run_cc`. Contagem de testes e exit code idênticos. A cobertura ainda vem do caminho
  antigo — **esta fatia não pode ser a última**, sob pena de cegar a cobertura.

- **F5 — instrumentação de cobertura nativa.** Marca de entrada por função de produção
  (`cov_mark(idx)`) e marcas de linha/branch. O caminho barato é um transform de AST tipada
  (prepend de `TExprStmt`), o caro é instrumentar no LIR. Depende de (e) para o dump.

- **F6 — a prova automática de zero `.c`.** Um passo do próprio gate que varre o diretório de
  trabalho depois da suíte e FALHA se qualquer `.c` não-versionado apareceu. Escrito como parte do
  gate, não conferência manual — senão o C volta em silêncio.

- **F7 — os sítios sob flag.** `--analyzer`, `--per-test-cov` e o `regrcov`.

---

## 4. O ponto de decisão do owner

**(e) é um bloqueio real, não um detalhe.** O contrato do gate termina em
`tk_cov_dump(getenv("TEKO_TKCOV"))`; sem uma forma honesta de chamá-lo de Teko, o gate nativo não
consegue devolver a cobertura ao pai. As saídas possíveis:

1. **Superfície nova em C** — `void tk_cov_dump_s(tk_str path)` ao lado do `tk_cov_dump(const
   char*)`, duas linhas em `teko_rt.{c,h}`. Simétrico ao `tk_cov_merge(tk_str)` que já existe. É
   C NOVO, e a carga proíbe C novo sem decisão do owner.
2. **Um builtin `cov_dump(path: str)`** no checker que o codegen/LIR baixa para
   `tk_cov_dump` — mas isso é exatamente o trocadilho de ABI de (e), rejeitado por M.3.
3. **Adiar a cobertura nativa** e manter o caminho de cobertura em C — mas aí o `.c` não morre,
   e o decreto não é cumprido.

A opção 1 é a única honesta, e é decisão do owner.
