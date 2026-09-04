# Entrega 4 do `ngen/` — superfície: default, `params`, sobrecarga, operadores

Plano executável. Escopo dado pelo dono (2026-09-04): **parâmetros default**,
**multiparâmetros à la C#**, **sobrecarga** por assinatura (a sobrescrita já
existe) e **sobrecarga de operadores**.

**Restrição dura do dono:** nada de `fn`/`func`/`def`/`function`. Funções e
métodos seguem o modelo do próprio mc — C/C++/C#, tipo de retorno primeiro
(`i64 area(uptr self)`). Não se ensina palavra introdutória de declaração e
não se reimplementa o `fn` do core (o `examples/lang` faz isso; nós não).

## 1. Descobertas MEDIDAS (probes em scratch, `mc 0.10.0`, a release do CI)

1. **As cinco `decl_*` do M31 existem e funcionam** (`mc/docs/reference/hooks.md`
   §"Asking about a declaration the core already parsed"; corpo em
   `mc/src/parse.mc:1886-1941`). **`decl_param_type` PRESERVA o id que
   `type_new` devolveu** — medido: `type_new("Ring")=8` e
   `decl_param_type(d,2)==8`, sem colapsar em `TY_*`. É o que torna sobrecarga
   e despacho de operador por tipo viáveis **sem ensinar o `fn`**.
2. **Só o que já foi parseado é visível.** `decl_find` de função declarada
   abaixo dá −1; um **protótipo** acima resolve. De dentro do próprio corpo a
   função ainda não está em `unit_head` — a ponte é um **`pass()`**, onde a
   unidade inteira existe.
3. **`syntax_infix` sobre operador do CORE é morto — e em SILÊNCIO.**
   `syntax_infix("+", …)` registra sem reclamar, mas `parse_unit()` chama
   `ops_init()` como primeira instrução (`mc/src/parse.mc:2110` → `:293-306`) e
   `infix_set` zera a coluna de handler (`:279`). Como `user_init()` roda ANTES,
   o handler nunca dispara: medido com `err_at` incondicional dentro dele —
   `1 + 2` compila e dá 3. **A rota de sobrecarga de operador é `pass()`, e só
   ela.** (Reportado à sessão que desenvolve o mc.)
4. **`MAXPARAMS` = 12**, não 8 (`mc/src/arena.mc:58`: 1..8 em registrador,
   9..12 na pilha). O teto de 8 em `mc/examples/lang/README.md:243-246` é do
   `lx`, não do core.
5. **Parâmetro default em função LIVRE é inalcançável hoje.** `parse_params`
   (`mc/src/parse.mc:1966-1993`) só aceita `tipo nome`, e não há hook no
   caminho: `parse_top` (`:2076`) consulta `syntax_find` só no primeiro token, e
   `word_add` recusa `i64/u8/…` (`mc/src/hooks.mc:233-238`). Em MÉTODO não há
   problema — o corpo do tipo é parseado por nós.
6. **A rota `pass()` foi provada ponta a ponta:** mangling de sobrecarga com
   reescrita do sítio de chamada, preenchimento de argumento default no
   `N_CALL`, e `N_BINARY`→`N_CALL` in-place preservando `nd_next`. O
   `function declared twice` mora em `mc/src/gen_resolve.mc:171`, depois dos
   passes — o pass chega primeiro.

**Correções que estas medições impõem ao `ngen/HANDOFF.md`:** a armadilha 6 do
§5.1 ("o core não reporta o tipo de um parâmetro") está **errada** — ele reporta,
só não antes de a declaração fechar; e o teto de parâmetros é 12, não 8.

## 2. Divisão que barateia tudo

O corpo de `class`/`struct`/`interface`/`trait` é parseado por **nós**
(`ngen/teko_class.mc:334` `tk_member`, lista em `:212` `tk_params`). Logo, para
**métodos** os quatro itens são alcançáveis **hoje**, sem API nova nem `pass()`.
Só **função livre** depende das `decl_*` e de passes — e o default de função
livre está bloqueado (§1.5).

## 3. Crumbs, em ordem

| # | o que ensina | rota | fixture |
|---|---|---|---|
| C0 | glob do CI aceita `surface_*.tk` (`.github/workflows/ngen.yml:129`) | — | nenhuma |
| C1 | **default em método** — `i64 scale(self, i64 k = 2)` | parse próprio | `surface_default_method.tk` |
| C2 | **sobrecarga de método** por assinatura | parse próprio | `surface_overload_method.tk` |
| C3 | **oráculo de tipo estático** em `pass()` (`teko_typeof.mc`) — sem superfície nova | `pass()` + `decl_*` | `surface_typeof_param.tk` |
| C4 | **sobrecarga de função livre** (mangling `nome__T0__T1`) | `pass()` | `surface_overload_free.tk` |
| C5 | **sobrecarga de operador** — `Vec operator+(self, Vec b)` | `pass()` sobre `N_BINARY` | `surface_operator.tk` |
| C6 | default em função livre — **BLOQUEADO** (§1.5); entrega só a metade de chamada | — | nenhuma |
| C7 | **`params`** — `i64 total(params xs)` | palavra-tipo + `pass()` | `surface_params.tk` |

Notas de forma: `operator` é membro **contextual** dentro do corpo do tipo (como
`virtual`/`override` já são), não palavra reservada no programa. `params` é
registrado com `type_new` — a grafia C# `params i64[] xs` **não** é alcançável
(o core não tem `[` em posição de parâmetro, e o `ngen` não tem tipo array).

Pontos perigosos, por crumb: em C2, `tk_slots_inherit` (`ngen/teko_class.mc:156`)
copia slots da base **por nome** e passa a precisar de (nome, assinatura), senão
uma derivada que sobrecarrega colide com o slot herdado. Em C3, a tabela
`tk_local` (`ngen/teko_struct.mc:392`) é global e sem escopo — o oráculo **não**
a herda, monta escopo por `N_FUNC`, e nome sombreado cai no caso conservador
(erro claro, nunca palpite). Em C7, o buffer variádico é estático e **não
reentra**: chamada variádica aninhada é recusada com mensagem própria.

## 4. Ritual por crumb

`mc build ngen --config <toml de host>` + **todas** as fixtures em exit 42 (as 9
atuais e as novas). Nos crumbs de `pass()` (C3-C5, C7), ritual extra: **prova de
no-op** — `--dump-ast` de `types_class.tk` e `types_interface.tk` idêntico antes
e depois de registrar o pass, quando o programa não usa a construção nova. É a
disciplina do `check-surface.sh` do próprio mc: um pass não mexe em árvore que
não é dele.

## 5. O único pedido de suporte ao mc

Para destravar o C6 (e simplificar o C7), o suporte mínimo seria **`on_param`** —
um hook na linha do `on_stmt`/`on_jump`, chamado por `parse_params` depois de
montar cada `N_PARAM`, com o parser no token seguinte, podendo consumir o
`= expr` e devolver o `N_PARAM`. Estimativa do architect: ~10 linhas no core, na
mesma forma dos hooks existentes. Alternativa mais estreita: o core aceitar
`= <constante>` e publicá-la por `decl_param_default(d, i)`.

## 6. Erratas e correção de rota (2026-09-04, depois de C1-C3)

**Errata do §2:** o corpo de `struct` **não** era parseado por `tk_member` — só
lia campos; `struct` não tinha método. O C1 puxou a correção para dentro do crumb
(lei do não-deferir): `struct` passa pela mesma máquina de membros do `class`, e
`virtual`/`override`/`use` num `struct` são recusados por nome.

**Dois defeitos silenciosos da entrega 3, expostos pelo C3** (medidos; código
errado, não mensagem ruim):
1. **Shadowing:** `i64 report(Shape s) { if (1) { Ledger s = new Ledger; }
   return s.area(); }` chama `ledger_area` sobre um `Shape` — a tabela `tk_local`
   (`ngen/teko_struct.mc:392`) é global e sem escopo, o `s` do bloco interno vaza.
2. **Resolução por nome único:** `a.extra()` com `a: A`, quando só `B` declara
   `extra`, compila e chama `b_extra(a)`. O correto é erro de compilação.

**Correção de rota — pelo mecanismo do mc, não por tabela própria.** O `ngen`
reinventou (mal) o que a doc e o `lx` já ensinam: rastreio de escopo é
`syntax_stmt("{")` (o módulo é dono de todo bloco — `mc/docs/reference/hooks.md:288-289`,
`examples/lang/lang_stmt.mc` `lg_block`), `p_blockdepth()` para a profundidade e
`on_jump` (M31) para as arestas de saída. Logo:
- a tabela de locais ganha **escopo por bloco** por essa via — empilha na entrada
  do `{`, desempilha na saída — e o shadowing resolve **no parse**, primeira
  linha de defesa; **não** se "defere todo `.` ao `pass()`" como paliativo;
- com o tipo do receptor conhecido, a busca de membro é **restrita àquele tipo**
  (e à cadeia de base/traits/interfaces dele) — nome que o tipo não declara é
  erro; o por-nome-único só sobrevive para receptor genuinamente sem tipo, e só
  até o oráculo (C3) responder;
- o oráculo do C3 fica para o que o parse **não alcança** (parâmetro de função de
  topo), como desenhado.
**Também no crumb do escopo (defeito de diagnóstico do C2, medido sem oráculo):**
`tk_call_refuse` (`ngen/teko_expr.mc:91-97`) mapeia `-2` para "the type of the left
side of `.` is not known here" — certo para `tk_method_by_name`, **errado** para
`tk_method_pick`/`tk_ifmeth_pick`, onde `-2` é "nenhuma assinatura aceita essa
quantidade de argumentos". `Alpha a; a.tally(1,2,3)` sem assinatura de 3 dá a frase
errada. Não miscompila; separar os dois significados de `-2` no contrato.

Esta correção entra como **crumb próprio, antes de C4/C5** — eles constroem em cima
da mesma resolução. Prova: os dois programas acima (o 1º sai 1; o 2º é rejeitado)
mais as fixtures existentes intactas e a prova de no-op do C3 preservada.

## 7. C7 landado com ressalva → C7b (2026-09-04)

**Landou** (`4521b3d8`): `i64 total(params xs)`, `xs[i]` por `syntax_infix("[")`
próprio (o core **não** constrói `N_INDEX`), sítio reescrito em
`total(tk_vaN(...), N)`. Teto real com `params`: 10 fixos e 12 argumentos por sítio.

**Ressalva medida pelo verificador:** `tk_va_at` (`ngen/lib/rt.mc`) faz `ld64` sem
guard — `xs[i]` fora do range devolve lixo do buffer em silêncio (provado: lê o
resto da chamada anterior). Fere a lei de falhar ruidosamente (`CLAUDE.md`, guard de
deref). **E a causa-raiz das duas restrições do crumb** (não-reentrância e lixo) é a
mesma: o pacote de argumentos mora num **buffer ESTÁTICO global** (`tk_va_buf[96]`).

**Orientação do dono:** o mc trabalha com **ponteiros 100% opacos** (`uptr`), e a
única extração é o **`&`** (address-of de local/global/função, `mc/docs/core-language.md:62`),
que os exemplos açucaram como **`ref`** (`examples/lang/README.md:55-75`, `lang.mc:72`).
Os agentes não incorporaram isso. **C7b, a correção:** o pacote passa a ser
**alocado por sítio** — na arena, por `rt_alloc(N*8)` (o mesmo caminho do `new`), ou
local + `&` — e o ponteiro opaco viaja com `xs_len`. Consequências: (1) chamada
variádica aninhada e variádica-dentro-de-variádica **deixam de ser recusadas** (não há
mais estado compartilhado); (2) `tk_va_at(xs, len, i)` ganha **guard de bounds com
`rt_panic`** (`arquivo:linha` no diagnóstico do pass onde couber); (3) `tk_va1..tk_va12`
somem — o pass emite a gravação por índice no bloco alocado.

**Ordem de passes (parecer do verificador):** registrar `pass(&tk_params_pass)`
**ANTES** do futuro pass de mangling do C4 — assim o C4 vê `total(ptr, n)` uniformizado
e não precisa saber o que é `params`. Guard que o C4 precisa: `total(params xs)` e
uma `total(uptr, i64)` colidem na mesma ABI depois do lowering.

**Errata de descrição (C2):** a mensagem de `fe750cdf` diz "a classe esconde as
sobrecargas da base, como em C#"; o que o código faz é **resolução por aridade,
nível a nível na cadeia** (cai na base quando a aridade não bate no nível atual).
Comportamento estável; a descrição é que está errada.

**C7b (em verificação, `49546d45`):** forma (a) — pacote por `rt_alloc(N*8)` com
`tk_va_put` encadeado na própria expressão; (b) local + `&` foi descartada com a doc:
`&` só aceita nome direto (`core-language.md:440`) e um pass sobre expressão não
insere statement antes de chamada que mora em condição/argumento/`return`. Guard nos
dois lados de `[0, n)` com `rt_panic`; recusas de reentrância removidas.
**Dívida nova, medida:** a arena bump do `rt.mc` não recupera — variádica de 2
argumentos em loop quente esgota os 4 MiB em ~262k chamadas (`teko: arena exhausted`,
exit 70). Falha **ruidosa**, nunca corrupção, mas é teto que o buffer estático não
tinha. A cura é reclaim/escopo de região (entrega de comportamento base, D214 item 3),
mesma dívida que o `new` já carrega — não deste crumb.

## 8. C8 — genéricos com constantes; C7c — `params` sobre C8 (dono, 2026-09-04)

**Ruling do dono:** o `params` com tamanho variável só se bloqueia pelo tamanho em
compile-time se o corpo for **instanciado por sítio com `N` constante** — é para isso
que ele pediu **genéricos com constantes** (`<T, const N: i64>`, provado no mc em
`examples/lang`: `Box<Circle, 4>`, D212). Fica **inline** e o índice literal é checado
contra `N` dentro da instância — sem análise interprocedural, sem guard de runtime
para o caso literal.

**C8 — genéricos com constantes (record/replay).** Precedente: `examples/lang/lang_class.mc:73-114`
(`lg_gen_record`: lê `<T, const N: i64>`, `p_start()` + `p_skip_balanced` — **não cria
a classe**) e `lang_type.mc:134-163` (`lg_replay`: monta `"class " + mangled + body`,
`p_subst_reset`/`p_subst_name`/`p_subst_int`, `p_push_source`, laço `top_add(parse_top())`;
`>>` por `p_resplit_punct(1)`, `lang_type.mc:91`). Guia: `mc/docs/guide/30-teaching.md`
§"Record and replay". Superfície C-like (D215): `class Box<T, const N: i64> { T items[N]; … }`
e `Box<Circle, 4> b = new Box<Circle, 4>;` — a forma do mc, sem `where` nesta fatia.
Instância é chave `(nome, args)` → mangling `Box__Circle__4`, uma vez por tupla.
Destrava também a forma genérica `<T>` de `wrap`/`unwrap` (handoff §5).

**C7c — `params` reescrito sobre C8.** Função com `params` vira **genérica em `N`**:
cada sítio com `k` argumentos instancia `total__k` com `N = k` substituído por
`p_subst_int`; `xs_len` deixa de ser argumento e vira a constante `N`; `xs[lit]` com
`lit < 0` ou `lit >= N` é **erro de compilação** na instância; índice não-literal
mantém o guard de runtime (`rt_panic`). O pacote continua alocado por sítio na arena
(C7b), até o reclaim. Teto `MAXPARAMS` = 12 permanece.

**Ordem revista da fila:** escopo pela via do mc (§6, corretude) → **C8** ∥ **C4**
(arquivos próprios: `teko_over.mc`) → **C7c** e **C5** (ambos tocam `teko_class.mc`
depois do C8) → C6 quando o mc der o hook.

## 9. C4 em verificação; C3b — o oráculo precisa tipar expressão (2026-09-04)

**C4 (`ecdd1a46`, em verificação):** `teko_over.mc`, pass registrado depois de
`params` e do oráculo. Decisão que difere do C2 de propósito: **toda** sobrecarga de
função de topo é renomeada (`pick__i64`, `pick__Vec`, `pick__i64__i64`, `tally__void`)
— nenhuma fica com o símbolo plano, para um sítio não reescrito virar **erro de link**
em vez de cair na primeira. Nome de assinatura única não muda. Guards: `&f` de
sobrecarregado; colisão ABI com `params` (`(uptr, i64)` homônima); `params` não se
sobrecarrega; `extern` e `main` não se sobrecarregam; ambiguidade recusada; o
`function declared twice` do core não é mascarado.

**Achado adjacente, medido — dívida do oráculo (C3b):** `tk_ty_of` responde por nome,
chamada, literal e cast, mas **não por `N_BINARY`/`N_UNARY`** (`pick(n - 1)` → "the type
of argument 1 of pick is not known here") nem por **acesso a membro escalar**
(`pick(self.side)` em método: `tk_pend_field` só registra no `xt` resultado de tipo
struct; escalar cai em −1). Ambos erram claro, não miscompilam — mas são formas comuns.
**A regra não é palpite, é a do core:** `mc/src/gen_resolve.mc` `res_binary` (~:486) —
tipo do binário = tipo do operando **esquerdo**; comparação e lógico = `i64`; `N_UNARY`
= tipo do operando, `!` = `i64`. Espelhar isso em `tk_ty_of`, e registrar no `xt` o tipo
escalar do campo em `tk_pend_field`. **Entra antes do C5** (operadores precisam tipar
`a + b` com `a` composto) e depois do escopo (mesmos arquivos: `teko_typeof.mc`,
`teko_expr.mc`). Fixture: `surface_typeof_expr.tk`.

**Fila revista:** escopo → C4 → **C3b** → C8 ∥ C5 → C7c → C6.

## 10. C3b em verificação; C3c — `.` sobre receptor escalar (2026-09-04)

**C3b (`9b73cd1f`, em verificação):** `tk_ty_of` tipa `N_BINARY`/`N_UNARY` espelhando
`res_binary` do core (usa o próprio `cmp_cond`); a tabela `xt` ganha `xt_ty` (tipo do
nó, escalar incluído) ao lado de `xt_str`; o placeholder deferido vira
`tk_call("tk_unresolved_member")` — sem o pass, o **`res_call` do core** recusa `call to
unknown function` com `arquivo:linha` (antes do linker; melhor que o pedido).

**C3c — achado adjacente, pré-existente:** `.` sobre receptor de tipo **escalar**
(`b.side.x` com `side: i64`, `x` declarado só por `Vec`) ainda cai no último recurso
por-nome e **compila**, emitindo load sobre um `i64`. Com o C3b o oráculo já distingue
"sem tipo" de "tem tipo e não tem membros" → recusar com `teko: i64 has no members`.
Entra junto do C5 (mesmos arquivos) ou como mini-crumb antes dele.

Nota do C4: literal em posição direta de argumento usa a preferência exata/frouxa
(`N_INT` sem tipo → desempata `i64`); literal dentro de binário (`pick(1 + 2)`) agora
responde `i64` pela regra do core — coerente, documentado.
**Fila revista:** escopo → C4 → **C8** → **C3b** ∥ C5 → C7c → C6.

## 11. C8 landado — o que ficou, e o que ele destrava (2026-09-04)

**Landou** em `ngen/teko_generic.mc` (módulo próprio; `teko.mc` só ganhou o
`#include`). O record é disparado no `class`/`struct` quando o nome é seguido de
`<` (`teko_class.mc:617`, `teko_struct.mc:713`): a lista de parâmetros é parseada,
tudo dali até o `}` é **gravado** por `p_skip_balanced`, nada é declarado, e o nome
do genérico é registrado com `syntax_stmt` — a única posição de onde
`Box<Circle, 4> b;` é alcançável, como o `lg_declstmt` do `examples/lang` faz.

O replay monta `class Box__Circle__4 <texto gravado>;`, liga os parâmetros
(`p_subst_name` para `T`, `p_subst_int` para `N`), empurra com `p_push_source` sob
`Box__Circle__4 instantiated from prog.tk:16` e drena `top_add(parse_top())` até a
profundidade voltar. Instância memoizada por `(nome, argumentos)`; ela entra na
tabela de tipos pelo **mesmo `type_new`** de uma classe qualquer, então `.`, `new`,
vtable e interface saem de graça. O `>>` de `Holder<Box<Circle, 2>>` é desmontado
por `p_resplit_punct(1)`.

**Três coisas medidas que o plano não previa:**

1. **O `;` do replay é obrigatório.** O `p_accept(K_SEMI)` que fecha um corpo de
   tipo roda depois do `}`; sem um `;` no fim do texto empurrado ele alcança o
   token seguinte da fonte de FORA e come o `;` do `new Box<Circle, 4>;`. O texto
   empurrado termina com `;` próprio.
2. **O replay tem de SALVAR o scratch de declaração.** Uma instanciação pode
   acontecer dentro do corpo de outro tipo (campo de tipo genérico, local numa
   método) — o `tk_class` aninhado zeraria a fila de traits (`tk_ntu`/`tk_nud`) e a
   lista de conformância (`tk_nconf`) do tipo de fora. `tk_gen_replay` salva e
   restaura os dois, mais `tk_line`/`tk_file`/`tk_own_methods`. Verificado por probe:
   classe com `use Counted;` + campo `Bag<Circle, 3>` + parâmetro e local genéricos.
3. **Campo array inline teve de ser ensinado junto** (`T items[N]`), porque é ele
   que faz a constante decidir o LAYOUT: `BOX__CIRCLE__2_SIZE` = 32 e
   `BOX__CIRCLE__4_SIZE` = 48, do mesmo texto. `p.items` é o ENDEREÇO do array,
   etiquetado com tipo do elemento e comprimento, e o `[` (`tk_bracket`) o consome
   no parse — não vira `N_INDEX`, então não cruza com o `params`.

**O bloqueio pelo tamanho constante — o que o dono pediu — está vivo:** `items[k]`
com `k` literal fora de `[0, N)` é erro de COMPILAÇÃO na instância
(`Box__Circle__4 instantiated from prog.tk:16:6: teko: index 5 is past the end of items[4]`).
Índice não-literal recebe guard de runtime `tk_ix`, **emitido pelo módulo uma única
vez e só no programa que indexa** — assim a prova de no-op continua exata (as 15
fixtures anteriores dão `--dump-ast` byte-idêntico a `043455b8`); pô-lo em
`lib/rt.mc` teria mudado a árvore de todo programa.

**Dívida medida:** `p.items[i]` sobre receptor que o parser não tipa (um parâmetro,
que só o oráculo resolve) não alcança o `[` de array e cai no `[` do `params`, que
recusa com `teko: `[` indexes a `params` list only`. Recusa clara, nunca
miscompilação; o fecho é no `teko_typeof.mc` (C3b).

**C7c fica pronto para escrever:** a máquina de que ele precisa — corpo instanciado
por sítio, `N` como literal substituído, índice literal barrado contra `N` — é
exatamente a que o C8 deixou.

## 12. C5 pronto (aguarda verificação), com duas regras que o plano não previa (2026-09-04)

**C5** (`feat/ngen-operators-v2` @ `c4124f26` = `299e1366` + costura com a API do C3c):
`teko_ops.mc`, `operator<op>` contextual em `tk_member`, pass entre o oráculo e o C4.
`N_BINARY` chega **intacto** ao pass (passes rodam antes de `fold()`), a troca in-place
sobrevive. 18/18; no-op nas 17.

**Armadilha medida, resolvida estruturalmente:** o próprio `ngen` constrói `N_BINARY`
com valor teko à esquerda — `p.side` é `ld64(p + SIDE)`, `items[i]` é
`ADD(ADD(obj, off), MUL(i, w))`. Sem distinguir, `operator+(self, i64)` declarado
transformaria todo acesso a campo em chamada. O pass trata o 1º argumento de
`ld8..st64` e a espinha esquerda desses `ADD`s como **endereço**, nunca operando.

**Regra de resolução (obrigatória por `surface_typeof_expr.tk:64`, `pick(v + zero)`):**
teko à esquerda que **não declara** o operador + valor do core à direita → o pass **não
toca** (é a aritmética de ponteiro do core). Teko dos dois lados, ou operador declarado
com assinatura que não casa → erro claro. Esquerdo do core + direito teko → recusa
("a reversed operator is not taught"). Unário → honest-stop.

**Adjacente (diagnóstico, não miscompila):** `operator` dentro de `interface` é recusado
por `teko_iface.mc` com "an interface declares methods, not fields: operator" — mensagem
confusa; dizer que operador em interface não é ensinado. Mini-ajuste em `teko_iface.mc`.

## 13. C7c pronto (aguarda verificação) — instância por cópia de AST (2026-09-04)

**Medido: record TEXTUAL de função de topo é inalcançável no mc 0.10.0** — `parse_top`
(`mc/src/parse.mc:2076-2082`) consulta `syntax_find` só no 1º token, que numa função é o
tipo de retorno, e `word_add` recusa keyword do core (`cannot redefine core keyword:
i64`). Confirma o bloqueio do C6. **Saída adotada (`feat/ngen-params-generic` @
`5104808a`):** como o único parâmetro genérico do `params` é a constante `N` — não há
tipo a substituir — a instância é **cópia da AST** com `N_INT` no lugar de `xs_len`
(`tk_va_inst`), memoizada por `k`, mangling `total__k`, moldura de erro `tk_gen_frame`
do C8. Sufixo numérico não colide com o `nome__Tipo` do C4. O template sai da unidade
(`tk_va_drop` → `N_NONE`) porque o `tk_ov_check_left` do C4 varre o array de nós.
`xs[lit]` fora de `[0, N)` → erro de compilação com prefixo de instância; não-literal
mantém `tk_va_at`. 17/17; no-op nas 16.

**Adjacentes:** (1) `teko_over.mc:177-192` `tk_ov_va_shape` ficou **obsoleta** — a lista
nunca mais lowera para `(uptr, i64)`; duas `g(uptr, i64)` escritas à mão num programa sem
`params` levam a mensagem errada ("a params list is (uptr, i64)…"). Apagar o guard.
(2) `params` em **método** é recusado (`wrong number of arguments`) — pré-existente;
`tk_method_pick` não sabe o que é lista. (3) O teto de 12 virou **política**: a instância
gasta um registrador a menos; se o dono quiser o teto real do ABI, são dois números.
