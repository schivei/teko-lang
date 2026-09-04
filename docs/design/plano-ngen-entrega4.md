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
