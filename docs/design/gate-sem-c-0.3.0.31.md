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

## 2.3 A MEDIÇÃO — o backend próprio não compila o compilador (bloqueio de fato)

F1 foi executado antes de qualquer porte, e o resultado inverte a ordem do plano. Método: a
semente 0.3.0.30 constrói `gen1` desta árvore (rota C, `--no-verify --release`); `gen1` — que é o
compilador PÓS-EXCISÃO, logo backend próprio e só ele — é então mandado construir a mesma árvore.

```
seed  build .            -> gen1                                    OK   (146.5s, rota C)
gen1  build . --no-verify -> gen2
      teko: .: const struct: initializer is not a struct literal (Tier-A follow-up) (#594)
```

A parada vem de `src/lir/lower_const.tks`:486 — **antes** de qualquer função ser lowerada. E não é
um caso de borda do compilador: um programa de CINCO linhas já para.

```
use teko::env
use teko::io
let a = teko::env::args()
teko::io::println($"argc={a.len}")
```
```
gen1 build .  ->  teko: .: native backend N1: fat-pointer receiver `call` not yet lowered (N2)
```

O que o backend próprio constrói hoje, provado no mesmo gen1, é isto e pouco mais:

```
use teko::io
teko::io::println("hello")
42
```
```
gen1 build .  ->  teko: .: built bin/argprobe (own backend)      ./bin/argprobe -> "hello", exit 42
```

O inventário ESTÁTICO das paradas honestas do caminho nativo (`src/lir` + `src/backend`) mede o
tamanho do vão: **53 mensagens distintas**, entre elas a família de ponto flutuante INTEIRA
(`A4-fp`/`B1-fp` — toda operação, todo literal, todo argumento, todo retorno f32/f64),
interpolação (`bool`/`float`/spec estático/spec dinâmico/furo de tipo qualquer), funções
genéricas, binding com desestruturação, padrões de `match` por alternação/faixa/slice/`null`,
atribuição composta, atribuição por deref de referência e atribuição a elemento de slice.

**Consequência para esta carga.** O binário de gate É o programa do compilador mais um `main` que
chama os `#test`. Se o backend próprio não lowera o programa do compilador SEM testes, não há
como ele produzir o binário de gate COM testes. Portar `run_native_gate` para a rota nativa hoje
não trocaria C por nativo: trocaria **gate** por **nenhum gate**.

A restrição inviolável da carga nomeia exatamente este caso — *"Se em algum momento você tiver que
escolher entre 'gate que não roda' e 'gate que ainda emite C', pare e reporte"*. É o que se faz
aqui.

## 3. O plano — decomposição em fatias

A ordem é obrigatória: **o caminho novo funciona antes de o velho sair**. Nenhuma fatia deleta
emissão de C; a deleção é o vagão seguinte.

- **F0 — o mapa** (este documento). FEITO.

- **F1 — medir o backend próprio.** FEITO, §2.3: ele não compila o compilador, nem um programa de
  cinco linhas com interpolação. F2..F7 ficam BLOQUEADOS atrás de F-1.

- **F-1 (nova, e primeira) — fechar o vão N2 do backend próprio até ele compilar o programa do
  compilador.** É o pré-requisito de tudo o que segue e é um VAGÃO, não uma fatia: 53 paradas
  honestas distintas, com a família de ponto flutuante inteira dentro. Sem ela, não existe binário
  de gate nativo — logo não existe gate sem C.

- **F2 — o `main` de gate nativo.** `lower_gate_program(prog, gate_stmts)`: modo em que
  `lower_item_function` NÃO descarta `is_test`, as statements soltas são descartadas, e o
  virtual-main vem das statements sintetizadas. Um novo módulo sintetiza o corpo do gate em AST
  TIPADA (`checker::TExprStmt` sobre `TCall` para os builtins `arena_push`/`print`/`cov_enter`/
  `<teste>`/`cov_leave`/`println`/`arena_pop`). Índices de `prog.items` preservados: o transform
  não insere nem remove itens, então o `idx` de cobertura continua o mesmo do pai.

- **F3 — os builtins que faltam no LIR.** ENTREGUE nesta carga: `call_symbol` ganhou a tabela
  `cov_* → tk_cov_*` e `arena_push/pop/commit → tk_arena_*`, em três predicados por família
  (`builtin_io_symbol`/`builtin_cov_symbol`/`builtin_arena_symbol`). Sem C novo — todo símbolo já
  existe em `teko_rt.h`, e todo nome já é builtin do checker (`scope.tks`:496–536). É um
  pré-requisito de F2 e ao mesmo tempo conserta um erro latente: `teko::cov_line_hit(...)` e
  companhia, que o PRÓPRIO `teko::coverage` chama, caíam antes no ramo de mangling
  (`mangle_fn_symbol("", "cov_line_hit")`) e teriam produzido um símbolo indefinido no link em vez
  de uma parada honesta. `flush_out` ficou de FORA: `tk_flush_out` existe no runtime mas não é
  builtin do checker, e criar superfície de linguagem que nada ainda consome não é honesto.

- **F4 — veredito sem cobertura.** `run_native_gate` passa a emitir `.o` + `link_object` em vez
  de `.c` + `run_cc`. Contagem de testes e exit code idênticos. A cobertura ainda vem do caminho
  antigo — **esta fatia não pode ser a última**, sob pena de cegar a cobertura.

- **F5 — instrumentação de cobertura nativa.** Marca de entrada por função de produção
  (`cov_mark(idx)`) e marcas de linha/branch. O caminho barato é um transform de AST tipada
  (prepend de `TExprStmt`), o caro é instrumentar no LIR. Depende de (e) para o dump.

- **F6 — a prova automática de zero `.c`.** ENTREGUE nesta carga como CATRACA:
  `scripts/no_emitted_c.sh`. Todo `.c` presente que o git não rastreia é, por definição, emitido;
  o script compara esse conjunto, EXATO, com a baseline declarada — falha se cresce (sítio novo) e
  falha se encolhe (sítio morreu e a baseline não foi atualizada), para não apodrecer virando
  carimbo. A baseline hoje é `bin/teko-tktest.c` + `bin/teko-regrcov.c`, e o cabeçalho carrega a
  medição de §2.3 como justificativa. Vira lista vazia quando F4/F7 pousarem. NÃO está ligado ao
  `pr.yml` — o workflow é do vagão, e a carga proíbe tocá-lo.

- **F7 — os sítios sob flag.** `--analyzer`, `--per-test-cov` e o `regrcov`.

---

## 4. Os pontos de decisão do owner

### 4.1 O bloqueio de ordem: o backend próprio precede o gate

A medição de §2.3 é o achado que reordena a esteira. A sequência que a carga desenhou era

> 1. o gate para de precisar do emissor; 2. o emissor é deletado; 3. mede-se o que sobra do runtime.

e ela tem um degrau escondido ANTES do (1): o backend próprio precisa compilar o programa do
compilador. Hoje ele não compila nem `$"{x}"`. Enquanto isso não mudar, **matar o emissor de C não
deixa o projeto sem gate por descuido — deixa por construção**, porque o binário do gate é o
programa do compilador.

Duas leituras possíveis, e a escolha é do owner:

1. **Fechar o vão N2 primeiro** (F-1), e só então portar o gate. É o caminho que preserva o gate a
   todo instante, e é um vagão inteiro.
2. **Aceitar que o emissor de C sobreviva enquanto o backend próprio amadurece**, com a catraca de
   F6 impedindo que o conjunto de `.c` cresça em silêncio. O decreto de zero C continua sendo o
   destino; o que muda é a data.

### 4.2 O bloqueio de superfície: `tk_cov_dump` não tem forma chamável de Teko

Independente de 4.1, quando o gate nativo for portado ele esbarra em (e). O contrato do gate termina
em `tk_cov_dump(getenv("TEKO_TKCOV"))`; sem uma forma honesta de chamá-lo de Teko, o gate nativo não
consegue devolver a cobertura ao pai. As saídas possíveis:

1. **Superfície nova em C** — `void tk_cov_dump_s(tk_str path)` ao lado do `tk_cov_dump(const
   char*)`, duas linhas em `teko_rt.{c,h}`. Simétrico ao `tk_cov_merge(tk_str)` que já existe. É
   C NOVO, e a carga proíbe C novo sem decisão do owner.
2. **Um builtin `cov_dump(path: str)`** no checker que o codegen/LIR baixa para
   `tk_cov_dump` — mas isso é exatamente o trocadilho de ABI de (e), rejeitado por M.3.
3. **Adiar a cobertura nativa** e manter o caminho de cobertura em C — mas aí o `.c` não morre,
   e o decreto não é cumprido.

A opção 1 é a única honesta, e é decisão do owner.
