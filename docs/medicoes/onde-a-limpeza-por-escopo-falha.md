# Onde a limpeza automática por escopo falha — em código existente

Pedido do dono, 2026-07-31: *"E eu sigo esperando um exemplo real de onde o que eu propus falha."*

Justo. As medições anteriores mostravam o `adopt` a não libertar — isso é evidência de que **a
implementação de hoje não faz nada**, não de que **a proposta esteja errada**. São perguntas
diferentes e eu tinha respondido à errada.

A regra proposta: *"sempre que um bloco finaliza, executa-se os defers ali pendentes e depois limpa a
memória para os itens órfãos naquela região."*

## Primeiro: a ordem que ele descreveu JÁ está implementada, literalmente

`src/codegen/codegen.tks`, `emit_loop_while` — o comentário do próprio código:

> *"…fire the defers first (**so a defer body can still read this iteration's block-locals**), THEN
> drop the loop body's region."*

E a região do corpo do laço é *"a fresh region every iteration … and **dropped at every exit edge**"*.
**Defers primeiro, limpeza depois, por aresta de saída.** A regra dele não é uma proposta nova: é o
que o emissor já faz — para as ligações que passam o predicado.

## O contra-exemplo, e é o emissor inteiro

`src/codegen/codegen.tks:8120`, `emit_block` — a função por onde passa **todo** o C que o compilador
emite:

```teko
fn emit_block(buf: []byte, body: []checker::TStatement, …) -> []byte | error {
    mut out = buf
    …
    loop {
        if i >= body.len { break }
        …
        out = match emit_stmt(out, s, …) { []byte as o => o; error as err => return err }
        …
        i++
    }
```

**`out` é re-ligado DENTRO do corpo do laço, a cada iteração, a um `[]byte` NOVO** que o `emit_stmt`
devolve. Esse buffer foi alojado durante *esta* iteração — lá no fundo, em `cb` → `append_fo` →
crescimento — e **tem de sobreviver à iteração seguinte e à saída do laço**.

**Se a região do corpo do laço admitisse `[]byte`, ela libertaria o acumulador debaixo dos próprios
pés, a cada volta.** E não é um caso de canto: `codegen.tks` tem **1371 chamadas a `cb(`**, e o
emissor inteiro tem esta forma. O mesmo padrão está no `checker`, no `lower` e no `assemble`.

## E aqui está a parte subtil, que é o que torna o exemplo útil

**A ligação `mut out = buf` está FORA do laço.** O nome é exterior; **o armazenamento é interior**.

Logo uma regra puramente sintáctica — *"foi ligado dentro do bloco?"* — **responde bem por acidente**
neste caso, e responde **mal** no caso simétrico:

```teko
loop {
    mut pedaco = teko::list::empty()   // o NOME nasce dentro
    …
    fora = teko::list::push(fora, pedaco)   // e o ARMAZENAMENTO sai
}
```

Aqui o nome é interior e o armazenamento escapa. **A pergunta certa nunca foi onde o NOME foi
ligado — é onde o ARMAZENAMENTO foi alojado e para onde ele flui.**

E é exatamente por isso que:

* **`escape.tks` não chega.** Ele calcula um **conjunto de NOMES** (`fn_escaping_vars` devolve
  `[]str`). Nomes não respondem a esta pergunta.
* **a espinha de escape transitivo é o pré-requisito nomeado**, e não uma preferência de desenho: é
  uma análise de *points-to*, sobre armazenamento, não sobre nomes.
* **o predicado `cg_same_named_struct` não é conservadorismo arbitrário.** Uma struct nomeada ligada
  por valor é a forma que **não flui**; `str` e `[]T` fluem **por construção** — são precisamente os
  acumuladores.

## O veredicto honesto sobre a proposta

**A proposta está certa como semântica e é insuficiente como regra de decisão.** O que falta não é a
limpeza (existe), nem a ordem (existe, e é a dele), nem os construtos (braços de valor, corpos de
laço e regiões de bloco já abrem região). **Falta o critério que diz quais alocações morrem ali** — e
esse critério não pode ser sintáctico, porque o exemplo acima o derrota nas duas direcções.

## O que isto NÃO diz

* Não diz que a proposta é impraticável — diz que ela **depende da espinha**, que já está nomeada
  como dívida em `typer.tks:5404`.
* Não medi a rota nativa; tudo isto é leitura da rota C e do emissor.
* Não medi quantos dos 1371 sítios de `cb(` estão dentro de laços com região aberta — o que daria o
  tamanho do estrago se o predicado fosse alargado sem a espinha.
