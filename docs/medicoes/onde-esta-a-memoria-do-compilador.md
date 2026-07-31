# Onde está a memória do compilador — medido, não suposto

Diagnóstico do dono, 2026-07-31:

> *"o problema com o compilador atualmente são dois (na ordem de uma mesma família já corrigida
> hoje): 1. Arquivos muito grandes… 2. Por serem muito grandes, ao ler um arquivo ele entra inteiro
> na memória (OOM). 3. Deveríamos ter leitura por buffer e não por arquivo completo, descartando
> trechos já lidos e interpretados."*

**A família está certa. O membro não é a leitura de ficheiro — e a aritmética diz isso por 300×.**

## 1. O mecanismo que ele descreve EXISTE

`src/build/assemble.tks:228` lê o ficheiro inteiro para um `str` e entrega-o ao lexer:

```teko
let src = match teko::io::read_file(sf.path) { str as s => s; error as e => return e }
match asm_lex_and_parse(sf.path, src) { … }
```

Confirmado. A questão não é se acontece — é **quanto pesa**.

## 2. Os números, e eles fecham a questão

| grandeza | valor |
|---|---|
| corpus fonte inteiro (142 `.tks`) | **5,5 MB** |
| maior ficheiro (`src/lir/lower.tks`) | **776,9 KB** |
| segundo (`src/codegen/codegen.tks`) | 674,0 KB |
| pico do build (rota C, `--release`) | **1712,2 MB** |
| **o texto fonte, como fração do pico** | **0,29 %** |

Mesmo que **todos** os 142 ficheiros ficassem residentes ao mesmo tempo e nunca fossem libertados,
seriam 5,5 MB de 1712 MB. **Ler por buffer e descartar o já lido pouparia, no melhor caso, 0,3 %.**
Não chega perto da OOM.

## 3. O que a observabilidade de arena diz — e ela já existia

`TEKO_ARENA_OBS=<path>` está no runtime desde antes desta lane e **nunca tinha sido corrida sobre um
build do compilador**. Corrida agora:

```
root (process-lifetime, never freed):     1926.3 MB
scoped (freed at region drop):               0.0 MB
reclaimed by region drops:                   0.0 MB   (11 of 5007 regions dropped)
reclaimed by test-gate rewinds:              0.0 MB
reclaim ratio: 0.0%  (reclaimed / allocated)

=== ROOT-lifetime bytes by call site: 1926.27 MB total ===
   0     1700.5 MB      3484564 allocs
   1       23.3 MB       160686 allocs
   2       20.7 MB        61927 allocs
   …
=== SCOPED-lifetime bytes by call site: 0.00 MB total ===
=== MALLOC str total: 66.4 MB across 2165811 buffers ===
=== CHUNKS: 4996 regions, 21134 chunks, malloc'd cap 1485.8 MB, used 1459.3 MB, tail-waste 26.6 MB ===
```

**Três factos, e cada um sozinho fecha a hipótese da leitura de ficheiro:**

1. **A taxa de recuperação é 0,0 %.** De 1926 MB alocados, praticamente nada volta. **11 de 5007
   regiões** foram largadas.
2. **Um único sítio de chamada tem 1700,5 MB — 88 % do total** — em **3 484 564 alocações** de ~511
   bytes em média. O texto fonte inteiro é 0,3 % do que esse *um* sítio aloca.
3. **A arena com nome próprio ("scoped") tem 0,0 MB.** Tudo vai para a raiz, que só liberta na saída
   do processo.

## 4. A família está certa, e é exatamente a de hoje

O defeito do `verdict_emit` corrigido hoje era: **alocar em ciclo, numa arena que só liberta à saída
do processo**. O pipeline de compilação é **o mesmo padrão, à escala do compilador inteiro** — cada
intermédio (tokens, AST, AST tipada, LIR, texto C) é alocado e **nenhum é libertado** até o processo
sair.

**E o instrumento para o resolver já está construído e não é chamado por ninguém.**
`tk_arena_push`/`tk_arena_pop`/`tk_arena_commit` existem no runtime e estão expostos como builtins
(`src/checker/scope.tks:745`, `src/lir/lower.tks:3885-3887`). Os únicos chamadores são **o portão de
teste** (`#109`, `#469` — `codegen.tks:12124/12129`). **O pipeline de compilação nunca os invoca.**

O `tk_arena_commit` — o par que faz um âmbito que ESCAPA dobrar as suas alocações no nível de baixo
em vez de as perder — está marcado no cabeçalho como *"enabling primitive — staged off; no compiler
source calls this yet"*. É a peça exata que um marco por ficheiro precisaria: **os itens analisados
comprometem-se, o fluxo de tokens descarta-se.**

## 5. O ponto do dono que se mantém, e por mérito próprio

**Ficheiros de 776,9 KB e 674,0 KB são violação de boa prática, e isso é verdade independentemente da
memória.** O W15 já manda dividir ficheiros grandes em módulos coesos com a mesma superfície pública.
O que muda com esta medição é só o **motivo**: divide-se por legibilidade e manutenção, não para
resolver a OOM — porque não resolve.

## 6. O que ficou POR medir, e é o próximo passo

**O sítio de chamada 0 não está identificado.** O `dladdr` devolveu `?` para todos os 90 sítios,
porque o binário do compilador não exporta os símbolos estáticos. Sem isso sabe-se *quanto* (1700,5
MB, 3,48 M alocações, ~511 B cada) e não se sabe *quem*.

Fechar isso precisa de uma de duas coisas — e nenhuma foi feita:
* religar o compilador com `-rdynamic` (ou não-estático) só para a corrida de medição; ou
* fazer o despejo imprimir o **endereço de retorno cru** em vez de só o índice, e resolvê-lo com
  `addr2line` contra o binário.

**Até lá, qualquer atribuição de causa a uma fase concreta é palpite.** O que está provado é a
estrutura — 0,0 % de recuperação, 88 % num sítio — não o nome do sítio.
