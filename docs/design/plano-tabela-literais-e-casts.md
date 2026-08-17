# Modelo: tabela de literais estáticos + regra de fold + disciplina de casts

> Status: **modelo em ratificação pelo dono** (não implementado). Este documento fecha o
> entendimento construído na conversa de 2026-08 antes de qualquer implementação. Nada de código
> de produção sai daqui até o dono ratificar. Causa-raiz única para quatro problemas: magic values
> no artefato, binário inchado, cast à toa, e o seed C não-cross-compilável (§16 monólito).

## 1. Problema

O gerador de código emite literais e constantes de forma ingênua:

- **Fold para literal inline.** Uma constante `SYS_MUNMAP: i64 = 11` é dobrada para o número no
  ponto de uso → o C sai `tk_syscall2(((int64_t)11ULL), …)`. Consequências:
  - **Magic value no artefato** — o `11` solto viola W15 no C emitido.
  - **Não cross-compila** — o número dobrado é o do **host que emitiu**. Um seed emitido em x86_64
    carrega `11` (munmap x86_64) sem o `215` (arm64), então `bootstrap/teko.c` — que é UM monólito
    compilado por `cc` em toda arquitetura/SO — quebra em arm64. Esta é a falha real de §16.
- **Sem dedup.** O mesmo literal é re-emitido a cada uso (ex.: a string `"syscall2"` aparece 3×
  no seed) → RODATA inchado, binário maior.
- **Cast à toa.** `((int64_t)11ULL)` (o valor já é `i64`, mas leva `(int64_t)` por cima) e
  `address to i64` que vira uma **conversão checada em runtime** (`tk_to_i64_u`, que ABORTA via
  `tk_panic_cast` se o bit 63 está setado) onde o intuito era só passar uma word de 64 bits.

## 2. A tabela de literais estáticos

Toda **literal value-type imutável** (que opera por cópia: primitiva, string, e array de
primitivas/strings) vira **uma entrada nomeada única** numa tabela em RODATA. **Todo uso emite uma
referência** à entrada — nunca o valor dobrado.

- **Dedup:** valores iguais compartilham a mesma entrada (resolve o `"syscall2"` 3×) → binário menor.
- **Emissão C:** a forma padrão é **`#define`** — funciona em qualquer posição, inclusive tamanho de
  array fixo `[N]`, `case`, inicializador estático (um `static const` do C não serve nessas posições).
  `static const`/símbolo com endereço só quando o endereço for realmente necessário.
- **Vale para os dois backends.** C e native: *se o código usa uma constante, a emissão é na
  constante (o símbolo), não no valor dela.*
- **Constantes de plataforma** (`SYS_*`, `PROT_*`, `MAP_*`, e futuros errno/layout) são entradas
  desta tabela que, quando variam por alvo (`#os`/`#arch`), saem **gated por `#if`**:

  ```c
  #if defined(__x86_64__)
    #define TK_SYS_MUNMAP 11
  #elif defined(__aarch64__) || defined(__arm64__)
    #define TK_SYS_MUNMAP 215
  #endif
  ```

  Assim o monólito carrega **todos** os alvos e o próprio `cc` escolhe na compilação final. **O §16
  cross-compile é subcaso desta tabela** — não é um fix separado.

## 3. Regra de fold

Fold = pré-computar uma expressão constante em tempo de compilação.

- **Fold permitido** quando **nenhum** operando da expressão depende de **algo guardado**. O valor é
  conhecido na emissão: dobra e guarda **o resultado** na tabela (deduplicado).
  `var a = 1 + CONSTANTE` → `11` na tabela.
- **Fold PROIBIDO** quando a expressão toca em **ALGO guardado** — e "algo" é **qualquer** entidade
  sob `#if`/`#os`/`#arch` (constante, tipo, símbolo — **não só constante**), contaminando
  **qualquer fold** (não só de constantes). O valor de algo guardado só é decidido na compilação
  final (pelo `#if` do C), então não dá para pré-computar.
  - **Contaminação total, tudo-ou-nada.** Basta um operando guardado → **zero fold na expressão
    inteira**. Nada de fold parcial.
    `var c = 3 + QUALQUER_COISA + CONSTANTE` (com `QUALQUER_COISA` guardada) → **não** pré-soma nem
    `3 + CONSTANTE`. Cada operando value-type entra na tabela (`3` novo, `10` já existe),
    `QUALQUER_COISA` é a entrada gated, e a conta inteira acontece no C/runtime referenciando os
    símbolos.
  - `var b = 2 + QUALQUER_COISA` → só o `2` vai para a tabela; a soma fica pro C/runtime.

Princípio: **valor conhecido na emissão → pode dobrar (e dedupar o resultado); valor que depende de
guarda → emite a expressão com referências, sem fold.**

## 4. Conversões e casts

O **mecanismo** de cast está OK — `x to T` é bem-vindo, e um `teko::casting<T, U>(t): U` unchecked
que dá pânico também. O problema é o **volume**: o dono afirma com certeza que **95%+ das conversões
na codebase são desnecessárias**. O trabalho é **eliminar** as desnecessárias, não redesenhar o cast.
Nenhuma parte relaxa a segurança.

### 4.1 Três baldes

1. **Segura e provável estaticamente → permitida, CUSTO ZERO** (sem checagem em runtime;
   reinterpretação ou zero-extend). O conjunto das conversões seguras que devem ser possíveis:
   - widening de inteiros (`u32→u64`, `i32→i64`, `u8→u64`…)
   - `bool → número` (inteiros e floats)
   - `f32 → f64`
   - `char ↔ u32`
   - `char → [4]byte` (e talvez `[4]byte → char`)
   - `inteiros → bigint`
   - `floats → decimal`
   - `str → []char`
   - entre **tipos estritos de mesmo tamanho e classe** — subtipo↔base, enums, flags
     (`type word: i64`; `w to i64`; `u to word`): o `to` é **obrigatório pela estritude** (tipos
     distintos), mas seguro, sem perda; pânico só se fosse impossível — e entre `word`/`i64` nunca é.
   - Linha implícito-vs-explícito (proposta, a confirmar): **widening numérico é implícito** (o
     compilador engole sem exigir `to`); **travessias de tipo nomeado** (subtipo↔base, enum, flag,
     `char↔u32`, `int→bigint`, `float→decimal`, `str→[]char`) **exigem `to`**, mas são seguras e sem
     custo.
2. **Pode perder → CHECADA:** narrowing (`i64 to i32`), `u64↔i64` quando o sinal importa. Pânico, ou
   erro-como-valor via **`teko::casting`** (já existe: `u64_to_u32`, `i64_to_i32`, `u64_to_i32`, … →
   `T | error`). Estender onde faltar.
3. **Desnecessária (as 95%) → ELIMINAR.** Três origens, a (b) é a maioria:
   - **(b) tipo mal-casado na API** (MAIORIA) — a API escolheu o tipo errado (ex.: `i64` onde o
     domínio é não-negativo → `u64`/word), espalhando `to` em todo chamador. Arrumar o tipo na fonte;
     os `to` somem. Criar novos tipos onde ajuda (ex.: word de 64 bits nos args de syscall — sem
     `u64→i64` que aborta em endereço alto).
   - **(a) widening seguro** (minoria) — o compilador passa a aceitá-lo implícito (balde 1).
   - **literais com `to` de inferência** — `var x = 42 to i64` usa o `to` como muleta pra dar tipo ao
     literal. O certo é **tipar a variável**: `var x: i64 = 42` — o literal nasce no tipo, sem `to`.

### 4.2 Convenção de tipagem (W15) — a raiz de grande parte do balde 3

- **A codebase do Teko é tipada explícita e fortemente.** Zero inferência de variável na casa do
  Teko; o literal nasce no tipo da declaração. Isso mata os `literal to T` de inferência, elimina a
  maioria das conversões desnecessárias, e **baixa o custo de compilação** (o compilador para de
  inferir).
- **O checker NÃO barra.** A inferência continua um **recurso válido da linguagem** para o
  desenvolvedor que USA Teko — a convenção é **interna** à codebase do compilador/stdlib, nunca uma
  regra imposta que barre o usuário.
- **Aplicação:** é um ferimento W15 disseminado que **até agentes que aplicam W15 passam direto**.
  Logo: varredura da codebase para tipar tudo + os **briefs dos agentes exigem tipagem explícita**
  como item de checklist (senão passam direto, como sempre passaram). A aderência é por disciplina e
  revisão, não por barreira de compilador.

### 4.3 Exemplos resolvidos

- `((int64_t)11ULL)` some: o `11` **já é `i64`** (nasce no tipo, referenciado da tabela §2) — sem cast.
- `address to i64` some: o argumento de syscall passa a ser uma **word de 64 bits** que casa com
  `u64` — sem conversão checada que aborta em endereço alto (balde 3-b).

## 5. Conexões

- **Arrays fixos (Doc-1):** um array literal imutável é exatamente uma entrada value-type nesta
  tabela. A tabela é o alicerce dos arrays fixos.
- **Reaproveitamento estático de literais (pré-Doc-1):** é este documento; o dedup em RODATA é a
  redução de binário já prevista.
- **§16 monólito:** resolvido como subcaso (§2), não como trilho separado.

## 6. Decisões ratificadas pelo dono (2026-08)

- **Escopo: UM TRABALHO SÓ.** A tabela geral, com as constantes guardadas como caso gated dentro
  dela — não dois trilhos (não "plataforma primeiro, resto depois"). A abordagem é integral e
  coerente; a *implementação* ainda é em etapas verificáveis (cada etapa: gen1 compila + `gen2==gen3`),
  mas o desenho é um só.
- **Stdlib de cast: já existe `teko::casting`.** Oferece conversões checadas que retornam
  `T | error` (errors-as-values) em vez de abortar — `u64_to_u32`, `i64_to_u32`, `u64_to_u8`,
  `u32_to_u8`, `u64_to_u16`, `u32_to_u16`, `i64_to_i32`, `u32_to_i32`, `u64_to_i32`. Também há
  `teko::math::checked` (aritmética checada). O trabalho é **reusar/estender** `teko::casting` (falta
  ex.: a família de/para i64↔u64 de mesmo tamanho) e **substituir o `X to T` embutido** (que aborta
  via `tk_panic_cast`) pelo caminho errors-as-values onde a falha é possível.
- **Criar novos tipos para EVITAR conversões.** Onde a conversão existe só porque o tipo não bate,
  cria-se o tipo certo e a conversão some. Caso concreto: um tipo "word de 64 bits" para os argumentos
  de syscall, para que `address: u64` case sem `u64→i64` checado (que aborta em endereço com bit 63
  setado). *Evitar* a conversão é preferível a *checá-la*.
