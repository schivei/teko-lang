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

## 6. Aberto (a fechar com o dono, NÃO decidir sozinho)

- **Escopo/ordem de implementação:** um trabalho único (a tabela geral com as guardadas como caso
  gated), ou começar estreito pelas constantes de plataforma (destravar arm64/monólito já) e expandir
  a tabela depois? — pergunta em aberto; aguardando o dono.
- **Forma exata da stdlib de verificação de cast** (nome/assinatura, erro-as-valor) — a desenhar.
- **Tipo "word de 64 bits"** para argumentos de syscall (evitar `u64→i64` checado nos endereços) — a
  desenhar junto com §16.
