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
