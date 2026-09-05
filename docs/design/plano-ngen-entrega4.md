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

## 31. `namespace` / `using` / `import` — desenho (architect-first, 2026-09-05)

Escopo: D218 ("`namespace`, açúcar sobre include; `using` ou `import`, tem exemplo pronto no mc"),
D215 (C-like), D220 (`internal` = dir do `mc.toml`), D226 (seguir C#), D227 (RC só no pass).
Precedente lido inteiro: `mini_compiler/examples/lang/lang_class.mc:568-666` (`lg_namespace`,
`lg_using`, `lg_ns_path`, `lg_import`, `lg_ns_register`), `lang_expr.mc:266` (`lg_ns_expr`),
`lang_type.mc:39` (`lg_resolve`), `lang_util.mc:33` (`lg_qualify`). A implementação é `.mc` — vale
o estilo dos 20 módulos do `ngen/` (cabeçalho `//`), não o Javadoc das leis de `.tks`.

### (a) Decisões

1. **`namespace A.B { … }` (bloco) e `namespace A.B;` (file-scoped, C# 10), as duas.** Reabertura
   = merge (é só um prefixo; não há nada a fundir). **Namespace aninhado em namespace: NÃO** —
   `A.B` já dá a mesma árvore com um handler linear; aninhar pediria pilha de prefixos por nada.
2. **Qualificado resolve por `syntax_stmt`/`syntax_expr` do 1º SEGMENTO, nunca pelo `tk_dot`.**
   `tk_dot` é infixo: só roda depois de um operando esquerdo, e `geo` não é valor nem tipo —
   ensiná-lo ao `.` exigiria um nó "sou namespace" (modelagem que o D217 recusa) e colidiria com a
   lógica de receptor. O handler consome `geo . Circle` inteiro antes de o laço de Pratt ver o `.`,
   então `tk_dot_follows` e o `Shape.made` estático ficam **intocados**: o namespace nunca chega a
   `tk_type_stmt`. `geo.Circle.made + 1` funciona porque `tk_static_member` consome `. made` e a
   Pratt retoma depois.
3. **Segmento de namespace é lido com `p_name()`+`p_next()`, jamais `p_ident()`** — o 1º segmento
   vira palavra reservada no registro e `p_ident()` exige `T_IDENT` (é o que `lg_declname` faz).
4. **Nome REAL = `A__B__Circle`**, separador `__`, namespace primeiro (precedente `lg_qualify`).
   `sr_name` guarda o nome cheio, então todo símbolo derivado (`A__B__Circle_new`, `_vt`, `_itab`,
   `A__B__Circle_area`) já sai prefixado sem tocar em emissor nenhum. `$` foi considerado (prova de
   não-colisão trivial) e **descartado**: exigiria provar `$` nas três tabelas de símbolo (ELF/
   Mach-O/COFF) e no `--dump-asm` das 5 pernas do CI, custo que a guarda de (c) evita.
5. **O nome CURTO de um tipo em namespace NÃO é `type_alias`.** É registrado como palavra com
   handlers do ngen nas três posições (`syntax` de topo, `syntax_stmt`, `syntax_expr`) e a
   identidade sai da lista de busca do ngen, no sítio. Causa-raiz: `alias_add`
   (`mc/src/hooks.mc:456`) é append-only e `alias_find` devolve o ÚLTIMO — dois `Circle` em dois
   namespaces dariam id errado **em silêncio**. Sem alias, `type_of_token` nunca responde pelo
   curto e o ngen decide sempre. É o que entrega o ponto REAL do namespace: `geo.Circle` e
   `mesh.Circle` coexistem.
6. **Lista de busca (C#):** do namespace corrente para fora, prefixo a prefixo (`A.B` → `A__B__X`,
   `A__X`, `X`), e só então os `using` do ARQUIVO. Declaração do nível vence `using` (é a ordem);
   dois `using` no mesmo nível → `teko: ambiguous name X (a, b)`. `using` e file-scoped são
   **por arquivo** (`nd_file`/`p_file()`, comparados por `str_eq`) — a semântica do C#.
7. **Posição de DECLARAÇÃO nunca usa a lista de busca:** qualifica com o namespace corrente e
   procura EXATO (`tk_struct_find(tk_ns_qualify(nome))`). Vale para o reabrir de `partial`
   (`teko_class.mc:1321`), senão uma parte escrita em outro namespace reabriria a classe errada.
8. **Função livre em namespace existe e é manglada** (`geo.area` → `geo__area`), por um `pass()`
   novo — o core parseia a declaração e o sítio de chamada, e só um pass vê a unidade inteira.
   `main` dentro de namespace é **recusado** (`teko: main is declared outside every namespace`);
   `extern` em namespace é **recusado** (mantém o ABI do C, mesma regra do `teko_over.mc`); global
   de topo em namespace é **recusado** (dívida, (e)).
9. **Renomear só o que ainda não está qualificado.** O pass pula toda declaração cujo nome já
   começa por um prefixo de namespace declarado — é o que impede `geo__Circle_new` (gerado pelo
   ngen a partir de `sr_name`) de virar `geo__geo__Circle_new`.
10. **Reescrita de chamada só quando o candidato qualificado EXISTE** (`decl_find >= 0`). Um
    `rt_alloc` dentro de corpo gerado num arquivo com namespace é sondado como `geo__rt_alloc`, não
    acha e fica plano. Nenhuma falha nova é silenciosa: o que não resolve já era erro de link.
11. **`import A.B;` = `lex_include("A/B.tk")` + `using A.B` implícito**, once-only (o `lex_seen` do
    `lex_include`, `mc/src/lex.mc:635`), relativo ao includer e depois a `[include].paths`. O
    `#include "x.tk"` cru **continua existindo** (as fixtures o usam para `../lib/rt.mc`): `import`
    é açúcar, não a única porta. Contrato do lookahead: chamar `lex_include` com o `;` ainda
    corrente e só então `p_next()` (`lg_import`, `mc/docs/surface.md:1204`).
12. **`internal` (D220) não muda uma linha.** `tk_origin_of_file` (`teko_access.mc:93`) decide por
    prefixo do dir do `mc.toml`: arquivo importado de dentro de `ngen/` é projeto; de um root de
    `[include].paths` fora dele (absoluto ou `../`) é externo. Verificado no código, não presumido.
13. **`using`/`import` só no topo, antes de qualquer namespace** — dentro de um bloco são recusados
    (o `using` é do arquivo; aceitá-lo aninhado prometeria um escopo que não temos).
14. **Instanciação de genérico qualificada (`geo.Box<T,4>`) é recusada** com mensagem: o nome curto
    do genérico resolve pela lista de busca e é a forma ensinada. Dívida barata, risco zero.

### (b) Hook → uso

| hook / API | uso |
|---|---|
| `syntax("namespace"/"using"/"import", &fn)` | substituem os três honest-stops de `teko.mc:155-157` |
| `parse_top()` + `top_add()` | corpo do bloco (precedente `lg_namespace`); `parse_top` devolve 0 quando um `syntax` já fez `top_add` |
| `do_directive()` | `#include`/`#define` dentro do bloco (o laço do `lg` não trata: seria erro cru) |
| `lex_include(path, line)` | o `import` |
| `type_new(cheio)` | identidade do tipo + a palavra `A__B__Circle` (inerte, como no `lg`) |
| `syntax(curto, &tk_ns_top)` | `Circle f(Circle c)` no topo — a ÚNICA posição de tipo que o core lê sem hook |
| `syntax_stmt(curto)` / `syntax_expr(curto)` | `tk_type_stmt`/`tk_type_expr` já existentes, agora com a lista de busca |
| `syntax_param` (o de `teko_default.mc:61`, um só) | parâmetro tipado pelo nome curto; ramo novo ANTES do de default |
| `syntax_stmt(seg0)` / `syntax_expr(seg0)` | `geo.Circle c = …;` / `geo.f(x)` / `geo.Circle.made` |
| `parse_params()`/`parse_function()`/`p_set_decl_name()` | o handler de topo do nome curto |
| `pass(&tk_ns_pass)` | mangle das funções livres + resolução do não-qualificado |
| `decl_find`/`decl_valid`/`nd_name`/`set_nd_name`/`nd_file` | o pass |

### (c) Mangling e prova de não-colisão

Formas geradas hoje: (1) `S`, identificador da fonte; (2) `X_m`, membro/estático/vt; (3) `X__T…`,
sufixo de sobrecarga (nomes de tipo) e de genérico (lexemas dos argumentos); (4) `f__k`, instância
de `params` (k é dígito). Nova: (5) `NS__S`, com `NS` = segmentos juntados por `__`.

(5) colide com (3) em princípio: `namespace A.B { class Circle }` e `A<B, Circle>` dão ambos
`A__B__Circle`; `namespace A { i64 f(…) }` e um `i64 A(f x)` sobrecarregado dão ambos `A__f`. Com
(4) não colide (segmento não é inteiro). A resposta **não é separador mágico, é a guarda**: toda
identidade gerada aterrissa ou na tabela de tipos ou na lista de declarações da unidade, e as duas
são consultáveis — `tk_type_add` recusa uma segunda linha com o mesmo nome cheio, e o passo de
rename recusa quando `decl_find(cheio) >= 0`. Logo **nenhuma colisão é silenciosa**: vira
`teko: the generated name is already declared: A__B__Circle`. Um nome de fonte escrito literalmente
como `geo__area` é tratado como já-qualificado (D31.9) — consequência aceita do `__`.

### (d) Crumbs

**N1 — `namespace` + `using` + tipos qualificados** (branch `feat/ngen-namespace`).
Arquivos: `ngen/teko_ns.mc` (novo), `teko.mc` (include + 3 `syntax`), `teko_struct.mc`
(`tk_struct_find:425` ganha o fallback; `tk_newname:698` aceita palavra que é nome curto de tipo em
namespace), `teko_access.mc` (`tk_type_word:379`, `tk_type_expr:281`, `tk_type_stmt:310`),
`teko_class.mc:1310` / `teko_iface.mc:460` / `teko_trait.mc:178` / `teko_struct.mc:859` /
`teko_generic.mc:254` (um `tk_ns_qualify` no nome declarado), `teko_generic.mc:495` (`tk_gen_ty`),
`teko_expr.mc:46` (`tk_new` lê nome qualificado), `teko_default.mc:61` (ramo de parâmetro).
Assinaturas novas (`teko_ns.mc`): `uptr tk_ns_current();` · `uptr tk_ns_qualify(uptr nome);` ·
`void tk_ns_add(uptr cheio);` · `i64 tk_ns_find(uptr cheio);` · `void tk_ns_register(uptr seg0);` ·
`uptr tk_ns_read_path(uptr pmem);` · `i64 tk_ns_resolve(uptr curto);` (lista de busca; −1 = nada,
erro em ambiguidade) · `void tk_ns_short_word(uptr curto);` · `i64 tk_ns_short_known(uptr curto);` ·
`i64 tk_ns_param_ty();` · `i64 tk_ns_top();` · `i64 tk_ns_stmt();` · `i64 tk_ns_expr();` ·
`void tk_namespace();` · `void tk_using();`. Extração sem mudança de comportamento:
`i64 tk_var_after_type(i64 ty, i64 line, uptr fl)` sai de `tk_gen_declstmt`
(`teko_generic.mc:507`) e serve aos dois. Fixture `surface_namespace.tk` (+
`ngen/tests/parts/ns_file.tk` para a forma file-scoped, trazido por `#include`, fora do glob): dois
namespaces com uma classe `Circle` cada, uso qualificado, `using geo;` deixando o curto resolver
dentro e fora, estático `geo.Circle.made`, reabertura do mesmo namespace, `A.B` aninhado;
`expect-exit: 42`. Gate: 26/26 no exit esperado; `--dump-ast` das 25 anteriores **byte-idêntico**;
`mc limits` `ok`; probes de recusa FORA de `tests/`: dois `using` ambíguos, `namespace` sem `{` nem
`;`, tipo curto sem `using` e sem qualificação, `extern`/global/`main` dentro de namespace.

**N2 — funções livres em namespace** (branch `feat/ngen-namespace-fn`). Arquivos: `teko_ns.mc`
(`i64 tk_ns_pass(i64 root);`, `uptr tk_ns_of_name(uptr nome);`,
`i64 tk_ns_rewrite_call(i64 n, uptr ns, uptr fl);`), `teko.mc` (`pass(&tk_ns_pass)` **depois de
`tk_partial_pass` e antes de `tk_params_pass`** — o nome tem de estar final antes de todo pass que
censa por nome), `teko_default.mc` (`void tk_default_rename(uptr velho, uptr novo);` trocando a
chave `fpd_name`, comparada por PONTEIRO em `tk_default_row_of_name:130` — sem isso o default de
uma função em namespace some). Duas varreduras, na ordem do `teko_over.mc`: renomeia todas as
declarações, depois resolve os sítios. Fixture `surface_namespace_fn.tk`: chamada qualificada,
chamada não-qualificada de dentro do próprio namespace, `using` resolvendo o curto, sobrecarga (C4)
e default (C6) sobre função em namespace, `main` no topo; `expect-exit: 42`. Gate igual ao N1
(27/27, AST das 26 idêntica) + prova de no-op do pass (AST idêntica quando não há namespace).

**N3 — `import`** (branch `feat/ngen-import`). Arquivos: `teko_ns.mc` (`void tk_import();`,
`uptr tk_ns_path_of(uptr cheio);`), `teko.mc` (troca do honest-stop). Fixture `surface_import.tk` +
`ngen/tests/parts/geo.tk` (este com `namespace parts.geo;` file-scoped): `import parts.geo;` duas
vezes (prova do once-only), uso pelo `using` implícito e pela forma qualificada, classe sem
modificador (`internal`) alcançada porque o arquivo está dentro do projeto; `expect-exit: 42`.
Gate igual + probe de recusa: `import` de namespace sem arquivo.

**Ritual (os três):** `rm -rf ngen/build` + build do zero, laço `--entry-only` com TODAS as
fixtures, `--dump-ast` byte-idêntico nas anteriores, `mc limits ngen` `ok`, e **config RELATIVO com
cwd no repo** (D224: config absoluto cega o `tk_origin_of_file` e nenhuma checagem de `internal`
dispara).

### (e) Fora do escopo (dívida) e pedido ao mc

Dívida, registrada e não escondida: cast para nome curto de namespace (`(Circle) x` cai no
`type_of_token` do core, que não responde pelo curto — a mensagem é a do core); global de topo,
`extern` e `main` dentro de namespace (recusados); `using` dentro de bloco; alias de `using`
(`using G = geo;`) e `using static`; genérico qualificado (D31.14); namespace aninhado (D31.1).

Pedido ao mc (texto pronto para enviar):
> O `ngen` precisa, para `namespace`/`import`, de três funções do core que o `examples/lang` já usa
> mas que não estão nas tabelas do `docs/reference/hooks.md` §4: `lex_include(path, line)`
> (`lang_class.mc:665`), `parse_top()` (`lang_class.mc:609`) e `do_directive()` (para `#include`
> dentro de um corpo que o módulo parseia). Pedimos publicá-las na doc de hooks — ou nomear a
> alternativa suportada. Achado adjacente: `alias_add` aceita registrar o mesmo lexema duas vezes e
> `alias_find` devolve o último, sem diagnóstico; um segundo `type_alias` do mesmo nome hoje troca
> o tipo de um programa em silêncio.

### (f) Riscos

1. **Colisão `__`** — mitigada pela guarda de (c); sem ela seria silenciosa. Reavaliar `$` se a
   guarda alguma vez disparar em código real.
2. **Ordem do pass do N2** — antes de `params`/`over`/`default`; errar aqui vira "wrong number of
   arguments" em vez de erro claro. A prova de no-op e a fixture de C4/C6 sobre namespace cobrem.
3. **`fpd_name` por ponteiro** (`teko_default.mc:124-135`) — o rename tem de gravar o MESMO ponteiro
   que foi para `set_nd_name`, senão a 4ª rodada do `teko_over.mc` erra.
4. **1º segmento vira palavra reservada program-wide** (`geo` deixa de ser nome de variável) —
   inerente ao `word_add` do mc, o mesmo do `lg`; documentar na fixture.
5. **Corpo de bloco sem ramo de `T_DIR`/`K_EXTERN`** — sem ele, um `#include` dentro do bloco morre
   com erro do core sem relação com a causa.
6. **`region crosses a file boundary`** (falso positivo conhecido, §23/§26) — pode aparecer se um
   genérico for a última declaração de um arquivo importado; contorno `;`, correção vem do mc.
7. **Escopo híbrido** (alerta da sessão do mc, §23): o namespace é decidido no PARSE para tipos e no
   PASS para funções. Não é a tabela de locais, são domínios disjuntos (tipo × declaração de topo),
   mas fica registrado: qualquer regra que precise dos dois ao mesmo tempo é sinal de erro.

**Tensões de lei:** nenhuma aberta. D215 × C#: `namespace`/`using` são forma C-like e o D218/D226
os nomeia — sem tensão. C# escopa nome por arquivo e o mc reserva palavra program-wide: resolvido
por D31.5 (o ngen é dono da resolução do nome curto), com o cast como única fresta, declarada. D217
(sem Variant): namespace é resolvido a NOME no parse, nada dinâmico. D227: o pass novo corre antes
do `tk_rc_pass`, que continua sendo o último.

## 32. N1 landado — `namespace`/`using` + tipos qualificados (2026-09-05, errata)

`feat/ngen-namespace`, módulo `teko_ns.mc` (novo). 26/26 em exit esperado; `--dump-ast` das 25
fixtures anteriores **byte-idêntico**; `mc limits` verdict `ok` (heap 520352→577696 B, dentro da
tolerância 1.00). Fixture `surface_namespace.tk` + `ngen/tests/parts/ns_file.tk` (file-scoped,
`#include`, fora do glob), cobrindo os 14 itens de (a) e as formas do (d).

**Desvios do desenho, medidos:**

1. **`tk_struct_find` não pôde virar diretamente "exact + fallback"** — um `tk_ns_resolve`
   chamando de volta `tk_struct_find` sobre uma string que ELE MESMO construiu (o prefixo de
   namespace, o candidato de `using`) recursa sem convergir: cada tentativa falha e o próximo
   candidato é maior, nunca repete o argumento anterior, então nunca bate a base da recursão —
   medido como `EXC_BAD_ACCESS` no topo da pilha (estouro de pilha) num `lldb bt`. Correção: o
   scan original vira `tk_struct_find_exact` (sem fallback), `tk_struct_find` passa a ser
   `exact-scan; se falhar, tk_ns_resolve`, e todo sítio INTERNO de `teko_ns.mc` que testa uma
   string que ele próprio montou (`tk_ns_try_prefixes`, o laço de `using` de `tk_ns_resolve`,
   `tk_ns_walk`) chama a versão exata. O mesmo troca em `teko_class.mc` (reopen check e
   `tk_class_reopen`) e `teko_generic.mc` (`tk_gen_close`/`tk_gen_struct`, que buscam por um
   nome já manglado) — nenhum desses precisa da lista de busca, só da resposta exata.
2. **`tk_type_stmt`/`tk_type_expr` (teko_access.mc) precisaram de um `if (si < 0) err_at2(...)`
   explícito**, não ficaram "unmodified" como a primeira leitura do (b) sugeria: com `Circle`
   um nome namespaced sem `using` nem qualificação, `tk_struct_find` agora PODE devolver -1, e
   sem o guard `sr_ty_at(-1)`/`tk_static_member(-1,...)` lia lixo fora da tabela em vez de
   reportar `teko: unresolved name` — é o que fecha a mensagem clara do probe "curto sem
   `using` nem qualificação".
3. **`tk_ns_seg_stmt` não podia reusar `tk_dot_follows`** (a peça que `tk_type_stmt` usa): essa
   função responde "um `.` segue o token ATUAL, ainda não lido" — certo quando o handler está
   sentado no PRÓPRIO nome do tipo, errado depois de `tk_ns_walk` já ter consumido `geo.Circle`
   inteiro, quando o token atual É o `.` ou já é o que vem depois dele. A checagem virou
   `p_id() != tk_ns_dot` (o parser está OU NÃO sobre um `.` agora). Sem essa correção o `.made`
   de `geo.Circle.made` era perdido silenciosamente e a leitura seguinte (`Circle` como se fosse
   o nome de uma variável) por acidente às vezes até compilava errado.
4. **`tk_new` precisou de uma segunda correção depois do `tk_ns_walk`**: o nome usado para
   montar o símbolo do alocador (`tk_new_pick`/`tk_ctor_name`) ficava o CURTO não-qualificado
   (`circle_new` em vez de `geo__circle_new`) quando a resolução vinha do fallback de
   `tk_struct_find` (namespace corrente ou `using`) em vez do `tk_ns_walk` explícito — o `si`
   resolvia certo, o texto do símbolo não. Corrigido lendo `name = sr_name_at(si)` assim que
   `si` é validado, antes de `tk_new_pick`. Invisível em código sem namespace (ali `name` já era
   `sr_name_at(si)` por construção).
5. **`teko_generic.mc:254` (`tk_gen_record`) NÃO ganhou `tk_ns_qualify`**, ao contrário do que a
   lista de toques do (d) sugeria: `tk_gen_find`/`tk_gen_declstmt` comparam pelo nome CURTO que o
   `syntax_stmt` carrega, e qualificar `gn_name` sem também reescrever essa busca quebraria o uso
   comum (não-namespaced) de generics. Como D31.14 já aceita "genérico qualificado é recusado"
   como dívida, um generic declarado dentro de um namespace continua registrado pelo nome CURTO
   simples, colidindo com o "duplicate generic" de hoje se outro namespace repetir o nome — dívida
   estreita, sem fixture que a exercite, registrada no cabeçalho de `tk_gen_record`.
6. **`main`/`extern`/global dentro de um namespace FILE-SCOPED não é pego** (só o BLOCO `{ }` é,
   via o laço que este módulo já possui): não há hook de "todo `parse_top` top-level" fora de um
   laço que o módulo mesmo controla, e ganhar um não é escopo do N1 (não registra `pass()`
   nenhum). Os probes usam a forma de bloco, que é pega. Fica para o `tk_ns_pass` do N2.
   **FECHADO pelo N2 (§33):** `tk_ns_scan_decls` varre `root` inteiro no `tk_ns_pass` e aplica o
   mesmo `tk_ns_reject_topkind` a todo nó cujo arquivo declarou um namespace file-scoped.
7. **N1b (2026-09-05): dois furos do verificador, ambos a mesma causa.** `tk_conf_name` (a lista
   `:`) e `tk_use` (o `use` de trait) liam o nome com um único `p_name()`/`p_ident()`, sem andar
   pelos segmentos `.` — o primeiro nunca via `geo.IShape` inteiro, o segundo nem sequer aceitava
   um nome namespaced (seu curto virou palavra reservada em `tk_ns_register`, e `p_ident()` exige
   `T_IDENT`). Corrigidos lendo por `tk_ns_read_path` (D31.3) e resolvendo bare pela lista de
   busca, qualificado por exato — `tk_conf_name` contra `tk_struct_find`, `tk_use` contra um novo
   `tk_trait_resolve` (a mesma busca de `tk_ns_resolve`, sobre a tabela de traits).

## 33. N2 landado — funções livres em namespace (2026-09-05, errata)

`feat/ngen-namespace-fn`, `teko_ns.mc` (`tk_ns_pass`, registrado logo depois de `tk_partial_pass`
e antes de `tk_params_pass`), `teko_default.mc` (`tk_default_rename`), `teko_class.mc` (o furo do
destrutor). 27/27 em exit esperado; `--dump-ast` das 26 fixtures anteriores **byte-idêntico**
(prova de no-op do pass quando não há função livre em namespace); `mc limits ngen` `ok`.

**Duas varreduras, uma tabela de site.** Sweep 1 (`tk_ns_scan_decls`) manglа toda declaração de
função livre/protótipo dentro de um namespace, bloco OU file-scoped, ANTES de qualquer sítio ser
lido; sweep 2 (`tk_ns_scan_calls`) resolve os sítios não-qualificados. O namespace de um BLOCO é
anotado no parse (`tk_ns_decl_note`, chamado no laço de `tk_namespace` sobre o nó que `parse_top`
devolveu, por identidade do nó — não por `nd_file`+linha, que a redação original do crumb sugeria,
mas o nó já é a chave exata que o resto do módulo usa) numa tabela nova (`nsb_node`/`nsb_ns`); o de
um FILE-SCOPED sai de graça de `tk_ns_file_get(nd_file(n))`. O de um SÍTIO (sweep 2) não precisa de
tabela nenhuma: por sweep 1 já ter rodado, o nome de toda função top-level namespaced já é o cheio
(`geo__area`), e `tk_ns_of_name` (novo, usado pelas DUAS pontas) extrai `geo` de volta por prefixo
— o `.` mais específico, não o primeiro que bater, para `namespace A` e `namespace A.B` coexistirem.

**`geo.area(x)` (qualificado) NÃO passa pelo pass.** `tk_ns_seg_expr`/`tk_ns_seg_stmt` do N1 só
resolviam tipo (`tk_struct_find_exact(acc)` bem-sucedido); estendidos com `tk_ns_qualified_call`:
quando `acc` é um namespace conhecido mas não um tipo, o token corrente (que `tk_ns_walk` já
deixou sentado exatamente sobre o nome da função, o `.` já consumido) é lido como identificador e
o `N_CALL` é montado com o nome cheio DIRETO, sem `decl_find` — a declaração pode vir mais abaixo
no arquivo, e mangling delas só acontece no pass; um `geo.nome` que não é tipo nem função vira
`unresolved qualified name`, nunca miscompila em silêncio.

**Achados que exigiram correção (mesma classe do §32):**

1. **Global sintetizado de uma classe namespaced apanhado pela recusa file-scoped.** A varredura
   unificada de sweep 1 passa por TODO nó de topo, incluindo o `_vt` global que `tk_class_close`
   emite bem depois do parse — e esse nó também é do arquivo namespaced. A guarda usa o MESMO
   `tk_ns_of_name`: só recusa um `N_GLOBAL` que ainda NÃO carrega prefixo de namespace (um global
   sintetizado já sai com o nome cheio, `tk_ns_qualify` correndo antes na declaração do tipo).
2. **A guarda de colisão de (c) não vale para `decl_find` em função.** Duas declarações de uma
   função namespaced com assinaturas diferentes (C4) aterrissam no MESMO nome cheio de propósito —
   é o que o `tk_over_pass` espera achar. `tk_ns_rename_decl` só recusa colisão contra a tabela de
   TIPOS (`tk_struct_find_exact`); `decl_find` fica de fora.
3. **O construtor JÁ estava correto** (`tk_gen_ty` → `tk_ns_param_ty` resolve o nome curto pela
   lista de busca antes de comparar com `sr_ty_at(ci)`, tipo contra tipo, não palavra contra
   palavra) — só o DESTRUTOR comparava `tk_word(name)` contra o nome QUALIFICADO
   (`teko_class.mc`, `tk_member_dtor`). Corrigido com `tk_ns_short_of` (novo, o inverso de
   `tk_ns_of_name`). A MESMA classe de bug estava latente no diagnóstico `void Name(...)` (C#'s own
   mistake) logo abaixo, também corrigida — um probe (`p7_void_ctor`, fora de `ngen/tests/`)
   confirma a mensagem certa em vez de aceitar `void Base(...)` como um método comum.

**Fixture** `surface_namespace_fn.tk` (`expect-exit: 42`): chamada qualificada (`geo.area`) e
sobrecarga C4 sobre função namespaced, chamada bare de DENTRO do namespace (`grow` chamando
`area`) e de FORA via `using geo;`, default C6 bare e qualificado, uma função namespaced chamando
uma PLANA bare (fica achatada, D31.10), e `Base`/`Derived` com construtor E destrutor pelo nome
curto mais `: base(v)`. **Probes de recusa** (fora de `ngen/tests/`): `main`/`extern`/global dentro
de namespace FILE-SCOPED; dois `using` com a mesma função ambígua (`teko: ambiguous name f (a,
b)`); chamada sem namespace nem `using` (erro do core, `call to unknown function`); chamada bare a
uma função do runtime (`rt_live`) de dentro de um namespace, achatada e ligada normalmente;
`void Name(...)` dentro de namespace.

## 34. N3 landado — `import` e o fecho da série namespace (2026-09-05, errata)

`feat/ngen-import`, `teko_ns.mc` (`tk_import`, `tk_ns_path_of`, `tk_ns_sep_replace`/
`tk_ns_dotted`, `tk_ns_file_saw_ns`/`tk_ns_mark_file_saw_ns`), `teko.mc` (troca do honest-stop),
`teko_class.mc`/`teko_trait.mc` (convenção de mensagem). 28/28 em exit esperado (`hello.tk` + as
27 do glob); `--dump-ast` das 27 fixtures anteriores **byte-idêntico** contra `5e401b01`; `mc
limits ngen` `ok`.

**`tk_import`** é sugar mecânico sobre o `lex_include` do core, no precedente exato de
`lang_class.mc`'s `lg_import` (`mini_compiler/examples/lang/lang_class.mc:657`): lê o caminho com
`tk_ns_read_path` (sem consumir o `;`), chama `lex_include` AINDA sobre o `;` (o contrato do
lookahead), só então `p_next()`, e adiciona a `using` implícita antes do include — o once-only é
inteiramente do `lex_seen` do core, nada de tabela própria. `tk_ns_path_of("A__B")` = `"A/B.tk"`,
via o mesmo scanner que converte "__" em um separador dado (`tk_ns_sep_replace`), reusado por
`tk_ns_dotted` (item 3 abaixo) trocando por `.` em vez de `/`.

**Posição — D31.13, "no topo, antes de qualquer namespace".** Dois guards: `tk_ns_current() != 0`
(dentro de um bloco de namespace aberto, mesma checagem que `tk_namespace` já faz contra
aninhamento) e uma tabela NOVA, `nsd_file`, que marca (por `p_file()`) todo arquivo em que
`tk_namespace` roda — bloco OU file-scoped, o import cheque contra ISSO, não contra um flag
global: um namespace declarado dentro do arquivo IMPORTADO é desse arquivo, nunca do importador,
então a fixture do once-only (duas `import parts.geo;` seguidas, cujo alvo declara seu próprio
`namespace parts.geo;`) não se autoderruba.

**Item 2, a dívida do verificador do N2 — `&f`/`&geo.f`.** `tk_ns_walk_calls_in` (sweep 2)
reescrevia só `N_CALL`; `N_ADDR` (o nó que `&nome` produz, carregando o nome bare do mesmo jeito)
entra na mesma condição — `tk_ns_rewrite_call` já opera por `nd_name`, então zero código novo
resolve `&f`. A forma qualificada precisou de ensino de verdade: o core exige que o operando de
`&` seja `N_IDENT` (`mc/src/parse.mc:892`), e `tk_ns_qualified_call` só sabia montar `N_CALL`.
Agora, sem um `(` a seguir, devolve `tk_id(full)` em vez de errar — a mesma filosofia D31.10 (uma
referência que não existe chega ao linker faltando, não é checada aqui) estendida de "chamada" a
"referência".

**Item 3, a convenção de mensagem — decidida e aplicada.** Grep completo de `sr_name_at`/nome
qualificado em mensagem ao dev por `teko_class.mc` e `teko_trait.mc` (o `use` de trait vive lá, a
mesma classe de bug que `tk_conf_name` do N1b já tinha). Duas categorias, nunca confundidas com a
resolução em si (que segue sobre o texto cru "__"-juntado, intocado):
- **nome da PRÓPRIA declaração** (a classe/`ci` sendo lida agora) → `tk_ns_short_of` — o dev nunca
  escreve o namespace ao se referir ao próprio tipo de dentro dele mesmo (o construtor sem tipo de
  retorno é o caso canônico: `Circle(...)`, nunca `geo.Circle(...)`);
- **nome REFERENCIADO** (a base/interface de `tk_conf_name`, o trait de `tk_use`, a base de
  `tk_base_ctor_call`/`tk_base_init`) → `tk_ns_dotted` — o texto que `tk_ns_read_path` leu É
  exatamente o que o dev escreveu, só com "__" no lugar de ".".
`teko_access.mc`'s `tk_deny_member` (a mensagem `X.m is private`) tem formato próprio e fica FORA
do grep pedido pelo crumb — achado adjacente, registrado, não tocado.

**Fixture** `surface_import.tk` + `ngen/tests/parts/geo.tk` (`namespace parts.geo;`
file-scoped): `import` duas vezes, `Circle` sem modificador (`internal`, D220) alcançada de
dentro do projeto, forma qualificada e bare (via o `using` implícito), `&twice`/
`&parts.geo.twice` cada um passado a `callp`. **Probes de recusa** (fora de `ngen/tests/`):
`import` de namespace sem arquivo (a mensagem crua do core, `cannot open`); `import` dentro de
`namespace { }`; `import` depois de um `namespace` no MESMO arquivo.

**Dívidas fechadas nesta série:** `&f` (item 2 acima). **Dívidas que seguem em aberto (fila,
`docs/design/port-teko-mc.md` + HANDOFF §5):** herança de interface, `using G = geo;`/`using
static`, genérico qualificado (D31.14), namespace aninhado (D31.1), ordem-livre de
tipo/declaração (§5.1 item 7). Próximo da fila: `const`, depois `switch` (D222).

## 35. N3b — o bug do verificador do N3: um local/parâmetro nunca perde para um `using` (2026-09-05)

`feat/ngen-namespace-shadow`, `teko_ns.mc` (`tk_ns_rewrite_call`, `tk_ns_walk_calls_in`,
`tk_ns_scan_calls`), `teko_access.mc` (`tk_deny_member`, achado adjacente do N3). 28/28 em exit
esperado; `--dump-ast` das 27 fixtures anteriores **byte-idêntico** contra `40814c22`; `mc limits
ngen` `ok`.

**O bug.** `tk_ns_walk_calls_in` (sweep 2) reescrevia todo `N_CALL`/`N_ADDR` cujo nome resolvesse
por prefixo de namespace ou `using`, sem checar se o nome bare já resolvia para algo mais próximo:
uma local/parâmetro do mesmo nome (`&f` virava o endereço da FUNÇÃO `geo.f`, não da local) e uma
declaração plana de topo com o nome exato (um `f` de fora de qualquer namespace perdia, em
silêncio, para o `geo.f` que um `using geo;` trazia).

**A ordem final de resolução de um nome bare** (C#, com a ressalva que o "atenção" do crumb
pediu confirmada): **(1)** local/parâmetro em escopo no sítio; **(2)** o namespace corrente do
sítio e seus prefixos, de dentro para fora (D31.6, inalterado -- um `f` dentro de `namespace geo`
que TAMBÉM declara `f` sempre vence, mesmo com uma `f` plana também visível); **(3)** SÓ quando
(2) não achou nada -- nem o sítio está dentro de um namespace, nem nenhum prefixo dele declara o
nome -- uma declaração plana de topo com o nome exato (`decl_find`); **(4)** os `using`s do
arquivo do sítio. Um `using` nunca vence o que já era visível sem ele; o passo (3) é o que fecha
essa fresta, sempre depois de (2), nunca antes -- se estivesse antes, o caso "namespace corrente
TAMBÉM declara o nome" quebraria, e é exatamente o que o crumb pediu para confirmar que não quebra.

**Onde vive o conjunto de nomes em escopo.** Reusada a MESMA tabela que `teko_typeof.mc` declara
para o seu próprio passe posterior (`sc_name`/`sc_ty`/`tk_nscope`, `tk_ty_scope_add`/
`tk_ty_scope_find`/`tk_ty_scope_var`/`tk_ty_scope_params`) -- a mesma que `teko_rc.mc` já reusa
para o seu passe, ainda mais tardio. `tk_ns_pass` roda ANTES de `tk_typeof_pass` (`teko.mc`), então
a tabela chega vazia; `tk_ns_scan_calls` a zera e a povoa do zero por função (parâmetros primeiro,
via `tk_ty_scope_params`), e `tk_ns_walk_calls_in` marca/restaura em cada `N_BLOCK` e registra
cada `N_VAR` só depois de caminhar seu próprio inicializador -- a MESMA disciplina de
`tk_ty_walk_list`. Nenhuma tabela nova: a chamada cruzada entre módulos `.mc` sem prototype
prévio já é o padrão do projeto (mc: "two top-level passes allow calling a function before it's
defined", `docs/core-language.md` -- `teko_ns.mc`, incluído antes de `teko_typeof.mc`/
`teko_access.mc` em `teko.mc`, já chamava símbolos dos dois antes desta mudança).

**Adjacente, fechado junto:** `teko_access.mc`'s `tk_deny_member` (a mensagem `X.m is private`)
usava `sr_name_at(owner)` cru -- o nome MANGLED (`geo__X`) -- em vez do pontilhado; agora
`tk_ns_dotted(sr_name_at(owner))`, a mesma conversão que o N3 já usa para todo nome REFERENCIADO
em mensagem.

**Fixture** `surface_namespace_fn.tk` estendida (exit 42 recalculado, códigos 12-14 novos): uma
local `f` sombreando `geo.f` sob `using geo;`, num bloco (`&f` é a local, provado por
`ld64(&f)==123`); uma `f` plana top-level vencendo o `using geo;` fora de qualquer namespace;
`geo.use_own_f` chamando `f(z)` de DENTRO de `geo`, resolvendo para o `geo.f` mesmo com a plana
também visível (a exceção do passo (2), confirmada). **Probes fora de `ngen/tests/`:** parâmetro
com o mesmo nome de uma função namespaced (`&f` do parâmetro, mesma prova por `ld64`); `f`
declarada num bloco interno e usada fora dele (não sombreia -- resolve `geo.f`, `f(2)` dá `1002`
mod 256 = `234`); a mensagem `geo.X.m is private` pontilhada.

**Fila:** inalterada -- `const`, `switch` (D222), closures/`ref`/`out` (D221).

### N3c -- o bug do verificador do N3b: um membro do tipo nunca perde para um `using` (2026-09-05)

`feat/ngen-namespace-member`, `teko_ns.mc` (`tk_ns_rewrite_call`, `tk_ns_scan_calls`,
`tk_ns_walk_calls_in`). 28/28 em exit esperado; `--dump-ast` das 27 fixtures anteriores
**byte-idêntico** contra `6ec5f55a`; `mc limits ngen` `ok`.

**O bug.** `tk_ns_pass` roda ANTES de `tk_typeof_pass` (`teko.mc`), então uma chamada bare dentro
de um MÉTODO já chegava reescrita para `geo__f` quando `tk_this_call` (`teko_this.mc`, "um nome que
o tipo declara como método vence uma função de mesmo nome de topo, igual em C#") sequer via o nome
-- `class Circle { public i64 f(i64 x) { ... } public i64 test(i64 x) { return f(x); } }` sob
`using geo;` chamava `geo.f`, não o próprio `Circle.f`, porque a reescrita do namespace já tinha
acontecido.

**A ordem final de resolução de um nome bare** (C#, a que o N3b já enunciava, com um degrau novo):
**(1)** local/parâmetro em escopo no sítio; **(2)** um MEMBRO (método, inclusive um herdado de uma
base, inclusive estático) do tipo/classe a que a função caminhada pertence -- o mesmo passo que um
`this.f()` escrito por extenso já dava, agora também para a forma bare; **(3)** o namespace
corrente do sítio e seus prefixos, de dentro para fora (D31.6, inalterado); **(4)** SÓ quando (2) e
(3) não acharam nada, uma declaração plana de topo com o nome exato (`decl_find`); **(5)** os
`using`s do arquivo do sítio.

**Onde vive a classe/struct do método corrente.** `tk_ns_call_cls` (novo, ao lado de
`tk_ns_call_site`), lido em `tk_ns_scan_calls` de `teko_class.mc`'s própria tabela de métodos via
`tk_method_of_fn` (a mesma função que `teko_this.mc`'s `tk_this_enter_fn` já usa para o seu passe
posterior -- cobre método, construtor, destrutor e acessor de propriedade, todos `N_FUNC` de
membro, sem tabela nova); a checagem em si é `tk_method_named_find(cls, name)` (`teko_class.mc`, já
caminha a cadeia de bases via `sr_base_at`). Duas declarações antecipadas (`teko_class.mc` e
`teko_this.mc` são incluídos DEPOIS de `teko_ns.mc` em `teko.mc`) -- o mesmo padrão de prototype
que este arquivo já usa para `teko_access.mc`/`teko_expr.mc`/`teko_default.mc`.

**Fixture** `surface_namespace_fn.tk` estendida (exit 42 recalculado, códigos 15-17 novos):
`Circle.test` chamando `f(x)` bare, resolvendo para o método da própria classe (não `geo.f`);
`Square : Shape` chamando `f(x)` bare dentro de um método que a DERIVADA não redeclara, resolvendo
para o método HERDADO da base (não `geo.f`); `Util.test_static` chamando `f(x)` bare dentro de um
método `static`, resolvendo para o membro estático (não `geo.f`). **Probes fora de `ngen/tests/`:**
um campo `f` (não um método) mais `&f` bare dentro de um método sem `this.` -- confirmado, no seed
`6ec5f55a` E nesta branch igualmente (não é regressão desta correção), que a superfície NÃO resolve
`&campo` bare para o endereço do campo: `teko_this.mc`'s `tk_this_fix` nunca trata `N_ADDR`, então
o nome cai na reescrita de namespace/`using` como qualquer outra chamada e o `&f` vira o endereço da
FUNÇÃO `geo__f` (exit 254, um crash, com um campo `i64 f` de valor 7 gravado antes) -- achado
adjacente, registrado, fora do escopo desta correção (que é só método); de FORA de `Circle`, `f(x)`
bare com `using geo;` em escopo e Circle.f existente (mas não chamado por `this`/um receptor)
resolve para `geo.f` (`f(5)` dá `1005 mod 256 = 237`) -- o membro de `Circle` não vaza para fora
da própria classe.

**Fila:** inalterada -- `const`, `switch` (D222), closures/`ref`/`out` (D221).

## 36. `const` landado -- açúcar sobre o `#define` do mc (2026-09-05)

D218: "o mc usa `#define`; construir `const` como açúcar". `ngen/teko_const.mc` (novo): topo
(bare ou `geo__N` namespaced, resolvido bare por um passe novo que estende `tk_ns_walk_calls_in`
a `N_IDENT` -- uma const não tem lookup em tempo de lowering como uma chamada tem, então o nó é
SUBSTITUÍDO por `N_INT`, não renomeado); membro (`Tipo__MAX`, checado antes de field/método em
`tk_static_member`, e no fallback de `tk_this_ident` para o bare); `Box<T, const N: i64>`
instanciado pelo NOME de um const (`tk_gen_targs` ganhou o ramo `T_IDENT`). Local recusado, de
propósito (`#define` é tabela única do programa). 29 fixtures, AST das 28 anteriores
byte-idêntica. HANDOFF.md §5 "CONST LANDADO" tem o detalhe completo, inclusive as dívidas
achadas (array local sem `[i]=v;`, redefinição cai num guard diferente do esperado).

## 37. Ternário `c ? a : b` landado -- hoist num pass() (D228, 2026-09-05)

D228: operador ternário, associativo à direita, mesma precedência de `||`. `ngen/teko_ternary.mc`
(novo). `syntax_infix("?", TK_TERN_PREC, &tk_tern_infix)` só constrói um placeholder
(`tk_ternary(c, a, b)`, o mesmo truque do `tk_defer_member` de `teko_typeof.mc` -- se o passe não
rodar, o núcleo recusa `call to unknown function`, nunca miscompila). **`TK_TERN_PREC` é 1, não
0** -- o crumb sugeria "0 ou o menor valor abaixo de `||`", mas `syntax_infix` recusa precedência
fora de 1..100 (`mc docs/reference/hooks.md` § `syntax_infix`), e 1 já é a linha mais baixa da
tabela (`language.md` §3), empatada com `||`; ler `b` com `parse_expr(TK_TERN_PREC)` (o MESMO
piso passado ao próprio operador, não piso+1) é o que dá a associatividade à direita -- um `?`
achado enquanto `b` está sendo lido é oferecido ao mesmo piso e cai no MESMO handler,
recursivamente.

**Posição do passe -- desvio do crumb, justificado.** O crumb sugeria registrar logo depois de
`tk_ns_pass` e antes de `tk_params_pass`. Medido que não dá: `tk_ty_of` (o oráculo de
`teko_typeof.mc` que este passe usa para tipar os dois braços) só enxerga o tipo de um `.` sobre
receptor que o parser não tipou (parâmetro, campo de tipo estático desconhecido) DEPOIS que
`tk_typeof_pass` reescreveu o placeholder deferido (`tk_unresolved_member`) no load/call que ele
representa -- antes disso o placeholder é uma chamada a um nome que nada declara, e `tk_ty_of`
responde -1, o mesmo "não sei" que daria pra um braço com tipo genuinamente desconhecido. Rodar
antes do oráculo faria um braço com `.` deferido falhar com "tipos diferentes" mesmo quando os
dois braços são, de fato, do mesmo tipo. `teko_rc.mc` roda por último pelo MESMO motivo (o
cabeçalho desse arquivo já registra: "depois que teko_typeof.mc resolveu todo acesso deferido e
todo nó tem seu tipo"). Registrado **logo depois de `tk_typeof_pass`, antes de `tk_ops_pass`**:
`params`/`typeof`, que rodam ANTES do ternário, nunca olham a FORMA da árvore (bloco/if/statement),
só censo por nome/tipo, então não perdem nada vendo o placeholder ainda intacto; `ops`/`default`/
`over`/`rc`, todos DEPOIS, passam a ver `if`/local comuns -- nenhum precisa aprender o que um
ternário é.

**Mecanismo do hoist (`tk_tern_lower`/`tk_tern_scan`/`tk_tern_stmt`/`tk_tern_branch`,
`teko_ternary.mc`):** cada `tk_ternary(c, a, b)` vira `T $t = 0; if (c) { $t = a; } else { $t = b;
}` inserido ANTES do statement envolvente, com o placeholder trocado por `$t` (`N_IDENT`) no
lugar exato -- a mesma forma "detach + node_assign + set_nd_next(keep)" que `teko_ns.mc`'s
`tk_ns_ident_to_const` e `teko_rc.mc`'s `tk_rc_park`/`tk_rc_hoist_cond` já usam. `T` sai de
`tk_ty_of(a)` comparado a `tk_ty_of(b)`; os dois braços são reduzidos (`tk_tern_scan`) ANTES da
comparação de tipo, e o `$t` recém-criado entra na MESMA tabela de escopo que `teko_typeof.mc`
mantém (`tk_ty_scope_add`) -- é o que deixa um ternário aninhado responder pelo seu próprio tipo
quando o externo pergunta.

**Preguiça com aninhamento -- o ponto que o crumb pedia atenção especial.** `c` é hoistado junto
da lista que o `if` também entra (roda incondicionalmente, então não custa nada); `a` e `b` são
cada um reduzido para dentro do SEU PRÓPRIO ramo (`thenOut`/`elseOut`), nunca para o preâmbulo
comum -- é essa escolha, não a ORDEM "de dentro pra fora" em si, que preserva a preguiça: um
ternário aninhado num braço (`c ? (x?y:z) : w`, ou o encadeamento à direita `c1 ? a : c2 ? b : d`,
que o parser right-recursivo já entrega como `tk_ternary(c1, a, tk_ternary(c2, b, d))` -- a MESMA
forma de árvore) tem seu próprio `if` construído DENTRO do ramo que o contém, então só roda quando
aquele ramo é tomado. Provado por probe fora de `tests/`: `0!=0 ? side(1) : (1!=0 ? side(2) :
side(3))` chama `side` uma vez.

**Condição de `while`/`for`:** como `teko_loop.mc` já rebaixa para `loop { if (!(c)) break; ... }`
antes deste passe rodar, um ternário na condição cai dentro do `if (!(c))` -- o "statement
envolvente" É esse `if`, que já está dentro do bloco do corpo do loop, então o hoist entra ali e
reavalia a cada volta. Provado na fixture (`while (i < 5 ? 1 : 0)`, laço termina com `i==5`).

**`return`/`if` sem chaves:** `tk_tern_branch` só embrulha o statement solto num bloco quando ele
de fato hoistou algo -- a mesma cerca que `tk_rc_branch` já usa para um temporário parked
(`teko_rc.mc`). Provado na fixture (`if (c != 0) return c == 1 ? 100 : 200;`).

**Fixture** `surface_ternary.tk` (30/30 em exit 42): inicializador, argumento, encadeamento à
direita, preguiça com contador provando UMA chamada só por lado, condição de `while`, braços de
objeto teko (`rt_live()` prova que a escolha não aloca um objeto novo), `return` dentro de `if`
sem chaves, ternário dentro de método. AST das **29 fixtures anteriores** byte-idêntica contra o
compilador da base `a4736222`. `mc limits ngen` `ok`. Probes fora de `tests/`: braços de tipos
diferentes (`i64`/`f64`) recusados com "the two arms of ?: have different types"; `?` sem `:`
recusado com "expected ':' in a ternary"; `a ? b` sem `:` como statement solto, mesma recusa;
ternário como lado esquerdo de atribuição recusado pelo NÚCLEO ("left side of assignment must be
a name" -- o resultado do ternário nunca é um `N_IDENT`).

**Limite conhecido (o próprio cabeçalho do arquivo documenta):** a avaliação de `c`/`a`/`b` é
hoistada para ANTES do statement que os continha -- um `f(x++, c ? a : b)` teria a chamada rodando
depois do hoist no MESMO statement (sem operador `++` de efeito em posição de expressão hoje, o
risco é teórico, registrado por completude).

## 38. `switch` landado -- statement como loop de uma volta, expression como açúcar sobre ternário (D222/D228, 2026-09-05)

D222 (statement + expression, `break N` atravessa, `when` = guarda) e D228 (expression = açúcar
sobre a cadeia de ternários, sem máquina própria). `ngen/teko_switch.mc` (novo); `ngen/teko_ternary.mc`
ganhou `tk_tern_hoist_var` (um segundo tipo de placeholder que `tk_tern_scan` reconhece: um `N_VAR`
real embutido no meio de uma expressão, hoisted incondicionalmente igual à condição `c` de um
ternário -- o que a `switch` expression usa para ler um `x` não-simples uma única vez).

**Statement, no parse:** `loop { if (t==1) {...break;} if (t==2||t==3) {...break;} ... {default;
break;} break; }`, `x` lido uma vez num `i64 $t` local. `break`/`break N` no corpo NÃO são
reescritos (o loop do switch já é o nível que a fonte vê); um `do`/`for` externo que envolve o
switch continua enxergando-o como só mais um `N_LOOP` no seu próprio `tk_loop_rewrite_stmt`, e a
composição sai certa sem nada extra aqui -- prova: `break 2` atravessando um `switch` dentro de um
`for` na fixture. `continue` no corpo do case é RECUSADO (não há `continue N` no núcleo; reescrever
para `break 1` sairia do switch, não continuaria o loop de fora -- a decisão que o próprio crumb já
antecipava, tomada sem precisar subir).

**Expression, sem máquina própria:** dobra `tk_ternary(cond, expr, tk_ternary(...))` da última
armação para trás; a condição da última NUNCA é testada (é a base incondicional), por isso exige-se
ao menos um `_` em algum lugar. `x` não-simples: um `N_VAR` embutido, hoisted pelo MESMO passe do
ternário.

**Dois achados corrigidos no processo:** `when` já estava reservado (`syntax_stmt`, entrega 1) --
`tk_kw` (só casa `T_IDENT`) nunca bate contra a palavra reservada; trocado por `tk_word` (a mesma
distinção que `teko_class.mc`'s próprio cabeçalho já documenta). E `syntax_infix` entrega o
operador JÁ CONSUMIDO ao handler -- `tk_switch_infix` não pode chamar `p_next()` de novo (o
`tk_tern_infix` do ternário já não chamava); o bug apareceu como "expected { after switch" comendo
o `{` de verdade.

**Fixture** `surface_switch.tk` (31/31 em exit 42). AST das 30 fixtures anteriores byte-idêntica
contra `3f223f9a`. `mc limits ngen` `ok`. Seis probes fora de `tests/`, todas recusadas com mensagem
clara: braço sem `break`, `case` duplicado, `case` não-constante, expression sem `_`, `continue`
dentro de `switch`, `switch` sem `{`.

**Dívidas registradas:** `case`/braço com um const NAMESPACED não resolve (o `#define` do núcleo só
dobra um nome bare no parse; um const qualificado só resolve num passe posterior); um `when` no
braço `_` textualmente último de uma expression não é testado (é a base incondicional da dobra).

## 39. Arrays fixos landados -- registro no parse (local) e num pass() (global), larguras, o que ficou fora (2026-09-05)

`ngen/teko_array.mc` (novo). Um LOCAL é observável no parse (`on_stmt`, o mesmo mecanismo do
campo-array de `teko_struct.mc`) e resolvido AO MESMO TEMPO que `[` é lido -- tabela própria
(`av_*`/`tk_narr`), escopo por bloco (`tk_block` ganhou uma segunda marca/restauração). Um GLOBAL
não é observável no parse (`on_stmt` não vê topo, e não há hook público sobre um) -- um LEITURA
fica como o `N_INDEX` que o `[` de `params` também deixa (o fallback de sempre) e um `pass()`,
registrado ANTES de `tk_params_pass`, varre `nnodes` procurando um global array e reescreve só os
que acham dono; uma ESCRITA não pode esperar (o núcleo recusa `g[i] = e;` no próprio parse), então
fica um placeholder (`tk_call("tk_unresolved_array", 0)`, o idioma do `.` deferido de
`teko_typeof.mc`) resolvido pelo mesmo passe.

**Larguras:** `ld8/16/32/64`/`st8/16/32/64` por `type_width` (já existiam). `ld32` é sempre
zero-extending (`language.md` §2) -- um elemento `TK_SINT` mais estreito que a palavra (`i32`) é
casteado pro próprio tipo depois do load, o idioma que a própria doc do núcleo documenta.

**Bounds:** um índice literal fora de `[0, N)` é erro de compilação, nas duas rotas. Um índice
dinâmico NÃO tem guard em runtime (precisaria de `panic` de superfície, que não existe ainda).

**Fora do escopo, registrado:** array de tipo struct/classe (recusado, local e global -- sem nome
próprio pro RC percorrer); `T[]` em heap com RC; `T[]` como parâmetro; `.Length` sobre um global;
um `params xs[i]` dentro do corpo REPLAY de outro `params` que também usa um array global (o passe
de arrays roda uma vez, antes da instanciação de `params` -- aresta rara, nenhuma fixture combina
os dois).

**Fix à parte, no mesmo lote:** `tk_switch_check_end` só olhava o último nó de TOPO do corpo do
`case` -- um corpo escrito como bloco explícito (`case 1: { ...; break; }`) caía no "control cannot
fall out of a case" mesmo terminando em `break`. Recursa em `N_BLOCK` agora, como
`tk_switch_no_continue_stmt` já fazia ao lado.

**Fixture** `surface_arrays.tk` (32/32 em exit 42). AST das 31 fixtures anteriores byte-idêntica
contra `6cf49db1`, exceto `surface_switch.tk` (o fix acima) -- idêntica entre o commit do fix e
este. `mc limits ngen` `ok`.

## 40. Prefixos veem pós-fixos -- `.`/`[` mais apertado que `- ! ~` (2026-09-05)

`!b[1]` era `(!b)[1]`, `-a.x` era `(-a).x`: `parse_unary()` do núcleo acha `- ! ~` na sua PRÓPRIA
tabela de prefixo (`ops_init`) antes de `parse_primary` -- onde `syntax_expr` mora -- e lê o
operando por recursão direta em `parse_unary()`, que nunca consulta `.`/`[` (`syntax_infix`, prec
12). `syntax_expr("-", ...)` não conserta nada (código morto, medido); só `+` escapa da armadilha
porque `ops_init` nunca o registrou. Correção em `ngen/teko_prefix.mc` (novo): `tk_dot`/`tk_bracket`
sinkam pela cadeia de `- ! ~` que RECEBERAM como `left` até o operando de verdade, resolvem o
`.`/`[` nele, e reembrulham -- o mesmo `N_UNARY` que o núcleo constrói para `-(a.x)` escrito com
parênteses. `tk_bracket` ganhou uma guarda (`tk_bracket_no_write`) para o operando-base de uma
cadeia sinkada nunca virar alvo do deferral de array GLOBAL.

**Fixture** `surface_operator.tk` estendida (32/32 em exit 42, sem fixture nova): `!b[1]`, `-a[0]`,
`!f.on`, `-p.x`, `~g[2]`, `!c.flag()`, `-(-a[1])`. AST das 31 fixtures não tocadas byte-idêntica
contra `e2d4d936`. `mc limits ngen` `ok` (`intrin` 8/8).
