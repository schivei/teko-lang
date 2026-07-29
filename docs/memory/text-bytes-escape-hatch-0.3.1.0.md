# `teko::text::bytes_of_str`/`teko::text::str_from_utf8` — a wrapper era não precisa (0.3.1.0)

**Encomenda**: uma lane anterior (`cargo/0.3.1.0-text-bytes`, base `remodel/0.3.1.0-linux-native-2`)
não criou `teko::text::bytes_of_str` porque concluiu que um WRAPPER Teko com esse nome, dentro da
própria namespace `teko::text`, chamaria o builtin bare `bytes_of_str` e **recursaria infinitamente**
(mesmo-nome-mesma-namespace vence em `lookup_call`). O raciocínio era bom E acabou por ser
irrelevante — medido, não assumido, abaixo.

**Resposta curta: `teko::text::bytes_of_str(s)` e `teko::text::str_from_utf8(bytes)` JÁ FUNCIONAM
hoje, sem wrapper nenhum, na rota C.** Nenhum wrapper foi escrito porque nenhum é preciso — e
nenhum EXISTE hoje (não há recursão possível porque não há o código que recursaria). Na rota
own-native (nativa) as duas param HONESTAMENTE num "not yet lowered" — um gap real, mas de uma
família totalmente diferente (registo de lowering, não resolução de nomes), já enumerado como
sibling em aberto em `docs/memory/0.3.1.0-linux-native-first-stop.md` (fim da entrada do degrau 17).

Commit medido: `9847fe6` (branch `cargo/0.3.1.0-text-bytes`, base `remodel/0.3.1.0-linux-native-2`).

## Método

`sh scripts/fetch_teko.sh` falhou com 403 (bloqueio de proxy do agente, conhecido). Escada usada:

```sh
sh scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1        # gen1 (C comitado)
TEKO_BACKEND=c .gen1/teko . -o .gen1b --no-verify --release       # gen1b (fonte ACTUAL, rota C)
```

`.gen1b` completou de facto (89.4s, `teko: memory: peak 1398.5 MB`) — a auto-construção via C route
fecha inteiramente nesta árvore; é a rota NATIVA da auto-construção que hoje pára no degrau 18
(`src/build/project.tks:253`, `reference deref-assignment not yet lowered`), um assunto sem relação
com este, já localizado noutra lane.

Toda sonda de valor foi um projecto standalone descartável em `/tmp/.../scratchpad/probe*/`
(`teko.tkp` + `main.tks`), nunca dentro da árvore, compilado por `.gen1b/teko` pelas DUAS rotas.

## 1. Rota C (`TEKO_BACKEND=c`) — funciona, valor confirmado

Fonte (string multi-byte: acento + emoji, para que um bug de fronteira não se esconda atrás de
ASCII):

```teko
let s: str = "café🐝"
let bs: []byte = teko::text::bytes_of_str(s)
println($"len_bytes={bs.len}")
let s2: str | error = teko::text::str_from_utf8(bs)
match s2 {
    str as r => println($"roundtrip_str={r}")
    error as e => println($"roundtrip_error={e.message}")
}
```

Build (`TEKO_BACKEND=c .gen1b/teko . -o outc --no-verify`): `checker 5/5 ✓ … cc outc/probe ✓`,
`teko: .: built outc/probe`. Execução:

```
len_bytes=9
roundtrip_str=café🐝
```

`9` é o valor certo: `c`+`a`+`f`+`é`(2 bytes UTF-8)+`🐝`(4 bytes) = 1+1+1+2+4 = 9. O roundtrip
devolve a string original byte a byte, acento e emoji incluídos.

**Qual implementação foi alcançada?** O `.c` emitido (`outc/probe.c`) contém:

```c
tk_slice_byte bs = tk_bytes_of_str(s);
...
tk_ffi_sres _h848r = tk_rt_str_from_utf8(_h848s.ptr, (uint64_t)_h848s.len);
```

— o BUILTIN injectado (`tk_bytes_of_str`/`tk_rt_str_from_utf8`, `scope.tks:622-627` +
`codegen.tks:3732`/`5548`), **não** a função Teko real de `src/text/text.tks:56`. Isto é o
esperado: o projecto de sonda (`probe`) não declara o seu próprio `teko::text::str_from_utf8`, e o
doc-comment de `scope.tks:498-505` já dizia que o fallback só entra quando "no real declaration
answers". Confirmado por leitura do `.c`, não assumido.

**A própria árvore do compilador alcança a função REAL**, não o builtin, nos dois únicos call
sites QUALIFICADOS (`teko::text::str_from_utf8`): `src/numeric/bigint/bigint.tks:531` e
`src/numeric/dec/dec.tks:205` — porque aí `cur_ns` é diferente e o qualificador `teko::text` casa
com a namespace REAL da função declarada em `src/text/text.tks`, e `lookup_call` vence sobre o
fallback (exactamente o que o doc-comment de `scope.tks` afirma). Todos os OUTROS call sites do
próprio compilador (`regex.tks`, `stream.tks`, `csv.tks`, `base64.tks`, `url.tks`, `json.tks`,
`str_iter.tks`, `byte_iter.tks`) chamam a forma BARE, fora da namespace `teko::text` — essas
alcançam o BUILTIN, não a função real, porque uma chamada bare só casa com uma ligação real cuja
`ns == cur_ns` (`call_binding_matches`, `scope.tks:271-274`).

## 2. A recursão temida — refutada por ausência do código, não por argumento

`src/text/text.tks:61-63` documenta `bytes_of_str` **num comentário apenas**:

```
// bytes_of_str(s: str) -> []byte — expose the raw UTF-8 bytes of a str as a []byte
// slice. Zero-copy; the returned slice views the same memory. Registered as a builtin
// (like str_of_bytes) in scope.c/tks; this comment is the canonical documentation.
```

Não há `pub fn bytes_of_str` nenhuma nesse ficheiro nem em nenhum outro (`grep -rn "fn bytes_of_str"
src/` não devolve nenhuma definição). O medo era: um WRAPPER `teko::text::bytes_of_str` chamando o
builtin bare de dentro da própria namespace recursaria (mesmo-nome-mesma-namespace vence em
`lookup_call`). **Esse wrapper nunca existiu** — a recursão nunca poderia ter acontecido, porque o
código que recursaria não foi escrito, não porque alguém o evitou correctamente. E não precisa de
ser escrito: `builtin_qualifier_ok` (`typer.tks:1536-1538`) já aceita qualquer callee "bare ou
literalmente enraizado em `teko`", a QUALQUER profundidade — `teko::text::bytes_of_str` é uma
dessas formas, tal como `teko::str::concat` já era. Nenhuma mudança de código fecha esta peça;
apenas a prova.

## 3. Rota own-native (nativa) — pára, mas por razão NÃO relacionada

O MESMO ficheiro, mesmo `.gen1b`, backend por omissão (sem `TEKO_BACKEND=c`):

```
teko: .: native backend N1: builtin `bytes_of_str` not yet lowered (N2) [in `snippet::text_byteview_roundtrip_probe`]
```

E, isolando só `str_from_utf8` (sem o `bytes_of_str` anterior no mesmo ficheiro, para que a PRIMEIRA
falha não esconda a segunda):

```
teko: .: native backend N1: builtin `str_from_utf8` not yet lowered (N2)
```

O checker chega ao MESMO ponto nas duas rotas (`checker 2/2 items ✓`, `monomorph 0/0`, `consteval
0/0` idênticos) — só a fase de LIR LOWERING (`src/lir/lower.tks`, `call_symbol` →
`native_builtin_symbol`) pára. `native_builtin_symbol` (`lower.tks:2172-2183`) é uma lista fechada
de dez famílias (io/cov/arena/str-query/str-slice/int-to-str/`str_of_bytes`/host-info/`peak_rss`/
`env::args`) — nem `bytes_of_str` nem `str_from_utf8` estão nela. Isto já estava ENUMERADO como
sibling em aberto, sem molde estreito identificado ainda, no fim da entrada do degrau 17 de
`docs/memory/0.3.1.0-linux-native-first-stop.md` ("`len`/`bytes_of_str`/`chars`/…" e
"`str_from_utf8`" na lista de host-FFI `sres`/`ures`/`slres`). Nada aqui contradiz esse mapa; esta
sonda apenas o confirma com um programa mínimo isolado em vez de o alcançar através da
auto-construção inteira (que hoje nem chega lá — pára antes, no degrau 18, um gap de LINGUAGEM,
não de resolução de builtin).

**Bónus, medido para não confundir os dois gaps**: uma função Teko REAL equivalente (não passando
pelo builtin nenhum, apenas `str(b)`, que ESTÁ na lista de `native_builtin_symbol` via
`str_of_bytes`) compila e corre nativamente sem problema:

```
$ ./outn/probe2
roundtrip_str=Café
$ echo $?
0
```

Mas a validação UTF-8 real (`valid_utf8`, `text.tks:17-47`) usa `match b { 0xC2..=0xDF => …, … }` —
um PADRÃO DE INTERVALO (`RangePattern`) — que tem o SEU PRÓPRIO gap nativo, distinto e já conhecido:
`native backend N1: range match pattern not yet lowered (N2)`. Ou seja: mesmo fechando o gap do
builtin injectado, a função REAL de `text.tks` teria um SEGUNDO obstáculo nativo antes de correr —
um gap de padrão de match, nada a ver com nomes ou namespaces.

## Veredicto final

| forma | rota C | rota own-native | implementação alcançada (rota C, projecto sem `teko::text` próprio) |
|---|---|---|---|
| `teko::text::bytes_of_str(s)` | funciona, valor certo (9 bytes) | pára: `builtin \`bytes_of_str\` not yet lowered (N2)` | builtin injectado (`tk_bytes_of_str`) |
| `teko::text::str_from_utf8(bytes)` | funciona, valor certo (roundtrip exacto) | pára: `builtin \`str_from_utf8\` not yet lowered (N2)` | builtin injectado (`tk_rt_str_from_utf8`) |
| `teko::text::str_from_utf8(bytes)`, dentro da PRÓPRIA árvore, chamada QUALIFICADA (`bigint.tks`/`dec.tks`) | — | — | a função REAL de `text.tks:56` (via `lookup_call`, não o fallback) |

**Conclusão para o dono**: a peça da RECURSÃO/nomenclatura está FECHADA — refutada, não corrigida,
porque não havia nada para corrigir. Nenhum wrapper foi escrito, nenhum é preciso. A peça do GAP
NATIVO continua ABERTA, mas é uma peça DIFERENTE (registo de lowering em `native_builtin_symbol`),
já mapeada noutro documento, e fora do escopo desta lane.

## Fixtures

- `/regressor.tkr` — duas `Feature`s (uma por KIND, `tkr_feature_is_compile_fail` decide pela
  PRIMEIRA scenario da feature — medido o preço de as misturar: a segunda scenario perdeu o seu
  próprio `When compilation fails` e foi julgada como "deve compilar"):
  - "teko::text byte-view escape hatches — …" :: rota C, `When built and run` / `Then exit = 0`.
  - "teko::text byte-view escape hatches — the OWN-BACKEND gap (KNOWN-STOP, …)" :: rota own-native,
    `When compilation fails` / `Then diagnostic = "builtin \`bytes_of_str\` not yet lowered"`.
- `/cases/text_byteview_roundtrip.tks` — a fonte partilhada pelas duas scenarios acima (multi-byte,
  0=ok / 1=comprimento errado / 2=roundtrip errado / 3=erro inesperado).

## Critério de aceitação da lane `.len` — cravado pelo dono (2026-07-29)

Literal do dono: *"o que esperamos é que o len de uma str 'café🐝' retorne 5, neste caso, e a
iteração resulte 5 passos extraindo os caracteres."*

A MESMA string que já serve de sonda deste documento pelo lado dos BYTES passa a servir de sonda
pelo lado dos CARACTERES. Uma string, os dois contadores, os dois medidos:

| expressão | valor exigido | estado |
|---|---|---|
| `"café🐝".len` | **5** | a implementar (hoje devolve bytes) |
| `teko::text::bytes_of_str("café🐝").len` | **9** | MEDIDO e a funcionar na rota C |
| passos de `for c in "café🐝"` | **5** | a implementar |

`5 = c + a + f + é + 🐝`. `9 = 1+1+1+2+4`. A string foi escolhida porque cobre as três larguras
UTF-8 que interessam (1, 2 e 4 bytes) — nenhum bug de fronteira se esconde atrás de ASCII, e a
diferença 5≠9 é grande o bastante para que uma troca de contador falhe alto em vez de passar
despercebida.

**Consequência que muda a sequência dos vagões.** "Extrair os caracteres" quer dizer que a variável
do laço é um `char`, e `char` é açúcar que baixa para `[]byte` (ruling do dono). Logo os 5 valores
produzidos têm larguras 1,1,1,2,4 — a iteração NÃO pode ser um passeio indexado por byte, precisa de
descodificar UTF-8 e devolver uma vista de largura variável.

E daí sai o encaixe: o primeiro gesto de qualquer dev que itere caracteres é comparar um deles —
`if c == c'é'`. Isso é EXACTAMENTE o buraco de Categoria 3 achado pela auditoria da superfície
óbvia (`cargo/0.3.1-superficie-obvia`, commits `6dbc5f7`/`7952d73`): `char == char` falha nas DUAS
rotas (rota C: `cc` recusa com `invalid operands to binary == (have 'tk_char' and 'tk_char')`;
nativa: `'char' has no single PrimKind`). Portanto **`char ==` não é um achado lateral — está no
caminho crítico desta lane** e tem de fechar ANTES de a iteração por caracteres ser entregue, senão
entregamos um laço que produz valores que ninguém consegue comparar.

Ordem que isto impõe: `char ==` → `c'X'` baixa para `[]byte` (vagão próprio do `char`) → `str`
carrega os dois contadores → `.len` conta caracteres (lane própria, com bump de versão e a semente a
seguir, porque o fixpoint não compara um compilador cuja semântica de `.len` difere da sua semente).

Fixture: tem de verificar VALOR (5 e 9), não que compila — a lei das fixtures desta esteira nasceu
precisamente de quatro miscompilações silenciosas que passaram por só verificarem compilação.
