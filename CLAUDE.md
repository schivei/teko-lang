# Instruções permanentes — teko-lang

## Idioma (REGRA DURA, persistente)
O dono (schivei) **NÃO fala inglês**. **TODA** comunicação no chat é em **PT-BR**,
sempre, sem exceção. Esta regra vale para todas as sessões e **persiste através de
compactação de contexto** — se este arquivo existe, a regra está em vigor.
(Código, mensagens de commit e nomes técnicos seguem a convenção do repo; a
conversa com o dono é PT-BR.)

## Ritmo de trabalho
- Continuar o processo de forma autônoma, mas **fazer pausas para ler** o que o
  dono pode ter escrito no meio do caminho, antes de decisões que importam.
- **Sem workarounds** (lei do dono): achar e resolver a **causa raiz**, nunca dar
  voltas para contornar o problema.
- **SEMPRE despachar um agente** (lei do dono 2026-08-17) para o trabalho de
  implementação — não fazer eu mesmo; preserva a sessão principal (menos tokens).
  O coordenador valida (build seco + fixpoint + reseed) e drena ff.
- Quando o dono está **levantando/analisando um problema comigo**, PARAR, LER o
  que ele manda e pensar JUNTO — não sair executando por cima nem ignorar as
  mensagens dele.

## Estilo de código (LEI DURA, dono 2026-08-18) — CLAREZA, MENOS TEXTO
Metas medidas: **doc-comment ≤ 10% do código; comentário `//` = 0%** (hoje: doc 46,6%, // 2,1%).
É para **REMOVER e REDUZIR — não mover** para outro lugar. Documentação de verdade mora em
`docs/design/`, escrita à parte; não é o dump dos doc comments do código.
- **Comentários inline `//` em `.tks`/`.tkt`: PROIBIDOS (0%).** Remover todos.
- **Doc comment só onde há `exp`** (superfície exportada). Em `pub` (interno) e privado: REMOVER.
  Doc de topo/introdução de arquivo: REMOVER.
- **Um doc comment não pode ser maior que o que documenta.** Curto; só o que a assinatura não diz.
  PROIBIDO: referências a docs (§/#/plano/crumb), história, "por que", explicação de arquitetura.
- **Mensagens de erro/log = estilo compilador padrão:** `arquivo:linha:coluna: "causa curta"`
  (ex: `unexpected type`, `expected ':' after ')'`, `unsupported (os,arch)`). Sem novelas nem refs.
- Retroativo, tree-wide. Mudança que toca o `teko.c` (mensagens, ou deslocamento de linha) exige reseed.
- **Testes: não se escreve teste para o que o compilador exercita ao se compilar** — o fixpoint (self-build)
  já é essa prova. A linguagem é MONÓLITO: a stdlib É o compilador — o self-build a compila E `gen1` a
  EXECUTA em larga escala (o compilador chama `list`/`str`/`map`/`io`/`fs`/`arena`/… ao compilar). A prova
  de "o compilador usa" está no próprio `src/` (se o código chama a função, ela é exercitada).
  Compilar o projeto inteiro (incl. os próprios `.tkt`) já exercita a maior parte da implementação.
  REMOVER: (a) **testes que validam a COMPILAÇÃO** (strings sintéticas p/ parser/codegen/ast) — são
  TAUTOLÓGICOS: o compilador nem chegaria a rodá-los se o parser/codegen estivesse quebrado; retóricos,
  incapazes de dizer nada; (b) comportamento de toda função que o `src/` chama (gen1 a executa). MANTER só
  o que o self-build genuinamente NÃO exercita: genéricos/monomorph (self-build = 0 instâncias), backend
  native, comportamento de função que o compilador NUNCA chama ao rodar, casos de erro/diagnóstico.
  Na dúvida, LISTAR para revisão — não remover.

## Convenções da linguagem/codebase
- **Não existe `let`/`mut` na superfície — só `var`** (e `const`).
- **Tipagem forte explícita na codebase do Teko** (via flag `--explicit`,
  default off, o checker só barra COM a flag — inferência segue recurso válido
  para quem USA Teko). Sob `--explicit`: `var`, `const`, parâmetros, campos e
  retorno (exceto void) exigem tipo na declaração — assinatura completa/
  explícita/forte/estática; e o gate de cast desnecessário vira ERRO (some do
  default). História viva em `mudancas-superficie-0.3.1.md` §11.2 (bloco EMISSÃO
  LIMPA), ANTES do §16.

## Leis de desenvolvimento (resumo — detalhe em docs/design/mudancas-superficie-0.3.1.md §11.2)
- **TESTES SÓ NO CI.** `teko test .` local dá OOM (ninguém roda). Validação local =
  **compilação** (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6291456`) +
  fixpoint (tc2==tc3) + cross-check offline.
- **Forward-only, sem PR:** drenar para `fix/retirement` por ff/cherry-pick.
- **Reseed** de `bootstrap/teko.c` quando uma mudança de **compilador** altera o C
  emitido (exige fixpoint gen2==gen3 + PROVENANCE + provenance_gate PASS).
  Módulos-folha não exigem reseed.
- **Teko é um monólito e precisa cross-compilar.** A perna C emite **UM** `teko.c`
  que compila em toda arquitetura/SO via `#if` do C — tem que **emitir tudo**
  (todos os alvos), não podar para o host que emite. Só o backend **native** emite
  um executável por (arch, SO), e ainda assim cross-compila.
- Teko-only (.tks), W15 (doc-comments-only, flatten/extract, helpers com nome único
  tree-wide, sem index-assign), sem VM/GC, arena.
- **§16 — C hand-written CONGELADO (lei do dono, 2026-08-17):** `src/runtime/teko_rt.c`,
  `teko_rt.h`, `src/win32_compat.h`, `src/assert/assert.c`, `assert.h` NÃO podem mais ser
  editados/estendidos/patcheados. Toda função de runtime vira **superfície Teko** (`src/*.tks`)
  emitida no `teko.c`, feita **à mão em Teko** (raw syscall no Linux) ou via **FFI da ABI do SO**
  (macOS libSystem / Windows kernel32-ntdll). O `teko.c` fica **auto-contido** — TUDO dentro dele,
  zero C hand-written linkado. A única mudança que esses arquivos recebem é **deleção** conforme
  cada pedaço migra. Se um bug/gap aparece num deles, **migra pra Teko**, não corrige em C.
  Mapa completo: `docs/design/plano-s16-expurgo-libc-completo.md`.
- **§16 — SEM ATALHOS (lei do dono, 2026-08-17):** nenhum workaround/degrade no expurgo do C. Toda
  função de libc vira implementação **real** em Teko (raw syscall / FFI-da-ABI-do-SO). **Se existe em C,
  existe em Teko.** Rulings ratificadas R1–R5 em `docs/design/plano-s16-expurgo-libc-completo.md` §5.
