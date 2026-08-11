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
`transient`), resolvidas por `svc<T>()` em comp-time. Os lifetimes SÃO tempos de vida de arena
(detalhe em `arena-especificacao-unica-0.3.1.md` §8).

**O que sai.** A DI por anotação da era anterior (`#singleton`/`#inject`, `src/checker/di.tks`
`choose_factory`) é substituída pela tabela estática nomeada.

**O que entra.** Keywords `service` (+ `sealed`), os lifetimes `singleton`/`scoped`/`transient`, o ctor
`static`, e `svc<T>()` como **intrínseco de comp-time** (o compilador substitui o call-site inline por
código por-lifetime; sem ABI de runtime). Chaves de string desambiguam múltiplos provedores de uma
interface — e a política de conflito é dura (ruling do dono): **se já há um registro do mesmo tipo sob a
mesma chave, é ERRO DE COMPILAÇÃO** (chaves distintas coexistem; mesmo-tipo-mesma-chave colide em
comp-time, nunca "último vence" silencioso). **Regra de escape:** valor de serviço nunca armazenado em campo, passado como argumento, ou
retornado em código de usuário — forçando o `claim` explícito via `svc<T>()`; o backend é isento mas o
que segura fica arena-bounded.

**O que resolve.** Resolução determinística em comp-time (tabela estática por análise), sem custo de
runtime, e um modelo de lifetime que casa com a arena em vez de brigar com ela. A regra de escape impede
que um serviço vaze para além da sua arena. **Sob threads** (ruling do dono 08-10), a resolução é **por
thread**: `singleton` vive na raiz da THREAD (não do programa), `scoped` na sub-raiz da thread — a faceta
de arena está no Doc 1 §8. É o "considerar DI no front e no back": a superfície é a mesma, o tempo de vida
é o da thread.

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

### 9.2b Uniões `|` — só estruturais e inline; `variant` para tipo-soma nomeado

**O que muda.** A união `|` é **estrutural e só inline** — vale em declaração de variável, parâmetro,
constraint de genérico e retorno (`fn f(): T | error | null`), mas **NÃO pode ser nomeada como um `type`**
(`type X = A | B` é rejeitado). Para um tipo-soma **nomeado**, usa-se **`variant`** (nominal, com tag), que
já é o mecanismo de todos os ADTs do compilador (`type Pattern = variant A | B | C`).

**O que resolve.** Separa claramente: `|` é composição estrutural no ponto de uso; dar **nome** a um sum
type é `variant`; e `type` fica reservado ao concreto (`struct`, newtype-sobre-primitivo, ou alias de um
tipo só). Custo zero — o corpus não tem nenhum `type X = A | B` estrutural (todos os 34 tipos-soma
nomeados já são `variant`).

### 9.2 Tipos de closure — `func<…>` / `action<…>`

**O que muda.** O **tipo** de uma closure passa a ser um subtipo de comp-time, ao modo do C# — em vez da
forma antiga baseada em `fn`. A closure **literal** continua `(params) => expr` (ou `(params) => { … }`) —
**sem `fn`, sem `-> R`**, retorno inferido (já era assim, `parse_expr.tks:308`).

**O que sai.** A anotação de tipo `fn(T): R` / `() -> T?` — que **colide** com uniões de retorno:
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

---

## 10. Concorrência — a superfície (`isolate`/`spawn`/`chan`, `async`/`await`, journaling)

A faceta de **arena** dela está no Doc 1 §7; aqui é a **superfície** que o usuário vê. Recomposto de
`concorrencia-isolate-spawn-chan` (08-03), `journaling-de-corrida` (07-30) e `paralelizacao-eixo1/eixo2`
(08-02).

### 10.1 Estratégia de token — tudo contextual

`spawn` (dispara), `await` (suspende) e `isolate` (modificador opcional de função — restringe a spawn-only)
são **keywords contextuais** (reconhecidas pelo parser por posição, sem reserva no lexer — a mesma norma
que `class`/`abstract`/`virtual`/`override` seguem); `chan` é um **tipo genérico** (`chan<T>`). **Não há
keyword `async`** — `await` sozinho suspende qualquer função (§10.3). Medido: **zero identificadores Teko
hoje** se chamam `spawn`/`chan`/`await`/`isolate`, então reconhecê-los por posição não quebra corpus.

### 10.2 `spawn` (corotina) + `chan<T>` — paralelismo real, memória isolada

`spawn` é uma **keyword de corotina** (estilo Go), **não uma função** — `spawn f(args)` lança `f` numa
corotina, argumentos **por cópia**, **sem retorno** e **sem `join`**. **Dois níveis de isolamento:** uma
**função comum sem retorno** roda como **thread** (compartilha a região de programa); uma **`isolate fn`**
roda em **isolamento total, como outro processo** (raiz própria, sem memória compartilhada, mais restrita
que thread; comunica só por `chan`) e é **spawn-only** (chamá-la direto = erro). Doc 1 §7.6.

```teko
spawn f(c.id)                                      // KEYWORD (não função): dispara e segue, args por cópia

// chan<T> MPSC (fan-in: N escritores, 1 leitor). Transporte: SOCK_DGRAM (Linux/macOS), mailslot (Windows)
static fn chan<T>::make(bounds: usize = 1): self  // 1 (default)=bounded-1 · N=bounded-N · 0=UNBOUNDED
              c.id: usize                          // o id — o que se passa à corotina (spawn f(c.id))
static fn chan<T>::writer(id: usize): Tx<T>       // extremo de escrita (copiável, N escritores)
static fn chan<T>::reader(id: usize): Rx<T>       // extremo de leitura (um só; 2º leitor = erro nomeado)
static fn chan<T>::close(id: usize)                // fecho do produtor
fn        Tx<T>::send(v: T): null | error          // envia; espera se bounded-cheio; error se fechado
fn        Rx<T>::pop(): T | closed                 // recebe; `closed` quando o último produtor fecha e esvazia

// WaitGroup — esperar N corotinas terminarem (NÃO há join no modelo de corotina)
fn wg_open(): usize
fn wg_add(wg: usize, n: usize): null | error
fn wg_done(wg: usize): null | error
fn wg_wait(wg: usize): null | error

fn hardware_parallelism(): usize                   // paralelismo concedido pelo SO
```

Um fluxo mínimo — a `main` cria o canal, uma corotina escreve, a `main` lê até o `closed`:

```teko
isolate fn produz(cid: usize) {          // corotina (isolate fn): sem retorno, só via spawn
    var tx = chan<i32>::writer(cid)
    var i = 0
    loop while i < 100 { tx.send(i); i = i + 1 }
    chan<i32>::close(cid)                 // fecha → o pop do leitor passará a devolver `closed`
}

fn main() {
    var c  = chan<i32>::make(64)         // bounded-64 (64 mensagens em voo, no máx.)
    spawn produz(c.id)                    // dispara e segue (sem join); passa o id
    var rx = chan<i32>::reader(c.id)
    loop {
        var v = rx.pop()                  // v: i32 | closed
        match v {
            i32    => usar(v),
            closed => break               // o fecho do canal É a sincronização
        }
    }
}
```

**Sem `join`:** dados só cruzam a fronteira por **cópia** (via `chan`); espera-se pelo **fecho do canal**
(o `pop` devolver `closed`) ou pelo `WaitGroup`. A camada de linguagem **não reimplementa**
limite/contrapressão/fecho — pede uma vez na abertura e confia no transporte do SO. *(A primitiva
`fork_join` de baixo nível sobrevive como mecanismo INTERNO do backend, para paralelizar o codegen — não
é superfície de usuário; o usuário escreve `spawn`.)*

### 10.3 `await` — alarga o retorno para `Intent<T>` (sem `async`)

**Não há `async`, e a função NÃO declara `Intent`.** A função retorna o seu tipo normal; o **`await`
ALARGA** o retorno para `Intent<T>` no ponto de chamada. Ao contrário de outras linguagens (que
**estreitam** a assinatura para `Task<T>`/`Promise<T>`), aqui a assinatura fica limpa e o alargamento é só
onde se espera.

```teko
fn calc(x: i32, y: i32): i32 { x + y }

var a = await calc(1, 2)       // a : Intent<i32>
if a.canceled { println("canceled") }
else          { println($"r = {a.value}") }
```

`await f(args)` **suspende** (cede, sem bloquear a thread), executa `f`, e o retorno cai em **`.value`** de
um `Intent<T>` criado no caller. Campos: **`.value: T`** + **`.canceled: bool`**. `Intent` (sem `T`) = o
desfecho de esperar uma função **sem retorno** (só `.canceled`). É onde se **garante a execução** — o
`spawn` é fire-and-forget, sem retorno.

**Várias tasks — sem `Intent` na assinatura:** `await teko::tasks::when_all(f(a), g(b), …)` e
`await teko::tasks::when_any(…)` devolvem uma **lista de `Intent`s**. O dev nunca escreve `Intent` num
retorno; só o `await` (direto ou via `when_all`/`when_any`) o produz.

- **Arena/fundações** (Doc 1 §7.9): **I/O** — reator (`epoll`/`kqueue`/`IOCP`), sem thread nova; **CPU** —
  corotina isolada de um pool (F1). Em ambos o `await` cede e retoma, nunca bloqueia.

**Não** existe thread compartilhando arena sem F1. E **`ref` não cruza** a fronteira: nem `spawn`/`chan`
aceitam `ref`, nem `<ref T>` genérico (§10.6) — preserva UAF; cruza cópia ou id.

### 10.4 `teko::journal` — o módulo de journaling

O journal é um **registro append-only, carimbado por corrida, segmentado por escritor**; a sumarização
**relê** (`fold`), não funde. Para quem escreve testes hoje, **nada muda** de grafia (`teko::test::scoped`
segue igual). Segue a **mesma lógica dos canais** (ruling do dono): tipo OOP, fábrica estática `make`,
operado pelo **id**, e **reside na arena raiz** (F2 — sobrevive a todas as tasks, Doc 1 §7.10):

```teko
pub type Record = struct { run: str, writer: str, kind: str, payload: str }

static fn journal::make(writer: str, roll: Roll = Roll::none, fmt: func<Record, str> | null = null): self  // abre
              j.id: usize                          // o id do segmento (a currency, como c.id)
static fn journal::append(id: usize, kind: str, payload: str): null | error  // write(2) O_APPEND, sem buffer
static fn journal::close(id: usize)              // fecha o segmento
// nível de corrida (sem id de segmento):
static fn journal::run_id(): str                 // <ns monotônico>-<pid> — nomear a corrida mata o lixo calado
static fn journal::run_root(): str               // bin/.tkrun/<run_id()>
static fn journal::fold(root: str, run: str): []Record   // relê; descarta lixo/linha rasgada; sintetiza `end`
static fn journal::scratch(base: str): str       // o compositor único de caminhos isolados
static fn journal::sweep(keep: str): usize       // limpeza é da corrida SEGUINTE, nunca da própria
```

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

// ex.: padrão (fmt omitido → formatação padrão), rolando a cada 100 MB:
var j1 = journal::make("app", Roll::size(100 * 1024 * 1024))

// ex.: formatação própria via closure — literal `(params) => expr`, sem `fn`:
var j2 = journal::make("app", Roll::daily, (r) => r.kind + "|" + r.payload)
```

O rolling é do runtime de durabilidade (o `append` verifica a política e rotaciona por `rename` atômico
antes de escrever quando ela dispara); o `fold` relê todos os ficheiros de um segmento em ordem,
transparente ao rolling. A closure `fmt` vive com o journal (arena raiz) e é chamada por `append`.

### 10.5 Decisões pra ti (recompostas de `concorrencia-isolate-spawn-chan` §9)

| # | pergunta | recomendação |
|---|---|---|
| D1 | `spawn` — função ou keyword? | **keyword de corotina** (estilo Go, contextual, sem `join`); `chan` é tipo genérico |
| D2 | `spawn <call-expr>` — agora ou depois? | **agora** — é a forma canônica da keyword: `spawn f(args)` (Go-style, args por cópia, sem retorno) |
| D3 | `chan_unbounded` — entra ou sai? | **ENTRA — responsabilidade do dev, não da linguagem** (ruling do dono 08-10) |
| D4 | `WaitGroup` — a forma acima ou só `Isolate[]`+`join`? | a forma acima (menos apoiada em medição) |
| D5 | `async`/`await` — separar as duas fundações ou `Intent<T>` único? | **separar** (ruling do dono 08-10): `Intent<T>` carrega cópia, `Intent` opaco |
| D6 | namespace — `teko::isolate`+`teko::threads` ou um só? | **um só, `teko::threads`** |

### 10.6 `ref` e genéricos sob concorrência — a regra de UAF (ruling do dono 08-10)

`ref` (borrow) **não pode cruzar fronteira de MT/async**, e um genérico **não pode ser parametrizado por
referência** — `<ref T>` é rejeitado pelo checker. Um borrow que atravessasse uma task/continuação
penduraria quando a arena do outro lado dropasse (UAF). Consequências de superfície:

- `spawn`/`chan_writer`/`chan_reader`/`async fn` **rejeitam** parâmetro/valor `ref`.
- `Foo<ref T>` não parseia/typa. `ref` sobrevive só como borrow **local** caller→callee que **não** cruza
  fronteira de concorrência (§3: `ref` já é só parâmetro).
- O que cruza a fronteira é **cópia** (valor) ou **id** (`u64`), nunca borrow.

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

**Nada em aberto na superfície.**
