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

## 14. Entrega 5 — comportamento base; crumb 1 = RECLAIM pela "arena automática" do mc (dono, 2026-09-04)

**Ruling do dono:** o reclaim segue o precedente do mc — a "arena automática" dos
exemplos. É o `examples/lang`: **arena fixa (4 MiB) com free lists por classe de
tamanho + reference counting por escopo**, tudo por hooks (`docs/guide/60-examples.md:171`,
`:264` "objects inside a 4 MiB arena, which is what proves the deallocation is real").
Peças, e onde o `ngen` já as tem:
- refcount no **word 1** do objeto (`+8`) — o `ngen` reservou desde o crumb do `class`
  (header 16 B: vtable@+0, refcount@+8); `Class_release` no **slot 0** da vtable;
- `rc_dec` para cada local de tipo classe na **saída do bloco**, em ordem reversa de
  declaração (`lang_stmt.mc:313-322`) — é o `syntax_stmt("{")` que o crumb de escopo já
  possui, agora injetando código; **`on_jump`** entra aqui, nas arestas `return`/`break`/
  `continue` (`hooks.md:495-541`);
- `return e` → `{ T $t = e; rc_inc($t); releases…; return $t; }` (`lang_stmt.mc:329-345`);
  `lg_eown` decide se a expressão já é dona (um `new`) ou precisa de `rc_inc`;
- `p.f = e` de campo classe: `rc_inc` do novo, `rc_dec` do antigo; `dispose(self)` roda
  quando o count chega a zero (`lang/README.md:78`); `rt_free` devolve ao free list.
Fecha a dívida de `new` e de `params` em loop quente (hoje: `arena exhausted` ruidoso).
**Prova:** fixture `surface_reclaim.tk` (`// expect-exit: 42`) com `live()` (contagem de
objetos vivos, como o `01-inherit.lx` imprime) voltando a 0 ao fim, e um loop de 1M
`new` + descarte que **não** esgota a arena. Ratchet: o pico de nós/heap do compilador não
cresce além do que o RC exige (`mc limits`).
Restrições: zero Variant (D217); nenhum toque no mc; forma C-like (D215).

## 15. Rulings D218 — o que muda na fila (2026-09-04, noite)

- **C5 está ERRADO e vai ser refeito (C5b):** operadores **como em C#** — estáticos, sem
  `self`, dois parâmetros explícitos (unário: um), `i64 + Vec` permitido (operador declarado
  em qualquer dos tipos dos operandos), resolução por sobrecarga sobre os dois tipos, pares
  obrigatórios (`==`/`!=`, `<`/`>`, `<=`/`>=`). O pass sobre `N_BINARY`/`N_UNARY` e a regra do
  §12 (endereço ≠ operando; core+core não se toca) **ficam**; muda a declaração, a tabela de
  candidatos e a resolução. A fixture `surface_operator.tk` é reescrita na forma nova.
- **Reclaim (entrega 5, crumb 1) redespachado** com **construtor/destrutor**: `Nome(params)`
  chamado por `new Nome(args)`; `~Nome()` chamado pelo release. Sem `dispose`.
- **Fila da entrega 5:** reclaim c/ ctor/dtor → C5b → `while`/`for` (prelude do mc) →
  `namespace`/`import`/`using` (lx) → `const` como açúcar sobre `#define` → (`match`/`when`
  em dúvida) → stdlib mínima. **Fora:** `var`, `type`. `break N` já é do core.
- **Pergunta ao dono em aberto:** C# escreve `public static … operator+` e `public Vec(...)`;
  o ngen ainda não ensina `public`/`static` (D196). Ensinar os modificadores junto, ou aceitar
  a forma sem modificador (`Vec operator+(Vec a, Vec b)`, `Vec(i64 x) { }`) até o D196 entrar?

## 16. D219 — `this` implícito e `base`: sweep ANTES do reclaim (2026-09-04, noite)

Métodos deixam de declarar `self`. O compilador injeta o receptor oculto (mesmo mecanismo
de hoje, `tk_params(... extra)`), `this` vira palavra contextual dentro de corpo de
tipo, nome não-qualificado que não é local/param resolve como `this.nome` (C#: local
sombreia campo), e `base.m()` chama a implementação da base direto (precedente `lx`:
`01-inherit.lx`, `README.md:71`). Interfaces: `i64 area();`. Construtor/destrutor
(D218) idem. Operador estático (D218) não tem `this`.
**Ordem:** este sweep toca `teko_class.mc`, `teko_iface.mc`, `teko_trait.mc`,
`teko_struct.mc`, `teko_expr.mc`, `teko_typeof.mc` e as 18 fixtures — por isso vem
**antes** do reclaim e do C5b, que escreveriam código na forma velha. Prova: 18/18 na
forma nova; `grep -c "self" ngen/tests` = 0; `base.m()` numa fixture existente.

## 17. D220 — visibilidade e `static`; fila revista (2026-09-04, noite)

Crumb **"membros C#"**: `public`/`private`/`protected`/`static` em membros, `public`/`internal`
em tipos; defaults do C# (tipo → `internal`, membro → `private`) **ratificados**; `internal` =
**código do projeto** (o que entra pelo `mc.toml` do próprio projeto; decidido pela origem da
declaração via `nd_file` — arquivo do projeto vs. bundle `<…>`/include externo), nem arquivo
nem namespace; checagem no `.`, na chamada, no
`new` e no `base.` — nos dois caminhos (parse e pass); `protected` = próprio tipo e derivadas;
`static` = sem receptor (é o que o operador do C5b usa; membro estático acessado por
`Tipo.m()`); nested **não**. Fixtures ganham `public` onde acessam de fora.
**Fila da entrega 5:** `this`/`base` (em voo) → **membros C#** → reclaim c/ ctor/dtor → C5b
operadores estáticos → `while`/`for` → `namespace`/`import`/`using` → `const`.

## 18. D221 — `loop` fica; closures sobre `&fn`/`callp` (architect-first) (2026-09-04, noite)

`loop`/`break N`/`continue`/`if` do mc **ficam**; `while`/`for` do prelude são adição.
**Closures:** crumb com desenho prévio (teko-architect): (1) função **local** (dentro de
outra) hoisted para o topo com nome manglado; (2) **lambda** inline; (3) tipo de função na
superfície e chamada `f(x)` açucarando `callp`; (4) passagem por `&fn` como `uptr`;
(5) **captura = PHP** (ruling do dono): explícita por **`use (a, b)`** na declaração da
função local/lambda; por valor por padrão, **`use (&a)`** por referência (o `&x` do mc);
objeto gerado com exatamente os campos do `use`; `use` é a mesma palavra contextual do
trait. Ao architect sobra: a forma C-like da lambda (ex.: `i64 (i64 x) use (a) { ... }`
como expressão, e função local nomeada `i64 f(i64 x) use (a) { }` dentro de outra),
tempo de vida do capturado por referência (escopo/RC), e recusas claras (capturar nome
que não é local; `use` sem função). Precedentes: `mc/docs/core-language.md`
(`&x` de função), `examples/desktop` (callbacks GTK bidirecionais), `examples/conc`
(`spawn` com `&fn`). Entra na fila depois de `const`.

## 19. D222 — `switch` (statement + expression), `break N` atravessa, `when` = guarda (2026-09-04, noite)

Crumb **`switch`** (`syntax_stmt("switch")` + `syntax_infix`/postfix para a forma expressão —
o `x switch { … }` tem `switch` à direita do operando; ver como o core deixa registrar uma
palavra em posição infixa sem ser operador do core): statement rebaixado a `loop` de uma
volta com `if`/`else` encadeados e `break` ao fim de cada braço (assim `break`/`break N`
do core atravessam como em C#); `default`; `case` com constante ou `when` guarda; expressão
com `=>` e `_`, tipada pelo oráculo (todos os braços do mesmo tipo, senão erro).
`match` sai da fila. Entra depois de `const`, antes das closures.

**§16 — sweep `this`/`base` pronto (`feat/ngen-this-base` @ `c224ac5e`, em verificação):**
módulo `teko_this.mc`; o receptor oculto chama-se **`this` também na AST** (`self` não
existe mais no `ngen/`); `this` é palavra (`syntax_expr`) válida só em corpo de tipo;
`base` é **contextual** (lido no `.`), rebaixado a chamada direta ao símbolo da base
escolhido por assinatura — contextual porque `offset_total(i64 base, params rest)` existe;
nome não-qualificado resolve no **pass** (o core entrega identificador cru; é onde locais/
parâmetros são legíveis), local/parâmetro sombreia campo. Prova: AST nova byte-idêntica à
velha em 16/18 módulo `name=self→this`; as 2 restantes divergem só pelo `base.m()`.
**Limite honesto:** campo array inline (C8) só por `this.items[k]` — recusa clara.

**§18, mecanismo (dono):** ponteiro de função, `ref T` e `out T` são **primitivas novas** por
`type_new` (Tier 4, `mc/docs/guide/96-a-new-primitive.md`: id ≥ `TY_MAX` é do módulo), não
`uptr` cru: o oráculo os distingue, `f(x)` sobre valor-função rebaixa a `callp` com o retorno
tipado pela assinatura; `ref`/`out` levam `&` no sítio e deref implícito no uso; `out` é o
DPS. Vai junto para o architect das closures — mesmo crumb de desenho.

## 20. D223 — propriedades, corpo default e `static` em `interface` (2026-09-04, noite)

Dois crumbs, logo após "membros C#":
- **Propriedades:** `get`/`set`/`value` contextuais em corpo de tipo; auto-propriedade gera
  campo de apoio (`private`); `p.X` → `get`, `p.X = e` → `set` nos dois caminhos (`tk_dot`
  no parse e `tk_pend_*` no pass); `virtual`/`override`/`static` e visibilidade por
  acessor; em `interface`, `i64 X { get; set; }` = assinaturas. Fixture
  `surface_property.tk`.
- **Interface v2:** corpo default (símbolo `Iface_m` no itab quando a classe não redefine;
  `this` dentro do default é o receptor implementador) e `static abstract` (o tipo
  implementador fornece `static`; `Tipo.m()`; conformidade checada como os métodos).
  Fixture `surface_iface_default.tk`. O crumb de membros em voo **recusa** `static` em
  interface com mensagem — este crumb substitui a recusa.

**§20 — propriedades e interface v2 PRONTOS** (`feat/ngen-properties`, 3 commits): módulo
`ngen/teko_prop.mc`; **acessor = método comum** da tabela do `teko_class.mc` (`get_X`/`set_X`),
de onde saem slot de vtable por acessor, `static`, sobrecarga e visibilidade por acessor;
auto-propriedade com campo de apoio `private` (`Nome__backing`); `set => ...` é STATEMENT
(`=` não é infixo do core); `get`/`set` lidos **só** dentro das chaves da propriedade e `value`
**só** no corpo de acessor — nenhuma palavra confiscada (`public T get()` de
`surface_generics.tk` intacto). Interface: corpo default vira `iface_m(uptr this)` no itab de
quem não redeclara, com `this` = receptor implementador e **todo** membro alcançado ali
despachando pelo itab; `static abstract` conforma como método e **não ocupa slot** (o slot é a
posição entre os de instância, `tk_ifslot`); propriedade de interface = assinaturas. 20/20 em
exit 42, AST das 18 anteriores byte-idêntica a `5579c34b`, `mc limits` ok. Limite: um default
só alcança membro declarado acima dele.

## 21. D224 — `abstract` (ruling) e `partial` (em avaliação) (2026-09-04, noite)

- **`abstract`:** entra no crumb de membros/propriedades ou logo após: `abstract class`
  (não instanciável), `abstract` método (sem corpo, slot de vtable, obriga `override` na
  primeira concreta), `abstract` só em classe `abstract`. Fixture `surface_abstract.tk`.
- **`partial class` (ratificado; método parcial NÃO — `partial` em método é erro claro):** custo a levantar — o tipo só fecha (layout, vtable,
  record de genérico) quando todas as partes foram lidas; como o mc parseia em uma passada e
  o `.` resolve no parse, parte declarada depois do 1º uso exige fechar o tipo no **pass**
  (o oráculo já resolve `.` deferido — é o mesmo mecanismo) ou exigir que as partes venham
  antes do uso. Sem método parcial. Fixture `surface_partial.tk`: a mesma classe em dois arquivos
  (`#include` do segundo), campos e métodos de ambas as partes, `new` depois das partes. Precedente do
  mc para "reabrir": `namespace` mergeando por prefixo (`examples/lang/README.md:265`).

**§17 — membros C# pronto (`feat/ngen-members` @ `0d3044a9`, em verificação):** módulo
`teko_access.mc`; **`internal`** = declaração lida de arquivo **dentro do diretório do
`mc.toml`** da build (sem config: o do arquivo de entrada); absoluto, `../` e `<bundle>` são
externos — decidido por prefixo de caminho normalizado, sem syscall. Duas origens apenas
(projeto / resto); instância de genérico herda a origem do template; membro de trait
copiado é da classe. `static`: campo → global mangled, método sem receptor, `Tipo.m()`.
AST 17/18 idêntica; `types_struct` diverge só pelo `static`.
**Achados:** (1) `p_start()` mente em token substituído (`subst_apply` troca `tok_start` pelo
lexema na arena) — usou `cp`; candidato a `p_cp()` público no mc; (2) `operator+` privado
ainda funciona de fora — o pass de `N_BINARY` não checa visibilidade; fica para o **C5b**
(operadores estáticos); (3) HANDOFF §5.1 item 8 estava obsoleto quanto a `base.m()`.

## 22. mc 0.12.0 — o que as releases 0.10.3/0.11.0/0.12.0 mudam na fila (2026-09-05, madrugada)

Baseline `fix/retirement` com **0.12.0**: 18/18 sem uma linha mudada. Do que entrou:
- **0.10.3 = M41.5 (PR #17), "the follow-ups the ngen consumer exposed":**
  1. **`syntax_param(&f)`** — hook na cabeça do laço de `parse_params`, antes de
     `type_of_token`, contrato de `syntax_lit`: `i64 f()` devolve um `N_PARAM` ou 0. Mais
     **`p_decl_name()`/`p_set_decl_name()`** (a que declaração o parâmetro pertence).
     **Desbloqueia o C6** (default em função de topo: gravar o default na declaração,
     completar no sítio por `pass()` + `decl_find` — a prova está em `lib/user_syntax_demo.mc`)
     e permite o `params` como palavra ensinada na declaração (hoje: `type_new` + pass).
     Guard novo: handler que **consome tokens e devolve 0 é recusado** (`tests/err/073`).
  2. **`syntax_infix` sobre operador do core FUNCIONA** (`ops_init` lazy; `syntax_infix` o
     chama primeiro); a precedência do módulo vence; `#infix` de fonte ainda derruba o
     handler; duplicata recusada. **Supersede o §1.3**: o C5b pode escolher entre
     `syntax_infix` (parse-time, tipa pelo oráculo do sítio) e o `pass()` sobre
     `N_BINARY` (já existe e resolve pelo tipo dos dois operandos). Preferir o **pass**
     (tem a regra de endereço do §12 e vê os dois tipos); registrar a escolha no C5b.
- **0.11.0 = M40**: AVR bare-metal, `uptr = 2` por `type_set_width` — não afeta o ngen.
- **0.12.0 = M42 (PR #19)**: **`--exe` em todo Linux sem `[linker]` e sem sysroot**
  (`elf-exe`/`elf-exe-x86_64`, dinâmico com musl/glibc por `[target].interp`/`.libc`).
  → as pernas Linux do CI podem dispensar `[linker] cc` (mini-crumb de CI; manter `cc`
  até medir que o `--exe` dinâmico roda no runner — o PR testou alpine e ubuntu 26.04).
**Fila (revista):** membros C# (em verificação) → propriedades/interface v2 → `abstract`/
`partial class` → reclaim c/ ctor/dtor → C5b → **C6 (agora alcançável, `syntax_param`)** →
`while`/`for` → `namespace`/`import`/`using` → `const` → `switch` → closures/`ref`/`out`.

## 23. Respostas da sessão do mc (canal `mini_compiler/build/NOTICES-teko.md`, 2026-09-05)

**Canal:** `send_message` mc→teko nunca é processado (esta sessão está sempre com agente em
voo); o arquivo `build/NOTICES-teko.md` (gitignored, no repo do mc) é o canal mc→teko — **ler
ao começar cada lote**. teko→mc por `send_message` funciona.
- **`syntax_param`:** a guarda ("consumed tokens and returned 0") é no fim da cadeia —
  registrar **por último** o handler que reivindica. A metade que o hook não faz: `f(1)` é
  parseado pelo core; completar o default é `pass()` + `decl_find`/`decl_nparams`
  (`lib/user_syntax_demo.mc` faz o ciclo). → C6.
- **Escopo — alerta:** tabela linear com marca no parse (`lg_block`, `lang_stmt.mc:466`)
  **OU** escopo pela árvore num `pass()` — **nunca híbrido**; foi a causa dos dois bugs
  silenciosos. O ngen hoje tem os dois (pilha no parse + oráculo por bloco no pass), verificados
  coerentes — **risco registrado**: qualquer divergência entre os dois é bug; candidato a
  unificar (o pass como fonte única) quando o reclaim/RC entrar, que também é por escopo.
- **`open`/`int` em `i64`:** hazard latente (bits altos de retorno de 32 bits). **M45** traz
  `i32` + retorno com o tipo declarado → `extern i32` para funções C que devolvem `int`. Até lá:
  retorno descartado (é o que `surface_overload_free.tk` faz com `chmod`).
- **Fila do mc:** patch pós-M42 (`--interp=`/`--libc=`; **`[target].libc = gnu|musl`** e
  `link = dynamic|static`) → trocar as pernas Linux do CI para `libc = "gnu"` quando sair;
  M45; M43 (sandbox); **M44 (pacotes estilo Go): o ngen seria o primeiro pacote "módulo de
  compilador"** — `[package]` com `lib`/`module`; regra: **um pacote nunca define `user_init`,
  exporta `<nome>_init()`** (`docs/specs/M44.md` §6 + emenda). Desenhar o `ngen` para virar
  `teko_init()` exportado.
- **Sem 1.0.0 sem coordenação com o ngen** — regra do dono.
- **`region crosses a file boundary`:** restrição de desenho com motivo (um `#include` dentro
  da região gravada muda o buffer; suportar exigiria copiar bytes com o include expandido e
  perder atribuição por arquivo). **Contorno certo = gravar a instância no arquivo declarante e
  replayar de lá.** Se `partial class`/genérico importado virar bloqueio real, mandar o caso.
- **`p_cp()`:** entra no lote do M45; até lá, acesso direto ao `cp` com comentário "temporário"
  (`teko_access.mc:253`).

## 24. O ngen como pacote do mc (M44, futuro) — forma já definida

`docs/specs/M44.md` §6: o pacote teko é do tipo **"both"**, como `<float>` — `[package]`
com `files`, `lib = "rt.mc"` (o que um PROGRAMA inclui) e `module = "teko.mc"` (o que um
COMPILADOR inclui, que **exporta `teko_init()` e nunca define `user_init`**; o projeto
consumidor escreve as seis linhas do `user_init` chamando `teko_init()`). Precedente:
`lib/user_float.mc`. Crumb quando o M44 sair: (1) `teko.mc` passa de `user_init()` para
`teko_init()` exportado + um `user_init` mínimo no projeto `ngen/` (o CI continua igual);
(2) `mc.toml` do `ngen/` ganha `[package] name = "teko"`, `files`, `lib`, `module`;
(3) regra do M44: todo arquivo lido sob a raiz do pacote tem de estar em `files`. Sem
mudança de superfície. Coordenar a numeração com o mc (sem 1.0.0 sem o ngen).

## 25. D225 — o rumo: auto-hospedagem da teko via re-arch do mc (M41)

Não é entrega agora; é o **destino** que ordena as entregas. Etapas, cada uma sem
mudança de superfície: (1) **recriar** o compilador teko das partes do `<mc/core>`
(`core_min` + as máquinas/writers que os alvos do CI usam), como `examples/avr` e o
`check-parts.sh` fazem — `[compiler].core` próprio em vez do bundle inteiro, medindo o
tamanho; (2) `subcommand("build", …)` → `teko build` como driver, `type_disable`/
`intrinsic_disable` para o que a teko redefine (candidatos: os tipos que a teko trata por
`type_new`); (3) M44: pacote `teko` com `teko_init()`; (4) **auto-hospedagem**: reescrever
`teko_*.mc` em teko (`.tk`), compilar com o `mc-teko` atual → `teko1`, com `teko1` → `teko2`,
`teko2` → `teko3`, `cmp teko2 teko3` byte-idêntico — o mesmo rito de bootstrap do mc, e o
único fixpoint que importa daqui para a frente (o `gen2==gen3` do `src/` congelado morreu com
o D211). Pré-requisitos de superfície para (4): tudo que os módulos `.mc` usam hoje — ponteiro
de função/`&fn`/`callp` (D221), `ref`/`out`, arrays globais, `#define`/`const`, `#include`,
`extern`, `switch` (D222), closures — logo a entrega 5 é, na prática, a lista do que a teko
precisa para escrever o próprio compilador.

## 26. Roadmap do mc até o 1.0.0 (da sessão do mc, `NOTICES-teko.md`, 2026-09-05)

Ordem: **(1) patch pós-M42** (0.12.x, em implementação): `[target].libc = "gnu"|"musl"`,
`[target].link = "dynamic"|"static"`, `--interp=`/`--libc=` → **trocar as pernas Linux do CI
para `libc = "gnu"`** quando sair. **(2) M45 `i32`** (0.13.0, em implementação): `i32` pelo
core via `type_new("i32", 4, 4, TK_SINT)` antes do `user_init`; kind **`TK_SINT`** (sinal por
kind: `type_new("i16", 2, 2, TK_SINT)` de um módulo ganha tudo); retorno de chamada e `return`
estendidos pelo tipo DECLARADO; `c_int()`; **`p_cp()` público** → no ngen: `extern i32` para C
que devolve `int`; `TK_SINT` nos inteiros assinados próprios; trocar o acesso ao `cp` por
`p_cp()`. **(3) M43 sandbox** (spec ratificada). **(4) M44 pacotes** (spec ratificada).
**(5) M46** linker estático de `.a` (candidato). **(6) M33 wasm** (último). Entre 4 e 1.0.0 só
correções; **1.0.0 = decisão do dono conosco**.

**M44 — o que vem de lá (NÃO desenhar do lado da teko):** identidade por nome de registro +
tag `vX.Y.Z`; `[deps]` = mínimo (MVS do Go); `#include <pack/lib.mc>`/`<pack>` resolvido por
lock → bundle → pacote `mc` (nunca o cwd); `mc.lock` com hash de conteúdo (dirhash sobre
`mc.toml` + `[package].files`; a lista é a fronteira — arquivo lido fora dela é erro); registro
`schivei/mc-registry` por PR; fetch por tarball da tag (`curl`/`wget`), `deps/` vendoring,
`[replace]`; comandos `mc pkg sync|add|list|vendor|verify|hash|check`, `mc install|update|
upgrade` (com `--yes`); binário `mc-slim`; `mc --version`. **O que é da teko:** o sistema de
pacotes da PRÓPRIA teko (imports/namespaces da linguagem), a forma do `teko_init()`, o
`user.mc` do compilador teko. O ngen = pacote "ambos" (`module = "mc_teko.mc"`,
`lib = "teko_rt.mc"`).

**Auto-hospedagem — já possível hoje (M41):** `mc build` com `[compiler].core =
"<mc/core_min>"` (+ partes) e `modules = ["<teko/mc_teko.mc>", "user.mc"]`; fixpoint
`teko2 == teko3` pelo protocolo do `scripts/bootstrap.sh` do mc (`cmp` dos objetos +
`--dump-asm` diff vazio). Com o M44 vira pacote pinado. O 1.0.0 dá a promessa de superfície
estável, não a capacidade. → **Crumb "compilador teko de `core_min`"** entra na fila da
entrega 5 (independente dos construtos): medir tamanho e provar que o CI de 5 pernas passa
com o core mínimo + as partes que os alvos usam.

**§21 — D224 pronto (`feat/ngen-abstract-partial` @ `65654014`, em verificação):** `abstract`
como C# (membro abstrato ocupa slot de vtable sem corpo; derivada concreta sem `override` é
erro nomeando propriedade e acessor); **`partial class` fecha no primeiro USO** (`new`, ou
derivação) ou no fim da unidade por um pass à frente dos demais — parte depois do uso é
erro claro; membro não precisa do fecho (nome nu resolve no pass). Pré-requisito feito:
tabelas por **posse** (`fd_cls`, `vs_cls`, `ci_cls`) em vez de fatias — tipo declarado
entre duas partes corrompia o layout em silêncio. Base só numa parte antes de membros;
interfaces em união livre. `partial` genérica em dois arquivos funciona (grava por parte no
arquivo declarante). **Defeito pego só pelo CI:** com config em caminho ABSOLUTO,
`tk_origin_of_file` diz "fora do projeto" para tudo e **nenhuma checagem de `internal`
dispara** — a validação local fica cega. **Regra: validar sempre com config RELATIVO e cwd
no repo** (o laço do coordenador já é assim). O `region crosses a file boundary` é
pré-existente: dispara quando a declaração gravada é a última coisa de um arquivo
incluído (`nopen` antes/depois); contorno `;` — reportado ao mc.

**§14/§15 — reclaim pronto (`feat/ngen-reclaim` @ `6212ab86`, em verificação; D227):** RC e
posse **só no pass** (`teko_rc.mc`, `tk_rc_pass` por último); `TK_VT_FIXED 2`; `refcount@+8`
reservado de fato (4 fixtures mudaram números de layout); ctor `public Nome(params)` com
`: base(args)`, `~Nome()`; release derivada→base→campos→`rt_free`; `rt_park`/`mark`/`sweep`
para valor possuído sem dono; 3 bugs fechados nas probes (marca de store no nó descartado
pelo `.` deferido → contagem negativa; possuído sem dono vazava; `decl_find` por nome errava
posse com sobrecarga). 23/23; nodes +3%; 1M `new` em 0,03 s com `rt_peak() <= 4096`.

**§26 — avisos do mc (2026-09-05, madrugada):** (1) patch pós-M42 pronto (release 0.12.x):
`[target].libc` vira família `"gnu"|"musl"` e a grafia soname é **recusada** → o CI do ngen
escolhe a grafia pela versão resolvida (`sort -V` vs 0.12.1); `[target].link`; flags. (2)
`region crosses a file boundary` no fim de arquivo incluído: **falso positivo confirmado** —
`p_skip_balanced` calcula `e` e só então chama `next()`, cujo lookahead fecha o arquivo; o fix
(decidir pelo frame do token de fechamento) entra no lote do M45. O `;` segue como contorno.

## 27. C5b landado — operadores como C#, resolvidos pelos dois operandos (2026-09-05)

Fecha o "C5 está ERRADO e vai ser refeito" do §15 (D218). Branch `feat/ngen-operators-cs`.

**Declaração.** `public static T operator<op>(A a[, B b])` em `class` e em `struct`:
membro **estático**, sem `this`, sem slot de vtable. Binários `+ - * / % == != < <= > >=
& | ^ << >>`; unários `- ! ~` — e `+`, aceito na declaração, mas **sem sítio**: o core
registra só `- ~ ! &` como prefixo (`mc/src/parse.mc` `ops_init`) e não há hook
`syntax_prefix`, logo `+v` é `expression expected`. Dívida do lado do mc, registrada.

**Resolução pelos DOIS operandos** (`teko_ops.mc`, tabela `op_mi/op_cls/op_tok/op_np/
op_t0/op_t1`): os candidatos são os operadores declarados pelo tipo de **qualquer**
operando e pelas **bases** dele; pelo menos um parâmetro tem de ser do tipo declarante.
Três rodadas, nessa ordem — **exata**; **literal** (a do C4: `N_INT` cai em `i64` na 1ª e
em qualquer inteiro do core na 2ª); **base** (operando de tipo DERIVADO num parâmetro da
base). A 3ª é uma precisão do "casamento exato" do ruling, e não uma folga: um objeto
derivado JÁ é um da base (campos base-first), a conversão é de zero bits, C# faz o mesmo,
e sem ela um operador herdado ficaria declarável e inalcançável. Como é a ÚLTIMA rodada, o
tipo que declara o seu próprio sempre vence. Duas declarações na mesma rodada = ambiguidade
recusada, **exceto na rodada base**, onde `tk_op_pick_best` aplica o desempate "ancestral
mais próximo" do C# (§12.6.4): entre `GrandBase`/`MidA`/`Kid`, `Kid + 2` escolhe o operador
de `MidA` por distância na cadeia de `base`; ambiguidade entre bases não-relacionadas segue
recusada (branch `feat/ngen-ops-nearest`).

**Pares obrigatórios** no `pass`, quando a unidade fecha — assim `partial class` escreve as
duas metades em partes diferentes. **Visibilidade no sítio** (`tk_check_member`): era o
achado 3 do crumb de membros, que o C5 não checava.

**A rota continua sendo o `pass()`**, apesar de o `syntax_infix` sobre operador do core ter
passado a funcionar no 0.10.3 (M41.5, §22): no parse o tipo de um operando que é parâmetro
ou `.` deferido não existe, e o handler não veria a regra de endereço do §12. Os dois
motivos ficam no cabeçalho do `teko_ops.mc`.

**Posse (D227):** o `tk_xt_put` do pass é o que diz ao `teko_rc.mc` que um operador com
retorno de classe entrega referência do caller. Medido com `rt_live()`: `(a+b)==c` não muda
a contagem (o temporário é parked/sweeped com a statement), `-a` e `2+a` sobem 1 cada, e a
saída do bloco volta a 0.

**Adjacente medido:** `p.side + 1` (campo `i64` de receptor teko) NÃO colide com a resolução
por dois operandos — todo endereço que o ngen constrói tem o valor teko à ESQUERDA e um
deslocamento à direita, e o marcador de endereço o pega antes de `tk_ops_binary`. O caso
novo (core à esquerda, teko à direita) não existe entre os nós que o projeto emite.

23/23; a AST das 22 fixtures que não usam operador é byte-idêntica à de `05dc7181`;
`mc limits` verdict `ok`.

## 28. C6 landado — default de parâmetro em função de topo, decisões (2026-09-05)

`syntax_param` (mc 0.10.3) desbloqueou o C6: `i64 add(i64 a, i64 b = 10)`, `add(1)` →
`add(1, 10)`. Módulo novo `ngen/teko_default.mc`; nada tocado em `ngen/mc.toml` nem no
`src/` do mc.

**Ordem do pass — decisão e porquê.** O crumb pedia para decidir se o preenchimento roda
antes do `tk_over_pass` ou dentro dele, e registrar o motivo: as DUAS coisas, cada uma na
metade certa.
- **Antes** (`tk_default_pass`, pass novo, registrado logo depois de `tk_ops_pass` e antes
  de `tk_over_pass`): resolve sozinho todo nome declarado EXATAMENTE UMA VEZ na unidade —
  não há tipo a comparar, só aridade contra a tabela de defaults, e a resposta nunca
  depende de outra declaração.
- **Dentro** (`tk_ov_fits_default`/`tk_ov_match_default`, uma QUARTA rodada em
  `tk_ov_resolve`, tentada só depois das duas de aridade exata falharem): um nome com MAIS
  de uma declaração é uma pergunta sobre TODAS as assinaturas ao mesmo tempo — só
  `tk_over_pass` tem a tabela de tipos dos candidatos. Tentar a rodada de default DEPOIS
  das duas exatas é o que dá de graça a regra do C# (§12.6.4.5): `add(1)` com `add(i64)` e
  `add(i64, i64 = 10)` resolve pela primeira, porque ela já venceu na rodada exata antes de
  a tabela de defaults ser sequer consultada — nunca há empate a desempatar.
- `tk_default_pass` DEIXA intocado todo nome com mais de uma declaração (contagem por
  varredura de `root`, não pela tabela de linhas — ver achado abaixo), justamente para não
  competir com a quarta rodada.

**Achado 1 — a contagem de "declarado uma vez" não pode ser pela tabela de parâmetros.**
Primeira versão contava linhas da própria tabela de `syntax_param` (uma por declaração com
≥1 parâmetro). Quebrou `surface_overload_free.tk`: `tally()` (aridade zero, nunca aciona
`syntax_param` — `parse_params` nem chama o handler quando o primeiro token já é `)`) ficava
INVISÍVEL para essa contagem, e a chamada `tally()` era preenchida contra o default de
`tally(i64 k)` por engano, virando `teko: tally takes at least 1 arguments`. Corrigido com
`tk_default_decl_count`, uma varredura de `root` contando toda declaração cujo nome bate
(`decl_valid` + `str_eq`), igual ao que `teko_over.mc` já faz para o seu próprio censo.

**Achado 2 — a quarta rodada não pode casar pelo `nd_name` do nó.** `tk_ov_rename` sobrescreve
o `nd_name` de CADA declaração (para o símbolo com sufixo) ANTES de `tk_ov_resolve` rodar
sobre qualquer chamada. Uma primeira versão de `tk_ov_fits_default` procurava a linha da
tabela de defaults por `nd_name(d) == fpd_name_at(i)` — comparação por IDENTIDADE de
ponteiro, que fazia sentido para o nome ORIGINAL mas não sobrevive ao rename (o ponteiro
mudou). Sintoma: uma sobrecarga genuína que precisava da quarta rodada (`foo(Vec v)` /
`foo(i64 a, i64 b = 9)`, chamando `foo(3)`) errava com `teko: no overload of foo matches
these arguments` mesmo com a rodada logicamente correta. Corrigido trocando a chave por
`od_name_at(i)` — o nome que `tk_ov_collect` guarda no INSTANTE da coleta, antes do rename
tocar o nó — e expondo `tk_default_ndef_of_name`/`tk_default_d0_of_name` (por NOME, não por
nó) em `teko_default.mc` para esse uso.

**Reuso, não duplicação.** `tk_param_default(mark)` (fold-para-constante, "deve ser
constante", "sem default depois de default", com as mesmas mensagens) e
`tk_fill_defaults(args, na, np, nreq, d0)` (append das constantes clonadas) são de
`teko_class.mc`, usadas como estão — a função de topo é só mais um chamador da MESMA tabela
`df_node`/`tk_ndflt` que método, construtor e assinatura de interface já usam.

**`p_decl_name()` distingue membro de função livre sem `p_set_decl_name` do C1.** Não
precisou de nenhuma coordenação: `tk_params` (membros, em `teko_class.mc`) tem laço PRÓPRIO
e nunca chama o `parse_params()` do core — é por isso que o C1 sequer precisou do
`syntax_param` — então o hook simplesmente nunca dispara para um parâmetro de membro. Zero
colisão, zero checagem extra.

**Recusas:** `params` com `=` no mesmo parâmetro, checado no PARSE (o handler já sabe que o
tipo é `tk_ty_params`); `extern` com qualquer default — checado por DECLARAÇÃO
(`tk_default_check_decls`, rodando sobre toda linha da tabela antes de olhar qualquer
chamada), não por chamada, porque um default nunca exercitado por nenhum call-site ainda é
uma recusa, não um default morto; `na < nreq` com a mensagem `teko: <fn> takes at least N
arguments`; e as duas regras herdadas do C1 (constante, sem-default-após-default), mesma
mensagem, mesmo código.

**O que NÃO coube:** nada. O crumb pedia para reportar se sobrou dívida — não sobrou nenhuma
das quatro combinações do escopo (função livre × sobrecarga × `params` × `extern`); os dois
achados acima foram bugs do PRÓPRIO trabalho, corrigidos antes de fechar, não dívida
deixada para depois.

Fixture nova `surface_default_free.tk` (1 e 2 defaults, chamada com 0/1/2/3 argumentos,
sobrecarga sem-default vencendo, chamada dentro do corpo de um método de classe). Gate:
24/24 em exit esperado; AST das 23 fixtures anteriores byte-idêntica à base `2af755e5`;
`mc limits` verdict `ok`.

## 29. `while`/`do`/`for` landados — rebaixamentos, rewrite de saltos, tokens (2026-09-05)

**Rebaixamentos** (`ngen/teko_loop.mc`, novo), literalmente os do crumb:
`while (c) stmt` → `loop { if (!(c)) break; stmt }`; `do stmt while (c);` →
`loop { loop { stmt break; } if (!(c)) break; }`; `for (init;cond;step) stmt` →
`{ init loop { if (!(cond)) break; loop { stmt break; } step; } }`. Os três nascem já em
`N_LOOP`/`N_IF`/`N_BREAK`/`N_BLOCK` do núcleo — nenhum pass a mais precisa saber que veio de
`while`/`for`, o `tk_rc_pass`/`tk_ops_pass`/`tk_typeof_pass` caminham a árvore igual à de um
`loop`/`if` escrito à mão.

**Rewrite de saltos** (`tk_loop_rewrite_stmt`, chamado só por `do`/`for` — `while` não precisa,
seu corpo já fica na profundidade que o programador escreveu): caminha o corpo ANTES de embrulhar,
contando `N_LOOP` do PRÓPRIO corpo entre ele e cada `break`/`continue`. Regra única, testada e
comprovada por indução na composição aninhada: `break k` com `k > profundidade` vira `break k+1`
(precisa ultrapassar o embrulho que este passe está prestes a acrescentar); `k <= profundidade`
fica intocado (já mira um loop que o PRÓPRIO corpo abriu, business as usual); `continue` na
profundidade 0 vira `break 1` (cai onde o `break;` do embrulho cairia — a condição/o passo);
`continue` em profundidade > 0 fica intocado. A dúvida que mais preocupou ao desenhar foi a
COMPOSIÇÃO: um `for` dentro de outro `for`, com `break 2` do usuário mirando os DOIS. Passo a
passo (fixture `surface_loops.tk`, bloco "nested"): o `for` interno aplica sua própria regra
primeiro (na sua própria chamada de `tk_for()`, que termina ANTES do `for` externo processar o
corpo dele), levando `break 2` a `break 3`; quando o `for` externo caminha ESSA árvore já
reescrita, ele vê o `break 3` na profundidade 2 (dois `N_LOOP` do `for` interno entre o topo do
corpo externo e o break) — `3 > 2` → vira `break 4`. `break 4` sai dos quatro `N_LOOP` nativos (os
dois pares um-tiro-mais-condição de cada `for`), que é exatamente sair dos dois `for`s por
completo, sem re-executar nada. A prova por `break N` (não sequencial: `language.md` §4, o
exemplo de dois `loop`s aninhados salta os DOIS de uma vez para `return s`, não um de cada vez) é
o que garante a composição: um `break` que o `for` interno já corrigiu para escapar DELE por
inteiro fica, para o `for` externo, "um break que já ultrapassa tudo que EU abri" — e ganha só
mais um `+1`, nunca dois. Confirmado rodando a fixture: `innerHits=1`, `outerHits=0` (o `for`
externo nunca chega a incrementar `outerHits`, prova de que o `break` saiu por completo antes da
1ª iteração terminar).

**Tokens de `++`/`--`/`+=`/`-=` — opção escolhida e por quê.** O crumb pedia (A) `syntax_infix`
devolvendo o nó de atribuição direto, ou (B) empurrar o `#token`+`#rule` do próprio
`lib/prelude.mc` por `p_push_source`. Nenhuma das duas é exatamente como o crumb descreveu, e o
motivo de desviar de cada uma é a razão de escolher a combinação final:
- **(A) puro não funciona:** `syntax_infix` roda em POSIÇÃO DE EXPRESSÃO — o nó que o handler
  devolve tem que ser uma EXPRESSÃO válida, e `N_ASSIGN` só é tratado no dispatch de STATEMENT
  (`gen_walk.mc`/`gen_resolve.mc`, ao lado de `N_LOOP`/`N_IF`/`N_BREAK`) — nunca no avaliador de
  expressão. Devolver um `N_ASSIGN` ali quebraria a passagem por `N_EXPRSTMT`. A ÚNICA forma de
  fazer (A) funcionar seria sintetizar `st64(&x, ld64(&x)+e)` (ponteiro cru), o que É uma
  expressão válida — mas perde a semântica: bypassa `operator+` de uma classe (C5b) e o RC de `x`
  (`teko_rc.mc` só reconhece `N_ASSIGN`, não uma chamada a `st64`), regredindo exatamente a
  garantia que D197 pede para primitivas de bypass. Registrar aqui por que (A) foi descartada, não
  só que foi.
- **(B) puro (`#token` PRÓPRIO na string empurrada) é redundante:** `word_add("+=")` (a MESMA
  chamada que `syntax`/`syntax_stmt`/`syntax_infix` fazem por baixo) já registra o token — chamar
  de novo dentro de um `#token` na string empurrada seria um segundo registro do mesmo lexema
  (inofensivo, `tok_add` deduplica por texto, mas sem propósito). E `word_add` sozinho NÃO chega
  na forma solta `x += e;`: passado o `ident = expr` do núcleo, o único fallback que
  `parse_stmt_core` tenta é `rule_find` no PRÓXIMO token — que só um `#rule` de verdade povoa.
- **A combinação landada:** `word_add` registra os quatro tokens (sem `#token`), e SÓ o texto das
  quatro linhas `#rule stmt: ...` de `lib/prelude.mc` é empurrado por `p_push_source` a partir de
  `tk_loop_init()` (chamado de `user_init()`). Isso funciona porque `drv_parse` (`driver.mc`) chama
  `lex_init(entry)` → `user_init()` → `parse_unit()`, NESSA ORDEM — o push acontece DEPOIS do
  arquivo de entrada já estar empilhado mas ANTES do primeiro `next()` de `parse_unit()`, então o
  texto empurrado é o PRIMEIRO a ser lido, processado como `#rule`/diretiva, e o lexer volta
  sozinho ao arquivo de entrada ao esgotar (a mesma semântica de `#include` que a doc de
  `p_push_source` promete). Medido: as quatro linhas landam como `#rule`s de verdade (confirmado
  pelo próprio `x += 3;`/`z++;`/`z--;`/`z -= 2;` da fixture rebaixando para `x = x + e;` e
  passando pelo `operator+`/RC genéricos, não por um caminho especial). O passo de `for` (`i++`
  etc.) NÃO passa pelo `#rule` — lê os mesmos tokens diretamente (não há `;` de fechamento ali
  para o `#rule` casar) e constrói o `N_ASSIGN` à mão, reaproveitando os MESMOS `tk_plus_tok`/
  `tk_minus_tok` que `word_add` já expôs.

Gate: `rm -rf ngen/build` + build do zero limpo; laço `--entry-only` 25/25 em exit esperado
(24 antigas + `surface_loops.tk` nova); AST das 24 fixtures anteriores **byte-idêntica**
(`diff -rq` vazio); `mc limits ngen` verdict `ok` (o `ld: unknown file type` que o mesmo comando
imprime depois é pré-existente — o `[target]` de `ngen/mc.toml` mira linux/x86_64 e o link do
`.o` ELF falha em QUALQUER host macOS, com ou sem este crumb; confirmado reproduzindo no commit
base antes da mudança). Cinco probes de recusa fora de `ngen/tests/`: `while` sem parênteses →
`expected ( after while`; `for` com um `;` faltando → `expected ; after for condition`;
`i64 while = 1;` → `name reserved by a syntax/type_alias registration: while`; `break 3` além de
um único `for` → `break out of range` (checagem do NÚCLEO, na compilação completa — não aparece
em `--dump-ast`, só no passe de resolve/codegen); variável do `init` usada depois do `for` →
`unknown name` (mesma checagem de escopo léxico que qualquer bloco já tem, porque o `for` é um
`N_BLOCK` de verdade — nada de tabela de escopo própria precisou ser ensinada).

## 30. mc 0.13.0 (M45) — `+` unário, `true`/`false`, `i32`/`p_cp()` (2026-09-05)

Três itens sem crumb próprio. **Adoção do 0.13.0**: `p_cp()` público troca a leitura crua de `cp`
em `tk_dot_follows`; `chmod` da `surface_overload_free.tk` vira `extern i32` (D5, uma chamada
devolve o que declara); nenhum contorno de "region crosses a file boundary" existia a remover.
**`+` unário** fecha a dívida do §27: `tk_unary_plus` (`syntax_expr("+")` + `parse_expr(11)`, a
precedência acima de `*`/`/`/`%`) devolve o mesmo `N_UNARY` do núcleo, `tk_ops_unary` resolve pelo
tipo e colapsa no próprio operando (`tk_ops_replace`) quando é um tipo do núcleo. **`true`/`false`**:
`N_INT` de 1/0 tipado `TY_I64` (como toda comparação, não `TY_U8`), reservados por `syntax_expr`.
Gate: 25/25; AST das 22 fixtures não tocadas **byte-idêntica** contra o compilador da base
`545b26b5` (0.13.0 dos dois lados); `mc limits` verdict `ok`.
