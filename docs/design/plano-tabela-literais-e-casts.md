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

## 4. Disciplina de casts

Nenhuma parte relaxa a segurança da linguagem.

1. **Todo cast permanece checado.** Não existe cast silencioso. Uma conversão que pode mudar o valor
   ou perder (ex.: `i64 to i32`, `u64 to i64`) é sempre verificada.
2. **Stdlib de verificação prévia.** Se o programador precisa castar, há uma forma de perguntar "esse
   cast é possível?" e tratar como **valor** (errors-as-values), em vez de bater no `tk_panic_cast`
   de surpresa. O abort deixa de ser o caminho normal.
3. **A raiz do "cast à toa" é tipo errado na origem/destino.** A maioria dos casts **não deveria
   existir** — some quando o valor **já nasce/está no tipo certo**. É uma **varredura nos tipos**, não
   um truque de codegen que "adivinha" no-ops.
   - `((int64_t)11ULL)` some porque o `11` **já é `i64`** (nasce no tipo certo, referenciado da tabela).
   - `address to i64` some quando o tipo do argumento de syscall é uma **word de 64 bits** que casa com
     `u64` — sem conversão checada que aborta em endereço alto.

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
