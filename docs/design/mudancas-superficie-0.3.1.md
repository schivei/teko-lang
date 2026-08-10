---
section: design
created: 2026-08-10
status: DESENHO — especificação das mudanças de SUPERFÍCIE da onda 0.3.1 (front + FFI). Companheiro do
        arena-especificacao-unica-0.3.1.md (o backend/arena). Consolida os §2–§7c do master
        lang-evolution-0.3.1-memory-and-surface.md numa ficha por mudança.
source: rulings do dono (var; ->:; self/base/static; Marshall; DI; aposentar unsafe; size/usize;
        sobrecarga) + lang-evolution-0.3.1-memory-and-surface.md
---

# Mudanças de superfície — o que sai, o que entra, o que resolve (0.3.1)

> **Estas são as mudanças de front-end e FFI da onda 0.3.1** — as que tocam gramática e assinatura, e
> por isso **obrigam reseed**. Pela ordem do dono, elas vêm PRIMEIRO (superfície → reseed), e só depois
> o backend faz o novo tratamento de arena (`arena-especificacao-unica-0.3.1.md`). Cada mudança abaixo é
> uma ficha: o que muda, o que sai, o que entra, o que resolve, com exemplo antes→depois.

**Disciplina de migração (aditivo primeiro, sweep depois):** cada mudança entra em uma janela
**aditiva** (a gramática nova é aceita AO LADO da velha, escrita na grafia velha para o seed atual
parsear), o reseed captura um seed que fala a grafia nova, e só então o **sweep** reescreve o `src/` e
remove a grafia velha. Isso mantém o bootstrap sempre construível.

---

## 1. `let`/`mut` → `var` — todo local é mutável

**O que muda.** Existe um único kind de binding local: `var`. `Const` permanece (é outro eixo — a
história de comp-time). `let` e `mut` deixam de ser kinds distintos.

**O que sai.** `BindKind::Let` e `BindKind::Mut` colapsam num só. Os três portões de intenção
(`&x`/`ref x`/`mem::free` exigirem `is_mut`) viram sempre-passa. A rejeição de "reatribuir um `let`"
some.

**O que entra.** `var` como a keyword de local. `is_mut` vira sempre-verdadeiro para locais. `mut`
sobrevive na janela aditiva como grafia **aceita-mas-no-op** (soft-deprecation), não erro de parse, para
o corpus não quebrar de uma vez. CF3 (const-propagation) re-baseia em **fluxo-de-atribuição-única** (um
local escrito exatamente uma vez É imutável de fato — derivável, byte-preservante).

**O que resolve.** Remove um eixo de binding redundante. A segurança nunca vinha da keyword
(`ast-computed-arena-assessment` §4.8): UAF/overflow são da arena, aliasing é da exclusividade F1. Sob
DPS, a inicialização de destino é interna ao lowering (abaixo do portão de borrow do usuário), então
manter `let` obrigaria a ginástica de "`let a = fun()` é mutável por referência"; `var` a dissolve.
Parâmetros e binds de match **continuam imutáveis** (eixo B.21, separado — não é `let`).

**Exemplo.**
```teko
// antes
let a = 0
mut b = 1
b = b + a        // ok; reatribuir `a` seria erro

// depois
var a = 0
var b = 1
b = b + a        // ok
a = 5            // ok agora — todo local é mutável
const K = 42     // Const permanece: um eixo separado
```

---

## 2. `->` → `:` no tipo de retorno — um operador de tipo só

**O que muda.** O tipo de retorno de função/closure usa `:`, o **mesmo** operador que já anota o tipo de
uma variável. Não há mais dois operadores de anotação de tipo.

**O que sai.** O token `Arrow` (`->`) — do lexer e do `token.tks`, no sweep final.

**O que entra.** Na janela aditiva, o parser aceita **`Arrow` OU `Colon`** no retorno (grafia velha
ainda parseia). No sweep, `src/` migra para `:` e o `->` deixa de parsear.

**O que resolve.** Equaliza a linguagem: se `x: T` define o tipo de `x`, `fn f(): T` define o tipo de
`f`. Um operador, uma regra.

**Exemplo.**
```teko
// antes
fn soma(a: i64, b: i64) -> i64 { return a + b }
var f = fn(x: i64) -> i64 { return x * 2 }

// depois
fn soma(a: i64, b: i64): i64 { return a + b }
var f = fn(x: i64): i64 { return x * 2 }
```

---

## 3. Remover `-> ref T` — retorno por referência sai

**O que muda.** Não existe mais retorno tipado-referência. `ref` sobrevive **só em parâmetros**.

**O que sai.** O arm de tipo de retorno `Reference` e seu cluster de portões (`check_ref_return_passdown`
e cia., ~5 funções de checker + 2 call-sites + 1 diagnóstico) e a rota de lowering do ref-return.

**O que entra.** Nada novo — é uma **remoção**. `ref` fica exclusivamente um dispositivo
caller→callee (borrow write-through, ex. `grow_inplace(ref []T)`).

**O que resolve.** Sob DPS todo retorno de agregado já aterrissa na storage do caller, então um retorno
por referência é redundante — o caller já tem o valor onde queria. Uso em produção: **zero** (só uma
probe e duas regressões de rejeição). Escape passa a ser uniformemente por DPS; borrow por F1 — dois
mecanismos ortogonais em vez de `ref` cavalgando os dois.

**Exemplo.**
```teko
// antes (vestigial — identity pass-down)
fn hand_back(ref ch: Ch) -> ref Ch { return ch }

// depois — o form não parseia; o valor já vem na arena do caller por DPS
fn hand_back(ch: Ch): Ch { return ch }
// `ref` só em parâmetro, para borrow:
fn grow(ref buf: []i64, v: i64) { push(ref buf, v) }
```

---

## 4. `self` / `base` / `static` — receptor e fábrica

**O que muda.** Métodos referenciam o objeto por `self` (keyword, receptor sintético — não um parâmetro
solto). `base` referencia o método da super numa sobrescrita. `static` marca uma fábrica/membro de tipo
(sem receptor).

**O que sai.** O receptor solto (o primeiro parâmetro untyped que hoje faz de `self`) e o
`allow_untyped_first` — no sweep.

**O que entra.** `self` sintético; `static` como keyword. `base` é **contextual** — só significa
super-receptor dentro de uma sobrescrita; **continua um nome de local válido** em código de produção que
já o usa (`driver.tks`, `resolve.tks`, `zlib.tks`), para não quebrar. Um parâmetro de usuário chamado
`self` é rejeitado.

**O que resolve.** OOP explícito e consistente: o receptor é uma keyword, não uma convenção posicional; a
super é nomeável; membros de tipo (fábricas) têm marca própria. Ctor de `service` (DI) usa `static`.

**Exemplo.**
```teko
// antes (receptor solto)
fn area(c) { return c.raio * c.raio * 3 }

// depois
type Circle = struct { raio: i64 }
fn Circle::area(): i64 { return self.raio * self.raio * 3 }
static fn Circle::unit(): Circle { return Circle { raio: 1 } }   // fábrica, sem receptor

fn Sub::render(): i64 { return base.render() + 1 }               // `base` = super, contextual
var base = 7                                                      // ainda um local válido
```

---

## 5. Marshall — ponteiros opacos com `__wrap`/`__unwrap`

**O que muda.** O ponteiro deixa de ser genérico (`ptr<T>`) e vira **opaco atômico** (`ptr`/`uptr`), sem
aritmética. O acesso ao valor por trás é por marshalling explícito.

**O que sai.** `ptr<T>`/`Uptr {}` genéricos e seu handling. Aritmética de ponteiro
(`p + 1`, `p[0]`) é rejeitada.

**O que entra.** Dois tipos atômicos opacos `ptr`/`uptr`. Métodos:
- `p.__wrap<T>(): T | error | null` — **falível**: checa liveness da arena + tag do tipo; devolve o
  valor, ou `error` (arena morta / tag divergente), ou `null` (endereço 0).
- `p.__unwrap<T>(): T` — **infalível**: expõe o valor sem checagem (o chamador garante).

**O que resolve.** Ponteiro cru seguro por construção: nada de aritmética, e o acesso passa por uma
checagem dinâmica (tag + liveness) no `__wrap`. É o que torna possível **aposentar `unsafe`** (§6) — o
`__wrap` supre a confiança que o `unsafe` dava por decreto. É também o handle honesto para FFI (embrulhar
um `ptr` estrangeiro).

**Exemplo.**
```teko
// antes
fn read(p: ptr<Node>): i64 { return p.value }   // deref genérico, confiança implícita

// depois
fn read(p: ptr): i64 | error {
    var n = p.__wrap<Node>()      // checa tag + liveness; error se arena morta / tag errada
    return match n { Node => n.value, error => n, null => 0 }
}
```

---

## 6. Aposentar `unsafe` e ponteiros crus

**O que muda.** A keyword `unsafe` e a contaminação por ela deixam de existir. Memória manual sai.

**O que sai.** `unsafe` (keyword + `is_unsafe` + contágio), memória manual (`mem::free`/`#must_free`/
`Arena` não-lexical/`RawBuf`/`Owned<T>`). Tudo no sweep, em passos: reclassificar as intrínsecas de
`teko::mem`/região como **seguras** → aposentar a memória manual → **deletar** a keyword `unsafe` por
último (quando nada mais a conter).

**O que entra.** Nada de keyword. O único resíduo de "confiança" é **FFI**: `extern fn` permanece, e a
fronteira estrangeira é cruzada embrulhando o `ptr` estrangeiro com `__wrap<T>()` (o honest-stop
dinâmico do §5). A `Arena` manual vira um **escopo** (região lexical); `mem::free` é obsoletado pelo drop
de região.

**O que resolve.** A segurança de memória é da arena (capacidade + tempo de vida) + F1, não de um selo
`unsafe` (`memory-unsafe-backend-remodel`). Com ponteiro opaco + `__wrap`, não há mais operação crua a
proteger — o selo perde função. Uma keyword a menos, e a promessa "isto é perigoso, confie" é substituída
por uma checagem real.

**Exemplo.**
```teko
// antes
unsafe fn poke(p: ptr<byte>) { mem::free(p) }

// depois — sem `unsafe`; região dropa no escopo; FFI embrulha o ptr estrangeiro
extern fn c_open(path: ptr): uptr
fn use_it(path: ptr): i64 | error {
    var h = c_open(path).__wrap<Handle>()   // confiança na fronteira = checagem, não decreto
    return match h { Handle => h.fd, error => h, null => -1 }
}
```

---

## 7. Injeção de dependência — `service`/`svc` com lifetimes de arena

**O que muda.** DI de primeira classe: classes de serviço com lifetime (`singleton`/`scoped`/
`transient`), resolvidas por `svc<T>()` em comp-time. Os lifetimes SÃO tempos de vida de arena
(detalhe em `arena-especificacao-unica-0.3.1.md` §8).

**O que sai.** A DI por anotação da era anterior (`#singleton`/`#inject`, `src/checker/di.tks`
`choose_factory`) é substituída pela tabela estática nomeada.

**O que entra.** Keywords `service` (+ `sealed`), os lifetimes `singleton`/`scoped`/`transient`, o ctor
`static`, e `svc<T>()` como **intrínseco de comp-time** (o compilador substitui o call-site inline por
código por-lifetime; sem ABI de runtime). Chaves de string desambiguam múltiplos provedores de uma
interface. **Regra de escape:** valor de serviço nunca armazenado em campo, passado como argumento, ou
retornado em código de usuário — forçando o `claim` explícito via `svc<T>()`; o backend é isento mas o
que segura fica arena-bounded.

**O que resolve.** Resolução determinística em comp-time (tabela estática por análise), sem custo de
runtime, e um modelo de lifetime que casa com a arena em vez de brigar com ela. A regra de escape impede
que um serviço vaze para além da sua arena.

**Exemplo.**
```teko
service sealed Clock singleton {
    static fn ctor(): self { return Clock { } }
    fn now(): i64 { return 0 }
}

fn tick(): i64 {
    var c = svc<Clock>()      // resolvido em comp-time: singleton → slot na raiz
    return c.now()
}

// rejeitado (regra de escape):
// var g; g = svc<Clock>()          // store proibido
// fun(svc<Clock>())                // passar como argumento proibido
```

---

## 8. `size`/`usize` — tipos de palavra de máquina

**O que muda.** Entram `size` (com sinal) e `usize` (sem sinal), os tipos de largura-de-palavra da
máquina. Posições de memória/coleção passam a usá-los.

**O que sai.** Nada por remoção; é aditivo. No sweep, posições `u64` de memória/coleção são reeboladas
para `usize`/`size`.

**O que entra.** `Size`/`Usize` no `PrimKind` + predicados + a tabela de lowering prim→tipo-de-máquina
(`Usize`/`Size` ⇒ `i64` em 64-bit). `usize` é um kind **distinto** de `uptr` (endereço opaco). Fica
**inerte** até ser usado — `src/` ainda diz `u64`, então byte-idêntico.

**O que resolve.** Correção de largura em posições de memória (`.len`/`.cap` de slice, índices, offsets,
tamanhos de arena, o header de slice, a maquinaria DPS/AL3) sem casts. Em 64-bit `usize == u64`, então o
reball é **byte-preservante** (é a prova de que `usize` baixa idêntico a `u64` no alvo). Posições de
fonte (`line`/`col`, `u32`) ficam intocadas.

**Exemplo.**
```teko
// antes
fn index(xs: []i64, i: u64): i64 { return xs[i] }

// depois — largura de máquina explícita; em 64-bit baixa idêntico a u64
fn index(xs: []i64, i: usize): i64 { return xs[i] }
```

---

## 9. Sobrecarga de método e de operador

**O que muda.** Funções de mesmo nome com assinaturas diferentes (sobrecarga de método). Operadores com
comportamento definido pelo usuário (sobrecarga de operador — **comportamento**, não casting; nada de
"explicit/implicit operator").

**O que sai.** A rejeição de "mesmo nome" hoje — relaxada para **distinção por assinatura de parâmetro**.

**O que entra.**
- **Método:** `select_overload` no caminho de chamada; sufixo de símbolo para conjuntos sobrecarregados.
  Inerte até existir um conjunto de sobrecarga.
- **Operador:** um branch de lookup de dunder por operador + o mapa operador→dunder + derivação
  automática de `__eq`/`__lt` para comparação. Inerte até um tipo definir um dunder; o caminho de prim
  fica byte-idêntico.

**O que resolve.** Ergonomia: mesma operação, nomes/tipos diferentes, sem inventar nomes distintos; e
operadores que fazem sentido para tipos do usuário (um `Vec2 + Vec2`), com o comportamento definido pelo
autor do tipo — sem coerção implícita escondida.

**Exemplo.**
```teko
// sobrecarga de método
fn draw(p: Point) { }
fn draw(p: Point, cor: i64) { }        // mesmo nome, assinatura distinta

// sobrecarga de operador (comportamento)
type Vec2 = struct { x: i64, y: i64 }
fn Vec2::__add(o: Vec2): Vec2 { return Vec2 { x: self.x + o.x, y: self.y + o.y } }
var v = Vec2 { x:1, y:2 } + Vec2 { x:3, y:4 }   // usa __add
```

---

## 10. Sequência (ordem do dono: superfície → reseed → backend)

1. **Aditivo (front/FFI):** cada mudança entra aceitando a grafia velha ao lado da nova, escrita na
   grafia velha (o seed atual parseia). Ordem entre elas é livre (são mutuamente independentes).
2. **Sweep:** reescreve `src/` para a grafia nova e remove a velha (dropa `->`, `let`/`mut`, receptor
   solto, `unsafe`, memória manual; rebola `u64`→`usize`).
3. **R1 — reseed** (uma vez): captura um seed que fala a grafia nova. Gate = self-reproduce da rota C +
   provenance + testes de superfície verdes. **O `gen2==gen3` nativo NÃO é gate do reseed nesta ordem** —
   ele vira o marco que fecha a fase seguinte.
4. **Backend — novo tratamento de arena** (`arena-especificacao-unica-0.3.1.md`): DPS/piso/elisão. Fecha
   o `gen2==gen3` nativo por edição de fonte, **sem segundo reseed** (o seed é da rota C, que já funciona;
   os bugs nativos são de fonte).

---

## 11. Pontos em aberto — para tua validação

1. **`mut` na janela aditiva** — aceito-no-op (soft-dep) vs. erro de parse imediato. Proposto: no-op,
   para o corpus não quebrar de uma vez. Confirmar.
2. **`base` contextual** — mantê-lo como nome de local válido (produção usa) exige que "super" só valha
   dentro de sobrescrita. Confirmar que essa contextualidade te serve, ou preferes `base` reservado
   (quebra os call-sites de produção).
3. **Chaves de string na DI** — a forma exata da chave (`svc<I>(key "x")`) e a política de conflito
   (A/B/C/D/E que levantaste) — validar a superfície final.
4. **`chan_unbounded`** — mesmo ponto do Doc 1 §11: manter com aviso ou remover.
