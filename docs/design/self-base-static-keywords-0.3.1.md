# `self` / `base` / `static` — futuras keywords: identificação (0.3.1.x) + plano de uso (próxima versão)

Ruling do dono (2026-08-03): `self`, `base`, `static` são aprovados como **futuras keywords** da
linguagem. O plano tem DUAS versões, deliberadamente separadas:

- **ESTA versão (0.3.1.x, groundwork):** ensinar o compilador a **IDENTIFICAR/reconhecer** as três
  palavras — sem quebrar o self-build atual, que usa `self` como PARÂMETRO de método em toda parte.
- **PRÓXIMA versão (fora do escopo aqui, só documentada):** **USAR** — `self`/`base` viram keywords
  IMPLÍCITAS dentro de métodos de classe/struct (o parâmetro receptor solto desaparece), e `static`
  passa a GATEAR de verdade static/instance para `class`/`struct`.

---

## 0. Nota honesta sobre nomenclatura — relação com D27 (`this`/`base`/`static`)

Existe uma decisão anterior ratificada, **D27** (`DECISION_LOG.md`, 2026-07-06,
`docs/design/oop-this-base-static.md`), hard-cut aprovado, que troca o receiver por **`this`**
(não `self`), remove o binding nomeado (`class Base(binding)` → `base` fixo) e introduz `static`
reservado. Essa decisão nunca foi implementada (o self-build de hoje ainda usa `self` como
parâmetro em ~90+ sites e `token.tks` ainda documenta `self`/`base` como "deliberadamente não
reservados").

A ruling de 2026-08-03 que originou este trabalho pede explicitamente **`self`** (não `this`) como
a palavra que vira keyword implícita. Isto é tratado aqui como uma **correção/refinamento do D27**,
não como uma ruling nova e independente: a única mudança de substância é a ESCOLHA DO NOME do
receiver implícito (`self` em vez de `this`) — o que na verdade **reduz** o custo de migração da
próxima versão (o corpo já usa `self` como convenção; o codemod deixa de precisar renomear ~90
sites, só de remover o parâmetro solto). O resto do design do D27 (síntese de `params[0]` sintético,
`base` como binding fixo, `static` como modificador do method) permanece válido e é o que a seção 3
abaixo assume. Isto NÃO foi re-submetido ao dono nesta sessão (implementador, não arquiteto) — fica
registrado para quem fizer a revisão da próxima versão confirmar explicitamente `self` vs `this`
antes de gravar como ratificação final.

---

## 1. Por que "identificar" tem que ser NÃO-QUEBRANTE

O self-build atual:
- usa `self` como o NOME do parâmetro receptor não-tipado em ~90+ sites de método (OOP A1 —
  qualquer nome é válido como receiver; `self` é só convenção, não sintaxe especial);
- usa `base` como uma variável local COMUM em produção (`src/driver.tks:177-189`,
  `src/checker/resolve.tks:947,1298,1708`, `src/compress/zlib.tks:31`) — nada a ver com OOP;
- não usa `static` como identificador em lugar nenhum do corpus (~zero colisões, confirmado por
  grep e já documentado no D27).

Reservar `self`/`base` como `TokenKind` de verdade quebraria a build agora mesmo (todo `fn
m(self, …)` deixaria de lexar como identificador). Por isso a abordagem escolhida é a mesma já
usada no compilador para `params`/`from`/`unsafe`: uma **keyword contextual** — o LEXER continua
emitindo um `Ident` comum; o reconhecimento acontece no PARSER, comparando o texto do token
(`tokens[p].text == "…"`) numa posição específica, sem tocar a tokenização.

---

## 2. O que foi implementado nesta versão

### 2.1 `lexer::is_future_reserved_word(text: str) -> bool` (`src/lexer/token.tks`)
Classificador puro: `true` sse `text` é `"self"`, `"base"` ou `"static"`. Não está ligado a
NENHUM caminho de execução além dos próprios testes — é a fonte única de verdade para "esta
palavra está na lista de futuras keywords", para uso futuro por tooling (`teko fmt`, um eventual
LSP) sinalizar uma colisão futura sem o compilador rejeitar nada hoje. O comentário de
`token.tks` que dizia `self`/`base` "DELIBERADAMENTE NÃO reservados" foi ajustado para deixar claro
que a not-reservation vale POR AGORA (ruling 2026-08-03 aprovou o caminho para reservá-los,
adiado para a próxima versão).

### 2.2 `static` — modificador CONTEXTUAL real (`src/parser/parse_decl.tks`, `src/parser/ast.tks`)
`static` ganhou reconhecimento de verdade no parser, no mesmo padrão do `unsafe` já existente:

```
fn is_static_modifier_at(tokens, pos) -> bool   // tokens[pos] é Ident de texto "static"?
fn consume_static_modifier(tokens, pos) -> Parsed<bool>
```

`parse_function` consome um `static` opcional logo após o bloco `abstract/virtual/override` (antes
de `extern`/`unsafe`) — ou seja, `pub static fn make() -> Point { … }` já parseia exatamente como o
design do D27 propõe. Isso popula um novo campo `parser::Function.is_static: bool`, **parseado
universalmente** (mesmo em `fn` de topo, onde não significa nada — mesmo precedente de `is_intern`)
e **não lido por NINGUÉM** além dos testes novos: o detector de staticidade continua sendo,
sozinho, `params.len == 0 || params[0].has_type` (`checker/di.tks:is_static_method`,
`checker/typer.tks`, `checker/collect.tks`) — zero mudança de semântica.

Todos os sites que constroem um `parser::Function{...}` (parser, `checker/synth.tks` — o
sintetizador de `eq`/`hash`/`compare`/`clone` —, `checker/resolve.tks`, `checker/collect.tks`,
`emit/tkb_read.tks`, e as fixtures de teste) foram atualizados para incluir o campo (default
`false`, ou preservado via `f.is_static` nos sites que copiam um `Function` existente). O formato
`.tkb` (wire format de pacote) **não mudou**: `write_function` nunca foi tocado e continua sem
serializar `is_static` — `read_function` sempre lê de volta `is_static = false`, o mesmo precedente
já usado para `has_arena_size` num método. Zero bump de versão de wire necessário.

### 2.3 `self` / `base` — NADA tocado além da documentação
Nenhuma mudança de parser/checker. `self` continua sendo só o nome convencional do parâmetro
receptor; `base` continua sendo o nome do binding sintético de upcast (quando presente) E uma
variável local comum em qualquer outro lugar. A única mudança é o comentário em `token.tks` e a
entrada na lista de `is_future_reserved_word`.

### 2.4 Por que não fazer mais que isso
Cogitado e descartado: (a) marcar o `Param` do receiver com um `is_self_named: bool` — descartado
por exigir tocar toda construção de `Param` (dezenas de sites) por um ganho de identificação
marginal, quando o predicado puro (`is_future_reserved_word`) já cobre o caso de uso de tooling; (b)
emitir um AVISO (warning, não erro) quando `self`/`base`/`static` aparecem fora das posições
esperadas — descartado porque isso inundaria o corpus de avisos sobre USO HOJE LEGÍTIMO (~90 sites
de `self`, dezenas de `base`), o que vai contra o espírito de "não quebrar", mesmo sendo
tecnicamente só um warning.

---

## 3. Plano da PRÓXIMA versão (fora do escopo desta entrega — só documentado)

Retoma o crumb plan já detalhado em `docs/design/oop-this-base-static.md` (D27), com o ajuste de
nome (`self` em vez de `this`) registrado na seção 0:

1. **Receiver implícito.** Em `parse_function`, quando `allow_receiver && !is_static`: o parser
   passa a INJETAR um `Param{name="self"; has_type=false}` sintético como `params[0]`, e **para de
   consumir** um receiver solto de dentro do `(...)` de origem. Isso preserva o invariante inteiro
   já usado pelo checker (`params[0].has_type == false ⇔ instância`) — `is_static_method`,
   `is_instance`, `method_sig_matches` continuam funcionando verbatim, e os genéricos de método
   (#254/#294) não precisam de nenhuma mudança porque ainda leem `params[0]`.
2. **`base` — nome fixo.** `parse_class_body` para de parsear `(binding)`; quando `has_base`,
   sempre fixa `base_binding_name = "base"` (zero mudança em `typer.tks:3110-3128`, que já
   sintetiza `let <binding>: <Base> = <self upcast>`).
3. **`static` passa a ser AUTORITATIVO.** `is_static_method` e equivalentes passam a ler
   `m.is_static` em vez do detector estrutural `params.len==0 || params[0].has_type` (o campo já
   existe, construído nesta versão — zero AST churn na migração).
4. **Diagnósticos novos, pequenos:** `self`/`base` usados como nome de parâmetro/`let` dentro de um
   método viram erro ("reservados como receiver/base"); `base` fora de uma subclasse (`!has_base`)
   vira erro; `static` num `fn` de topo continua erro (`allow_receiver == false`).
5. **Codemod mecânico do corpus:** ~90 sites de `fn m(self, …) { … }` → `fn m() { … }` (self já
   está implícito, corpo INALTERADO — a única mudança de nomenclatura já não é necessária, já que
   ficamos com `self`); `synth.tks` passa a emitir `Function` sem o parâmetro receptor solto e com
   `is_static` real.
6. **Prova obrigatória:** o fixpoint (gen1==gen2 ou gen2==gen3, conforme o modo) tem que continuar
   byte-idêntico — o D27 já demonstra que codegen/VM leem `params[0]` POSICIONALMENTE (nunca por
   nome), então a migração é fixpoint-neutra por construção.

Este plano NÃO é implementado aqui — só documentado, conforme o escopo desta entrega.

---

## 4. Prova de não-quebra (fixpoint C-route)

Ver o corpo do PR / relatório da sessão para o resultado exato do gate (`build_with_seed_fallback.sh`
+ `fixpoint_gate.sh`, backend C, modo bootstrap: `gen2 == gen3` byte-idêntico). A mudança desta
versão é aditiva-apenas (um campo novo em `parser::Function`, um modificador contextual novo, uma
função pura nova) e não é lida por nenhum consumidor de codegen/VM/backend nativo — por construção
não deveria haver NENHUMA diferença de bytes gerados; o gate confirma isso empiricamente.
