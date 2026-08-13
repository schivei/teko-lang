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

**O que entra.** `var` como a keyword de local. `is_mut` vira sempre-verdadeiro para locais. **`let`/`mut`
NÃO ficam como no-op** (ruling do dono): são varridos para `var` e depois **erram** — superfície que não
existe mais, não uma grafia tolerada. (Durante a janela aditiva o parser aceita ambos só para o seed
construir o `src/` ainda-não-varrido; pós-sweep+reseed, `let`/`mut` são erro de parse.) CF3
(const-propagation) re-baseia em **fluxo-de-atribuição-única** (um local escrito exatamente uma vez É
imutável de fato — derivável, byte-preservante).

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
fn soma(a: i64, b: i64): i64 { return a + b }
var f = fn(x: i64): i64 { return x * 2 }

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
fn hand_back(ref ch: Ch): ref Ch { return ch }

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

**O que entra.** `self` sintético; `static` e `base` como **keywords reservadas** (ruling do dono: se
`base` vira keyword, não fica também como nome de local — "mesma coisa" de `mut`). Os poucos sítios de
produção que usam `base` como local (`driver.tks`, `resolve.tks`, `zlib.tks`) são **varridos** para outro
nome no sweep. `base` = o super-receptor numa sobrescrita. Um parâmetro de usuário chamado `self` é
rejeitado.

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

**O que entra.** Dois tipos atômicos opacos `ptr`/`uptr` e **quatro funções — duas estáticas
(construtoras, infalíveis) e duas de instância (acessoras, falíveis):**
- `ptr::__unwrap<T>(ref T): ptr` e `uptr::__unwrap<T>(ref T): uptr` — **estáticas, infalíveis.** Pegam uma
  **referência a um valor** `T` e devolvem o **ponteiro opaco**. Infalível porque só tomam o endereço —
  **`__unwrap` devolve o PONTEIRO, não o valor**; não há nada a checar.
- `ptr.__wrap<T>(): T | error | null` e `uptr.__wrap<T>(): T | error | null` — **de instância, falíveis.**
  Do ponteiro opaco recuperam o **VALOR** `T`, checando liveness da arena + tag do tipo: `error` (arena
  morta / tag divergente), `null` (endereço 0).

**O que resolve.** Ponteiro cru seguro por construção: nada de aritmética, e o acesso passa por uma
checagem dinâmica (tag + liveness) no `__wrap`. É o que torna possível **aposentar `unsafe`** (§6) — o
`__wrap` supre a confiança que o `unsafe` dava por decreto. É também o handle honesto para FFI (embrulhar
um `ptr` estrangeiro).

**Exemplo.**
```teko
// antes
fn read(p: ptr<Node>): i64 { return p.value }   // deref genérico, confiança implícita

// depois — construir o ponteiro opaco a partir de um valor (estática, infalível → devolve o PONTEIRO)
var node = Node { value: 42 }
var p = ptr::__unwrap<Node>(ref node)            // : ptr

// recuperar o VALOR do ponteiro opaco (instância, falível → checa tag + liveness)
fn read(p: ptr): i64 | error {
    var n = p.__wrap<Node>()                     // : Node | error | null
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
`transient`), resolvidas por **`svc<T: service>(key: str | null = null): T`** em comp-time (o constraint
`T: service` — §9.2b — garante que só serviços se resolvem; a `key` é opcional: sem chave resolve por tipo,
com chave constante desambigua/resolve por nome — inclusive `svc<Tx<T>>("chave")` do canal, §10.2). Os
lifetimes SÃO tempos de vida de arena (detalhe em `arena-especificacao-unica-0.3.1.md` §8).

**O que sai.** A DI por anotação da era anterior (`#singleton`/`#inject`, `src/checker/di.tks`
`choose_factory`) é substituída pela tabela estática nomeada.

**O que entra.** A keyword `service` (**sempre selado** — um serviço **não pode** ser `virtual` nem
`abstract`, então `sealed` é redundante e não se escreve; é o mesmo default do `class`, que só se abre com
`virtual`/`abstract`), os lifetimes `singleton`/`scoped`/`transient`, o ctor
`static`, `svc<T: service>(key: str | null = null): T` como **intrínseco de comp-time** (o compilador
substitui o call-site inline por código por-lifetime; sem ABI de runtime) — **chave ausente = `panic`**, e há
**`has_svc<T: service>(key): bool`** para checar antes. Chaves de string desambiguam múltiplos provedores de uma
interface — e a política de conflito é dura (ruling do dono): **se já há um registro do mesmo tipo sob a
mesma chave, é ERRO DE COMPILAÇÃO** (chaves distintas coexistem; mesmo-tipo-mesma-chave colide em
comp-time, nunca "último vence" silencioso). **Regra de escape:** valor de serviço nunca armazenado em campo, passado como argumento, ou
retornado em código de usuário — forçando o `claim` explícito via `svc<T: service>(...)`; o backend é isento mas o
que segura fica arena-bounded.

**O que resolve.** Resolução determinística em comp-time (tabela estática por análise), sem custo de
runtime, e um modelo de lifetime que casa com a arena em vez de brigar com ela. A regra de escape impede
que um serviço vaze para além da sua arena. **Sob threads** (ruling do dono 08-10), a resolução é **por
thread**: `singleton` vive na raiz da THREAD (não do programa), `scoped` na sub-raiz da thread — a faceta
de arena está no Doc 1 §8. É o "considerar DI no front e no back": a superfície é a mesma, o tempo de vida
é o da thread.

**Exemplo.**
```teko
service Clock singleton {
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

## 9. Sobrecarga (método, operador, tipo) e tipos com métodos sobre primitivo/enum/flag

**O que muda.** Funções de mesmo nome com assinaturas diferentes (sobrecarga de método). Operadores com
comportamento definido pelo usuário (sobrecarga de operador — **comportamento**, não casting; nada de
"explicit/implicit operator"). E **tipos** de mesmo nome com aridade genérica distinta — `Intent` (opaco,
sem dado) e `Intent<T>` (carrega a cópia do dado) coexistem: é a razão do dono para overload servir também
a tipos, não só a métodos/operadores.

**O que sai.** A rejeição de "mesmo nome" hoje — relaxada para **distinção por assinatura de parâmetro**.

**O que entra.**
- **Método:** `select_overload` no caminho de chamada; sufixo de símbolo para conjuntos sobrecarregados.
  Inerte até existir um conjunto de sobrecarga.
- **Operador:** um branch de lookup de dunder por operador + o mapa operador→dunder + derivação
  automática de `__eq`/`__lt` para comparação. Inerte até um tipo definir um dunder; o caminho de prim
  fica byte-idêntico.
- **Tipo com métodos sobre primitivo/enum/flag** (o que dá aos operadores um lar): um `type` respaldado
  por um primitivo (ou `enum`/`flags`) pode carregar **métodos** — **estáticos**, **de instância
  (readonly)** e **operadores** — mas **NÃO tem campos** (o valor É o primitivo). Métodos são aceitos
  também em **`enum`** e **`flags`**. Sem corpo `{}`, o tipo **comporta-se como o próprio primitivo**
  (transparente — um alias/newtype sem superfície nova).

**O que resolve.** Ergonomia: mesma operação, nomes/tipos diferentes, sem inventar nomes distintos;
operadores que fazem sentido para tipos do usuário (um `Vec2 + Vec2`) com o comportamento definido pelo
autor — sem coerção implícita escondida; e **dar comportamento (métodos/operadores) a um primitivo, enum
ou flag** sem embrulhá-lo numa struct (nem pagar um campo).

**Exemplo.**
```teko
// sobrecarga de método
fn draw(p: Point) { }
fn draw(p: Point, cor: i64) { }        // mesmo nome, assinatura distinta

// sobrecarga de operador numa struct
type Vec2 = struct { x: i64, y: i64 }
fn Vec2::__add(o: Vec2): Vec2 { return Vec2 { x: self.x + o.x, y: self.y + o.y } }
var v = Vec2 { x:1, y:2 } + Vec2 { x:3, y:4 }   // usa __add

// tipo sobre primitivo, com métodos — SEM campos
type Celsius = i32 {
    static fn from_f(f: i32): Celsius { return (f - 32) * 5 / 9 }   // estático
    fn to_f(): i32 { return self * 9 / 5 + 32 }                     // instância (readonly)
    fn Celsius::__add(o: Celsius): Celsius { return self + o }      // operador
}
var t = Celsius::from_f(212) + 10        // usa __add; `t` É um i32 por baixo

type Meters = i32                        // sem corpo → comporta-se como i32 puro

// métodos em enum / flags
enum Dir { N, S, E, W } {
    fn oposto(): Dir { return match self { N => Dir::S, S => Dir::N, E => Dir::W, W => Dir::E } }
}
```

### 9.1 Valores default de parâmetro

**O que muda.** Um parâmetro pode ter valor default (`bounds: usize = 1`) — pedido antigo do projeto,
formalizado agora (é o que o `chan<T>::make(bounds: usize = 1)` usa). Uma chamada que omite o argumento
recebe o default.

**O que entra.** Sintaxe `nome: T = <const-expr>` na assinatura; o default é uma **expressão de
comp-time** (const), preenchida no call-site pelo checker. Só na **cauda** da lista de parâmetros (um
parâmetro com default não pode preceder um sem default). Interação com sobrecarga (§9): se uma chamada
fica **ambígua** entre uma sobrecarga e uma aridade preenchida por default, é **erro de compilação** —
mesma disciplina "conflito colide em comp-time" da DI.

**Exemplo.**
```teko
static fn chan<T>::make(bounds: usize = 1): self
var a = chan<i32>::make()      // bounds = 1 (default)
var b = chan<i32>::make(0)     // bounds = 0 (unbounded)
```

### 9.2b Uniões `|` — DECLARAÇÃO vs CONSTRAINT (dois `|` distintos); `variant` para tipo-soma nomeado

**O que muda.** A união `|` tem **duas posições de uso distintas**, e nunca é um `type` nomeado (ruling do dono):

1. **União de DECLARAÇÃO / retorno — estrutural, inline.** Vale em declaração de variável, parâmetro e
   retorno: `fn f(): T | error | null`. É composição estrutural no ponto de uso (o valor É um dos ramos).

2. **União de CONSTRAINT — de FORMAS, no bound de um genérico.** Um constraint é uma **disjunção de
   conjunções**: `<T: Alt1 | Alt2 | …>`, onde cada `Alt` é `Termo & Termo & …`. Os **termos** são:
   - **palavras de forma:** `class`, `service`, `struct` — forçam a *forma* do tipo. `service` **aceita um
     lifetime** (`service singleton` / `service scoped` / `service transient`), forçando também **como** o
     serviço vive;
   - **interfaces** (`Ifce`) e **tipos concretos** (`str`, …) — forçam conformidade/identidade;
   - **`notnull`** — o único termo que **só entra por `&`, nunca por `|`** (não é uma forma-alternativa, é
     um modificador): proíbe o genérico de ser nulo, **nem na definição**.

   ```teko
   <T: class & Ifce | struct & OutraIfce | str>   // T = (class que faz Ifce) | (struct que faz OutraIfce) | str
   <T: notnull>                                     // T não pode ser nulo (único uso de notnull sozinho)
   <T: class & notnull>                             // classe não-nula
   <K: service singleton & IChannelKind<T>>         // o constraint do transporte de canal (§10.2): service SINGLETON
   ```

   **Por que forçar o lifetime é honesto (ruling do dono).** O canal força `service singleton` no `make`
   (§10.2). Sem isso, o usuário poderia declarar um transporte `service transient` (uma instância nova a cada
   resolução — quebra o "um canal por chave em F2") e o compilador teria de **redirecionar deliberadamente**
   essa implementação errônea para singleton, escondendo o erro. Com o lifetime no constraint, um transporte
   que não seja `singleton` **falha na compilação** no ponto do `make` — falha honesta, não coerção silenciosa.

**O que resolve.** Separa o `|` estrutural (o valor é um dos ramos) do `|` de forma (o *tipo* tem uma das
formas), e dá ao genérico poder de exigir forma (`class`/`service [lifetime]`/`struct`), conformidade
(interface/tipo) e não-nulidade (`notnull`) — sem nunca virar um `type` nomeado. Para um tipo-soma
**nomeado**, continua **`variant`** (nominal, com tag — `type Pattern = variant A | B | C`), o mecanismo de
todos os ADTs do compilador. `type X = A | B` **estrutural** segue **rejeitado**; custo zero (os 34
tipos-soma nomeados do corpus já são `variant`).

### 9.2 Tipos de closure — `func<…>` / `action<…>`

**O que muda.** O **tipo** de uma closure passa a ser um subtipo de comp-time, ao modo do C# — em vez da
forma antiga baseada em `fn`. A closure **literal** continua `(params) => expr` (ou `(params) => { … }`) —
**sem `fn`, sem `-> R`**, retorno inferido (já era assim, `parse_expr.tks:308`).

**O que sai.** A anotação de tipo `fn(T): R` / `(): T?` — que **colide** com uniões de retorno:
`fn(T): R | null` é ambíguo entre `(fn(T): R) | null` e `fn(T): (R | null)`.

**O que entra.** Dois construtores de tipo genéricos, **subtipos de comp-time** (monomorfizados, sem a
sobrecarga de um objeto delegate em runtime):
- **`func<T1, …, Tn, R>`** — recebe `T1…Tn`, **retorna `R`** (o último parâmetro de tipo é o retorno).
- **`action<T1, …, Tn>`** — recebe `T1…Tn`, **sem retorno**.

**O que resolve.** Declaração mais simples e **sem colisão**: `func<Record, str> | null` é inequívoco.

**Exemplo.**
```teko
var dobro: func<i32, i32>            = (x) => x * 2
var log:   action<str>              = (m) => print(m)
var fmt:   func<Record, str> | null = null           // sem colisão (era `fn(Record): str | null`, ambíguo)
```

### 9.3 Atribuição múltipla e decomposição de array — `var a, b = …` e `var [a, b] = arr` (sem tipo tupla)

**O que muda.** Entram a **atribuição múltipla** (vários alvos = vários valores, posicional) e a
**decomposição de array** (desempacotar um `[]T` em alvos posicionais), **sem tipo tupla**. Atribuição
múltipla é a base do `await` (§10.3) e dá **retorno múltiplo** de graça. (A decomposição por NOME
`{ x; y }` de campos de struct, B.13, continua.)

**O que entra.**
- `var a, b, c = e1, e2, e3` — liga posicional; **tipos inferidos por posição** (cada alvo pega o tipo do
  seu valor).
- `var a, b: T = e1, e2` — **um tipo só** anotado, compartilhado por todos os alvos.
- **Decomposição de array:** `var [a, b, c] = arr` — desempacota um **array `[]T`** (um tipo REAL) em
  ligações posicionais. **Fica** — é importante para trabalhar com arrays.
- **Retorno múltiplo:** `fn div(): (i32, i32)` (a assinatura lista os retornos); o chamador liga com
  `var q, r = div(17, 5)`. **Não há tipo tupla** — o `( )` só aparece na assinatura e no ponto de ligação,
  nunca como valor de 1ª classe (`var t = div(…)` não existe).

**O que resolve.** Ligar/retornar vários valores e desempacotar arrays sem struct nomeado nem tipo tupla —
sistema de tipos enxuto. A atribuição múltipla é a notação natural para várias tasks (§10.3), matando
`when_all`/`when_any` **e** o `await` de array (mas a decomposição de array em si fica).

**Exemplo.**
```teko
var e, f = 1, "abc"           // e : i32, f : str — tipos por posição (atribuição múltipla)
var g, h: i32 = 1, 2          // g, h : i32 — tipo compartilhado

var [x, y, z] = [1, 2, 3]     // decomposição de array []i32 (tipo real)

fn div(a: i32, b: i32): (i32, i32) { return (a / b, a % b) }   // retorno múltiplo (NÃO um tipo)
var q, r = div(17, 5)                                           // q = 3, r = 2
```

---

### 9.4 Traits (decorador achatável) — e a aposentadoria das *structural traits*

**Onda própria** (não embutida na implementação de §9/§10/§11). Uma `trait` deixa de ser "contrato de
assinaturas" e passa a ser **decorador**: achata-se no tipo que a compõe (nem sub, nem super objeto) e
**não é tipo**. Redefine o comportamento das traits.

**O que uma trait É — mixin concreto auto-contido:**
- **Só membros CONCRETOS** — **campos** (com tipo, default opcional), **métodos-com-corpo** (properties
  `get`/`set`, `static fn`, métodos de instância) e **`const`s**. **Zero membro abstrato/sem-corpo** — isso é
  papel da *interface*. (Campos **VOLTAM**: como tudo achata, um campo de trait é só um campo que achata no
  host — bani-los era preciosismo. Um `const` não é campo, é comp-time; sempre coube.)
- **Auto-contida:** todo `self.X` que um corpo referencia é **declarado na própria trait** (ou numa trait que
  ela **compôs**, abaixo). Uma trait apontando pra algo que **não declara/desconhece** está **visivelmente
  errada** → erro na própria trait, não na composição. Cai o antigo "contrato-sobre-host": o backing `_nome`
  do getter `nome` mora **na trait** (o getter lê `self._nome`, nunca a property — senão recursão).
- **Não é tipo:** sem `var`/parâmetro/retorno/campo/constraint tipado por trait, sem dispatch, sem
  trait-object. Reside só (1) na definição da trait e (2) no `&`-compose de um tipo.
- **`self` = o host que a compõe**, valor (`self._x`) e tipo (`static fn new(): self`).
- **`&` compõe, e traits compõem traits:** `trait C & A & B { … }` achata A e B em C — e C **conhece** A/B,
  logo pode referenciá-los (a auto-contenção estende-se ao que a trait compôs). Achata em **classe
  (selada/virtual/abstrata), struct e service**.
- **Traits carregam interface:** `trait T & IFoo { … }` = T implementa **IFoo inteira** (auto-contida); todo
  host que compõe T passa a **satisfazer** IFoo — é o **host** (tipo) que despacha, a trait não. Padrão
  parcial: uma trait pode só **contribuir** métodos sem declarar `& IFoo`; aí o **host** declara `& IFoo` e a
  composição inteira fecha o contrato (ex.: `NeByEq`). É o substituto **explícito** da structural aposentada
  (um `trait Equatable & IEq` / `Comparable & IOrd` escrito à mão, composto pra dar a capacidade).
- **Colisão = identidade estrutural, comparada pela AST.** Dois membros de mesmo nome ao achatar:
  **compara-se a AST dos membros conflitantes** (pós-parse — normaliza espaços/comentários, mas é
  estrutural: identificadores e ordem contam, `a+b` ≠ `b+a`, nome de parâmetro conta). **AST igual** →
  **absorve num só**; **qualquer diferença** → **erro de compilação**. Subsume o **diamante** (mesma trait 2× = idêntica =
  absorvida, **idempotente**) e integra **sobrecarga** (assinaturas diferentes = overload, coexistem; só
  entram na regra os de mesma assinatura). Resolve o micro-fork struct-vs-trait: redefinir um membro de trait
  com corpo diferente = **erro**, sem override silencioso. *Nota:* dois privados idênticos porém
  semanticamente distintos viram **um campo compartilhado** (absorção é estrutural, não sabe de intenção).
- **Sem `match` sobre trait:** trait não tem discriminante; nome de trait em *subject*/*case* de match =
  **erro nomeado**. Traits compostas idem.
- **Modelo mental:** mixin (estado + comportamento concretos que achatam), estilo trait-de-Scala/`partial` —
  mas **não é tipo** (não instancia, não referencia, não despacha por si; o **host** é que vira o tipo).

**A regra de desambiguação por AST é GERAL, não específica de trait.** Onde quer que a composição por `&`
**funda membros de mesmo nome**, aplica-se a mesma regra — **AST igual → funde num só; qualquer diferença →
erro**: **interface ∘ interface** (`IFoo & IBar`, ou um tipo que implementa várias — duas assinaturas
homônimas idênticas viram um contrato; conflitantes erram), **tipo ∘ (trait/interface)**, e a
composição de **operador-interfaces** (`IEq & IOrd`…). Uma regra uniforme e decidível, não uma por construto.

**Structural traits (`Eq`/`Ord`/`Hash`/`Clone`/`Default` + sinônimos `Hashable`/`Comparable`) —
APOSENTADAS.** Eram *compiler-shadow*: nomes **hardcoded** (`is_structural_trait`, `resolve.tks`) cujos
corpos o compilador **sintetizava dos campos** (`synthesize_structural_methods`, `synth.tks` — gera
`eq`/`compare`/`hash`/`clone`/`default` campo-a-campo, sem metadata de runtime, Law M.0). A máquina
**funciona**, mas **uso real em `/src/**/*.tks` = zero** (nenhum derive `& Eq…`, nenhuma chamada
`.eq()`/`.compare()`/…), e **contradiz o princípio no-shadow** — o dev nunca vê nem escreve o corpo.
Decisão do dono: **remover** — síntese + reconhecedores (`resolve.tks`, `collect.tks`, `monomorph.tks`).
Quem precisar de igualdade/ordem/hash **implementa interface explícita**, visível. (Deleção de código do
compilador — tarefa de implementer numa onda.)

**A capacidade renasce como interface que OBRIGA um operador por contrato** (ruling do dono — habilitado
pelo overload de operador, §9). Em vez da síntese-shadow, `Eq`/`Ord`/… viram **interfaces cujo contrato é
um operador** (`operator __eq`/`__lt`/…, §9). O tipo cumpre **escrevendo o operador** (explícito, visível);
o genérico constrange na interface e **despacha o operador através de T** — exatamente o que a structural
NÃO conseguia (era opaca-em-T, `resolve.tks:863` devolvia superfície vazia; foi por isso que o `Map`
desistiu e virou `str`-keyed). A interface despacha por vtable, então `<K: IEq & IHash>` **destrava** o
`Map<K, V>` genérico que nunca funcionou.

**A interface OBRIGA a counter-part do operador** — completude, sem meia-comparação. Igualdade por
**negação** (`==` obriga `!=`, com `!=` = `!(==)`); ordem por **reflexão** (`<` obriga `>` = operandos
trocados, e `<=` obriga `>=`). O contrato lista **as duas** assinaturas; um **trait-decorador opcional**
entrega o corpo óbvio da counter-part (zero shadow — quem quiser um `!=` não-trivial, tipo NaN, escreve o
próprio em vez de compor o decorador).

```teko
type IEq = interface {
    operator __eq(left: self, right: self): bool     // contrato: ==
    operator __ne(left: self, right: self): bool     // counter-part OBRIGATÓRIA: !=
}

// decorador opcional: entrega o corpo óbvio de !=, sem mágica de compilador
type NeByEq = trait {
    operator __ne(left: self, right: self): bool { !(left == right) }
}

type Point = struct IEq & NeByEq {                   // == à mão, != herdado do decorador
    x: i32
    y: i32
    operator __eq(left: self, right: self): bool { left.x == right.x && left.y == right.y }
}

fn index_of<T: IEq>(xs: []T, needle: T): i32 {       // == despacha pelo contrato IEq — REAL
    var i = 0
    loop { if i >= xs.len { return -1 }; if xs[i] == needle { return i }; i++ }
}
```

Compartilhar o *corpo* de um operador entre tipos de mesma forma é papel do **trait-decorador** (achata
`operator __eq(...) { … }`); comparação campo-a-campo genérica (o que a síntese fazia por mágica) **não**
volta — cada tipo escreve o seu, ou compartilha via decorador quando faz sentido. **Constraint =
interface-only, sem exceção.**

**Sobra UM construto de capacidade:**

| construto | é tipo? | pode constraint? | corpo |
|---|---|---|---|
| **interface** | sim (dispatch) | **sim** | assinaturas (o dev implementa) |
| **trait-decorador** | não (achata) | não | métodos-com-corpo (o dev escreve) |

---

## 10. Concorrência — a superfície (`spawn`/`chan`, `await`, journaling)

A faceta de **arena** dela está no Doc 1 §7; aqui é a **superfície** que o usuário vê. Recomposto de
`concorrencia-isolate-spawn-chan` (08-03), `journaling-de-corrida` (07-30) e `paralelizacao-eixo1/eixo2`
(08-02).

### 10.1 Estratégia de token — tudo contextual

`spawn` (dispara) e `await` (suspende/prefixo de ligação) são **keywords contextuais** (reconhecidas pelo
parser por posição, sem reserva no lexer — a mesma norma que `class`/`abstract`/`virtual`/`override`
seguem); `chan` é um **tipo genérico** (`chan<T>`). **Não há necessidade de uma keyword `async`** (o
`await` prefixo já basta, §10.3) **nem de um `isolate`** (uma thread no mesmo processo ainda pode corromper
e só fala por canais do SO — quem precisa de isolação real usa outro binário). Medido:
**zero identificadores Teko hoje** se chamam `spawn`/`chan`/`await`, então reconhecê-los por posição não
quebra corpus.

### 10.2 `spawn` (corotina) + `chan<T>` — paralelismo real, memória isolada

`spawn` é uma **keyword de corotina** (estilo Go), **não uma função** — `spawn f(args)` lança `f` como
uma **thread** fire-and-forget — sub-raiz de arena própria (F1), argumentos **por cópia**, **sem retorno**
e **sem `join`**. Não há isolamento tipo-processo na linguagem (quem precisa usa outro binário). Doc 1 §7.6.

```teko
spawn f()                                          // KEYWORD (não função): dispara e segue, SEM id, args por cópia

// chan<T> MPSC (fan-in: N escritores, 1 leitor). TODO transporte é um `service singleton`; a chave é CONSTANTE.
// TRANSPORTE = contrato extensível (SO, memória, Kafka, RabbitMQ, RPC, WS, HTTP, …):
type IChannelKind<T> = interface { fn init(key: str); fn send(v: T); fn recv(): T; fn end() }

// make: cria, chama K.init(key), registra o serviço na RAIZ DO PROGRAMA (F2) sob a chave,
// e devolve o CONTEXTO (ctx) — o WaitGroup MANUAL do canal + um fecho de reserva:
static fn chan<T>::make<K: service singleton & IChannelKind<T>>(key: str, bounds: usize = 1): Ctx | error
                                                   // error: conflito de chave (runtime) ou falha do K.init (abrir o transporte)
fn has_svc<T: service>(key: str | null = null): bool   // checar existência antes de resolver (evita o panic do svc)
              // key:    CONSTANTE (literal/const, comp-time) — o "nome" do canal
              // bounds: 1 (default)=bounded-1 · N=bounded-N · 0=UNBOUNDED

// Os extremos são clamados por svc, pela MESMA chave constante (svc = comp-time, inline):
var rx = svc<Rx<T>>("chave")                       // LEITOR único
var tx = svc<Tx<T>>("chave")                       // ESCRITOR (N)
fn  Tx<T>::send(v: T): null                        // devolve null; a propriedade tx.closed diz se fechou (drenado)
fn  Tx<T>::close()                                 // o PRODUTOR fecha: invoca o end() (fecha + drena)
fn  Rx<T>::pop(): T | Closed                       // recebe; o erro ESPECÍFICO `Closed` quando encerrado e drenado

// WaitGroup MANUAL — add/done disponíveis TAMBÉM nos handles (o ctx é transient; o worker não o alcança):
fn  Tx<T>::add()                                   // um PRODUTOR se registra pelo SEU handle
fn  Tx<T>::done()                                  // um PRODUTOR sinaliza fim pelo SEU handle
fn  Rx<T>::add()                                   // um CONSUMIDOR se registra pelo SEU handle
fn  Rx<T>::done()                                  // um CONSUMIDOR sinaliza fim pelo SEU handle
fn  ctx::add(n: usize)                             // o CRIADOR registra N (antes do spawn = race-free)
fn  ctx::wait()                                    // o criador bloqueia até o contador zerar
fn  ctx::close()                                   // fecho de reserva do canal

fn hardware_parallelism(): usize                   // paralelismo concedido pelo SO
```

Um fluxo mínimo — a `main` cria o canal (recebe o **ctx**), faz o **`add` manual**, uma corotina resolve o
escritor por chave, escreve e faz **`done` manual**; a `main` lê até encerrar e `wait`. **Nenhum id trafega:**

```teko
fn produz() {                            // função sem retorno — SEM id (resolve por chave)
    var tx = svc<Tx<i32>>("nums")        // extremo de escrita, pela chave constante
    var i = 0
    loop while i < 100 { tx.send(i); i = i + 1 }
    tx.close()                           // o PRODUTOR fecha → o pop do leitor passará a devolver `Closed`
    tx.done()                            // done() pelo próprio handle (o ctx é transient, inalcançável aqui)
}

fn main() {
    var ctx = chan<i32>::make<OsChan<i32>>("nums", 64)   // cria em F2 sob "nums"; devolve o ctx
    ctx.add(1)                            // add() MANUAL — 1 task a esperar
    spawn produz()                        // dispara e segue (sem join) — SEM id
    var rx = svc<Rx<i32>>("nums")         // o LEITOR também por svc, pela chave
    loop {
        var v = rx.pop()                  // v: i32 | Closed
        match v {
            i32    => usar(v),
            Closed => break               // erro específico: encerrado e drenado — a sincronização
        }
    }
    ctx.wait()                            // bloqueia até o done()
}
```

**Sem `join`:** dados só cruzam a fronteira por **cópia** (via `chan`); espera-se pelo **fecho do canal**
(o `pop` devolver `Closed` após `end()`) ou pelo `ctx.wait()`. A camada de linguagem **não reimplementa**
limite/contrapressão/fecho — pede uma vez na abertura e confia no transporte. *(A primitiva
`fork_join` de baixo nível sobrevive como mecanismo INTERNO do backend, para paralelizar o codegen — não
é superfície de usuário; o usuário escreve `spawn`.)*

**`make` devolve o `ctx`; ambos os extremos por `svc` (ruling do dono).** A criação é por `make`, que devolve
o **contexto** (`ctx`) — o **WaitGroup** do canal e um fecho de reserva. Os extremos são clamados por chave:
`svc<Rx<T>>(key)`, `svc<Tx<T>>(key)` — coordenação sem trafegar id.

**WaitGroup MANUAL; `add`/`done` também nos handles — não bloquear o dev (ruling do dono).** A contagem é **do
usuário**, **nunca automática** (o `spawn` não registra nada, a saída da task não faz `done` sozinha). Como o
`ctx` é **transient** e um worker não o alcança, mas sempre segura o seu `tx`/`rx` (estável, F2), tanto
**`add`** quanto **`done`** ficam **também nos handles**: `tx.add()`/`tx.done()` (produtor) e `rx.add()`/
`rx.done()` (consumidor). O criador ainda tem `ctx.add(n)` e `ctx.wait()`. **Caveat (não trava):** adicionar
**depois** do `spawn` (via handle) em vez de **antes** (via `ctx.add`) é a corrida "add-depois-do-spawn", e é
**responsabilidade do dev** — o `ctx.add(n)` antes do `spawn` é o caminho race-free, mas ele coordena como quiser.

**Registro (compilador) vs materialização (`make`) — dois modos de chave (ruling do dono).** `Ctx`/`Rx<T>`/
`Tx<T>` **não** são serviços do usuário — são handles que o compilador registra e o `make` materializa. O
registro tem duas fases, e o modo é escolhido pela **forma da chave**:
- **Chave CONSTANTE (literal/`const`) — registro ESTÁTICO, inline (o default, custo zero).** O compilador
  pareia cada `svc<…>("literal")` ao `make` da mesma constante, **conhece o `K`**, monomorfiza `Ctx`/`Rx`/`Tx`
  + as ops do transporte, reserva o slot **inline**; o `make` só **materializa**. **Sem lookup em runtime.**
  Conflito (mesmo `chan<T>`+chave em dois `make`) = **erro de compilação**.
- **Chave VARIÁVEL (`str` de runtime) — PRÉ-REGISTRO + FINALIZAÇÃO em runtime.** Para o caso em que a chave
  não é conhecida na compilação (um canal por conexão/request/usuário), o compilador **pré-registra a FORMA**
  (monomorfiza `(chan<T>, K)`: handles por `T` + um **registro de ops** do transporte), e o `make` **finaliza**
  a ligação chave→instância num **registro processo-inteiro chaveado pela string** (F2). Aí `svc<Rx<T>>(var_key)`
  é um **lookup em runtime**, e as ops de `Rx`/`Tx` viram **chamada indireta** (ponteiro de função) — **não**
  vtable de interface, **não** o Round 3. **Custo:** um lookup + uma indireção por op. Conflito = **erro em
  runtime** e sai como **`error`** — por isso `make` devolve **`Ctx | error`** (o `error` cobre o conflito de
  chave e a falha do `K.init`, p.ex. abrir o socket). **`svc` de chave não encontrada = PANIC (ruling do
  dono)** — o `svc` é infalível no tipo (devolve `T`); chave ausente é `panic`. Para checar **antes** há
  **`has_svc<T: service>(key): bool`**. (O `T` continua estático nos dois modos; só o `K` é apagado no `svc` variável.)

**O `ctx` É O DONO do lifetime — transient (ruling do dono, ratificado).** O serviço singleton do canal vive em F2, mas
**quem o possui é o `ctx`** (transient, na região do criador). Quando o `ctx` cai, ele **cascateia o
teardown**: `end()` → desregistra a chave → **libera** o serviço + `Rx` + `Tx` de F2. O canal, os dois
extremos e o serviço morrem **junto com o `ctx`**. O **UAF é fechado por construção**: (a) `ctx.wait()`
barreira — o criador espera todas as tasks antes de a região cair, então nada vivo aponta para o que morre;
(b) resolução por **chave** (não ponteiro) — depois do teardown, `svc<…>("chave")` **falha** em vez de
pendurar. O **"free"** é a **reclamação por-entrada de F2** disparada pelo drop do `ctx` (disciplinada pela
arena, não um `mem::free` manual) — a única capacidade nova de arena que isto exige (Doc 1 §7.8).

**Quem fecha o canal — o PRODUTOR (ruling do dono).** O **`Tx` tem o `close()`** — quem sabe que não há mais
mensagens é o produtor, então é ele que fecha (`tx.close()` → `end()`; convenção Go); com N produtores,
coordena por `ctx.wait()` e fecha **uma vez** (idempotente). O fecho também fica **no `ctx`** (`ctx.close()`),
**caso precise**. O **consumidor (`Rx`) não fecha** — faz `pop()` até receber o erro **específico `Closed`**
(encerrado + drenado, distinto de um `error` de transporte). Ambos observam: `Rx` pelo `Closed`, `Tx` por
`tx.closed`.

**Transporte extensível — `service singleton & IChannelKind<T>` (ruling do dono).** O transporte **não é um enum
fechado**: é uma **interface** genérica com `init(key)`/`send(T)`/`recv(): T`/`end()` (o `init` é o método
prévio que liga o transporte à chave; o `end()` sinaliza fecho + dreno). A linguagem entrega os built-ins
`OsChan<T>` (default — `SOCK_DGRAM`/mailslot) e `MemChan<T>` (fila em-processo, sem syscall), e o dev
**pluga o seu** — Kafka, RabbitMQ, RPC, UDP, WebSocket, HTTP, o que for. É **native-only**: código Teko
falando o protocolo, ou link dinâmico FFI a uma lib de sistema, nunca um `.c` local.

**Totalmente estático — DI por CHAVE CONSTANTE (ruling do dono).** Todo transporte é um **`service singleton`**
(o constraint `K: service singleton` faz um transporte não-singleton **falhar na compilação** no `make`, em
vez de o compilador redirecionar silenciosamente uma implementação errada — §9.2b), e o
canal é resolvido pela **chave constante** (um literal/`const`, comp-time), como `svc<T>("chave")`. `make<K:
service singleton & IChannelKind<T>>(key, bounds): ctx` cria o canal, chama `K.init(key)`, registra o serviço
na **raiz do programa (F2)** sob a chave e devolve o **ctx** (WaitGroup); **ambos** os extremos vêm por
`svc<Rx<T>>("chave")` / `svc<Tx<T>>("chave")` — resolvidos **inline em comp-time**. Assim **nada trafega id**
(nem no `spawn`), e **nada reconstrói o tipo**: a chave constante + o tipo dizem tudo em compilação. O serviço
do canal vive em **F2** (exceção de lifetime — não raiz-de-thread — por ser comunicação entre tasks), do
`make` ao fecho. **Não** é dispatch dinâmico do Round 3 — pré-requisito é só a conformidade estática de
interface (já existe) + o `service` DI por chave. **Conflito de chave = erro de compilação** (§7).

```teko
type IChannelKind<T> = interface { fn init(key: str); fn send(v: T); fn recv(): T; fn end() }

// o dev escreve o seu transporte — um `service singleton` que satisfaz a interface (conformidade estática):
service KafkaChan<T> singleton & IChannelKind<T> {   // singleton: forçado pelo constraint do make
    brokers: str
    topic: str
    fn init(key: str) { /* liga o transporte ao tópico derivado da chave `key` */ }
    fn send(v: T)     { /* serializa v e publica (Teko puro ou FFI dinâmico) */ }
    fn recv(): T      { /* consome e desserializa em T */ }
    fn end()          { /* fecha o produtor e drena */ }
}

var ctx = chan<Order>::make<KafkaChan<Order>>("orders", 64)  // cria, init("orders"), serviço → F2; devolve o ctx
var rx = svc<Rx<Order>>("orders")                            // leitor resolvido por chave (comp-time)
var tx = svc<Tx<Order>>("orders")                            // escritor resolvido por chave (comp-time)
```

### 10.3 `await` — alarga o retorno para `Intent<T>` (sem necessidade de `async`)

**Não há necessidade de `async`, a função NÃO declara `Intent`, e o `await` é um PREFIXO de ligação/atribuição** (não um
operador de expressão). A função retorna o seu tipo normal; `await var a = f()` faz `a` receber um
`Intent<T>` em vez do valor cru — **alarga**, ao contrário das linguagens que **estreitam** a assinatura
para `Task<T>`/`Promise<T>`. (Consequência: não há `await` inline numa expressão — liga-se primeiro.)

```teko
fn calc(x: i32, y: i32): i32 { x + y }

await var a = calc(1, 2)       // a : Intent<i32> — o `await` prefixa a ligação
if a.canceled { println("canceled") }
else          { println($"r = {a.value}") }

await var b, c = fb(), fc()    // atribuição múltipla (§9.3): b, c : Intent<…>
await a = fa()                 // reatribui um `a` existente (remanescente de outro intent)
```

`await f(args)` **suspende** (cede, sem bloquear a thread), executa `f`, e o retorno cai em **`.value`** de
um `Intent<T>` criado no caller. Campos: **`.value: T`**, **`.canceled: bool`**, **`.failure: error | null`**
(razão do cancelamento). Erro-de-negócio fica no `.value`; cancelamento é `.canceled`+`.failure`. `Intent`
(sem `T`) = o desfecho de esperar uma função **sem retorno** (só `.canceled`/`.failure`). É onde se
**garante a execução** — o `spawn` é fire-and-forget, sem retorno.

**A `Intent` é PROTEGIDA — o dev LÊ, o runtime GRAVA, ninguém inicializa à mão.** Struct puro (sem trait):
backing fields **privados** (`_value`/`_canceled`/`_failure`, visíveis só a runtime/compilador), face de
leitura por **`exp get`**, escrita do desfecho por **`pub set`** (o runtime grava dado no sucesso, ou
`canceled`+`failure` no cancelamento), e a única construção é **`pub static fn new`** — interna. A grafia
`exp`/`pub` é **forward-compatible**: hoje tudo lê como `exp` (sem enforcement); quando o **§11** entrar,
`pub` passa a valer e a proteção **auto-corrige**, sem refactor.

```teko
exp type Intent = struct {           // esperar uma função SEM retorno — só o desfecho
    _canceled: bool
    _failure: error | null

    exp get canceled(): bool          { self._canceled }
    exp get failure(): error | null   { self._failure }
    pub set canceled(v: bool)         { self._canceled = v }
    pub set failure(v: error | null)  { self._failure = v }

    pub static fn new(c: bool, f: error | null): self {
        self { _canceled = c; _failure = f }
    }
}

exp type Intent<T> = struct {         // esperar uma função COM retorno T — o valor cai em `.value`
    _value: T | null
    _canceled: bool
    _failure: error | null

    exp get value(): T | null         { self._value }
    exp get canceled(): bool          { self._canceled }
    exp get failure(): error | null   { self._failure }
    pub set value(v: T | null)        { self._value = v }
    pub set canceled(v: bool)         { self._canceled = v }
    pub set failure(v: error | null)  { self._failure = v }

    pub static fn new(v: T | null, c: bool, f: error | null): self {
        self { _value = v; _canceled = c; _failure = f }
    }
}
```

**Dependências da forma** (todas forward-compatible): **§9** (properties `get`/`set`, factory estática,
`self {}`), **item 14** (value-struct mutável — o `set` escreve `self._x`, o que a regra "struct é
readonly" proíbe hoje), **§11** (enforcement `exp`/`pub`). E uma **consequência de arena**: value-struct +
preenchimento pelo runtime exige que o `set` acerte **a mesma instância** que o awaiter segura — nada de
cópia entre o fill e a leitura (residência F1/F2, §10.2).

**`cancel()` — global, como `panic`, ciente de suspensão.** O runtime marca se a tarefa corrente está sob
`await`. `cancel()` **em suspensão** → cancela o `Intent` corrente (`.canceled = true`, `.failure` = a
razão); **fora de suspensão** → dispara um `panic`. Um verbo só que degrada de cancelamento cooperativo
para panic.

**Várias tasks — atribuição múltipla (§9.3), sem `when_all`/`when_any` nem `await` de array.** O `await`
prefixa uma ligação múltipla e **cada alvo vira um `Intent`**, todas esperadas:

```teko
await var a, b, c = fa(), fb(), fc()   // a, b, c : Intent<…> — todas esperadas em paralelo
```

Como **não há throwing** (cancelada ou não, a task sempre executa até um desfecho), esperar todas é seguro;
inspeciona-se cada `Intent` (`.value`/`.canceled`/`.failure`). Isso torna `when_all`/`when_any` e o `await`
de array desnecessários. O dev nunca escreve `Intent` num retorno; só o `await` o produz.

**Descartar o retorno — `await _ = f()`.** Espera `f` completar (garantia de execução, por suspensão) **sem
capturar** o desfecho — como esperar uma `Task` em C# sem guardar o resultado. Usa o `_` (o mesmo descarte
do resto da linguagem), que não abre variável nem materializa o `Intent`. Difere do `spawn`, que **não**
espera:

```teko
await _ = liberar_cache()      // espera; nenhum Intent capturado
await _, x = fa(), fb()        // descarta o 1º, captura o 2º em x : Intent<…>
```

- **Arena/fundações** (Doc 1 §7.9): **I/O** — reator (`epoll`/`kqueue`/`IOCP`), sem thread nova; **CPU** —
  corotina isolada de um pool (F1). Em ambos o `await` cede e retoma, nunca bloqueia.

**Não** existe thread compartilhando arena sem F1. E **`ref` não cruza** a fronteira: nem `spawn`/`chan`
aceitam `ref`, nem `<ref T>` genérico (§10.6) — preserva UAF; cruza cópia ou id.

### 10.4 `teko::journal` — o módulo de journaling

O journal é um **registro append-only, carimbado por corrida, segmentado por escritor**; a sumarização
**relê** (`fold`), não funde. Para quem escreve testes hoje, **nada muda** de grafia (`teko::test::scoped`
segue igual). Segue **exatamente o mesmo modelo dos canais** (ruling do dono): o **sink** é um
`service singleton` que satisfaz uma interface, o `make` recebe a **chave constante** e devolve o **ctx**, e
o **escritor** é clamado por `svc` — tudo residindo na **raiz do programa (F2)** (Doc 1 §7.10):

```teko
pub type Record = struct { run: str, writer: str, kind: str, payload: str }

// o SINK do journal é um contrato — um `service singleton`, como o transporte de canal:
type IJournalKind = interface {
    fn init(key: str)                 // método prévio: liga o sink à chave constante
    fn append(rec: Record)            // grava um registro (write(2) O_APPEND, sem buffer)
    fn end()                          // fecha + flush
}

// make: cria, chama K.init(key), registra o service singleton em F2 sob a chave, devolve o ctx:
static fn journal::make<K: service singleton & IJournalKind>(
    key: str, roll: Roll = Roll::none, fmt: func<Record, str> | null = null): ctx

var jw = svc<Jw>("chave")            // o ESCRITOR do journal, clamado por chave (Jw = handle de escrita)
fn  Jw::append(kind: str, payload: str): null | error
fn  Jw::close()                      // o produtor fecha (invoca end()); reserva em ctx.close()

// nível de corrida (funções de corrida, sem chave de segmento) — inalterado:
static fn journal::run_id(): str                 // <ns monotônico>-<pid> — nomear a corrida mata o lixo calado
static fn journal::run_root(): str               // bin/.tkrun/<run_id()>
static fn journal::fold(root: str, run: str): []Record   // relê; descarta lixo/linha rasgada; sintetiza `end`
static fn journal::scratch(base: str): str       // o compositor único de caminhos isolados
static fn journal::sweep(keep: str): usize       // limpeza é da corrida SEGUINTE, nunca da própria
```

O sink default é o **`FileJournal`** (`service singleton & IJournalKind` — `write(2)` `O_APPEND` sem buffer,
com rolling); o dev pode plugar outro sink (um serviço remoto de log, etc.), como qualquer transporte de canal.

**Rolling e formatação — configuráveis pelo dev** (ruling do dono; é aqui que um journal de 10 GB se
gere). O `make` recebe, com defaults, a **política de rolling** (QUANDO rotacionar para um novo ficheiro)
e o **formato** (COMO cada registro é serializado):

```teko
enum Roll {                       // QUANDO rolar (política de rotação)
    none,                         // um único ficheiro (default)
    size(usize),                  // rola ao atingir N bytes  → ex.: Roll::size(100 * 1024 * 1024)
    daily,                        // rola por data (um ficheiro por dia)
    custom(func<SegStat, bool>)   // o predicado do dev decide (tamanho, idade, contagem, o que for)
}

// fmt é uma CLOSURE (func<Record, str>), default null: se null (ou omitido), usa-se a formatação padrão
// (o formato interno "kind<TAB>payload"); senão, a closure do dev serializa cada registro.

// ex.: padrão (FileJournal, fmt omitido → formatação padrão), rolando a cada 100 MB:
var ctx = journal::make<FileJournal>("app", Roll::size(100 * 1024 * 1024))   // devolve o ctx
var jw  = svc<Jw>("app")                              // escritor por chave
jw.append("evt", "iniciado")

// ex.: formatação própria via closure — literal `(params) => expr`, sem `fn`:
var ctx2 = journal::make<FileJournal>("audit", Roll::daily, (r) => r.kind + "|" + r.payload)
```

O rolling é do runtime de durabilidade (o `append` verifica a política e rotaciona por `rename` atômico
antes de escrever quando ela dispara); o `fold` relê todos os ficheiros de um segmento em ordem,
transparente ao rolling. A closure `fmt` vive com o journal (arena raiz) e é chamada por `append`.

**Papel duplo + ciclo de vida (rulings do dono, Parte 4).**
- **Ambos os papéis, um módulo só.** O journal é **ao mesmo tempo** durabilidade dev-facing **e** o veículo
  do **journaling de concorrência** — incluindo o produto que o motivou: escrever as saídas de teste em
  **paralelo** para pós-análise. Tudo passa pelo **sink** (destino IO) e pelo **out** (formato); a mesma
  abstração cobre **texto e binário**.
- **Escrita concorrente binária.** O caso motivador: N escritores → `chan<Rec>` (bounded MPSC) →
  orquestrador agrega/sumariza → **`.tkj`** (binário, append-only, quadro **prefixado por comprimento**,
  precedente `.tkb`); relê-se com `teko journal corrida.tkj -o corrida.log`. Binário **por enquadramento**
  (o `expected`/`got` de uma asserção tem quebras de linha; um quadro prefixado torna a carga opaca e
  incorruptível ao enquadramento), **portátil** (little-endian fixo — `tkb_buf.tks` desloca bytes, um
  journal de CI arm64 lê num x86_64). O registro carrega `writer`+`seq` (atribuição + perda **detectável**);
  a captura de `panic`/`exit` mantém o **processo vivo**. Desenho completo do produto: artifact das leis
  §17–§27 (dono, 2026-07-30).
- **Lazy.** O journaling **liga no primeiro uso** (custo zero antes) e **fica ligado até o fim** (residência
  **F2**). Nem sempre-ligado, nem compilado-fora, nem nível-em-runtime.

### 10.5 Modelo de concorrência — resumo (decisões fechadas)

| tema | fechado (08-10) |
|---|---|
| **`spawn`** | keyword (Go-style); dispara uma **função sem retorno** como **thread** fire-and-forget; args por cópia; sem `join` |
| **`isolate`** | **sem necessidade** — thread no mesmo processo ainda pode corromper e só fala por canais do SO; isolação real = outro binário |
| **`chan<T>`** | tipo genérico MPSC; `make<K: service singleton & IChannelKind<T>>(key, bounds = 1): ctx`; WaitGroup **manual** — `add`/`done` no `ctx` **e** nos handles (`tx`/`rx`, pois o ctx é transient), `ctx.wait()` no criador; extremos por `svc<Rx>`/`svc<Tx>`; unbounded = `make(key, 0)` |
| **transporte** | `IChannelKind<T>` = `interface { init(key); send(T); recv(): T; end() }` **extensível**; built-ins `OsChan`/`MemChan` + plug do dev (Kafka/Rabbit/RPC/UDP/WS/HTTP); **DI por chave** — todo transporte é `service singleton` (não-singleton = erro), vive em **F2**; chave **constante** = registro estático/inline, chave **variável** = pré-registro + lookup runtime; **elimina id no `spawn`**; não dispatch dinâmico |
| **fecho** | **responsabilidade do PRODUTOR** — `tx.close()` invoca `end()` (fecha + drena), idempotente; reserva em `ctx.close()`; consumidor faz `pop()` até o erro específico `Closed`. `Rx::pop(): T \| Closed`; `Tx::send(): null` + `tx.closed` (ambos observam) |
| **`await`** | **sem necessidade de `async`**; prefixo de ligação que **alarga** o retorno para `Intent<T>` por suspensão; sem inline; `await _ = f()` descarta o retorno |
| **`Intent<T>`** | `.value` / `.canceled` / `.failure`; **`cancel()`** global (cancela o Intent, ou `panic` fora de suspensão) |
| **várias tasks** | por **atribuição múltipla** (`await var a, b = fa(), fb()`), sem `when_all`/`when_any` |
| **`ref`** | não cruza fronteira; `<ref T>` proibido (§10.6) |
| **namespace / afinidade** | `teko::threads`; afinidade **válida, registrada como capacidade futura** |

### 10.6 `ref` e genéricos sob concorrência — a regra de UAF (ruling do dono 08-10)

`ref` (borrow) **não pode cruzar fronteira de MT/async**, e um genérico **não pode ser parametrizado por
referência** — `<ref T>` é rejeitado pelo checker. Um borrow que atravessasse uma task/continuação
penduraria quando a arena do outro lado dropasse (UAF). Consequências de superfície:

- `spawn`/`svc<Tx<T>>`/`svc<Rx<T>>`/uma função esperada por `await` **rejeitam** parâmetro/valor `ref`.
- `Foo<ref T>` não parseia/typa. `ref` sobrevive só como borrow **local** caller→callee que **não** cruza
  fronteira de concorrência (§3: `ref` já é só parâmetro).
- O que cruza a fronteira é **cópia** (valor) ou **nome** (a chave constante do canal), nunca borrow.

---

## 11. Sequência (ordem do dono: superfície → reseed → backend)

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

**Ordem dos itens de roadmap dentro desta janela** (rulings do dono):
- **item 14 — value-struct mutável** (C#-style via property, tirando o bloqueio "struct é readonly"):
  **ANTECIPADO.** Está no caminho crítico da Intent (o `pub set` que escreve `self._x`, §10.3) e do `Ctx`
  performático; entra cedo.
- **expansão da stdlib — ANTES da §11.** O cerne é **aumentar os recursos do dev** (um **catálogo** de novos
  itens — passo focado próprio, separado de "como separa"); e decidir a **separação** que permite o
  **self-link com o programa final**. É quem **povoa** o `exp`/`pub` que a §11 depois formaliza. Bônus: **já
  reduz parte dos problemas de memória** (o RSS super-linear do item 13 — o programa linka contra o monólito
  pré-compilado em vez de re-arrastá-lo como fonte).
  - **Mecanismo (ruling do dono): self-`.tkh` do monólito.** Teko é monolítico; **nada de extrair pacote**. O
    compilador, ao compilar, **exporta o `.tkh` dele próprio**, e o programa final **linka contra o monólito**
    via esse header. Só o que é **`exp`** entra no self-`.tkh` → reforça a necessidade de `exp` no visível ao
    dev. **Substitui** a ideia de pacote-separado (o fork P2 da prep).
- **§11 — visibilidade `exp`/`pub`: PENÚLTIMA** (não mais a última). O enforcement vira muita coisa
  hoje-visível em **ilegal** (muitas falhas a corrigir), e a grafia já é **forward-compatible** (hoje tudo
  lê como `exp`; a proteção da Intent auto-corrige quando ela entrar), então atrasá-la minimiza retrabalho.
  **Forma a superfície de visibilidade** sobre a qual a §12 opera.
- **§12 — libc-direct / `#if` / `#os` / macro: ÚLTIMA.** Invertida com a §11: precisa **operar sobre a §11
  já formada** (a visibilidade `exp`/`pub` decidida), então só entra depois dela.

---

## 12. Decisões — todas resolvidas pelo dono (2026-08-10)

1. ~~`mut` na janela aditiva~~ — **superfície removida ERRA, sem no-op.** "Vale pra tudo": `let`/`mut`,
   `->`, receptor solto, `unsafe`, `ptr<T>` genérico são varridos para a grafia nova e depois erram — não
   ficam como grafia tolerada. (Na janela aditiva o parser aceita ambos só para o seed construir o `src/`
   ainda-não-varrido; pós-sweep+reseed, a grafia velha é erro de parse.)
2. ~~`base` contextual~~ — **`base` é keyword reservada** (§4); os sítios de produção que o usam como local
   são varridos para outro nome. Não fica como nome válido.
3. ~~chave-string da DI~~ — **mesmo tipo sob a mesma chave = ERRO DE COMPILAÇÃO** (§7); chaves distintas
   coexistem, nunca "último vence" silencioso.
4. ~~`chan_unbounded`~~ — entra, responsabilidade do dev.
5. ~~tipo do canal (enum `os`/`mem`)~~ — **transporte é um `service singleton & IChannelKind<T>` extensível**
   (`interface { fn init(key); fn send(T); fn recv(): T; fn end() }`): built-ins `OsChan` (default) /
   `MemChan`, e o dev pluga Kafka/Rabbit/RPC/UDP/WS/HTTP. **Totalmente estático — DI por CHAVE CONSTANTE:**
   `make<K: service singleton & IChannelKind<T>>(key, bounds): ctx` devolve o **ctx** (WaitGroup); **ambos**
   os extremos por `svc<Rx<T>>("chave")` / `svc<Tx<T>>("chave")` (comp-time, inline). O serviço vive na **raiz
   do programa (F2)** — exceção de lifetime, por ser comunicação entre tasks — do `make` ao fecho. Fechar = do
   **produtor** (`tx.close()`; reserva `ctx.close()`); `Rx::pop()` até o erro específico `Closed`. **Elimina
   passar id no `spawn`.** Não dispatch dinâmico do Round 3.
6. ~~`isolate` / `async`~~ — **sem necessidade de existirem** (o `await` prefixo basta; thread no mesmo
   processo não justifica `isolate` — isolação real = outro binário).

**Nada em aberto na superfície.** *(Pré-requisitos registrados, ordenados, que a onda de concorrência exige —
e nenhum é o dispatch dinâmico do Round 3: (a) **conformidade estática de interface** (`type X = interface
{…}`); (b) o **solver de constraint de forma** — `<T: forma & ifce | … | tipo>`, com `service [lifetime]` e
`notnull` (§9.2b); (c) o **`service` DI resolvido por chave constante** (`svc<T: service>("chave")`). O
transporte de canal depende dos três.)*

---

## 13. Value-struct mutável (roadmap item 14) — remover o bloqueio "struct é readonly"

**Antecipado** (§11): está no caminho crítico da Intent (o `pub set`, §10.3) e do `Ctx` performático. Remove
o bloqueio que torna struct imutável, para mutação **in-place** (comportamento de classe) **sem** virar
objeto — continua **value type** (cópia na atribuição/passagem, sem identidade nem heap), pela performance.

### 13.1 Modelo de mutabilidade — tudo é `var`

Sem `let`/`mut` (§1): **toda variável é `var`, mutável por default.** Não há marca de tipo "mutable struct"
— qualquer struct é mutável. Consequências (rulings do dono):

- **`val` NÃO é binding de usuário** — é **marca interna**, o que restou: o **alias de match** (`as`,
  readonly raso — não re-vincula, transparente a props/métodos internos, ver plano-match-universal) e
  **literais**. Fora disso, não se escreve `val`.
- **Parâmetros de função e método comportam-se como `var`** (garantido) — mutáveis no corpo. `val` fica só
  para aliases e literais.
- **Mutação = escrita direta:** `s.x = v` para campo acessível; **property `set`** para o
  controlado/computado/encapsulado (como a Intent: `_value` privado, só `pub set` escreve).
- **Value semantics (struct) vs identidade (objeto):**
  - **struct (value):** atribuir/passar **copia o valor**; mutar um parâmetro/local é **local**.
  - **classe/serviço (objeto, reference):** atribuir/passar copia a **referência**; `a.b = v` muta o objeto
    **compartilhado** — propaga por **identidade**, sem `ref`. (Sítio gated por visibilidade: `b` acessível
    ou `set` acessível.)
  - **`ref`** é a ponte do value type: `ref a: T` é **alias do slot** da variável do chamador, para
    **qualquer** `T`; permite **reatribuir tudo** (`func(ref a: T) { a = T { … } }`), não só um campo —
    preserva-se só o **ponteiro** do ref. (Borrow local, §3/§10.6.)

### 13.2 `self` — ref implícito, e onde aparece

**No acesso, `self` é `ref`** (Parte 2): num método, `self` é ref implícito do receptor, então a mutação
**gruda** na instância — e **não copia** o struct a cada chamada (a performance que motiva o item 14). Um
método que escreve `self` é **mutante**; um que só lê é **não-mutante**, e o compilador **infere** isso do
corpo — **(i) sem marca `mut fn`**. Chamar um método mutante sobre receptor **imutável** (literal, alias
`val`) = **erro**.

**Serve em qualquer tipo** — `struct` / `class` / `trait` / `service` — **exceto** subtipos de
primitivo/enum/flags, que são **`val-ref` readonly** (self é ref só-leitura; não se mutam).

**As posições de `self`:**
- **como TIPO** (o próprio tipo): em declaração de variável, campo, propriedade, retorno ou parâmetro —
  ex.: `static fn new(): self`, `p: self`.
- **construção:** `self { … }` (Block-B).
- **acesso:** `self.x` / `self::y` — **aqui `self` é `ref`**.

**Materialização e o ponteiro do ref (representação em runtime):**
- **objeto (class/service):** cada instância tem **arena própria** → ponteiro/identidade intrínseco; o `ref`
  é natural.
- **struct (value):** é **fat** — carrega um **cabeçalho** (mais simples que o de objeto: só `uptr`/`ptr`, o
  ponteiro para si) que habilita o `ref`/`self`. Uma **cópia é nova materialização — novo cabeçalho, novo
  ponteiro** (é o que preserva value semantics: mutar a cópia não toca o original).
- **subtipo de primitivo/enum/flags (readonly):** é **thin** — fica do tamanho do primitivo, **sem
  cabeçalho**; o runtime **define `self` na hora da chamada** do método que o usa (self **transiente**). Por
  serem **readonly**, esse self-ref transiente basta — não há mutação a propagar. (Dava pra usar o mesmo
  transiente em struct, mas a escolha é **cabeçalho** para struct — mais direto que remontar `self` a cada
  chamada.)

### 13.3 `readonly` — imutabilidade opt-in (default é mutável)

Tudo é `var` por default; **`readonly` é o opt-in** de imutabilidade (modelo C#), em dois níveis:

- **struct inteira:** `readonly struct Foo { … }` marca a **struct toda** como somente-leitura (igual ao
  `readonly struct` do C#) — todos os campos são readonly.
- **campo:** `readonly id: u64` marca um **campo** individual como somente-leitura.

**Regra:** um campo readonly — solo ou vindo de uma struct readonly — só pode ser definido na **construção**
(`self { … }`) ou por **valor default** na declaração; **nunca mutado depois**, nem por fora nem por dentro
(um método que tentasse `self.id = …` sobre campo readonly = **erro**). É a garantia à prova de mutação
**interna** que a property só-`get` (§13.2) não dá — as duas coexistem: `get`-only encapsula, `readonly`
congela.

---

## 14. Macros — duas famílias, identificadas por keyword (roadmap, cluster §12)

Teko ganha **macros** (trazidas do "pós-1.0 / no macros" por ruling do dono — reversão deliberada do
`DECISION_LOG:320`/`TEKO_LEGISLATION:607`), em **duas famílias distintas**, cada uma **identificada por
keyword** (o dev **diz** o que quer, o compilador **sabe** o que é) e chamada com **`@`**. As duas se
distinguem pelo **estágio de pipeline** — e é o estágio que **fixa o que os args são** (sem híbrido).

### 14.1 Família A — sintática *(keyword: `macro`)*
- **Aplica na AST, ANTES do type-check** (pré-passo): `parse → AST → [expand] → TYPE-CHECK do resultado →
  lower`. Args = **AST crua** (`.node`/`.source`/`.len`); os tipos **ainda não existem**, logo **sem
  `.type`/`.value()`** dentro do corpo. Produz **código** (AST), que é **type-checado depois** — código
  mal-tipado gerado falha no type-check normal, apontando a expansão.
- **O mecanismo DEFINIDOR: a macro COPIA-SE no local de uso — ALARGA a AST.** Não é chamada (como função)
  nem despacho — é **expansão inline**: o corpo é **copiado** na posição da chamada, com os args
  substituídos, e a AST **cresce** ali. É essa cópia-in-place que distingue uma macro de uma função/trait.
- **Não explicita tipos** — os args são **anônimos até serem materializados na AST** (e só então
  type-checados). É isso que **sustenta os variadics**: sem tipo explícito, a aridade é livre.
- **Uma macro nomeada ≈ `structural trait`, mas como HELPER que encapsula** — é o herdeiro **explícito e
  visível** do que a *structural trait* tentava ser (encapsular um padrão estrutural), só que **escrito à
  mão, sem shadow**, e copiado in-place no uso.
- **O splice: `lowering { … }` + `${expr}`.** O corpo da macro é **lógica comptime que roda ANTES de
  alargar** e decide **quais blocos `lowering` emitir** (ou nenhum) — expansão condicional/montada. Dentro de
  `lowering { }` tudo é **verbatim** (copiado na íntegra na AST); **exceto `${expr}`**, que **injeta o
  nó/valor computado** pela macro. (Nome `lowering` = "baixa código na AST", no lugar de `quote`; escape
  `${}`.)
  ```teko
  macro log_if(...args) {                       // sem retorno → EMITE código
      if args.len > 0 {                          // lógica comptime — roda antes de alargar
          lowering { teko::io::println(${args[0]}) }   // verbatim, com ${} injetando o nó do arg
      }                                          // if falso → não alarga nada
  }
  @log_if(msg)   // → teko::io::println(msg)   ;   @log_if()  → nada
  ```
- **`macro` NÃO tem retorno** (ruling do dono) — ela **se substitui e alarga a AST** (emite via `lowering`),
  **sempre**. Inspeciona args (`.len`/`.node`) na sua lógica comptime, mas **não retorna** — emite. O caso
  "computa um valor" **nunca é `macro`; é `comptime`** (que retorna literal, §14.2). Ex.: contar args é
  `comptime count(...args): usize { args.len }`, não `macro`.
- **Hygiene = mangle, sem erro** (ruling do dono). Colisão de variável **nunca erra**: um binding que a macro
  introduz no verbatim de `lowering` (o `var t` do `swap`) é **manglado** para um nome único (`t$…`), então
  jamais colide com o `t` do usuário. Dois detalhes forçados:
  - **mangle estável/determinístico** (chave = macro+sítio+nome+índice de expansão), **nunca aleatório** —
    senão duas compilações do mesmo fonte divergem e **quebram o fixpoint byte-idêntico**;
  - **só o verbatim dentro de `lowering` é manglado** (é da macro); o que entra por **`${}`** é **nó do
    usuário e fica INTACTO** (referencia o escopo do usuário — manglá-lo capturaria errado). É essa
    assimetria que É a hygiene. E ela é **estanque:** o `${}` é a **ÚNICA ponte** do escopo comptime da macro
    (fora do `lowering`) pra dentro do verbatim — nada de fora aparece dentro senão por escape. Por isso o
    mangle é **exatamente** o verbatim: o que está lá é da macro; o que entrou por `${}` é do usuário; a
    lógica comptime de fora nem vira AST.
- Modelo **Rust/Lisp** (expand-então-tipa). Funciona com args de runtime (`@count(a, b)` conta nós, não
  avalia nada).

### 14.2 Família B — avaliação comptime *(keyword: `comptime`)*
- **Os tipos JÁ são conhecidos** — roda **DEPOIS do type-check**: `parse → AST → TYPE-CHECK → [comptime eval]
  → lower`. Args = **valores comptime-conhecidos**; computa um **valor** inlined. Modelo **Zig comptime**
  (tipa-então-avalia).
- **Avaliar VALOR de runtime = erro; inferir TIPO de runtime = OK** (ruling do dono — a regra fina). Como
  roda pós-typecheck, o `comptime` **avalia valores** — e um valor só é avaliável se for **comptime-const**;
  **avaliar um local de runtime = erro**. Mas o **TIPO** de qualquer arg é comptime-conhecido (mesmo de um
  runtime), então uma `comptime` **genérica** pode **inferir o tipo** de um arg de runtime **sem avaliá-lo**.
- **O retorno da `comptime` (quando houver) é inlined como LITERAL** (ruling do dono): `var x = @sizeof<i32>()`
  fica como se fosse `var x = 4`.
- **`sizeof` são DOIS construtos** (correção): o **comptime** dá o **slot** de `T` (`T` explícito, sem param
  de valor); a versão que recebe um **valor** é uma **função runtime** (bytes ocupados). O `@` distingue:
  ```teko
  // src/mem
  exp global comptime sizeof<T>(): usize { /* tamanho do SLOT de T, comp-time */ }
  @sizeof<i32>()        // → 4  (comptime, slot)

  exp fn sizeof<T>(t: T | null = null): u64 { if t == null { return 0 } /* bytes OCUPADOS por t */ }
  sizeof(x)             // runtime — tamanho ocupado pelo valor x
  ```
  (o modificador `global` — acesso sem namespace, fim do shadow de parse — fica no §15.)
- **`@sizeof`/`@typename` = macro comptime** (visível, no-shadow) sobre a reflexão de tipo (`T.size`/
  `T.name`), **não builtin oculto**. Sub-decisão que resta: **quanto de reflexão de tipo expor**
  (`.size`/`.name`/`.fields`…) — e a reflexão só computa **valores comptime** (contar campos, somar
  tamanhos), **nunca** toca valor de campo em runtime.
- **NÃO há terceira classe de macro** (ruling do dono). "Runtime macro" é contradição — o código que a macro
  expande **já roda em runtime**; a macro em si é sempre **comp-time**. O limite é claro: **macro = comp-time**
  (A: expande AST; B: computa valor comptime) e **não lê valor de runtime**; **tudo que precisa de valor de
  runtime é FUNÇÃO/MÉTODO real** (stdlib). Um `eq_by_fields` que compara campos em runtime é **função/método**
  (ou a impl de interface+operador, §9.4), **não macro** — a "tensão do `.fields`" **dissolve-se**: nunca foi
  território de macro.

### 14.3 Consequências
- **Fork "o que args é" resolvido pelo estágio:** A = **AST-only**, B = **valores typed**. Sem construto
  híbrido (supera o 1C single-construct do `plano-macro`, a revisar).
- **FFI:** `extern` + a família — `extern macro` (macro C função-like) / `extern comptime` (constante C,
  `O_RDONLY`).
- **Chamada:** `@nome(...)` nas duas. **Keywords fechadas: `macro` (Família A) / `comptime` (Família B)**
  (ruling do dono).

---

## 15. `global` — acesso sem namespace, e o fim do shadow de parse

**O que resolve (ruling do dono): parar o shadow do parse.** Hoje builtins (`sizeof`, `list`, `str`…) são
**injetados no parse** — shadow, invisível ao dev. Com `global`, viram **declarações reais e visíveis** (em
`src/mem`, `src/list`…) alcançáveis **sem qualificar o namespace**. É o **no-shadow aplicado aos builtins** — o
mesmo princípio que aposentou a structural.

- **`global`** modifica uma declaração de **nível de namespace** — **função, tipo ou constante**.
- **Variável NUNCA é global** — sem estado mutável global.
- **Acesso direto:** um símbolo `global` é alcançável **sem o namespace** — `@sizeof<T>()`, não `mem::sizeof`.
- **Colisão:** se já houver a **mesma assinatura exportada** como global → **erro de compilação** (assinaturas
  diferentes coexistem, como overload).
- **Compõe com visibilidade:** `exp global comptime sizeof<T>(): usize` — `exp` (entra no `.tkh`) + `global`
  (sem namespace) + `comptime`.
- **Tipos global** (confirmado): um `global type Intent` fica alcançável como `Intent`, não
  `teko::threads::Intent` — como `str`/`error` (core) já são, agora **explícito** via `global`.
- **Toda a superfície ambiente vira `global`** (ruling do dono). Tudo que hoje se usa sem qualificação —
  `println`, `sizeof`, ops de `list`/`str`, … — passa a ser **declaração `global` explícita** na stdlib
  (ex.: `global fn println(...)` em `src/io`), no lugar da injeção de parse. **Elimina o shadow por
  completo** — e amarra na triagem `exp` da stdlib (o que é `global` é a superfície ambiente exportada).

---

## 16. FFI libc-direct (roadmap §12) — `extern` aponta pra libc, `teko_rt` desaparece

**A meta (ruling do dono): o `extern … from "teko_rt"` de hoje DESAPARECE** e vira **apontamento FFI pra a
libc de cada OS e arquitetura**. É por isso que **macros e pragmas** (`#os`/`#arch`) importam — a variação
por-plataforma dos bindings vive atrás deles.

- **Forma (Fork A = A3):** a **lógica pura** (os twins `str_eq`…) vira **função Teko** comum (sai do seam,
  **sem `extern`**); o **piso de syscall** (`write`/`read`/`malloc`) vira **`extern fn` apontando pra libc**
  (per OS/arch), resolvido pelo **own-linker** (.33–.34) **sem `cc`**. O `teko_rt` **some por inteiro** —
  parte vira Teko, parte vira extern-libc.
  ```teko
  extern fn write(fd: i32, buf: *u8, n: usize): size = "write" from lib "c"   // size, NÃO isize
  ```
- **Tipos na fronteira (correção do dono):** **`isize` NÃO existe** — é **`size`** (palavra de máquina
  assinada) ou **`usize`** (§8). **Ponteiros opacos** só pros **outros casos** (o que não mapeia direto).
- **O compilador CONVERTE tipos na fronteira FFI** (ruling do dono) — marshalling teko↔C **automático**
  (`str`↔`char*`+len, `i32`↔`int`, `size`↔palavra…) pra **reduzir a opacidade**: o dev **não embrulha tudo à
  mão**. Sub-decisão que resta: **quais conversões o compilador conhece** (a tabela) vs o que fica opaco.
- **Aberto (acopla a Fork B/C):** quem escreve os bindings — usuário vs `teko::sys` curada — e ambos vivem
  atrás de `#os`/`#arch`.
