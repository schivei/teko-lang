# Passagem de sessão — lane 0.3.1.0 "Linux gera NATIVE" (2026-07-30)

Escrito porque o container reiniciou duas vezes num dia e o dono precisa de poder mudar de sessão sem
perder o fio. **Este documento é a fonte única do estado.** Tudo aqui é medido, com SHA; onde é
hipótese, está dito.

- **PR:** `schivei/teko-lang#99`
- **Vagão:** `remodel/0.3.1.0-linux-native-2`, HEAD **`757e575`** (worktree `/home/user/wt-lin`)
- **Como ler o CI sem cegueira:** `scripts/ci_full_log.sh` (§2c). Foi o instrumento que faltava todo o
  dia, e a §2b existe porque eu não sabia que existia.
- **Objetivo da lane:** as pernas Linux gerarem com o backend NATIVO (o `fixpoint_backend` por perna
  vive em `scripts/ci_producer_matrix.sh`)
- **O repo é um FORK.** `schivei/teko-lang`; o upstream é `teko-org/teko-lang`. `release.yml` e
  (desde hoje) `nightly.yml` só correm na org.







## 0o. DEGRAU 29 DRENADO — o A4-fp morreu, e ele achou um valor errado CALADO no x86 (2026-07-30)

`cargo/0.3.1.0-degrau-29` @ `1a1ed32` drenado. 15 ficheiros, +1686/−185. `drain_guard` OK, `.github/` intocado.

**Conferências no merge, todas limpas:** `fn` do `corpus.tks` **235 → 252** (subiu), `fn` do `lower.tks` **547 → 549** (subiu); **zero** splices e zero desequilíbrio de chaves nos 14 `.tks`/`.tkt` tocados; correspondência da fixture nas duas direcções — 138 chamadas, todas definidas, e a única definida-e-nunca-chamada continua a ser a excepção legítima `f_fat_field_len`.

### A PARAGEM MORREU, e as três pernas presas nela ficam livres

`A4-fp: float-op / FPR encoding deferred to 0.3.1` era o que prendia `test / linux-arm64-glibc`, `Memory paranoid (arm64-glibc)` e `test / macos-arm64` na PRIMEIRA fila do `own_native`. O projecto passa a emitir para `arm64-linux` e `arm64-macos`.

**Codificações cruzadas contra `llvm-mc -triple=aarch64 -show-encoding`, nunca derivadas do próprio codificador** — e a certificação é número-por-número: **205 formas FP distintas** extraídas do objecto emitido, reassembladas e comparadas palavra a palavra, **0 divergências**; desmontagem completa **33 100 instruções, 0 `<unknown>`**.

### O DEFEITO NOVO, e é a MESMA FAMÍLIA do degrau 27

`mut n: f32 = -7.25` dava **`-7.2500028745271266`** pela rota própria e `-7.25` pela rota C; **atravessando uma chamada dava `0`**. E é **no x86-64**, não no arm64 que o agente foi fechar. O checker não propaga o `f32` esperado através do **menos unário**, e a lowering negava em `f64` sem estreitar — um double num registo que tudo a jusante lia à largura simples. Corrigido em `narrow_unary_float_to_result`.

**É o terceiro membro desta família num dia:** o renderizador `f32` do degrau 27 (`$"{f:F2}"` de `2.5` dava `0.10` no nativo), o buraco de largura do `MCvt` no arm64, e agora o menos unário no x86. **A lição é sobre onde procurar:** o valor errado calado aparece sempre onde uma largura é assumida em vez de propagada, e as fixtures que o apanham são as que afirmam TEXTOS DIFERENTES para os MESMOS decimais (`0.1f+2.5f` = `2.5999999046325684` contra `2.6000000000000001`) — comparar valores não apanha, comparar a RENDERIZAÇÃO apanha.

Outro achado seu, também calado: `FCmpLt`/`FCmpLe` iam para o `lt`/`le` do inteiro, que leem VERDADE num `FCMP` não-ordenado — **`nan < x` dava true**. Passam a `lo`/`ls`, cruzado contra `clang --target=aarch64-linux-gnu`.

### RITUAL, e a prova que vale mais que o verde

`native_dry_gate` **verde com assinatura idêntica**, medida com a **gen2 da árvore** (e registado o contraste: com a **gen1 da semente** pára noutro sítio, `… emit_u32_le` — as gerações param em sítios diferentes, como está na lei); **fixpoint `VERDICT: PASSED — gen2 == gen3 byte for byte`**, mesmo sha256, 4 232 496 bytes; **unitários na gen2 1152/1152 `ok`**, zero pânicos, reconciliados um a um (1140 + 14 − 2); corpus `own_native` **`exit 42` nas duas rotas**; quatro alvos emitem.

**Prova por reversão:** revertendo SÓ dois braços de `encode_inst_word`, a paragem volta com o texto exacto do CI nos dois alvos arm64. Revertendo a correcção de largura, a rota própria falha a fixture `f32` **e a rota C fica verde** — a divergência que a fixture existe para caçar. Isto é a asserção que eu não conseguia fazer de fora.

### PARAGENS QUE FICAM NOMEADAS, não escondidas

`pin_args`/`select_param` com >8 argumentos de uma classe (janela de arity, **simétrica** GPR/FPR — não é buraco de floats); `UCVTF` codifica e é testado mas é **inalcançável** porque `LUnOp` não declara `IToF` sem sinal; `minst_interp` sem a família float (espelha o gap do interp da LIR); `%` sobre floats é **recusado** pelo checker e `x / 0.0` **armadilha**.

### O QUE FICOU POR MEDIR, e a razão é a máquina

A fase de **regressões** dentro do `teko test .` foi **inanida por contenção** — três suítes de agentes em simultâneo, 15 GB/16 GB, a dele a **7 % de CPU**, um projecto em 40 minutos. Parou-a para libertar a máquina e mediu o **canal directo** em vez dela. A fase unitária está completa e verde. **Isto é resposta aceitável** e foi o que eu autorizei: dizer qual fase ficou por medir vale mais que repetir três vezes contra uma máquina saturada.

**COLISÃO QUE FICA PARA O PRÓXIMO DRENO:** ele tocou **`src/lir/lower.tks`** em três sítios (`lower_unary`, `lower_int_to_f32` e duas funções novas) — o ficheiro que eu lhe pedira para evitar, e tocou-o com razão, porque o defeito era da sua família. **`cargo/0.3.1.0-degrau-31` está VIVO no mesmo ficheiro** (a guarda de divergência gordo/escalar). Esse merge resolve-se **por inspecção**, nunca com `--union`, e a contagem de `fn` do `lower.tks` (agora **549**) não pode descer.

## 0n. A PERNA WINDOWS DRENADA, e o agente REFUTOU a minha inferência (2026-07-30)

`cargo/0.3.1.0-windows-leg-2` @ `0c81989` drenado. `drain_guard` OK. Conferências no merge: `fn` do `corpus.tks` **235 → 235** (a branch não tocou a fixture), zero chamadas do teste sem definição, e a **terceira direcção** medida — as quatro novas (`mingw_path_evidence`, `mingw_triple_evidence`, `MINGW_PATH_EVIDENCE_PHRASE`, `MINGW_TRIPLE_EVIDENCE_PHRASE`) são **privadas**, e o teste vive na mesma namespace, logo a visibilidade chega. Os **três** testes onde havia um estão verificados por nome (`..._convicted_by_its_path_without_any_probe`, `..._innocent_spelling_is_left_for_the_triple_to_judge`, `..._triple_reading_convicts_the_gnu_abi_and_acquits_the_unknown`).

### A asserção mingw: a cadeia completa, e a declaração que o meu grep não achou

`const HOST_CC_NAME: str = "cc"` vive em **`src/build/regression.tks:645`**. A cadeia: `mingw_cc_evidence("cc")` → `linker_is_mingw("cc")` é falso (a grafia é inocente) → **executa** `cc_target_triple`, que faz `spawn_redirected(["cc","-dumpmachine"])` → no runner Windows o `cc` resolve para `/c/mingw64/bin/cc` e responde um triplo MinGW → evidência não vazia → `== ""` falso. A 1165 cairia a seguir pela mesma razão; a 1164 dispara primeiro, e é por isso que a mensagem dizia `is_true`.

Ritual reportado: `native_dry_gate` **verde com paragem idêntica** (medida pelo agente na base, com gen1 própria); **fixpoint `gen2 == gen3` byte a byte E `gen2.c == gen3.c`**; unitários na gen2 **1142 iniciados, 0 falhas** — 1140 + 2, porque um teste virou três — e o agente confirma a armadilha: **1135 linhas terminam em `ok` e 7 empurram-no para a linha seguinte**. Semente: `bootstrap/teko.c` (o `fetch_teko.sh` dá 403 por token inválido), via degrau `1e441aa`.

### A REFUTAÇÃO, e é minha

Eu escrevi que a mesma assinatura `0xC0000005` em **duas fixturas independentes** *"promove a hipótese de causa única no arranque"* e mandei olhar primeiro para a entrada sintetizada e o alinhamento de pilha. **Errado nas duas metades, e o log desmente-me:**

1. **Não são duas fixturas independentes — é UMA.** O `main.tks:43` do `own_native` chama `f_alias_fat_field()`, e `own_arith_exit` é a fila `[0]`: cai com o binário. Uma causa, dois sintomas na mesma cadeia.
2. **`regressor.tkr (14 builds)` contra 16 filas no ficheiro: o regressor CORTA na primeira falha.** As quatro que nunca correram incluem **`alias_fat_field (C route)`**. Portanto **a rota C desta fixtura está POR MEDIR em Windows, não verde** — e eu invoquei a regra do oráculo (*"a divergência nativo × C é bug do nativo"*) **sem medição do lado C**. Invocar o oráculo sobre um lado que não correu não é aplicar a regra: é presumi-la. Contraprova do agente: em Linux dá **18 builds**, com as mesmas filas presentes.
3. E `byte-view round-trip (own-native)` — a única outra fila que compara duas `str` — **não correu**, logo não era contra-exemplo de nada.

### A CAUSA PROVADA: a ABI de Win64 contra o par gordo

`tk_str` tem **16 bytes** (`teko_rt.h:45-48`). Em Win64 um agregado só viaja em registo com 1/2/4/8 bytes; **16 viajam por referência**. O LIR passa sempre um valor gordo como `(ptr, len)` — que é a ABI da **SysV**. A correcção que já existe, `str_pair_by_ref_x86` (`isel_x86_64.tks:1373`), está fechada a **sete símbolos** (`is_str_arg_builtin`, `lower.tks:3259`) **e a `args.len == 2`**: cobre a família de UM `tk_str` e é *estruturalmente* incapaz de cobrir a de DOIS. Sobram quatro entradas por valor que o nativo chama: **`tk_str_eq`, `tk_str_contains`, `tk_str_ends_with`, `tk_rt_last_index_of_ok`**. As outras foram achatadas de propósito, e em SysV as duas formas são a MESMA ABI — é por isso que isto é invisível em Linux e macOS.

Explica os três observáveis sem sobras: *não escreveu nada* (estoura NA comparação, e a cadeia do `main.tks` são `if` silenciosos); *só em Windows* (é a única ABI da matriz com `max_reg_arg_bytes < 16`); *"duas" fixturas* (é uma). Bónus: as dez filas `defer_*` que PASSAM entram no runtime por `tk_panic_str`, que **está** na lista dos sete.

**Excluído por medição:** arranque, entrada sintetizada, alinhamento de pilha (aritmética de `frame_sub_size_x86`/`compute_frame_layout_x86` verificada), secções/relocações PE, compilação (compilou 1301 s e correu; 13 filas own-native anteriores passaram com o mesmo emissor), e a família `executable_suffix`/`binary_output_path`/`sibling_object_path` — *"um binário obsoleto não escolheria justamente a fila que compara strings"*.

**Não empurrado, e a recusa é correcta:** sem host Windows, mudar ABI por raciocínio numa perna já vermelha é o palpite que o brief proíbe. **E há prova host-independente disponível:** `isel_x86_64_test.tkt` já tem um descritor `WIN64` (linha 482) e pode afirmar a sequência emitida para `tk_str_eq` **sem runner**. O desenho: generalizar a materialização por referência para N pares gordos quando `max_reg_arg_bytes < 16`, com a aridade gorda por símbolo ao lado de `is_str_arg_builtin`; SysV intocado pela guarda, fixpoint imóvel.

**Também por medir:** o arco C de `alias_fat_field` em Windows, e a fase de regressão completa em Linux até ao fim.

## 0m. AS PERNAS x86_64 ANDARAM DUAS FILAS — e a guarda diz o que falta a seguir (2026-07-30)

Execução `30539595001` (`8d781ea`, o conserto das sete pernas). **Medido, e é progresso limpo:**

- **`check_coff: FAIL` desapareceu.** Zero ocorrências. A fila `own_cross_x86_64_windows_emits_coff` passa agora nas três pernas x86_64.
- Elas avançaram para a fila **seguinte**: `own_cross_arm64_linux_emits_elf` (`test / linux-x86_64-musl`, `Memory paranoid (musl)`, `regressor wasm`).
- `arm64-glibc`, `mem-paranoid arm64` e `macOS` continuam em `own_arith_exit` (A4-fp = degrau 29, em branch); Windows em `own_arith_exit` (`0xC0000005`).
- Stop nativo, único: degrau 31. Zero `skipped`.

### A GUARDA NOMEOU O CONSERTO SEGUINTE, seis vezes

```
check_elf: FAIL — no cross-capable LLVM disassembler/relinker for an arm64 object exists on
Linux-x86_64 (looked for llvm-objdump / ld.lld) — install LLVM's lld+objdump on this host, or do
not route a cross-ELF check here — a gate that passes with nothing to check is a hidden error
```

`llvm` deu o `llvm-readobj` que fechou o COFF; o ELF cruzado quer também o **`ld.lld`**, que vive no pacote **`lld`**, não no `llvm`. Aplicado nas oito instalações (as sete pernas da suíte + o `regressor-full`).

**E é uma guarda bem escrita:** não disse só "falhei" — disse **o que procurou** (`llvm-objdump` / `ld.lld`), **o que instalar**, e a alternativa legítima (*"or do not route a cross-ELF check here"*). É o molde do que quero das asserções que o agente da suíte está a construir: uma falha que diz o que esperava, o que obteve, e o que fazer.

## 0l. O `llvm` FUNCIONOU ONDE CHEGOU — e dois dos meus três sítios eram a perna errada (2026-07-30)

Execução `30535419502` (`5e14c6e`), log integral. **Progresso medido em duas frentes** e um erro meu, o mesmo de sempre.

### O QUE ANDOU

- `unknown function`: **zero** em todo o log. Fechado.
- Stop nativo, único: `vt_table is not a fat-pointer local (internal)` — degrau 31.
- `assertion failed`: **duas** ocorrências, o MESMO teste na MESMA perna (Windows).
- **Zero `skipped`**, e o `no_skips_gate` diz *"every declared row ran. No skips."*
- **O `regressor wasm` passou a fila do COFF** e avançou para a seguinte: falha agora em `own_cross_arm64_linux_emits_elf`. O `llvm` que eu lá pus **funcionou**.

### A FILA QUE FALHA, POR PERNA — e ler uma e generalizar mente

| perna | `own_native` pára em | `regressor` |
|---|---|---|
| `test / linux-x86_64-musl` | `own_cross_x86_64_windows_emits_coff` | — |
| `Memory paranoid (musl)` | `own_cross_x86_64_windows_emits_coff` | — |
| `regressor wasm` | **`own_cross_arm64_linux_emits_elf`** (avançou) | — |
| `test / linux-arm64-glibc` | `own_arith_exit` (A4-fp = degrau 29) | — |
| `Memory paranoid (arm64-glibc)` | `own_arith_exit` (A4-fp) | — |
| `test / macos-arm64` | `own_arith_exit` (A4-fp) | — |
| `test / windows-x86_64` | `own_arith_exit` (`0xC0000005`) | `alias_fat_field` |

### O ERRO, e é a QUARTA vez com a mesma forma

Eu pus `llvm` em **três** sítios que instalavam `clang`. Medido no log, pelas linhas `##[group]Run` de cada perna: **só UM deles era uma perna que corre a suíte** (`regressor-full`). Os outros dois eram `cli-surface-linux-x86_64-glibc` e `seed-debut` — jobs que não correm o corpus. E as pernas que precisavam (`test-linux-*`, `mem-paranoid*`) **não instalam nada**: só correm uma sonda de diagnóstico (`for t in cc clang gcc file python3`) e vivem do que a imagem traz — e a imagem traz `clang` sem `llvm-readobj`.

**Editei onde a string batia, não onde a necessidade estava.** É exactamente a lição do `.exe` (medi dois sítios, eram nove) e a do predicado de gordura (o dono apanhou-a hoje). A regra que eu escrevo para os agentes falhou em mim: **enumerar a família é enumerar quem NECESSITA, não quem casa com o `grep`.**

**Conserto aplicado:** um passo próprio — *"Install the object-format parsers the cross gates read with"* — nas **sete** pernas que correm a suíte (`test-linux-arm64-glibc`, `test-linux-arm64-musl`, `test-linux-x86_64-glibc`, `test-linux-x86_64-musl` e as três `mem-paranoid`), e **revertidos** os dois sítios onde eu não tinha necessidade medida — o comentário que lá pus alegava uma razão que não era verdade naquele job, e um comentário falso no CI é pior que nenhum. Conferido: 7 passos novos, 1 `clang llvm` (o `regressor-full`, medido a funcionar), 2 `clang` sozinhos, YAML válido.

**Não medido, e digo-o em vez de o presumir:** macOS e Windows param antes de chegar às filas de objecto, logo **não sei** se têm os parsers. Quando o degrau 29 e a violação de acesso de Windows fecharem, essas duas pernas dirão.

## 0k. TERCEIRO REINÍCIO DO CONTENTOR — restaurado de um INSTANTÂNEO ANTIGO (2026-07-30 ~10:19)

Não foi um reinício limpo: a árvore local voltou a **`9bc292a`** (`merge(carga): cargo/20-extern-return-narrowing`), este ficheiro **não existia**, e as worktrees dos agentes de hoje (`wt-d30`, `wt-d31`, `wt-unkfn`, `wt-winleg`) tinham desaparecido — só restavam as de sessões anteriores. Recuperado com `git fetch` + `git checkout -B … origin/…`.

**O que se perdeu:** uma secção §0j já escrita e NÃO empurrada. **O que se salvou:** tudo o que estava empurrado, incluindo o dreno do degrau 30. A lei *"escreveu? comita e empurra"* vale para mim exactamente como para os agentes — e esta foi a terceira vez hoje que o contentor a cobrou.

## 0j. A EXECUÇÃO PÓS-DRENO, MEDIDA INTEIRA — e o `own_native` falha por TRÊS razões diferentes

Execução `30528940780` sobre `954b2c9`, log integral. **`unknown function` desapareceu do CI** — zero ocorrências, o dano de §0h está fechado. E o `own_native` continua vermelho, mas por causas novas, **diferentes por perna**, porque a feature pára na PRIMEIRA fila que falha e cada perna chega a uma fila diferente:

| pernas | fila que falha | causa | dono |
|---|---|---|---|
| `linux-x86_64-musl`, `Memory paranoid (musl)`, `regressor wasm` | `own_cross_x86_64_windows_emits_coff` | `check_coff: FAIL — o parser 'llvm-readobj' está ausente` | **minha (CI)** |
| `linux-arm64-glibc`, `Memory paranoid (arm64)`, `macos-arm64` | `own_arith_exit` | `A4-fp: float-op / FPR encoding deferred` = **degrau 29** | `cargo/0.3.1.0-degrau-29` |
| `windows-x86_64` | `own_arith_exit` | `exit -1073741819` = **0xC0000005 ACCESS_VIOLATION**, sem escrever nada | `cargo/0.3.1.0-windows-leg-2` |

Três coisas que isto ensina:

1. **A fila do COFF passou a ser ALCANÇÁVEL pela primeira vez.** Eu tinha registado que ela *"nunca é atingida"* — era verdade e deixou de ser, porque as filas anteriores passaram a passar. É a segunda vaga ao nível da fixture: **o que se mede é só o que a execução alcança**.
2. A guarda `check_coff` fez exactamente o que devia: *"a gate that passes with nothing to check is a hidden error"*. Um `OBJ_CHECK_ALLOW_SKIP=1` no CI teria escondido isto e é proibido. **Conserto: `llvm` entra ao lado do `clang` nos três sítios de instalação do `pr.yml`** — o pacote `clang` não carrega `llvm-readobj`, o `llvm` carrega. Conferido: zero `install -y clang` sem `llvm` depois da mudança.
3. **A ACCESS_VIOLATION de Windows tem a MESMA assinatura em duas fixturas independentes** — `own_native.exe` e o `alias_fat_field` do `regressor.tkr`, ambas `-1073741819` e ambas sem escrever nada. Isso promove a hipótese de causa única no emissor/encoder de Windows, e é evidência que o agente da perna Windows ainda não tinha.

### O RESTO DA MEDIÇÃO, sem surpresas

- Stop nativo, único em todo o log: `native backend N1: 'vt_table' is not a fat-pointer local (internal) [in cg_pair_is_iface_vtable]` — **degrau 31 confirmado pelo CI**, nas duas pernas de fixpoint nativo, com o front-end inteiro a passar (lexer/parser 143/143, checker **6462/6462**, monomorph 0/0, consteval **576/576**).
- `assertion failed` em todo o log: **duas** ocorrências, e são o MESMO teste na MESMA perna (`pt_a_mingw_cc_is_convicted_by_its_path_without_any_probe`, Windows). Fora de Windows, zero pânicos.
- Unitários: **1140** em seis pernas; Windows arranca 368 e aborta no tal teste.
- **Zero `skipped`** nas 28 linhas de tally.
- `regressions 1 run, 0 skipped, 1 failed` × 12 continua a ser o unitário que prova por inversão que um regressor listado e inexistente é erro de manifesto. Não é defeito.

### UM NÚMERO QUE NÃO GOSTO, e fica a olho

O `own_native` passou a compilar em **620 s** (musl), **815 s** (arm64-glibc), **628 s** (macOS) e **1301 s** (Windows) — `compile 99%`, um build. Antes do dreno falhava em ~520 ms, mas falhava DEPRESSA por erro de compilação, logo os números não se comparam directamente. **Não afirmo regressão de desempenho**; afirmo que uma fixture a 21 minutos de compilação numa perna vai começar a esbarrar em timeouts, e que isto precisa de uma medição própria (a mesma fixture, o mesmo compilador, antes e depois do dano) antes de se lhe chamar qualquer coisa.

## 0i. DEGRAU 30 DRENADO, e há DEGRAU 31 — a escada avançou por medição (2026-07-30)

`cargo/0.3.1.0-degrau-30` @ `a082254` drenado no vagão. `drain_guard`: sem mudança em
`.github/workflows/`. Merge `ort` sem conflitos, 7 ficheiros, +905/−82.

### A PARAGEM VIVA NÃO ERA O `null` — ERA A ARIDADE

`lower_null_pattern_test` testava a etiqueta literal `0` sob a guarda do classificador de união-nula de
**dois** membros, logo só respondia a `{ null, X }`. O `emit_variant_wrap` do próprio compilador faz
`match … { []byte as o; error as e; null }` sobre `[]byte | null | error` — **três** membros.

E o gémeo obrigatório: `return null` para dentro de três membros caía no `lower_variant_construct`,
cuja busca POR TIPO não tem caso `Null`, e parava com *"value's type is not a member of its declared
variant (internal)"*. **Landar só o leitor teria sido pior que nada** — um braço `null` a testar uma
etiqueta que ninguém escreve.

Achado de brinde, corrigido pelo agente: as etiquetas `0`/`1` eram literais enquanto o classificador
aceitava **as duas ordens** de membro — um `{ X, null }` teria os dois braços trocados, **em silêncio**.
Agora são procuradas (`variant_null_member_index`).

### DEGRAU 31 — o novo stop, e é PROGRESSO, não defeito

```
base: native backend N1: `null` match pattern not yet lowered (N2) [in `teko::codegen::emit_variant_wrap`]
nova: native backend N1: `vt_table` is not a fat-pointer local (internal) [in `teko::codegen::cg_pair_is_iface_vtable`]
```

Provado com um diferencial **2×2** (compilador base/novo × fonte base/nova): o compilador **novo** na
fonte **base intocada** dá a paragem nova; o compilador **base** na fonte nova continua a dar a antiga.
**A fronteira é do gerador, não da fonte.**

Mecanismo, com repro de 10 linhas reproduzido NO COMPILADOR DA BASE: `typeexpr_is_fat_named` desiste em
`single_segment_name(nt.path)` e o doc de `typeexpr_is_fat_walk` di-lo — *"multi-segment path … is
never fat"*. Um parâmetro cujo tipo é um alias **QUALIFICADO** para um gordo
(`vt_table: checker::TypeTable`, alias de `[]TypeReg`) é ligado como ESCALAR e a leitura fat estoura.
Alias de um só segmento funciona; qualificado não. **Não é** o `native_iface_fat_known_stop` (esse é o
RESULTADO de despacho, não o parâmetro).

### O RITUAL QUE O AGENTE CORREU (e é o padrão a exigir)

- FIXPOINT **gen2 == gen3 byte a byte** e `gen2.c == gen3.c`, duas vezes: antes e depois do merge forward.
- Unitários na **gen2** da árvore fundida: **1140 testes, 1140 ok, zero pânicos**. Contagem de `#test`:
  base 1131 → HEAD 1140, **as +9 são todas de upstream, zero do agente**.
- `TEKO_MEM_PARANOID=1` exit 0, pico **2192.7 MB** (gen1 normal 1562.8 MB).
- Corpus `own_native` completo: rota própria **42**, rota C **42**.
- Os três known-stops da família medidos na gen2 — **nenhum levantou**.
- Semente: `bootstrap/teko.c` → gen0 → gen1 (`TEKO_BACKEND=c`), porque **`fetch_teko.sh` dá HTTP 403**.

### AS TRÊS CONFERÊNCIAS DE §0h/§0c, CORRIDAS POR MIM NO MERGE

| conferência | antes | depois |
|---|---|---|
| `fn` em `corpus.tks` (não pode DESCER) | 202 | **235** |
| splices / chaves desequilibradas em todos os `.tks` | — | **0** |
| `f_*` chamado sem definição (direcção 1) | — | **nenhuma** (129 chamadas) |
| `f_*` definido e nunca chamado (direcção 2) | — | só `f_fat_field_len`, a excepção legítima que o agente nomeou |

### DUAS COISAS REPORTADAS PELO AGENTE, e a primeira é uma LEI NOVA

1. **NENHUM código de saída de fixture pode passar de 255 — e 256 mapeia para 0.** Medido:
   `exit(260)` dá **4** no POSIX. A fixture `own_native/main.tks` usa 260–269 **e também usa 4–13**,
   logo uma falha na linha 260 sai com o código da linha 4: **a falha é atribuída à cena errada.**
   Não é falso-verde (o `.tkr` espera 42), é MENTIRA SOBRE QUAL linha quebrou — e a barra do tronco
   proíbe exactamente isto. Pior ainda: a faixa 250–259 que o agente sugeriu tem o 256, que mapeia
   para **0**.
   **Corolário da família, medido em todo o repo:** os únicos códigos > 255 são estes dez, num único
   ficheiro (`examples/regressions/own_native/main.tks`). O `99999999999` de
   `src/casting/casting.tks` é um literal numérico de teste de cast, não um código de saída.
   **Faixas livres ≤ 255 nesta fixture:** 81-89, 93-99, 101-129, 141-159, 162-169, 177-189, 193-209,
   **235-255**. O remap fica enfileirado para 235–244, com a guarda a fechar dos dois lados: nenhum
   código de fixture > 255.
2. **A rota C não alarga união em união mais larga.** `let l: i64 | null | error = if n == 0 { null }
   else { n }` — o `if` junta-se em `i64 | null` e a rota C emite `tk_u_null_i64` para um slot
   `tk_u_null_i64_error`: `error: invalid initializer`. Pré-existente, família do ALARGAMENTO de
   uniões, não do padrão `null`.

E uma atribuição que se fecha: o SIGABRT de `lwt_lowers_str_index_loads_the_byte_off_rodata` foi
atribuído por medição a `1ea5b68` (a guarda de fronteira, que mudou só `src/lir/lower.tks` e nunca o
teste que fixava a sua saída) e **já estava corrigido upstream em `3fe4018`**. Não é do agente, e está
fechado.

### A REPARAÇÃO DAS CAUDAS, medida uma TERCEIRA vez — e o número muda

O agente do degrau 30 reparou o mesmo dano de §0h, e mediu-o contra os progenitores `5f5eca0`/`880dc37`:
**sete aberturas `/**` e SEIS finais de função** (`0` + `}`) perdidos nas junturas, com o ficheiro a não
lexar em `3710:18: unexpected character`. Eu contei "sete caudas" e o agente do `unknown function`
também. **Sete/seis, não sete/sete** — a discrepância fica registada em vez de arredondada, porque a
lei de §0h depende de se contar o que se mediu.

## 0h. O `unknown function` ERA DANO MEU — a `--union` comeu sete caudas de função (2026-07-30)

**A causa está encontrada, e não era do compilador.** Nenhuma das minhas duas hipóteses (cap de
declarações, árvore `src/` não carregada) estava certa. O `corpus.tks` da fixture `own_native` estava
**com o fonte estragado**, e quem o estragou fui eu, ao resolver dois merges com `git merge-file
--union`.

### O QUE A `--union` DEIXOU

Sete vezes, o corpo de uma função corria directamente para dentro do doc-comment da função seguinte —
o `0`, o `}`, a linha vazia e o `/**` **todos ausentes**:

```teko
fn f_slice_elem_store_boundaries() -> i64 {
    …
    if ys.len != 5 { return 11 }
 * D27_TENTH_F32 — `0.1` held as an `f32`, whose EXACT binary64 value …
```

Sítios (linhas na árvore reparada): **3707, 3833, 3893, 3949, 4010, 4085, 4121**.

### A REPARAÇÃO É FIEL, e verifiquei-o em vez de acreditar

`+ 0` / `+ }` podia ser um fecho arbitrário que enfraquecia a fixture em silêncio — uma função cuja
cauda original devolvia outra coisa passaria a devolver `0` e o teste ficava verde por engano. Fui aos
commits **anteriores** ao dano:

| commit | cauda de `f_slice_elem_store_boundaries` |
|---|---|
| `e0a3491`, `0ddd4a6` (pré-dano) | `… if ys.len != 5 { return 11 }` / **`0`** |
| `ffe7580` (pós-dano) | o corpo do `d27_ftoa_of` — o splice |

A cauda restaurada é a original. O dano entrou em **`1103ffb`** e **`ffe7580`**, os dois merges que eu
resolvi com `--union`.

### A LEI, corrigida (era minha, e estava demasiado larga)

Eu escrevera: *"`git merge-file --union` é a resolução correcta para conflitos puramente aditivos de
fixture."* **Estreita-se:**

> `--union` só é segura quando as hunks em conflito são **registos inteiros e auto-delimitados** (uma
> linha por caso, um bloco fechado). **Um corpo de função `.tks` não é um registo auto-delimitado**: a
> `--union` pode escolher uma fronteira de hunk que faz desaparecer o fecho de um lado e o abridor do
> outro, e o resultado **compila-se como se fosse outra coisa** em vez de dar conflito.
>
> Depois de QUALQUER resolução automática num `.tks`, a conferência obrigatória é **contar as `fn`
> declaradas antes e depois**: o número não pode DESCER. E no caso de uma fixture, todo o `f_*`
> chamado no `main.tks` tem de resolver.

Isto explica também porque é que o meu contra-exemplo de §0f (a chamada da linha 90 desconhecida e a
da linha 80 conhecida) não era um cap: **o ficheiro inteiro perdia as declarações**, e o que se via
era a cauda por ordem de CHAMADA. A leitura de §0f fica de pé; a causa é esta.

### O QUE JÁ ESTÁ REPARADO, e a colisão que fica para o dreno

- `cargo/0.3.1-own-native-unknown-fn` @ `7a2f49b` — as sete caudas; e `e8f76fb` acrescenta o que
  faltava no instrumento: **o excerto de um build falhado passa a guardar os DOIS extremos** (foi a
  cauda que me fez ler uma fronteira inexistente).
- `cargo/0.3.1.0-degrau-29` @ `1601eb4` — **as MESMAS sete truncaturas**, reparadas em paralelo,
  porque o agente precisava da fixture inteira para construir. **Colisão por comportamento, não por
  ficheiro.** No dreno toma-se UMA das reparações; o resto de cada branch é aditivo.

### DOIS DEGRAUS FECHADOS EM BRANCH (a aguardar o fim dos agentes, não drenados)

- **Degrau 29** — `cargo/0.3.1.0-degrau-29` @ `5c5c4c4`: *"fechar A4-fp — a família float inteira
  baixa em arm64, pinada por byte"*.
- **Degrau 30** — `cargo/0.3.1.0-degrau-30` @ `d4d48e2`: *"o padrão `null` num match deixa de assumir
  a aridade dois"*, + `643b688` (valor nas duas rotas, saídas 260-269).
- **Perna Windows** — `cargo/0.3.1.0-windows-leg-2` @ `50d8307`: *"a asserção mingw deixa de falar do
  cc do HOST"*.

Nenhum agente reportou fim; **não se drena branch de agente vivo**. Empurram ao escrever, logo nada se
perde se o contentor cair.

### CONFIRMADO NESTE CICLO: o `.exe` resolveu o que tinha de resolver

A perna `test / windows-x86_64` **deixou de morrer por falta de `teko.exe`**: agora arranca 368 testes
unitários e constrói 26 projectos de regressão antes de parar. O que a mata hoje é outra coisa (§0f,
causa 3), e está despachada.

## 0g. A FASE UNITÁRIA DEIXOU DE ABORTAR — em TODAS as pernas menos Windows (2026-07-30)

Pergunta pendente do ciclo, respondida pelo log INTEGRAL da execução `30526530472` (topo `757e575`,
`scripts/ci_full_log.sh`, 12 jobs em falha, nada truncado).

### A RESPOSTA

| perna | `test … …` arrancados | pânicos |
|---|---|---|
| `test / linux-x86_64-musl` | **1140** | 0 |
| `test / linux-arm64-glibc` | **1140** | 0 |
| `test / macos-arm64` | **1140** | 0 |
| `Memory paranoid (linux-x86_64-musl)` | **1140** | 0 |
| `Memory paranoid (linux-arm64-glibc)` | **1140** | 0 |
| `regressor / all capabilities (wasm)` | **1140** | 0 |
| `test / windows-x86_64` | **368** e ABORTA | **1** |

`grep -o 'assertion failed: [a-z_]*'` sobre o log inteiro dá **duas** ocorrências e **as duas são o
mesmo teste na mesma perna** (o ficheiro do job e o ficheiro do passo repetem a linha):
`pt_a_mingw_cc_is_convicted_by_its_path_without_any_probe … assertion failed: is_true`. O conserto dos
dourados (`e317b44`) **aguentou**: fora de Windows não há pânico nenhum.

E **zero `skip`** em toda a suíte, nas sete pernas — `grep -oiE 'test … \.\.\. skip[a-z]*'` não devolve
nada, e o tally de regressões dá `0 skipped` em todas as 28 linhas.

### DUAS CORRECÇÕES DE CONTAGEM, e a segunda é um erro meu de método

1. O número real é **1140**, não 1133 nem 1117. A árvore cresceu.
2. Eu primeiro anunciei **1138** e uma perna com **1133** — "cinco testes que não correm no
   regressor wasm". **Era artefacto do meu `grep`.** Eu ancorava em `... ok` na MESMA linha, e um
   teste que imprime saída própria empurra o `ok` para a linha seguinte:

   ```
   test teko::checker::same_type_cast_is_redundant_warning ... warning: redundant cast: …
   ok
   ```

   Os cinco "ausentes" eram quatro testes de aviso de cast redundante e um de uso do `fmt`. O
   regressor corre `teko test . --arith-cast-gate` (as outras pernas correm `teko test .` seco), e é
   o gate que ARMA o aviso — daí a saída interleaved só ali. Contando `test … \.\.\.` sem ancorar no
   `ok`, as seis pernas dão **1140 exactamente iguais**. **Lição: uma fronteira de `grep` não é um
   facto.** É a mesma família do erro da cauda (§0f, causa 2), no mesmo dia.

### DUAS COISAS QUE O TALLY MOSTRA E NÃO SÃO DEFEITO

- `regressions 1 run, 0 skipped, 1 failed` × 12 → é o teste unitário
  `run_regression_sources_missing_path_is_a_manifest_error` a provar por INVERSÃO que um regressor
  listado e inexistente é erro de manifesto (`examples/regressions/__definitely_missing__.tkr`). A
  falha é o entregável do teste.
- **Não há `A4-fp: float-op` em nenhuma perna.** O único stop nativo no log integral é o do degrau 30
  (`native backend N1: 'null' match pattern not yet lowered (N2)`). Ou seja: **o degrau 29 não está no
  caminho crítico do CI hoje** — o fixpoint pára antes de o alcançar. Fecha-se por valor próprio, não
  para desbloquear a lane.

## 0f. A VAGA DE 12 JOBS VERMELHOS DE `757e575` — LIDA, e METADE NÃO É DEFEITO (2026-07-30)

Doze jobs vermelhos chegaram por webhook em duas execuções seguidas (`30526044023` sobre `d3ab105`,
`30526530472` sobre `757e575` — o topo actual). Os dois commits são de documentação, logo **o estado
é o mesmo nas duas** e a vaga não foi causada por eles. Li o log INTEGRAL (§2c) e a vaga tem **três**
causas, não doze. Quem recuperar a sessão não precisa de repetir a leitura.

### CAUSA 1 — `artifact / linux-x86_64-glibc` e `artifact / linux-arm64-musl`: **VERMELHO POR DESENHO**

```
fixpoint: | teko: .: native backend N1: `null` match pattern not yet lowered (N2)
          [in `teko::codegen::emit_variant_wrap`]
fixpoint: VERDICT: FAILED — gen1 does not build the source it came from
```

Isto é o **degrau 30**, o degrau aberto que está a ser trabalhado. E não é regressão: está escrito no
próprio `pr.yml`, no comentário do passo do fixpoint (linhas ~451-455):

> *"A `native` LEG IS EXPECTED TO GO RED TODAY, and that is the deliverable, not a regression to patch
> around — the native backend does not build the compiler yet and the stop it reaches is named by
> address in `docs/memory/0.3.1.0-linux-native-first-stop.md`. The red measures the distance left. To
> turn it back, one word per leg in `scripts/ci_producer_matrix.sh`."*

**Não voltes a diagnosticar isto.** As pernas `native` do fixpoint só ficam verdes quando o backend
nativo construir o compilador. Enquanto o degrau 30 estiver aberto, este vermelho é a régua.

**E é por isto que outros três gates caem em cascata, sem terem defeito próprio:**

| Gate | Porque cai |
|---|---|
| `CI gate` | a âncora `artifact-linux-x86_64-glibc` é a perna do fixpoint nativo |
| `Sanitizer gate` | o `mem-paranoid` consome a saída dessa mesma âncora |
| `Test suite gate` | a âncora falha e o `test-linux-x86_64-glibc` fica `skipped` por condição |

Ou seja: **1 causa → 5 jobs vermelhos.** Contar jobs sobre-conta causas; foi por isso que a vaga
pareceu um colapso e não é.

### CAUSA 2 — `own_native` falha em TODAS as pernas, e a lista de erros é uma **CAUDA**

`test / linux-x86_64-musl`, `test / linux-arm64-glibc`, `test / macos-arm64`,
`test / windows-x86_64`, `Memory paranoid` (musl e arm64-glibc) e `regressor / wasm`: todas dão
`regressions 11 run, 0 skipped, 1 failed` (Windows dá 2, ver Causa 3) e a fila é sempre a mesma —
`examples/regressions/own_native/own_native.tkr — own_arith_exit[0]: compile failed (exit 1)` com
`unknown function: f_*`. Universal, não é gémeo divergente.

**A ARMADILHA, e eu quase caí nela:** o harness imprime *"captured output **tail**"*. Os
`unknown function` que aparecem no log são as linhas **81..120** do `main.tks` — exactamente as
**últimas 40** chamadas. Isso PARECE uma fronteira posicional que deixa as 79 primeiras resolver, e
não é: é a cauda a cortar as anteriores.

A medição que desfaz a ilusão, e é um contra-exemplo, não uma opinião:

| chamada em `main.tks` | definição em `src/corpus.tks` | na cauda desconhecida? |
|---|---|---|
| linha 90 `f_arm64_bigframe_locals` | **2909** | **SIM** |
| linha 80 `f_div_signed_i32_value` | **2935** (DEPOIS) | não |

Se a fronteira fosse a ordem de DEFINIÇÃO, a de 2909 resolvia e a de 2935 não. É o contrário. O que
separa as duas é a ordem de **CHAMADA** — o que é exactamente o que uma cauda truncada produz, e é
incompatível com "cap de N funções por ficheiro". Primeira hipótese passa a ser: **a árvore `src/` do
fixture não está a ser carregada de todo** e por isso *todo* o `f_*` é desconhecido. Medição que
decide: correr o build do fixture **sem o harness** e ver se o PRIMEIRO erro é a linha 2 (`f_arith`).
Isto foi enviado ao agente que possui o assunto (`cargo/0.3.1-own-native-unknown-fn`).

### CAUSA 3 — Windows tem DUAS falhas próprias, e uma delas é um teste que assere sobre o host

O envelope de known-stop rebentou honestamente — é a guarda a funcionar:
`known-stop: more than the pinned row failed (or none did) — this envelope must not cover a second`.

1. **`pt_a_mingw_cc_is_convicted_by_its_path_without_any_probe`** (`src/build/project_test.tkt:1160`)
   → `teko: deliberate panic: assertion failed: is_true`. A mensagem diz `is_true`, logo a linha 1165
   (`is_false`) está excluída. Candidata: a **1164**,
   `is_true(mingw_cc_evidence(HOST_CC_NAME, …) == "")` — porque o próprio doc-comment do teste diz
   *"the Windows runner's `cc` resolves to `/c/mingw64/bin/cc`"* e a seguir assere que o cc deste host
   **não** é MinGW. Em Windows as duas frases contradizem-se. `mingw_cc_evidence`
   (`src/build/project.tks:1540`) convicta por caminho **ou** por triplo, e o triplo **executa** o
   compilador — duas dependências do host dentro de uma linha que se lê como literal.
   **É a terceira vez nesta lane** que uma asserção aparentemente literal chega a estado do host por
   saltos. Lição de novo: *segue o callee, não leias o call site como literal.*
2. **`regressor.tkr` → `alias_fat_field (own-native)[0]: exit -1073741819`** = `0xC0000005`,
   ACCESS_VIOLATION, e *"the program wrote nothing to stdout or stderr"*. A MESMA fila passa em Linux
   e macOS: **gémeo divergente**, e vale a regra do oráculo (é bug do nativo até prova em contrário).

Ambas foram despachadas juntas para `cargo/0.3.1.0-windows-leg-2` — a perna Windows é **both-tier**,
logo bloqueia o `Test suite gate` em qualquer modo, e é por isso que as duas andam no mesmo brief.

### O QUE ISTO MUDA NA MINHA CONTABILIDADE

Eu tinha registado `regressions 11 run, 0 skipped, 1 failed` como o estado medido do vagão, sem dizer
que **essa 1 é o `own_native` e faz a perna cair**. Não é uma falha tolerada por envelope nenhum em
Linux/macOS: é vermelho a sério, em todas as pernas, e é o item de maior valor da fila depois dos
degraus. Fica corrigido aqui.

## 0f. A `ACCESS_VIOLATION` de Windows TEM CAUSA PROVADA — e o meu "duas fixturas independentes" era uma leitura errada do log (2026-07-30, `cargo/0.3.1.0-windows-leg-2`)

Medido no log integral do job **90829251715** (run `30528940780`, SHA `954b2c9`).

**A minha inferência estava errada, e o próprio log a desmente.** Eu li duas fixturas a estourar com
`0xC0000005` e concluí "causa única do lado de Windows, provavelmente no arranque do processo". A parte
"causa única" está certa; a parte "arranque" está errada, e o que a decide é uma linha que eu não somei:
`regressor.tkr (14 builds)`. **O ficheiro tem 16 filas de build e só 14 correram** — o regressor para o
ficheiro na PRIMEIRA fila que falha. As 14 que correram são exactamente as filas ATÉ `alias_fat_field
(own-native)` inclusive (argv + 2 qualifier + 10 defer + ela). As quatro que ficaram por correr são
`alias_fat_field (C route)`, `variant_member_compare (C route)` e o par `byte-view round-trip`.

Consequências imediatas, e as duas doem:

1. **A rota C desta fixtura NUNCA correu em Windows.** A "regra do oráculo" que eu invoquei (a
   divergência nativo-vs-C é bug do nativo) não tem medição nenhuma deste lado — o arco C está por
   medir, não verde.
2. **`byte-view round-trip (own-native)` também nunca correu**, e é a única outra fila own-native de
   `regressor.tkr` que compara duas `str`. Não é contra-exemplo de nada.

**A CAUSA, provada por leitura do código.** `tk_str` é `{ const tk_byte *ptr; size_t len; }` — 16
bytes (`src/runtime/teko_rt.h:45-48`). Na ABI Microsoft x64 um agregado só viaja em registo com
tamanho 1/2/4/8; **16 bytes viajam POR REFERÊNCIA** (o chamador copia para um temporário e passa o
ENDEREÇO). Na SysV o mesmo agregado viaja em DOIS registos inteiros. O LIR achata sempre um valor gordo
em `(ptr, len)`, o que É a ABI da SysV e NÃO é a de Win64 — e a correcção que existe para isso,
`str_pair_by_ref_x86` (`src/backend/isel_x86_64.tks:1373`), está fechada a **sete símbolos** e a
`args.len == 2`:

```teko
fn str_pair_by_ref_x86(abi: AbiDescriptor, symbol: str, args: []u32) -> bool {
    abi.max_reg_arg_bytes < X86_STR_ARG_BYTES && lir::is_str_arg_builtin(symbol) && args.len == (2 to u64)
}
```

`is_str_arg_builtin` (`src/lir/lower.tks:3259`) lista `tk_print`, `tk_println`, `tk_eprint`,
`tk_eprintln`, `tk_write`, `tk_ewrite`, `tk_panic_str`. **Toda a família de UM `tk_str` está coberta;
a de DOIS não está, e a guarda `args.len == 2` torna-a estruturalmente incobrível** — dois pares
achatam para quatro operandos.

Cruzando os protótipos de `teko_rt.h` com os símbolos que `lower.tks` emite, sobram **quatro** entradas
que ainda recebem `tk_str` POR VALOR e não têm a correcção:

| símbolo | assinatura em C | pares gordos |
|---|---|---|
| `tk_str_eq` | `bool tk_str_eq(tk_str a, tk_str b)` | 2 |
| `tk_str_contains` | `bool tk_str_contains(tk_str s, tk_str needle)` | 2 |
| `tk_str_ends_with` | `bool tk_str_ends_with(tk_str s, tk_str suffix)` | 2 |
| `tk_rt_last_index_of_ok` | `bool tk_rt_last_index_of_ok(tk_str hay, tk_str needle, uint64_t *out_index)` | 2 + 1 escalar |

Todas as OUTRAS entradas do runtime que o backend nativo chama já foram reescritas ACHATADAS de
propósito — `tk_str_concat_len(const tk_byte*, uint64_t, const tk_byte*, uint64_t, uint64_t*)`,
`tk_str_slice_len`, `tk_str_of_bytes_len`, `tk_slice_str_eq(const tk_str*, uint64_t, …)`. Estas quatro
ficaram com a assinatura de struct. **Em SysV as duas formas são a MESMA ABI, por isso são
indistinguíveis em Linux e macOS**; em Win64 são ABIs diferentes e o achatamento é o errado.

**O mecanismo exacto, e já está escrito no próprio código.** O doc-comment de
`pin_str_pair_by_ref_x86` descreve o sintoma idêntico de quando isto foi apanhado para `tk_print`:
*"emitting SysV's two-register form on Win64 makes the C-built callee … read the STRING'S OWN first 16
bytes as `{ptr; len}` and `fwrite` through the resulting garbage pointer."* Para `h.s != "abcde"` o
nativo pinha `RCX = a.ptr`, `RDX = a.len`, `R8 = b.ptr`, `R9 = b.len`; o `tk_str_eq` compilado por clang
lê `RCX` como `tk_str*`, carrega os bytes de `"abcde"` como se fossem um ponteiro e faz `memcmp` nesse
endereço → **`0xC0000005`**.

**Isto explica os TRÊS observáveis, e nenhum sobra:**

- *"não escreveu NADA"* — o estouro é NA comparação. `cases/alias_fat_field.tks` é
  `exit(alias_field_probe())` e o probe não imprime; o `main.tks` do `own_native` é uma cadeia de `if`
  silenciosos. Nenhum dos dois chega a escrever.
- *"só em Windows"* — Win64 é a única ABI da matriz com `max_reg_arg_bytes < 16`. Em SysV/AAPCS64 o par
  achatado é literalmente a convenção correcta.
- *"duas fixturas"* — é UMA causa. `main.tks:43` do `own_native` chama `f_alias_fat_field()`, e a mesma
  cadeia passa antes por `f_str_equality` (item 28) e por outros `==` de `str`; o `own_native.exe`
  estoura no PRIMEIRO `tk_str_eq` que avalia, muito antes do item 44. `own_arith_exit` é a fila `[0]`
  desse ficheiro e cai com o binário, não pela sua própria aritmética.
- *bónus, e é a confirmação mais limpa*: as **dez** filas `defer_*` que PASSARAM em Windows entram no
  runtime por `tk_panic_str` — que **está** na lista dos sete. As que passam são as cobertas; a que
  falha é a primeira não coberta. `alias_fat_field (own-native)` é a primeira fila own-native de
  `regressor.tkr` que compara duas `str`.

**O que fica EXCLUÍDO por medição, e não é pouco:** não é o arranque do processo, não é a entrada
sintetizada, não é o alinhamento de pilha do prólogo, não são secções/relocações do PE, e não é
compilação — `own_native.exe` compilou 1301 s e correu, e as 13 filas own-native anteriores
(argv, qualifier, defer) correram e passaram com o MESMO emissor, a MESMA entrada e o MESMO objecto
COFF. Também não é a família `executable_suffix`/`binary_output_path`/`sibling_object_path` de
`cff49b4`: um binário obsoleto não escolheria justamente a fila que compara strings.

**A CORRECÇÃO, e porque NÃO a empurrei.** O desenho certo é generalizar a correcção em vez de a alargar
por lista: em `isel_x86_64.tks`, quando `max_reg_arg_bytes < 16`, materializar CADA par gordo do
argumento no seu próprio slot de 16 bytes e pinar só os endereços — o que cobre a forma de 1 par (o que
já existe), a de 2 pares, e a de 2 pares + escalar do `tk_rt_last_index_of_ok`, com a lista de aridade
gorda por símbolo em `lower.tks` ao lado de `is_str_arg_builtin` (fonte única, como hoje). O caminho
SysV fica intocado pela guarda `max_reg_arg_bytes < 16`, logo o fixpoint não se move.

Não a empurrei porque **não tenho host Windows para a validar**, e uma mudança de ABI por raciocínio
numa perna já vermelha é exactamente o palpite empurrado que o brief proíbe. O que a decide numa
corrida: implementar a generalização e ver `alias_fat_field (own-native)` passar em Windows **e** as
quatro filas que hoje nunca correm (`alias_fat_field (C route)`, `variant_member_compare (C route)` e o
par `byte-view round-trip`) passarem a correr. Prova host-independente disponível em Linux enquanto
isso: `isel_x86_64_test.tkt` já tem um descritor `WIN64` (linha 482) e pode afirmar a sequência emitida
para `tk_str_eq` sem runner nenhum.

**A CONTRAPROVA, medida em Linux na gen2 desta branch:** `teko: regression ok regressor.tkr (18
builds, 14.1s)` — **18**, com `alias_fat_field (own-native)` E `byte-view round-trip (own-native)` nas
filas. Windows fez **14** e parou. O mesmo ficheiro, o mesmo compilador, dois números: a diferença não
é o que cada perna tem para correr, é onde cada perna PARA. Nada em `regressor.tkr` é saltado em
Windows por capacidade — é o corte da primeira falha.

**E uma lição de instrumento, que é minha:** `(N builds)` no relatório do regressor é o número de filas
que CORRERAM, não o número de filas do ficheiro. Comparar esse N entre pernas — ou com o `grep -c "When
built and run"` do `.tkr` — diz de graça quantas filas ficaram por correr, e foi só essa subtracção que
separou "duas features estouram por acaso" de "uma causa, e a segunda fixtura nem chegou a ser medida".

## 0e. O `.exe` FECHADO — e o brief que eu escrevi estava incompleto (2026-07-30, `cff49b4`)

Eu medi **dois** sítios que nomeavam o executável (`project.tks:1827` e `:2635`) e escrevi o brief sobre
eles. **São NOVE.** O agente enumerou-os, e cinco dos sete que eu não vi **teriam ficado inlançáveis em
Windows** pela mesma regra do loader: `run_native_gate`, `run_project`, `run_analyzer`,
`run_one_test_cov`, `build_regression_cov_exe`. Consertar só os meus dois seria o defeito "um dos membros
da família" — no brief onde eu próprio invoquei esse corolário.

**A generalização que ele fez e eu não tinha visto:** a regra chaveia-se no **FORMATO DE IMAGEM**
(`target_objfmt`), não no SO — *"`.exe` não é um hábito do Windows, é como um PE se nomeia para o loader
o achar"*. Isso absorveu **de graça** um terceiro nome montado à mão, o `.wasm` de `emit_native_wasm`, e
o próximo alvo que emita PE ou wasm herda o nome certo sem segunda decisão.

**E apanhou o efeito de segunda ordem que eu não previ:** `tkr_run_one_row` fazia
`check_object_wellformed(binp ~ ".o")` — com `.exe` isso pediria `bin/snippet.exe.o` e faria uma build
**perfeita** reportar artefacto malformado. Resolvido com um `sibling_object_path` que **substitui** a
extensão em vez de a concatenar.

### CORRECÇÃO À MINHA FILA — `own_cross_x86_64_windows_emits_coff` NÃO é uma falha

Eu listei-a como item da fila. **Não é:** a linha `own_arith_exit` é a primeira do canal, a feature pára
na primeira falha, e **`own_cross_x86_64_windows_emits_coff` nunca é alcançada**. Não falha — **não
corre**. Sai da fila; entra como consequência do §0d.

**Isso torna o §0d mais sério do que o vermelho sugere:** um canal que reporta pela linha errada faz
todo agente que o leia tirar a conclusão errada, e eu fi-lo duas vezes (atribuí à `A4-fp` do arm64 e à
corrupção de ambiente de um agente). Despachado com mandato de **bissetar antes de consertar** e de
**não tocar na cobertura** — o defeito é a composição do build, não as fixtures.

### Um achado adjacente que fica registado, não corrigido

`emitted-C identity: gen2.c != gen3.c` (10 719 554 vs 10 719 618 bytes) **com binários idênticos**, e
presente **também na base**. A diferença medida é `double ceiling = 5;` contra `5.0` mais deslocamento de
gensym: **gen1 (da semente) e gen2 (da árvore) diferem como GERADORES**, o que é a forma saudável sob
esta cadeia. O veredito pinado — binário `gen2 == gen3` — passa. **Não é regressão**, e vale saber antes
que alguém o descubra e assuste.

## 0d. `unknown function: f_*` É REAL NO CI — e eu descartei o relato do agente (2026-07-30)

**Correcção a mim, e é a segunda vez hoje que dispenso um agente depressa demais.**

O agente dos dourados reportou, como red-flag, que `own_native.tkr` falhava a **compilar** com dezenas de
`main.tks:NN: unknown function: f_*`, e não pela `A4-fp` documentada. Eu atribuí-o à corrupção de
ambiente que ele próprio tinha reportado (outro agente transformou o binário dele num directório) e
escrevi que *"o CI não mostra `unknown function` em sítio nenhum"*.

**Medi. Está no CI, na execução 30524751917 (`e317b44`), em todas as pernas:**

```
unknown function: f_append_fo_bulk_bytes   f_append_fo_grow_chain   f_append_fo_interleaved_buffers
unknown function: f_arm64_bigframe_locals  f_cast_narrow_in_range_keeps_value   … (dezenas)
```

**Ele estava certo. Eu estava errado, e por um raciocínio errado:** a corrupção do ambiente dele
explicava *um* sintoma, e eu usei-a para explicar *outro* sem o medir.

### O QUE JÁ ESTÁ MEDIDO, e o que fica de fora

| medição | resultado |
| --- | --- |
| chamadas no `main` sem definição no corpus | **zero** (119 chamadas, 120 definições; a sobra é `f_fat_field_len`) |
| visibilidade | **todos os 120 são não-`pub`, e sempre foram** — o `main` chama-os nus e isso funcionou meses. **Não é regressão de visibilidade** |
| `A4-fp: float-op` | **já NÃO aparece** nesta execução — a falha do `own_native` mudou de carácter |
| fase unitária | **verde: 1131 `ok` nas três pernas, zero pânicos** (o conserto dos dourados funcionou) |

**Logo a causa não está na árvore de fontes — está em COMO o build que falha é composto.** O suspeito
principal, e é o que a investigação deve atacar primeiro: as linhas com **`Given source = "cases/X.tks"`**.
Se o harness **acrescenta** o ficheiro de caso ao conjunto de fontes em vez de o **substituir**, então o
`main.tks` do projeto — que chama os 120 `f_*` — entra no build junto com um único ficheiro de caso, e
**todas** as chamadas ficam pendentes. Isso explicaria a cascata inteira e o facto de a mensagem citar
`main.tks`.

**Quatro linhas novas de `cases/` entraram hoje** (duas do degrau 28, duas da leitura fora de fronteira),
o que é consistente com a falha ter mudado de carácter exactamente agora.

### A LIÇÃO, e é a mesma nas duas vezes

**Explicar um sintoma não explica os outros.** Quando um agente reporta duas anomalias e uma delas tem
causa conhecida, a segunda **continua por medir**. E quando um agente contradiz o CI, o resultado é uma
**discrepância a medir** — não um lado em que acreditar. Já escrevi esta lição hoje em §2b, e voltei a
falhá-la.

**NÃO OWNED. É o próximo despacho quando abrir vaga**, e tem prioridade sobre a fila anterior: uma
falha de composição de build faz um canal inteiro reportar por uma razão que não é a sua, e isso engana
todo agente que a leia.

## 0c. DUAS CONFERÊNCIAS QUE FALTAVAM, e um perigo do trabalho paralelo (2026-07-30)

### A minha lista de cinco conferências tinha um BURACO DE DIRECÇÃO

Eu verificava *"`fn f_*` definidas e NÃO chamadas"* — e **nunca o inverso**. Um agente reportou
`main.tks:NN: unknown function: f_*` em cascata, que é **exactamente a direcção que eu não media**: o
`main` a chamar o que o corpus não define. Se uma união tivesse perdido definições, a minha lista teria
dito "tudo bem".

**Medido depois de o levantar: 119 chamadas, 120 definições, zero chamadas sem definição** (a sobra é
`f_fat_field_len`, a excepção conhecida). A árvore está consistente — mas **a conferência entra na lista
de qualquer forma**, porque a ausência dela era sorte, não método:

```
comm -23 <(grep -oh 'f_[a-z0-9_]*' main.tks | sort -u) \
         <(grep -oh '^fn f_[a-z0-9_]*\|^pub fn f_[a-z0-9_]*' src/corpus.tks | sed 's/^pub //;s/^fn //' | sort -u)
```

**A regra geral, que vale para além deste ficheiro:** uma conferência de correspondência entre dois
conjuntos tem de correr **nas duas direcções**. Verificar só um lado é meia medição, e a metade que falta
é sempre a que morde.

### E O QUE EXPLICOU O RELATO DELE — o perigo do scratchpad partilhado

O mesmo agente reportou, como red-flag 1, que **outro agente vivo transformou o binário dele num
directório** a meio da corrida: construiu com `-o <scratchpad>/gen2/teko` e o `gen2/teko` que era
FICHEIRO passou a ser DIRECTÓRIO (`rc=126, Is a directory`). Ele reconstruiu num sítio privado.

**O `unknown function` dele é quase certamente consequência disso**, e não da árvore: o CI, sobre o mesmo
SHA, não mostra `unknown function` em sítio nenhum — mostra `A4-fp` (arm64) e
`own_cross_x86_64_windows_emits_coff` (x86-64).

**REGRA NOVA PARA TODO BRIEF:** o scratchpad da sessão é **partilhado** entre agentes vivos. Cada agente
constrói num **subdirectório próprio e nomeado por si** (`<scratchpad>/<nome-do-agente>/…`), nunca num
caminho genérico como `gen2/`. E **um relatório de agente que descreve corrupção de ambiente tem de ser
lido à luz dela** — o que ele viu depois pode ser sintoma, não causa. Eu quase tratei a red-flag 2 dele
como defeito da árvore.

## 0b. AS DUAS CONSEQUÊNCIAS DO DRENO SEM RELATÓRIOS — ambas previstas, ambas materializadas

Eu drenei quatro branches sem relatório, **escrevi que o CI seria o árbitro**, e o CI cobrou as duas
coisas que faltavam. Registadas porque cada uma tem uma lição que não é sobre estas branches.

### CONSEQUÊNCIA 1 — um valor por omissão resolve-se no CHAMADOR (24 jobs vermelhos por um token)

`call_inst` ganhou `ret_type: LType = LType::I64`. Dentro de `teko::lir` compila; mas o **valor** por
omissão é materializado em **cada sítio de chamada**, e **dez ficheiros fora de `teko::lir`** chamam-na.
Consertado em `fbbed32b` qualificando o default. A regra está no digesto.

**E o diagnóstico apontou o ficheiro errado.** Dizia `isel_arm64_test.tkt:397:112`; esse ficheiro está
**intacto** e a sua linha 397 tem **18 caracteres**. `397:112` é a posição do `LType::I64` em
**`src/lir/lir.tks`** — a mensagem junta o **ficheiro do chamador** com a **linha:coluna da declaração**.
Gastei várias medições a confirmar que o ficheiro acusado estava limpo. **Achado a corrigir** quando esta
família for tocada.

### CONSEQUÊNCIA 2 — dois agentes no mesmo comportamento, em direcções opostas, sem se verem

- Um fixou de manhã um **texto dourado** do LIR para a leitura indexada de `str`, com **igualdade de
  texto inteiro**, e escreveu no doc-comment que a forma forte foi escolhida para não passar se o
  lowering deixasse de produzir texto.
- O outro, à tarde, **acrescentou guarda de fronteira à LEITURA** de elemento (`icmp`/`branch`/
  `tk_panic_oob_at` antes do load) — porque o nativo devolvia lixo onde a rota C panicava.

**As duas mudanças estão certas. A expectativa envelheceu em horas**, e `lwt_lowers_str_index_loads_the_byte_off_rodata`
aborta com SIGABRT — o que mata **todas** as pernas `test` e `Memory paranoid`, porque a fase unitária
pára no primeiro `assert` falhado.

**A lição que interessa, e não é "usem dourados mais fracos":** foi precisamente a **força** do dourado
que apanhou isto em horas em vez de meses. Enfraquecê-lo seria trocar detecção por conforto. O que
falta é **coordenação**: dois agentes cujo trabalho se cruza no mesmo comportamento têm de saber um do
outro, e **é o integrador que o sabe** — a colisão declarada no brief cobria FICHEIROS
(`src/lir/lower.tks`), e estes dois nem partilhavam ficheiro: um mexeu no lowering, o outro no `.tkt`.

**Regra nova para os briefs: declarar a colisão por COMPORTAMENTO, não só por ficheiro.** "Alguém está a
mudar o que a leitura indexada emite" é a informação que faltava, e nenhum dos dois a teve.

## 0a. REINÍCIO DO CONTENTOR — 2026-07-30 ~06:50 UTC, e o que se salvou

**Os CINCO agentes morreram com o contentor, e todos os worktrees (`/home/user/wt-*`) desapareceram.**
A lei de *"escreveu? comita e empurra JÁ"* pagou-se: **quatro das cinco branches tinham trabalho
empurrado e completo**. A quinta — o `.exe` — **não tinha branch nenhuma**, logo esse trabalho está
perdido por inteiro. Foi a mais recente (despachada ~05:43), e o brief está pronto em §3d.

**Drenei as quatro num só ciclo** (`9de67a6` → `ffe7580`, **27 ficheiros, +1700/−149**):
`gemeo-macos`, `kind-desconhecido-panica`, `degrau-27`, `leitura-fora-de-fronteira`.

**NENHUMA TRAZIA RELATÓRIO**, portanto o ritual não foi confirmado por elas nem por mim: verifiquei
estrutura e deixei o CI ser o árbitro. Isso está dito aqui de propósito — se algo estourar, é este o
sítio onde a razão está escrita.

### O QUE OS COMMITS DELAS REVELARAM, e duas coisas corrigem-me

**1. O gémeo de macOS NÃO era um gémeo divergido, e o erro era meu.** Eu escrevi que
`pt_target_name_and_objfmt_are_one_source` era *"inteiramente independente do host"* e que, portanto,
falhar num só host **tinha** de ser divergência de geração. **Falso.** A assertiva que caía era

```teko
teko::assert::is_true(target_links_with_cc(NativeTarget::X8664Linux))
```

e `target_links_with_cc` → `links_with_cc` → **`cross_note`**, que compara com
`host_default_target_guess()`. Em arm64-macos o anfitrião é `Arm64Macho`, logo `x86_64-linux` **é**
cruzado, uma build cruzada para no objecto, e a resposta honesta é **`false`**.

**Eu li os SÍTIOS DE CHAMADA como literais e não segui a FUNÇÃO CHAMADA até ao estado do host.** É a
mesma classe do meu erro do `sed -E`: afirmei uma causa com confiança sem seguir a cadeia até ao fim.

**E o raio estava a crescer:** a assertiva só sobrevivia porque a adivinha antiga, só de SO, respondia
`X8664Linux` para **qualquer** anfitrião Linux. Desde que `host_target_for_os` aprendeu a arquitectura,
**`arm64-linux` junta-se a `arm64-macos` a refutá-la** — a mesma assertiva ia começar a cair também na
perna `linux-arm64-glibc`. O conserto chegou antes de o segundo host a apanhar.

**2. Um TERCEIRO valor errado calado da mesma classe, achado pela varredura de irmãos do degrau 27.**
Todo renderer `tk_*` declara parâmetro `double`, mas um buraco `f32` guarda **precisão simples**, e
**nada o alargava**: a rota C recebia o `(double)` implícito do `cc`, o backend próprio **não recebia
nada**. Medido no mesmo programa:

| forma | rota C | backend próprio |
| --- | --- | --- |
| `$"{f:F2}"` com `f: f32 = 2.5` | `2.50` | **`0.10`** |
| `$"{f:G}"` | `2.5` | **`4.65274e-310`** |

**Verde desde o degrau 19.** E o par de constantes da fixture é escolhido: `2.5` é exactamente
representável nas duas larguras, `0.1` não — logo uma rota que acerta numa e erra na outra está a ler o
registo na largura errada. Veio junto `fix(lir,backend): o LCall passa a registar a classe do resultado
— o buraco que faltava do B1-fp`.

### O QUE APRENDI A DRENAR, e custou-me dois danos no mesmo passo

Ao resolver os conflitos aditivos do corpus, fiz **duas coisas erradas seguidas**:

1. **Um script meu truncou `examples/regressions/own_native/main.tks` a ZERO bytes** — abriu o ficheiro
   em `'w'` e só **depois** falhou na escrita (tinha invertido a ordem de `re.subn`, que devolve
   `(texto, contagem)`). O `git merge --abort` restaurou tudo. **Regra: nunca abrir em `'w'` antes de o
   novo conteúdo estar calculado e validado.**
2. **A minha resolução por regex ficou desequilibrada.** Refiz com `git merge-file --union`, que é a
   resolução correcta para um conflito puramente aditivo, e é do git em vez de minha.

**E a verificação que eu usava estava ERRADA para estes ficheiros.** O handoff mandava contar
`{`/`}` — mas `main.tks` tem **231 chaves em 117 linhas**, porque conta as da interpolação `$"{...}"`.
O contador é ruído aqui, e eu quase abortei um dreno são por causa dele.

**As verificações que DE FACTO detectam uma união má, e são estas que ficam:**

| conferência | porquê |
| --- | --- |
| `fn`/`const` **duplicados** no corpus | é o sintoma directo de uma união que duplicou um bloco |
| chamadas `if f_*()` duplicadas no `main` | idem, do outro lado |
| **códigos de saída** duplicados | duas fixtures a reclamar o mesmo número |
| linhas `Scenario:` duplicadas, **chave = a linha INTEIRA** | um detector por primeiro token já deu falso positivo aqui e quase apagou metade da cobertura de cast |
| `fn f_*` definidas e **não chamadas** pelo `main` | excepção conhecida e única: `f_fat_field_len` |

Todas passaram. **Faixas de saída, medidas e sem colisão:** 213–215 (degrau 28), 220–225 (degrau 27),
230–234 (leitura fora de fronteira).


### O DREGRAU 27 FECHOU, E A ESCADA AVANÇOU — medido no CI, SHA `ffe7580`

A paragem do `ftoa` **desapareceu**. O compilador nativo atravessa agora, sem parar:

```
lexer 143/143 ✓   parser 143/143 ✓   checker 6449/6449 ✓   monomorph 0/0 ✓   consteval 571/571 ✓
teko: .: native backend N1: `null` match pattern not yet lowered (N2) [in `teko::codegen::emit_variant_wrap`]
```

**Isto prova duas coisas ao mesmo tempo:** o degrau 27 está fechado, e **o meu dreno de quatro branches
sem relatório está são pelo menos até ao fim do front-end** — o checker passou de 6406 para 6449 itens
(as fixtures novas) e todas as fases passam.

**Degrau 30 é agora o ÚNICO obstáculo entre a lane e uma gen2 nativa, que nunca existiu.** É a mesma
família que os degraus 25 e o arco `null-adopt` já tocaram, portanto há molde.

## 0. MODO AUTÓNOMO — 2026-07-30, o dono foi dormir

*"Vou dormir, te deixo no modo autônomo, tem bastante trabalho por aí."*

**O que o integrador faz enquanto ele dorme:** drenar agentes que terminam, despachar da fila ao teto de
4, verificar CI pelo log integral (`scripts/ci_full_log.sh`), e manter este documento vivo. **Não** promove
ao tronco, **não** faz bump de versão, **não** fecha a lane — isso é dele.

### CINCO AGENTES A CORRER (teto 4, um a mais por ordem explícita)

| agente | branch | porquê |
| --- | --- | --- |
| **degrau 27 — `ftoa`** | `cargo/0.3.1.0-degrau-27` | a paragem viva do self-host; destranca o ponto de fixo **e** torna efectiva a lei da emissão nativa |
| **último abort unitário (`zext`)** | `cargo/0.3.1-zext-expectativa` | provado por sonda que é o ÚLTIMO: sem ele, **1117** testes arrancam e a fase unitária fica verde |
| **leitura fora de fronteira** | `cargo/0.3.1-leitura-fora-de-fronteira` | divergência medida: nativo devolve lixo, rota C panica. Valor errado calado |
| **gémeo de macOS** | `cargo/0.3.1-gemeo-macos` | teste sem dependência do host que falha só em macOS |
| **`kind` desconhecido panica** | `cargo/0.3.1-kind-desconhecido-panica` | **5º por ordem directa**: *"precisa de correção já, ou estaremos ferindo nossas leis"* |

**Quando o do `kind` terminar, voltar ao teto de 4.**

### FILA, por valor

1. **`.exe` no Windows** — brief pronto e MEDIDO, ver §3d. Dois sítios de chamada, e o desenho certo já existe no mesmo ficheiro.
2. **Terceira passagem do documento do `tdb`** — em forma de **PROPOSTA** (lei nova de forma), com interop fora, alvo *"a melhor experiência de dev"*, e "SEM C LANG".
3. **`kind = "tool"`** — **BLOQUEADO** pelo portão do `tdb` (proposta, não entra nesta versão nem na seguinte).
4. **As duas regressões "expected a compile failure but the build succeeded"** (`native_iface_fat_known_stop.tkr`, `diagnostics.tkr`) — **atenção: é KNOWN-STOP a ficar vermelho, e pela lei do dono isso NÃO significa necessariamente que o defeito foi corrigido; pode significar que uma GUARDA se perdeu.** Não owned. Vale investigar.
5. `own_native.tkr → own_cross_x86_64_windows_emits_coff` — o `cc` falha no C gerado. Não owned.


### MEDIÇÕES DA PRIMEIRA EXECUÇÃO COM `Arm64Linux` (SHA `ebfb6be8`, perna macOS)

Quatro coisas, e três são notícia boa:

1. **Degrau 28 FECHADO no CI** — `slice element index-assignment` já não aparece. O 29 apareceu no seu lugar,
   que é o comportamento esperado de uma escada.
2. **`regressions 10 run, 0 skipped, 1 failed`** — **ZERO skips.** Os 21 skips da perna arm64 eram todos
   `unsupported TEKO_TARGET "arm64-linux"` e desapareceram com o crumb 3, **sem tocar em CI**. Confirma a
   decisão de não aplicar o crumb 5.
3. **A pergunta do agente do AArch64-ELF está RESPONDIDA:** a linha nova `own_cross_arm64_linux_emits_elf`
   **não saltou** em `test / macos-arm64`, logo o host macOS **tem** desmontador e religador LLVM
   cross-capable. **Nenhum provisionamento é necessário.**
4. **CORRECÇÃO À MINHA PRÓPRIA FILA, e importa:** as duas regressões que dois agentes reportaram como
   *"expected a compile failure but the build succeeded"* — `native_iface_fat_known_stop.tkr` e
   `diagnostics.tkr` — estão **`regression ok` no CI**. Não reproduzem. A causa provável é o compilador
   que os agentes semearam à mão de `bootstrap/teko.c` (porque `fetch_teko.sh` falha nesta sessão) diferir
   do que o CI usa. **Portanto NÃO despachar "guardas perdidas" — não há prova de que exista guarda
   perdida.** O que existe é uma discrepância entre a escada local dos agentes e a do CI, e isso é o
   achado a registar. Prioridade da fila baixa de 2 para o fim.

### CORRECÇÃO À CORRECÇÃO — três agentes contra uma leitura de CI, e eu dispensei depressa demais

Escrevi acima *"NÃO despachar guardas perdidas — não há prova de que exista guarda perdida"*. **Isso foi
prematuro.** Contagem actual: **três agentes independentes** (`zext`, `cast-narrow`, e o do degrau 28)
reportam as mesmas duas linhas a falhar, com a mesma mensagem, em corridas separadas:

```
native_iface_fat_known_stop.tkr → "expected a compile failure but the build succeeded"
diagnostics.tkr                 → "expected a compile failure but the build succeeded"
```

E o CI mostra `regression ok` para as duas. **Três observações concordantes não são ruído.** O que eu tenho
não é "nenhuma prova de defeito" — é **uma discrepância reproduzível**, e essa é a coisa a investigar.

### O EIXO PROVÁVEL, e é verificável com um comando

**O CI e os agentes não correm a suíte com a MESMA geração.** As pernas `test` correm o **asset publicado**
(que é a **gen1**, produzida por `produce_assets.sh`) sobre a árvore. Os agentes correm a **gen2** que
construíram. E `scripts/fixpoint_gate.sh` **assere `gen2 == gen3`, nunca `gen1 == gen2`** — o próprio
cabeçalho di-lo, e com razão: sob a cadeia 0.3.1.0 a gen1 vem de um gerador diferente, logo `gen1 != gen2`
é a forma saudável.

**Consequência que ninguém escreveu ainda:** se a gen1 e a gen2 divergirem em **comportamento** (não só em
bytes), as pernas `test` medem a gen1 e ninguém mede a gen2 — e uma rejeição que só a gen2 perde é
**invisível ao CI por construção**. Isso é um buraco de cobertura, não um defeito de fixture.

**O PASSO que o fecha** (e é um passo, não um alarme — lei de forma do dono): correr as duas linhas com a
**gen1** e com a **gen2** da MESMA árvore e comparar. Três resultados possíveis, e cada um diz o que fazer:

| resultado | significado |
| --- | --- |
| gen1 rejeita, gen2 **não** | **a gen2 perdeu a guarda.** É defeito real e o CI não o vê. O mais grave dos três |
| as duas rejeitam | o que os agentes viram vem da semente deles (`bootstrap/teko.c`), não da gen2 — e aí a lição da semente aplica-se |
| nenhuma rejeita | o CI está a medir outra coisa, e a pergunta muda para *o que o asset publicado é de facto* |

**Custo: uma escada, que o agente já constrói de qualquer maneira.** É o próximo despacho depois do `.exe`.

**A LIÇÃO, e é geral:** um agente que semeia de `bootstrap/teko.c` está a construir a partir da SAÍDA
desta árvore, não do release. As falhas que ele vê e o CI não vê podem ser artefactos dessa diferença —
como já aconteceu hoje com "três erros de tipo" que eram do binário obsoleto. **Um relatório de agente
que nomeia uma regressão tem de dizer com que semente correu**, e o integrador tem de a confrontar com
o CI antes de a pôr na fila. **Mas confrontar não é dispensar:** quando o agente e o CI discordam, o
resultado é uma DISCREPÂNCIA a medir, não um lado a acreditar. Eu fiz as duas coisas erradas em sequência —
primeiro aceitei sem confrontar, depois dispensei sem medir.



### ESTADO MEDIDO DO VAGÃO — topo `2d65bb87`, execução 30515067207 (log integral)

**O que MELHOROU hoje, medido e não suposto:**

| | antes | agora |
| --- | --- | --- |
| fase de testes unitários | abortava com SIGABRT; **269 dos 1117** nunca arrancavam | **1117/1117, zero pânicos** em todas as pernas Linux |
| `assertion failed: str_contains` | em duas pernas | **desapareceu** |
| degrau 28 (`s[i] = v`) | vermelho em **todas** as pernas | **fechado** |
| `LNK1120`/`LNK2019` (Windows, 128 bits) | matava a perna `artifact` | **zero** |
| skips | **21** na perna arm64-glibc | **`0 skipped` em TODAS as pernas** |
| pernas do Windows | nunca corriam (sem asset) | correm — e destaparam o `.exe` |

**OS TRÊS VERMELHOS QUE RESTAM, todos com dono:**

| vermelho | onde | quem |
| --- | --- | --- |
| `assertion failed: is_true` | `pt_target_name_and_objfmt_are_one_source`, **só macOS** | agente vivo (gémeo) |
| `native backend N1: builtin ftoa` | `teko::codegen::cb_f64_literal` — a paragem do self-host | agente vivo (degrau 27) |
| `A4-fp: float-op / FPR encoding deferred to 0.3.1` | `own_arith_exit`, arm64 | **degrau 29, na fila** |
| `ERROR: … no dl/windows-x86_64/teko.exe` | `src/build/project.tks:1827` e `:2635` | agente vivo (`.exe`) |

**CINCO AGENTES VIVOS** (todos com escrita recente): degrau 27, leitura fora de fronteira, gémeo de macOS,
`kind` desconhecido, `.exe`. **Teto é 4** — quando dois fecharem, repor só um.

### DISCIPLINA DE PUSH — medida em 2026-07-30, e o defeito era meu

**Medido:** das últimas oito execuções de `pr.yml` no vagão, **sete estavam `cancelled`**. A única com
veredito era `ebfb6be8`, muito atrás do topo. Eu estava a ler CI de uma execução velha sem perceber porquê.

**A CAUSA, e não é intermitência do GitHub.** `pr.yml:219-221`:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number }}
  cancel-in-progress: false
```

Com `cancel-in-progress: false`, o grupo mantém **uma** execução a correr e **uma** pendente. Uma terceira
que chegue **cancela a pendente**. Logo cada push meu deslocava a que estava à espera, e só a que já corria
chegava a veredito.

**A consequência é pior que atraso: é CEGUEIRA.** Um dreno de produto empurrado entre dois commits de
documentação pode nunca ser medido, porque o push seguinte cancela a sua execução pendente. **Um dreno que
ninguém correu é exactamente o "verde sobre linha não executada" que esta lane persegue** — na outra ponta.

**REGRA ADOPTADA, e vale para qualquer sessão:**

- **um push por ciclo**, não um por commit. Comitar localmente à vontade; empurrar uma vez.
- **um dreno de produto empurra-se SOZINHO**, e espera-se pelo seu veredito antes de empurrar documentação
  por cima. O que precisa de CI tem prioridade no canal.
- **antes de ler CI, confirmar que a execução escolhida NÃO é `cancelled`** — uma `cancelled` não tem
  veredito e ler-lhe as partes que correram é tirar conclusão de meia medição.

### CRUMB 5 do AArch64-ELF — NÃO APLICADO, e a razão é medição, não preguiça

O agente deixou-mo por ser workflow (só o integrador toca `.github/workflows/`). **Medi antes de aplicar, e
ele ficou em grande parte OBSOLETO pelo próprio dreno do crumb 3:**

- os **21 skips** da perna `linux-arm64-glibc` eram **todos** `unsupported TEKO_TARGET "arm64-linux"`. Com
  `Arm64Linux` a existir, vão a **zero** sem tocar em CI. A metade valiosa do crumb 5 aconteceu sozinha.
- o que sobraria era acrescentar `no_skips_gate.sh` + provisionar wasmtime aarch64. **E aí colide:**
  `scripts/no_skips_gate.sh` rejeita **qualquer** skip, incluindo a linha wasm — logo, sem wasmtime, a perna
  ficaria vermelha pela linha wasm. **Mas pôr wasmtime numa perna de teste faz `scripts/wasm_known_stop_gate.sh`
  ficar VERMELHO por desenho** (ele assere que existe **exactamente um** provedor de motor wasm, o
  `regressor-full`), e retirar esse pin é a *promoção* que o dono ruleou ser trabalho da versão dedicada do
  wasm: *"KNOWN-STOP, wasm terá a própria versão para refinar."*

**Portanto é uma colisão entre dois rulings do dono** (skip é falha × wasm refina na sua versão), e negociação
de KNOWN-STOP é **dono↔integrador**, nunca de agente. **Fica para ele decidir, com o número na mão:** depois do
crumb 3, quantos skips restam de facto na perna arm64? Se for **só a linha wasm**, o pin já cobre e não há nada
a fazer. **A próxima execução do CI sobre `36b2ab45` ou posterior responde** — é a primeira com `Arm64Linux`.

### O PATCH DO AGENTE **NÃO** DEVE SER APLICADO VERBATIM, se algum dia entrar

Ele propôs `run: … teko test . 2>&1 | tee teko-test.log`. Isso **reintroduz** o defeito que custou a esta lane
um `exit 127` opaco no Windows: os passos correm com `-e -o pipefail`, e sem `set +e` o teste que falha mata o
passo antes do gate. E `rc=$?` depois de um pipe dá o estado do **último** comando do pipe. A forma correcta
está no passo do Windows em `pr.yml`: `set +e` → comando → `rc=$?` → `set -e` → `cat` → gate.

## 1. A escada de degraus — onde está

Cada paragem do backend nativo é um "degrau". A escada é o produto desta lane: enquanto ela não
fechar, o ponto de fixo nativo não fecha e as duas pernas nativas ficam vermelhas **por desenho**.

| degrau | o quê | estado |
| --- | --- | --- |
| 24 | `f64_bits`/`f64_from_bits` — alias do próprio VReg | **fechado**, confirmado no CI |
| 25 | união-nula em colocações sem tipo declarado | **fechado**, confirmado no CI |
| 26 | `append_fo` sem lowering, em `teko::codegen::cb` | **fechado e DRENADO** — confirmado: já não aparece |
| **30** | **padrão `null` num `match` sem lowering**, em `teko::codegen::emit_variant_wrap` | **ABERTO — é a paragem VIVA do self-host.** Agente vivo. É o **único obstáculo** entre nós e uma gen2 nativa |
| **29** | **`A4-fp`: codificação de float/FPR em arm64**, em `own_arith_exit` | **ABERTO** — o gémeo arm64 do arco `b1-fp-x86`. Na fila |
| 27 | builtin `ftoa` sem lowering, em `teko::codegen::cb_f64_literal` | **FECHADO e DRENADO** (`ffe7580`) — aguarda confirmação de CI. Trouxe consigo o `f32` calado dos renderers |
| **29** | **`A4-fp`: codificação de operação de float / FPR em arm64**, em `own_arith_exit` | **ABERTO — descoberto ao drenar o 28.** É o **gémeo arm64** do arco `b1-fp-x86`, que fechou os floats só para x86-64 |
| 28 | atribuição a elemento de slice (`s[i] = v`) sem lowering | **FECHADO e DRENADO** (`36b2ab45`) — confirmado no CI: já não aparece. Foi regressão do meu dreno |

Texto exacto das duas, do log completo (§2c):

```
teko: .: native backend N1: builtin `ftoa` not yet lowered (N2) [in `teko::codegen::cb_f64_literal`]
teko: examples/regressions/own_native: native backend N1: slice element index-assignment not yet lowered (N2) [in `own_native::f_implicit_widen_targets`]
```

**O 28 é um caso de escola, e o erro é meu.** `f_implicit_widen_targets` é a fixture do alargamento
implícito que veio do arco da **aridade numérica** — que eu drenei. Ela escreve num elemento de slice,
e o backend nativo não sabe lá chegar. Ou seja: **a fixture que provava a aridade é ela própria fora
do alcance do backend**, e eu drenei-a sem que nenhuma perna nativa a tivesse compilado. É a SEGUNDA
VAGA a chegar exactamente onde estava avisado — e chegou por um dreno meu, não por descoberta do
self-build. **Não a "conserte" mudando a fixture para evitar o slice:** isso troca uma paragem honesta
por cobertura fingida. `s[i] = v` é linguagem corrente; o lowering é que falta.

**A SEGUNDA VAGA, e não a esqueça:** os degraus são só o que o SELF-BUILD encontra. O corpus, as
regressões e os `.tkt` **nunca** foram compilados pelo backend nativo em CI, porque o ponto de fixo
falha antes do job `test`. **Não prometa "faltam N degraus".** O degrau 28 é a prova: apareceu sem
que o self-build tenha avançado um passo.

## 2. AS CINCO BRANCHES — TODAS DRENADAS (2026-07-30, depois do segundo reinício)

**Estado: DRENADAS.** O vagão está em `8f94c0b1` e as cinco entraram, nesta ordem: degrau 26,
`b1-fp-x86`, `saida-equalizada`, `aridade-numerica`, `mingw-fora-da-rota-c`. O CI deste SHA é a
primeira corrida em que o ponto de fixo nativo pode passar do degrau 26 **e** as três pernas que
tinham `B1-args` podem ficar verdes de uma vez.

**Depois do dreno, também feito:** mingw removido dos DOIS workflows (nenhuma invocação resta), com o
ruling de 2026-07-27 registado como **superseded por medição** em vez de apagado.

### TRÊS COISAS APRENDIDAS NO DRENO, que valem mais que ele

1. **O conflito em `lower_cast` tinha as duas resoluções ingénuas ERRADAS.** `cast_unop_of` mudou de
   assinatura numa das branches (passou a devolver `LUnOp | error`). "Ficar com o nosso" **não
   compilaria** (o vagão chamava-o inline); "ficar com o deles" **perderia calada** a correção do
   valor errado do `f32`. Eram duas guardas **disjuntas** de dois agentes no mesmo dispatch, e ambas
   tinham de sobreviver. Só apareceu por ler as TRÊS versões da função (base, vagão, branch) em vez de
   compor à vista.
2. **A conferência de integridade deu um FALSO POSITIVO que quase apagou cobertura.** Acusou três
   cenários duplicados no `.tkr`; eram os pares `own-native` + `C route`, que é o padrão do projeto, e
   o detetor colapsava-os por apanhar só o primeiro token do `Scenario:`. **Chave correta é a LINHA
   inteira do cenário.**
3. **"Manter os dois lados" num conflito aditivo duplica o que o git já juntou FORA do hunk.** Foi o
   que aconteceu, e foi a conferência de marcadores que apanhou (`main.tks` ainda tinha `<<<<<<<`
   depois de eu ter dado o merge por resolvido, porque o `tail -6` do output do merge me escondeu dois
   conflitos).

### O QUE ENTROU EM CADA UMA (para o histórico)

Os SHAs abaixo são os das branches **como estavam ao serem drenadas**. Todas estavam integralmente
empurradas quando o container reiniciou — zero commits à frente do remoto, verificado um por um. **Os
agentes morreram com o container; o trabalho não, e foi a regra de "empurrar sempre" que o garantiu.**

### `cargo/0.3.1-aridade-numerica` @ `f1ee57df` — 19 ficheiros, +1290
Ruling do dono: menor cabe em maior sem cast; estreitamento **nunca trunca** e panica em runtime só
se não couber; `teko::casting` é a forma recuperável (`T | error`).
- `fix(nativo)`: a guarda de ajuste em runtime que faltava na rota nativa
- `feat(checker)`: alargamento implícito, e **`teko::casting` ganhou o seu primeiro consumidor**
- fixtures por VALOR nas duas rotas
- **Faixa de saída usada: 170–189.** Toca `src/checker/typer.tks`, `bigint`, `dec`.

### `cargo/0.3.1.0-degrau-26` @ `8e3df0d7` — 4 ficheiros, +262
`append_fo` baixado para o append de bytes em bloco do runtime; fixtures por valor nas duas rotas.
- **Faixa de saída usada: 190–192.** Toca `src/lir/lower.tks`.

### `cargo/0.3.1-mingw-fora-da-rota-c` @ `ec25b511` — 6 ficheiros, +928
Quatro peças: guarda que grita na rota C, clang+MSVC no Windows, `-v` na falha de link, cross que
para no objeto. **E agiu sobre a sonda que eu corri**: a peça 2 encolheu para o que a medição
mostrou, e o `link` cru saiu da lista de candidatos.
- Também: linha de link do Windows MSVC-correta (`-lm` fora, `/Brepro` no lugar do flag GNU)
- Toca `src/build/project.tks`, `src/build/linker.tks`, `src/build/regression.tks`

### `cargo/0.3.1-b1-fp-x86` @ `89870d1e` — 12 ficheiros, +2157
- **`B1-args` (argumentos em pilha) e `B3-xmm-callee-saved` FECHADOS** — o `B1-args` era a única
  falha real em TRÊS pernas ao mesmo tempo (macOS, musl x86-64, Windows)
- família SSE2 pinada **por byte**, cruzada contra o `as` da GNU
- **achado próprio:** *"the LIR lost float WIDTH, and f32 computed the wrong value"* — outro valor
  errado calado
- **Faixa de saída usada: 150–159.** Toca `src/lir/lir.tks`, `src/lir/lower.tks`, `isel_x86_64`,
  `encode_x86_64`, `regalloc_x86`

### `cargo/0.3.1-saida-equalizada` @ `ff293632` — 6 ficheiros, +195
Equaliza o código de saída ao byte baixo. **Eu mapeei três portas; ele achou QUATRO.** Toca
`src/runtime/teko_rt.{c,h}`, `src/lir/lower.tks`, `src/codegen/codegen.tks`.
- **Faixa de saída reservada: 200–209.**
- Quando isto drenar, a regressão `defer_cascade_exit` passa a verde no Windows **sem tocar no
  harness**.

### A ORDEM QUE FOI USADA, e funcionou
`src/lir/lower.tks` é tocado por quatro delas. A ordem por delta crescente evitou conflito nos três
primeiros; o quarto (aridade) deu quatro conflitos, todos resolvidos por composição de peças provadas.

**Antes de cada dreno**, o ritual que evitou duas regressões hoje: conferir chaves `{`/`}` e
`/**`/`*/` balanceadas, códigos de saída duplicados, chamadas sem definição no corpus, e correr os
gates estruturais (`objfile_gate_test.sh`, `wasm_known_stop_gate.sh`,
`native_selfhost_known_stop_test.sh`, `ci_gate_coverage.sh`).

**A LACUNA QUE ESTE DRENO EXPÔS, e o conserto — com uma correção do dono dentro.** Os gates acima
conferem *que ficheiros* entram e a *forma* do CI; **nenhum confere se a soma ainda funciona.** Cinco
branches verdes em separado não fazem um vagão verde: o dreno acendeu quatro pernas por **um** teste,
e o `src/lir/lower_test.tkt` foi **auto-fundido pelo git sem conflito** produzindo expectativas que não
batem com o `lower_cast` fundido — o git junta duas edições de teste e o resultado corresponde a
nenhuma das duas.

Eu propus como conserto "construir gen1 e correr a suíte". **O dono corrigiu: os testes correm na
gen2/gen3, não na gen1.** A gen1 é construída pelo compilador LANÇADO; a gen2 é a primeira construída
pelo compilador novo a partir do fonte novo, e é nela que a suíte tem sentido. O ritual correto é
`scripts/fixpoint_gate.sh` (que produz gen2 e gen3 e prova gen2 == gen3) **e a suíte sobre a gen2** —
não a gen1. A minha corrida local com gen1 achou a falha por acaso, porque era de tipagem de teste;
com outra classe de defeito teria mentido.

## 2b. REGRESSÕES DO DRENO (2026-07-30) — duas, e uma NÃO está explicada

O dreno das cinco branches ficou verde no ritual local mas **acendeu duas pernas que estavam verdes**.
Ambas são minhas: eu drenei um arco que muda o Windows **sem a medição no Windows que eu próprio
declarei necessária** no mesmo dia. A regra existia; não a apliquei ao meu dreno.

### `artifact / windows-x86_64` — EXPLICADA, conserto a decidir por medição
O link passou a ir por `link.exe` da MSVC (era o objetivo) e morre em **seis símbolos de inteiro de
128 bits**:

```
__divti3 __udivti3 __modti3 __umodti3 __floattidf __floatuntidf   (LNK1120)
```

São helpers que o mingw trazia pela **libgcc**; a MSVC não tem libgcc e o **compiler-rt do clang não é
ligado por omissão em alvo MSVC**.

**Medido na árvore:** a linguagem **rejeita** `i128`/`u128` na superfície (fixtures de compile-fail
`reject_i128`/`reject_u128`) e **nenhum `.tks` invoca** os helpers — mas `teko_rt.h` tem **56**
ocorrências de `__int128`, e os braços i128 dentro de `tk_div`/`tk_rem`/`tk_int_to_float` é que puxam
os builtins.

**Dois consertos, e a sonda decide:** (a) ligar o compiler-rt do clang, se a lib existir na imagem;
(b) excluir os braços i128 em alvo MSVC, que são inalcançáveis da superfície. A sonda
(`theory/sonda-toolchain`) foi estendida para reproduzir o `LNK2019` e medir três candidatos: clang
nu, `--rtlib=compiler-rt`, e a lib de builtins nomeada. **Uma vaga de agente está guardada para este
conserto.**

### `test / linux-arm64-glibc` e `Memory paranoid (linux-arm64-glibc)` — METADE explicada

**Explicado: 21 skips.** As linhas que precisam do alvo próprio do host saltam porque **`arm64-linux`
não existe em `NativeTarget`**:

```
unsupported TEKO_TARGET "arm64-linux" — supported: x86_64-linux, x86_64-windows, arm64-macos, wasm32-wasi, ...
teko: regressions 10 run, 21 skipped, 1 failed (8 builds)
```

O corpus cresceu muito com os cinco drenos e **toda** linha own-native nova salta ali. Sob a lei do
dono, skip é falha. **É exatamente o que o agente dos crumbs AArch64-ELF está a corrigir** (crumb 3
cria `Arm64Linux`); quando aterrar, as 21 correm.

**EXPLICADO em 2026-07-30 com o log completo (§2c).** O `1 failed` e o exit 134 são **duas coisas
distintas**, e a minha leitura anterior confundia-as:

- **exit 134 = SIGABRT do `teko-tktest`.** A fase de testes unitários **ABORTA na PRIMEIRA assertiva
  que falha** e não continua. A fase de regressões corre depois e propaga o 134 no fim.
- **`1 failed` é da fase de REGRESSÕES**, não dos unitários — é a linha `own_arith_exit`, e a causa é
  o **degrau 28** (§1), que é uma regressão do MEU dreno.

**A CORREÇÃO QUE ISTO IMPÕE AO MEU PRÓPRIO REGISTO.** Eu escrevi que falhava **"um teste em 849"**.
Isso não é demonstrável a partir desta prova: como a fase unitária aborta no primeiro `assert` falhado,
**o que está atrás do primeiro nunca correu**. E o primeiro falhado é **diferente por host**:

| perna | primeiro `assert` a falhar | nota |
| --- | --- | --- |
| `linux-arm64-glibc`, `linux-x86_64-musl` | `str_contains` em `teko::lir::lwt_prim_kind_of_resolves_enum_to_int_cast_widens` | expectativa desatualizada, já na fila |
| `macos-arm64` | `is_true` em `teko::build::pt_target_name_and_objfmt_are_one_source` | **NOVO, não estava registado em sítio nenhum** |

Logo há **pelo menos DOIS** testes unitários a falhar, não um, e quantos estão atrás de cada abort é
**desconhecido**. Consertar o do cast não garante verde — garante ver o próximo.

**O de macOS é um GÉMEO QUE DIVERGIU, e é o achado mais interessante do dia.** O corpo de
`pt_target_name_and_objfmt_are_one_source` (`src/build/project_test.tkt:738`) é **inteiramente
independente do host**: as 14 assertivas comparam `target_name`/`target_objfmt`/`target_os_name` de
variantes **literais** de `NativeTarget` com literais de string. Nenhuma toca `host_target_for_os`.
Um teste sem dependência do host que falha **num** host não pode ser expectativa errada — é
**divergência de geração/runtime no arm64-macho**. Medido também que **não é novo**: falha igual na
execução 30508737150 (SHA `8f94c0b1`), portanto é a "1 falha" de macOS que eu tinha atribuído
inteiramente à regressão do own_native — eram **duas**, e eu contei uma.

**Recomendação anterior REVOGADA.** Eu tinha registado "não escavar antes do crumb 3, porque o log não
cabe na cauda". A premissa era falsa: o log completo sempre foi obtível (§2c). A escavação custou
quatro chamadas.

## 2c. O INSTRUMENTO QUE FALTAVA — log INTEGRAL de CI, e uma correção a mim mesmo

O dono, 2026-07-30: *"Quanto aos logs, que só consegue a cauda, pode instruir o CI a guardar o log
completo como artefato quando uma falha ocorrer, assim consegue baixar o artefato para analisar. Use
theory para isso."* **Ele tinha razão sobre o problema e eu estava errado sobre a causa.** O problema
era real — eu lia CI por `get_job_logs`, que devolve uma **cauda** (`tail_lines`, 500 por omissão), e
diagnosticava pernas com 180 KB de log por 500 linhas do fim. O que estava errado era supor que fazia
falta **mudar o CI**.

**MEDIDO (execuções 30509216571 e 30508737150 da PR #99):**

| pergunta | resposta medida |
| --- | --- |
| `get_workflow_run_logs_url` + `curl` dá o log completo? | **sim** — 660 KB comprimidos, **225 ficheiros**, 2.5 MB de texto, **21 jobs**, um ficheiro por PASSO, nada truncado |
| funciona em execuções que já passaram? | **sim**, retroativamente |
| funciona numa execução **em curso**? | **não** — 404, e o 404 vem já no pedido do URL (medido na 30509727122) |
| e artefactos normais, dá para os descarregar? | **sim** — `download_workflow_run_artifact` devolve URL assinado, e o `curl` do sandbox traz o ZIP (provado com `teko-c-macos-arm64`, 1.2 MB → `teko.c` de 10.6 MB) |

**Fixado em `scripts/ci_full_log.sh`** (guardas provadas por inversão), com o comando que interessa:

```
grep -rn 'assertion failed\|native backend N1\|Process completed with exit code [^0]' <dest> | cut -c1-220
```

**RECOMENDAÇÃO, e é NÃO mexer em `pr.yml`.** O artefacto-em-falha resolveria uma cegueira que já não
existe, ao preço de `upload-artifact` em 27 jobs — churn de CI na lane, contra a barra do tronco. Fica
**uma** fronteira registada e não implementada, porque mexe em `pr.yml` e precisa da palavra do dono:
**enquanto a execução corre, o log completo não existe**; para espiar uma perna vermelha antes do fim,
só a cauda por job serve.

**O QUE NENHUM DOWNLOAD DESFAZ, e é um achado à parte:** quando uma linha de regressão falha, o
**nosso** harness imprime `captured output tail:` e **corta**. Esse truncamento é do produto, não do
GitHub. Não custou nada hoje (a cauda continha o diagnóstico), mas custará no dia em que o erro
estiver no meio.

## 3. FILA — não despachado

**Vagas: 4 de 4 OCUPADAS** (teto 4). A correr: **AArch64-ELF crumbs 2–5**, **`MRelocKind::None`**,
**`fmt --apply`**, **remoção dos 128 bits** (que é o conserto do Windows). **Nada sai daqui até uma
vaga abrir** — ruling do dono: *"Se já tem 4 agentes, segura sua onda, enfileire."*

**Ordem recomendada quando abrir vaga:** (1) degrau 28, porque é regressão de dreno e parte TODAS as
pernas; (2) o teste do cast, que destranca quatro; (3) o gémeo de macOS; (4) degrau 27.

| item | porquê | nota |
| --- | --- | --- |
| **Degrau 28 — lowering de `s[i] = v`** | **regressão do meu dreno**, e parte a linha `own_arith_exit` em **todas** as pernas (macOS, x86_64-musl, arm64-glibc, regressor). Prioridade 1 | `native backend N1: slice element index-assignment not yet lowered (N2) [in own_native::f_implicit_widen_targets]`. **Não mudar a fixture para evitar o slice** — trocaria paragem honesta por cobertura fingida |
| **Consertar `lwt_prim_kind_of_resolves_enum_to_int_cast_widens`** | é o **primeiro** `assert` a falhar em `linux-arm64-glibc` e `linux-x86_64-musl`, e a fase unitária **aborta** ali (SIGABRT/134) — logo destranca a visão do resto, não necessariamente o verde | expectativa desatualizada, não defeito: afirma `%1 = sext %0`, e a aridade decidiu que um alargamento sem perda **não emite conversão nem guarda** (`lower_cast_fit_guard` começa por `if cast_is_lossless_widen { return ctx }`). **Não apagar** — tem de passar a afirmar a AUSÊNCIA da conversão. O sinal negativo já está provado por VALOR em `f_cast_widen_keeps_value` (`-5 to i64`) e no alargamento implícito (`-2147483648`), nas duas rotas |
| **Gémeo divergido de macOS: `pt_target_name_and_objfmt_are_one_source`** | teste **sem dependência do host** que falha **só** em `macos-arm64` (`assertion failed: is_true`) → divergência de geração/runtime no arm64-macho, não expectativa errada. Não é novo (falha já em `8f94c0b1`) | mandato: **primeiro dividir o teste** para saber QUAL das 14 assertivas cai (o rasto só dá `+636` no símbolo), depois caçar a divergência de lowering. Instrumento certo: `agent-fast-lane.yml` com `runner: macos-latest`, que É despachável. Sob a regra do oráculo, divergência é bug do nativo até prova em contrário |
| **SEGUNDA PASSAGEM DO DEBUGGER — brief pronto, ver §3b** | o dono leu o orçamento e reprovou: *"o trabalho do arquiteto foi pessimo, nao tem um exemplo de prova de conceito, de como seria a superficie para isso ou como utilizar em cada tipo de debugger mencionado"* | **a falha é do MEU brief**, não do arquiteto: pedi orçamento e não pedi PoC, superfície, nem contra-medida. Entra na próxima vaga |
| **O Windows não põe `.exe` no executável** | **ACHADO NOVO, 2026-07-30, e é DEFEITO DE PRODUTO.** Descoberto porque o dreno dos 128 bits destrancou o `artifact / windows-x86_64`: ele passou a publicar, e a perna `test / windows-x86_64` — **que nunca tinha corrido em toda a lane** — falhou logo com `ERROR: the producer's upload has no dl/windows-x86_64/teko.exe`, tendo publicado `teko` | **medido: `src/build/` não acrescenta `.exe` em host nenhum.** O ficheiro É um PE válido (`assert_asset_arch` passou), mas o Windows resolve um nome sem extensão **acrescentando** `.exe`, logo `teko` não é lançável por nome. **O CI está correcto nos dois lados** — `produce_assets.sh` já trata `*.exe` e o consumidor espera `teko.exe`; é o produto que erra. Conserto: o nome do executável ganha `.exe` quando o alvo é Windows. Segunda vaga outra vez: consertar uma perna acendeu outra que nunca tinha corrido |
| **`kind` desconhecido panica + o kind `tool` novo** | **RULING DO DONO 2026-07-30**, duas ordens: *"esse else não deveria fazer fallback mas causar panico, kind desconhecido é erro no compilador"* e *"Tem que adicionar o novo kind proposto"*. Pré-requisito de `tdb` | **MEDIDO, e não é um enum de uma linha.** Ver §3c abaixo: a árvore codifica a dicotomia "Binary ou não-Binary" em TRÊS sítios, e `Tool` quebra-a por ser executável (tem `main`) **e** empacotável |
| **Degrau 27 — builtin `ftoa`** | é a paragem VIVA do self-host nas duas pernas nativas; a escada não avança sem ela | `native backend N1: builtin 'ftoa' not yet lowered (N2) [in teko::codegen::cb_f64_literal]`. O pin `scripts/native_selfhost_known_stop.sh` já a aceita como paragem honesta (deixou de nomear o degrau, de propósito) |
| **`fmt --apply` explícito** | dono aprovou: *"Sim: fmt --apply explícito"* | o meu despacho foi **recusado na camada de permissão** logo depois; nunca chegou a correr, e eu não o repeti (chamada recusada trata-se como decisão). **Precisa da palavra do dono para andar.** Contrato pinado em `scripts/fmt_cli_test.sh` |
| **Híbrido do `main`** | desenho **fechado** no digesto de leis | precisa do arquiteto para ordenar crumbs |
| **AArch64-ELF crumbs 2–5** | crumb 1 (relocação) fechado e provado em hardware | crumb 3 cria `Arm64Linux` em `NativeTarget` e destranca a perna arm64-Linux |
| **`MRelocKind::None`** | `plain_word`/`branch_word` (`encode_arm64.tks:117,139`) põem `Call` como default inerte — o valor "branch" como default de um campo que toda instrução carrega. Foi a semente do bug de relocação | mata a classe na raiz |
| **Debugger, Camada 1** | orçamento entregue e drenado (`docs/design/debugger-orcamento-0.3.1.md`) | 6 crumbs; recomendação é parar ali |


## 3b. BRIEF PRONTO — segunda passagem do debugger (o dono reprovou a primeira)

**A crítica do dono, 2026-07-30, verbatim:** *"eu li o doc do debugger e o trabalho do arquiteto foi
pessimo, nao tem um exemplo de prova de conceito, de como seria a superficie para isso ou como
utilizar em cada tipo de debugger mencionado. Embora eu nao tenha pedido um debugger proprio, ja que
ele levou mais de uma hora pra produzir isso, poderia ter orcado o restante dos pontos e tambem a
contra-medida (debugger proprio)."*

**A CULPA É DO BRIEF, E O BRIEF É MEU.** Eu pedi *"orçar a implementação de um debugger"* e o
arquiteto orçou exactamente isso, com quatro experimentos medidos e sete correções ao esboço do dono
— trabalho sólido no que foi pedido. O que **eu** não pedi, e o dono queria: prova de conceito, a
superfície de utilização, o uso por debugger, o orçamento das camadas restantes, e a contra-medida.
Um arquiteto que corre mais de uma hora tinha orçamento de sobra para as cinco. **Lição: quando o
pedido é "orça X", perguntar antes se o dono quer também o custo de NÃO fazer X.**

**O QUE A SEGUNDA PASSAGEM TEM DE ENTREGAR — cinco peças, nenhuma opcional:**

1. **PROVA DE CONCEITO REAL.** O Experimento D já produziu um objeto que gdb *e* lldb aceitaram. Isso
   tem de virar artefacto reproduzível e versionado, não prosa: o `.tks` de referência, os bytes das
   três seções, e o comando que qualquer pessoa corre para ver o breakpoint parar. Sem isto o
   orçamento é uma promessa.
2. **A SUPERFÍCIE, concreta.** Qual é a flag? `teko build . -g`? Um perfil no `teko.tkp`? O que sai no
   `--help`? Onde ficam os bytes de depuração num `.tkl`? Isto está no orçamento como uma linha
   ("o interruptor de perfil") e tem de ser um desenho.
3. **USO EM CADA DEBUGGER MENCIONADO**, com o texto que o dono escreve/cola: gdb no terminal, lldb no
   terminal, VSCode via `cppdbg`, VSCode via CodeLLDB. Um `launch.json` completo por cada, não uma
   referência a "um exemplo em docs/".
4. **AS CAMADAS RESTANTES ORÇADAS**, não "o penhasco": Camada 2 (com a sondagem dos nomes através do
   regalloc identificada como crumb próprio e o resto orçado *condicionalmente* a ela), Camada 3, e
   Windows/CodeView com número. "5+ crumbs, um deles perigoso" não é orçamento.
5. **A CONTRA-MEDIDA: DEBUGGER PRÓPRIO, ORÇADO.** O dono não pediu um, e a recomendação de não fazer
   pode manter-se — mas uma recomendação de não fazer **sem o custo do que se recusa** não é
   decidível. Orçar: ptrace/`mach_vm`, breakpoints por `int3`/`brk`, leitura da nossa própria tabela
   de linha (que a Camada 1 cria de qualquer forma), e um adaptador DAP. E dizer o que um debugger
   nosso daria que gdb/lldb **não** dão — se a resposta for "nada", isso é a prova da recomendação, em
   vez de a asserção que está lá hoje.

**RESTRIÇÕES:** o arquiteto **não implementa produto**; escreve em `docs/design/`. Não abre PR. Empurra
para a branch em que trabalha assim que escreve. Nunca toca `.github/workflows/**`.


## 3c. `kind = "tool"` — medido, e a armadilha que triplica o trabalho

**Ordens do dono, 2026-07-30:** (1) *"esse else não deveria fazer fallback mas causar panico, kind
desconhecido é erro no compilador"*; (2) *"Tem que adicionar o novo kind proposto"*.

**Onde vive:** `src/build/tkp_rule.tks:9` — `type Artifact = enum { Binary; Static; Shared; Package }`.
O fallback silencioso está em `src/build/manifest.tks:565`.

**A ARMADILHA, e é o que faz isto não ser um enum de uma linha.** A árvore não codifica quatro kinds;
codifica uma **DICOTOMIA** — `Binary` contra tudo o resto — e escreve-a por extenso:

```teko
// (C7.1m) The three non-Binary kinds are LIBRARY kinds — they forbid a main.tks.
fn check_main_file_rule(artifact: Artifact, has_main: bool) -> Artifact | error {
    if artifact == Artifact::Binary && !has_main { return error { … "requires a main.tks" } }
    if artifact != Artifact::Binary && has_main  { return error { … "may not have a main.tks" } }
```

E outra vez em `src/build/project.tks:3094`: `if m.artifact != Artifact::Binary { return base }`.

**`Tool` quebra a dicotomia**, porque é as duas coisas ao mesmo tempo: **é executável** (tem `main.tks`,
compila como executável normal na máquina do dev) **e é empacotável** (emite um `.tkl`). Logo
`artifact != Artifact::Binary && has_main` **rejeitaria** um `tool` legítimo, e a mensagem de erro
mentiria dizendo *"a library project (static/shared/package)"*.

**Portanto o conserto certo NÃO é acrescentar um membro e remendar os `if`.** É trocar os testes de
VARIANTE por testes de PROPRIEDADE — algo como `artifact_requires_main(a)` e
`artifact_is_packageable(a)` — de modo que acrescentar um kind futuro não obrigue a caçar dicotomias
espalhadas. É o mesmo padrão que fechou o degrau da relocação: **tornar o estado errado
inexpressável**, em vez de corrigir cada sítio que o expressa.

### CORRECÇÕES DO DONO, 2026-07-30 — e a metade que faltava na dele

**(1) O `if` ajusta-se, não se refactoriza.** Verbatim: *"é visível que SIM tem que acrescentar o novo
tipo E ajustar o if adicionando um AND (&&) NOT (!=) Tool"*. Aceito: a forma é
`artifact != Artifact::Binary && artifact != Artifact::Tool && has_main`.

**MAS A DELE É NECESSÁRIA E NÃO SUFICIENTE, e a razão é um corolário que ele próprio ratificou:**
*"uma paragem que dispara para 1 dos 4 membros de uma família é pior que nenhuma."* `check_main_file_rule`
tem **DOIS** `if`, e ele nomeou o segundo:

```teko
if artifact == Artifact::Binary && !has_main { return error { … "requires a main.tks" } }   // <- ESTE também
if artifact != Artifact::Binary && has_main  { return error { … "may not have a main.tks" } }
```

Se só o segundo levar `&& != Tool`, um **`tool` SEM `main.tks` passa em silêncio** — e um `tool` sem
`main` não tem comando para instalar. O primeiro `if` tem de virar `(Binary || Tool) && !has_main`.
Um buraco no sentido oposto é a mesma classe de defeito.

*(Nota: quando se escreve `Binary || Tool` num sítio e `!= Binary && != Tool` no outro, isso **é** o
predicado — dar-lhe nome é só grafia, e é barato. Mas a decisão da forma é do dono, e ele escolheu o
`&&`; a metade que falta é o que não é negociável.)*

**(2) Um `tool` em `[deps]` NÃO é recusado — é TOLERADO e IGNORADO.** Verbatim: *"Não precisa recusar,
só não precisa existir como dependência, e se existir (aqui sim tem trabalho) precisa ser ignorado pelo
compilador (para não importar/linkar)."*

Ele tem razão que aqui há trabalho, e **localizei-o**: `src/build/project.tks:215-232`,
`load_deps_program`. O laço faz, por cada entrada de `m.deps`, um `load_dep_program(m.deps[di])` que lê
o `.tkb` do dependente e **injecta os seus itens** no ambiente de tipos.

```teko
if di >= m.deps.len { break }
let dep_prog = match load_dep_program(m.deps[di]) { … }
```

**O conserto: resolver o KIND do dependente ANTES de carregar o `.tkb`, e saltar quando for `Tool`.** E
uma consequência que simplifica: como um dependente Teko entra por **injecção de itens**, saltar o
carregamento **salta o link por construção** — não há um segundo sítio a tratar. O que existe hoje é o
oposto do que se quer: `load_dep_program` vai directo ao `.tkb`, sem consultar o kind.

**A fixture obrigatória:** um projeto que declara um `tool` em `[deps]` **constrói**, e um símbolo do
`tool` **não é resolúvel** no projeto. Provar as duas metades — que não estoura E que não importa. Só a
primeira passaria se o `tool` fosse carregado e por acaso não colidisse.

**A ORDEM DOS CRUMBS, e ela importa:**

1. **`kind` desconhecido passa a ERRO DURO**, com a lista dos aceites na mensagem. Sozinho, e primeiro
   — porque enquanto o fallback existir, `kind = "tool"` escrito por alguém é silenciosamente um
   binário comum, e um verde sobre isso não significa nada. A fixture é de compile-fail sobre um `.tkp`
   com `kind = "binari"`.
2. **A dicotomia vira predicado.** Refactor sem mudança de comportamento — os quatro kinds actuais têm
   de dar exactamente as mesmas respostas. Prova: as fixtures existentes de `check_main_file_rule` sem
   uma alteração.
3. **`Tool` entra**, e entra num sítio onde a grafia errada grita e onde a dicotomia já não existe.
   Fixtures: um `tool` **com** `main.tks` é aceite; a mensagem de erro do caso de biblioteca deixa de
   mentir sobre quais são os kinds de biblioteca.
4. **O `.tkl` de um `tool`** — o que o escritor de pacote põe lá dentro, e o que `[deps]` recusa. É aqui
   que vive a parte que o dono nomeou: *"sem adicionar como dependência de projeto (não entra nas
   dependências do tkp)"*.

**Referência nomeada e aplicável: C#.** `dotnet tool` (`PackageType=DotnetTool`) é o único dos quatro
com um TIPO de pacote declarado; `cargo install` e `go install` dão o mesmo efeito instalando algo que
por acaso tem binário, **sem** tipo próprio. O dono atribuiu C# para addins, e aqui aplica-se de facto.


## 3d. O `.exe` do Windows — medido, e o brief está pronto

**O sintoma:** `test / windows-x86_64` morre em `ERROR: the producer's upload has no dl/windows-x86_64/teko.exe`.
O produtor publicou `teko`. O CI está correcto nos **dois** lados (`produce_assets.sh` já trata `*.exe`, o
consumidor espera `teko.exe`); é o **produto** que nomeia a saída sem extensão em todos os hosts. Um ficheiro
PE chamado `teko` **não é lançável por nome**, porque o Windows resolve um nome sem extensão acrescentando
`.exe`.

**OS DOIS SÍTIOS, medidos em `src/build/project.tks`** — e são dois, o que faz disto um caso de família:

```
1827:    let binp = teko::str::concat(od, "/", stem)     <- rota C
2635:    let binp = teko::str::concat(od, "/", stem)     <- rota NATIVA
```

**Consertar só um é o defeito "um dos membros da família".** As duas rotas produzem executáveis e as duas
nomeiam-nos igual.

**E O DESENHO CERTO JÁ EXISTE, 800 linhas abaixo, no mesmo ficheiro** — não se inventa nada:

```teko
fn archive_output_path(od: str, stem: str, format: ArchiveFormat) -> str {
    match format {
        Coff => teko::str::concat(od, "/", teko::str::concat(stem, ".lib"))
        _    => teko::str::concat(od, "/", teko::str::concat("lib", teko::str::concat(stem, ".a")))
    }
}
```

O arquivo **já** é nomeado por formato de alvo (`.lib` em COFF, `lib*.a` no resto). O executável não. **O
conserto é um irmão desta função** — `binary_output_path(od, stem, target)` — chamado dos dois sítios, e
**não** um `if` improvisado em cada um. Assim, o próximo alvo que precise de sufixo entra num só lugar.

**O que o brief tem de exigir além disso:**
- **quem CONSOME `binp`** nos dois sítios — se algum passa o caminho ao linker, ao `chmod`, ou o imprime,
  todos têm de ver o mesmo nome. Um sítio que continue a montar o nome à mão é o defeito de volta.
- **`teko test .` e o harness de regressão**: se algum invoca o binário construído por nome derivado, tem de
  seguir o mesmo helper. Medir, não presumir.
- **fixture**: construir para alvo Windows e afirmar que o ficheiro emitido termina em `.exe`; e que nos
  outros alvos **não** termina em `.exe`. As duas metades — só a primeira passaria se o sufixo fosse posto
  em todos os hosts, o que partiria Linux e macOS.
- **não tocar** `produce_assets.sh` nem `pr.yml`: ambos já estão certos, e o segundo é do integrador.


## 4. DECISÕES DO DONO EM ABERTO

Uma só, e não é bloqueante para a lane:

- **`fmt --apply`: dizer se pode andar.** Ele **aprovou** (*"Sim: fmt --apply explícito"*), mas o
  despacho do agente foi **recusado na camada de permissão** imediatamente depois, e eu não o repeti —
  chamada recusada trata-se como decisão, não como falha. As duas coisas contradizem-se, então a
  próxima sessão deve **perguntar se foi clique errado** antes de despachar. Uma palavra basta.

Todo o resto levantado neste dia foi respondido e está executado ou registado.

## 5. LEIS E DIRETRIZES — onde vivem

O digesto é `docs/memory/teko-laws-digest.md`; a disciplina de trem é
`docs/memory/teko-stacked-train-discipline.md`. **Leia os dois antes de despachar qualquer coisa.**
O que segue é o que se usa em todo despacho:

### Sobre o dreno
- **`scripts/drain_guard.sh <ref>`** antes de todo merge: recusa um dreno que traga
  `.github/workflows/**`. Já existe e provou-se por inversão contra a minha própria branch de sonda.
  `--allow` para uma mudança de CI deliberada.

### Sobre agentes
- **Máximo 4 em paralelo** (o dono subiu para 5 e voltou a 4 em 2026-07-30: *"vamos manter o pace, 4
  agentes no máximo"*). Seis implementadores derrubaram o sandbox uma vez.
- **Esperar CI não é estado permitido.** Um agente que chegou a uma pergunta que só o CI responde está
  **TERMINADO**: escreve handoff (branch + SHA + qual pergunta ficou pendente + o que fazer com cada
  resposta), termina, e **liberta a vaga**. Com teto de 4, uma vaga em espera é 25% da capacidade
  parada.
- **Escreveu? Comita e empurra**, na branch onde estiver — `cargo/**` ou `theory/**`. Não é "quando
  estiver pronto", é ao escrever.
- **Empurrar SEMPRE**, a cada avanço, mesmo trabalho feio. Numa `cargo/**` custa ZERO e não dispara
  CI. **É esta regra que fez o reinício de hoje custar nada.**
- **NUNCA abrir PR.** O integrador dreno.
- **NUNCA autorar ficheiro sob `.github/workflows/`**, em nenhuma branch, incluindo `theory/**`. Se o
  workflow estiver num commit do agente, um cherry-pick de volta traz o CI restrito para o vagão — **e
  isso já aconteceu: foi assim que a fast-lane foi para a main numa sessão anterior.**
- **Force-push bloqueado para TODOS, inclusive o dono.** Push recusado: merge forward-only, ou
  committe local e reporte o SHA (o object store é partilhado entre worktrees).
- **O agente não cunha KNOWN-STOP** — levanta red-flag. Negociação é entre o dono e o integrador. Ele
  PODE promover um known-stop que ficou vermelho por o vão ter fechado.
- **`theory/**` é o campo de provas.** Um push a `theory/**` dispara a `agent-fast-lane.yml`
  (validação completa). **O agente empurra e SEGUE — nunca espera**, porque não tem acesso à API do
  GitHub (403) e ficaria ocioso **e cego**. Ele reporta a branch e a pergunta; **ler o CI é trabalho
  do integrador**.
- `workflow_dispatch` **não** existe para workflow que só vive numa theory (404 — o GitHub só o expõe
  a partir do branch default). A **fast-lane vive no default**, logo o integrador consegue pedir-lhe
  `runner: windows-latest`/`macos-latest`/`ubuntu-24.04-arm`.
- **Faixas de código de saída fechadas nas duas pontas.** Houve duas colisões por faixas abertas.
  Ocupadas: 100, 130–140, 150–159, 160–169, 170–189, 190–199, 200–209. **A próxima começa em 210.**
- **Nunca `git add -A` em `examples/`** — um agente levou seis binários de build assim.

### Sobre o produto
- **`bootstrap/teko.c` é SAÍDA, não entrada.** Nunca se toca, nem os campos de versão de `teko.tkp`.
  Ficheiros C de geração são construídos pelo CI, e só quando o CI passa verde.
  `src/runtime/teko_rt.{c,h}` é seed escrito à mão e **pode** crescer por razão genuína de runtime.
- **Regra do oráculo:** enquanto a rota C existe, divergência entre nativo e C é **bug do nativo até
  prova em contrário**. A prova em contrário já apareceu uma vez (a rota C pára onde o nativo acerta,
  num closure com parâmetro de união-nula) — registado para ninguém corrigir o nativo para imitar um
  defeito do C.
- **mingw é PROIBIDO.** Cross-compiling é objetivo declarado, **depois** dos nativos, e nunca por
  mingw. MSVC no Windows. Linker nativo em vez de `cc`/`gcc` quando houver binário fim-a-fim.
- **Zero menções a "VM"** em texto novo (só `docs/design/vm-retirement.md`).
- **`void` é banido. Sobrecarga é banida.** Ao invocar uma das quatro referências de desenho
  (superfície → Rust, controlo → Zig, addins → C#, comportamentos → Go), **verificar que a NOSSA
  superfície suporta o que a referência oferece.** Eu escorreguei nisto hoje.
- **W15:** só doc-comments `/** */` na declaração, zero `//` inline, sem valores mágicos.
- **SKIP conta como falha.** Limitação conhecida vira KNOWN-STOP: verde enquanto quebrado, VERMELHO
  quando fecha. Um KNOWN-STOP vermelho **não** significa necessariamente que o erro foi corrigido —
  pode ser que a direção mudou.
- **`fmt` NÃO é portão de CI.** É convenção, não imposição (dono). **Nunca armar `fmt --check`.** 16
  ficheiros em `src/` estão fora de formato; é dívida opcional.
- **Régua do tronco:** só vai ao tronco se passar pelo CI, sem erros, alertas ou erros escondidos que
  não disparam.

## 6. VERMELHOS DO CI, todos com causa conhecida

| perna | o quê | fecha com |
| --- | --- | --- |
| `artifact / linux-x86_64-glibc` e `linux-arm64-musl` | ponto de fixo nativo no degrau 26 | dreno do degrau 26 |
| `test / macos-arm64` | `B1-args` (1 falha; eram 2 antes da relocação fechar) | dreno do `b1-fp-x86` |
| `test / windows-x86_64` | `B1-args` + `defer_cascade_exit` | `b1-fp-x86` + `saida-equalizada` |
| `regressor / all capabilities` | `B1-args` | `b1-fp-x86` |
| `CI gate`, `Test suite gate`, `Sanitizer gate` | agregadores ancorados numa perna nativa | a escada |

O `Sanitizer gate` **não pode** ficar verde antes de a escada fechar: exige o `mem-paranoid`, que
monta a perna nativa. Mas deixou de ser vermelho **vazio** — os dois oráculos novos
(`mem-paranoid-linux-x86_64-musl` e `-linux-arm64-glibc`) passam.

## 7. Achados medidos e NÃO corrigidos

1. **Despacho por interface através de local tipado-interface dá SIGSEGV no nativo**, `0` na rota C,
   **sem união à vista**. Medido idêntico antes do degrau 25, logo pré-existente. Família que o
   `native_iface_fat_known_stop` circunda.
2. **A rota C pára onde o nativo acerta** — closure com parâmetro de união-nula. Vão do oráculo.
3. **`/usr/bin/link.exe` no Git-Bash é o `link` do MSYS, não o linker da MSVC.** O agente do mingw já
   tirou o `link` cru dos candidatos; confirmar ao drenar.
4. **`teko::casting` tinha zero consumidores** — só o medidor de métrica o vigiava. O agente da
   aridade deu-lhe o primeiro.
5. **`str` está em obra:** `teko_rt.h` tem dois words com `len` em BYTES; a decisão de 29/07 põe
   `.len` em caracteres e um terceiro word. Um pretty-printer escrito hoje erraria duas vezes.
6. **Nomes de locais não atravessam o `regalloc`** — bloqueia orçar a Camada 2 do debugger sem sondar.
7. **16 ficheiros fora de `fmt`**, sem portão (e por ruling, sem portão a criar).

## 8. Erros MEUS deste dia, para não se repetirem

Registados porque cada um custou tempo e alguns quase custaram correção errada:

0. **DECLAREI-ME CEGO SEM PROCURAR O INSTRUMENTO, e depois desenhei CI para uma cegueira inventada.**
   Passei o dia a diagnosticar CI pela **cauda** de `get_job_logs`, escrevi no handoff que a mensagem
   do pânico estava "fora de alcance", e **recomendei não escavar** com base nisso. O log integral
   sempre esteve a uma chamada de distância (§2c). Pior: quando o dono propôs guardar o log como
   artefacto, o meu instinto foi **implementar em `pr.yml`** em vez de medir primeiro se fazia falta —
   teria posto `upload-artifact` em 27 jobs para resolver um problema meu. **Antes de mudar o sistema,
   medir se o instrumento já existe.**
1. **Contei UMA falha onde havia DUAS, por não saber que o harness aborta.** Escrevi "único teste a
   falhar em 849". A fase unitária faz **SIGABRT no primeiro `assert` falhado** — o que está atrás
   nunca corre. E o primeiro falhado **difere por host**: em macOS é outro teste, que assim ficou
   invisível no meu registo. *"Primeiro falhado" nunca é "único falhado" num harness que aborta.*
2. **Drenei a fixture da aridade sem que perna nativa nenhuma a tivesse compilado** — e ela usa
   `s[i] = v`, que o backend nativo não sabe baixar (degrau 28). É a terceira vez neste dia que drenei
   algo cuja medição eu próprio tinha declarado necessária.
3. **Aceitei "não pude verificar" como resposta** do agente da relocação, em vez de o mandar provar em
   `theory/**`. O dono corrigiu: *"É pra isso que DEVE usar uma theory/**"*.
4. **Recomendei NÃO travar a fast-lane em `theory/**`.** Errado — é a exclusividade que a torna campo
   de provas previsível.
5. **Propus `-> void` invocando o C#**, quando `void` e sobrecarga são banidos aqui. Invocar a
   referência sem medir a nossa superfície.
6. **Concluí que a ausência de portão de `fmt` era buraco**, esticando "sem erros escondidos" até
   cobrir estilo. Não cobre.
7. **Disse que `MRelocKind` precisava de variantes GOT.** Refutado por medição: zero relocações contra
   símbolo indefinido.
8. **Disse que a Camada 1 do debugger era `.debug_line` só.** Refutado: sem `.debug_info` +
   `.debug_abbrev` o gdb não põe o primeiro breakpoint.
9. **`rc=$?` depois de um `| head`** dá o estado do `head`. Li rc=0 e quase concluí que o
   `fmt --check` não falhava.
10. **Escrevi `set -u` sem `set +e`** ao extrair um gate, reintroduzindo no mesmo ficheiro o defeito que
   tinha corrigido horas antes. **Todo passo que captura `$?` sem limpar o `-e` é este bug.**
11. **Atribuí o `exit 127` do Windows ao `sed -E`.** Era o `set +e` ausente; o gate nunca corria.
12. **Deixei um comentário mentir** no cabeçalho da minha própria sonda ("ONE host") depois de a
    converter para matriz.

**O padrão:** invocar uma lei ou referência sem medir se ela se aplica, e medir a coisa errada com
confiança. O antídoto que funcionou todas as vezes: **prova por inversão** — aplicar, reverter, ver o
comportamento errado reaparecer.

## 9. O padrão técnico dominante da lane

**Gémeos que divergiram** — duas rotinas irmãs onde uma foi corrigida e a outra não. **Nove instâncias
medidas.** E o corolário que o degrau 25 deu, que vale como régua:

> Uma paragem que dispara para 1 dos 4 membros de uma família é pior que nenhuma: **certifica os
> outros 3.**

Ao fechar qualquer coisa nesta lane: **varra a família inteira e liste os irmãos**, inclusive os que
não vai corrigir.
