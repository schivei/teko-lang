# `bootstrap/teko.c` — o C versionado que mata a escada (0.3.1)

> **Estado:** ativo. Ruling do dono 2026-07-27: *"não precisamos construir escada na org, apenas no
> vagão e versionar a saída teko.c"*, refinado no mesmo dia para *"o vagão 15 já está provado, só
> precisa continuar a partir dele"* e *"quando passar, o teko.c produzido no 20 vira o seed"*.

## O que é

Um compilador Teko **congelado em C**, commitado. `scripts/build_with_seed_fallback.sh` o compila
(`cc`), obtém um binário, e constrói o tip com ele em UMA geração — sem escada, sem seed baixado,
sem sondar histórico.

## Por que existe

O seed publicado 0.3.0.30 não constrói o tip do vagão 20: morre em
`comptime_fold.tks:1569: operands must be the same type (no promotion — B.22)`. A escada existia
para contornar isso, subindo seed → rung1 → rung2 → tip. Custava **392s de 780s** por job no
x86_64, seis jobs, todo push.

O C versionado substitui os dois degraus intermediários por um `cc`.

## De onde este arquivo veio, e por que NÃO do vagão 20

Colhido do **vagão 15** (`feat/0.3.1-regressor-onda-b`, `a34a3aa`) — o estado mais novo que fechou
verde ponta a ponta. A tentação era usar o C do próprio vagão 20, que a escada também emite. **Não
serve, e o motivo é estrutural, não de qualidade:**

- no vagão 20 o backend C **foi removido** (`project.tks`: *"the own backend, which is now the only
  backend"*);
- compilado, o C do vagão 20 dá um binário que só tem o backend nativo;
- e o nativo ainda para em `fat-pointer receiver \`call\` not yet lowered (N2)` ao construir o
  compilador. **Medido**, não suposto.

Ou seja: o binário do vagão 20 não se auto-hospeda hoje. O do vagão 15 sim — porque ainda tem a
fase `emit C`. Enquanto o N2 não fechar, o C versionado tem de vir de uma geração que ainda emite C,
e o vagão 15 é a mais nova que emite.

## Os três riscos, todos medidos antes de commitar

| risco | verificação | resultado |
|---|---|---|
| o C do 15 linka com o runtime do 20? | `git diff` de `teko_rt.h`/`assert.h` entre `a34a3aa` e o vagão 20 | **ABI puramente aditiva** — duas funções novas (`tk_flush_out`, `tk_rt_arch`), zero remoções, zero assinaturas alteradas; as únicas linhas removidas são comentários |
| o C é específico de arquitetura? | `grep` por `__x86_64__`/`__aarch64__` | **nenhum** — as 19 menções são strings de dados (nomes de alvos). UM arquivo serve x86_64 e arm64, ao contrário dos binários seed |
| o binário do 15 constrói o vagão 20? | build seco completo | **sim** — `cc` 58s + build 305s |

## A cadeia, e onde ela termina

    hoje        bootstrap/teko.c (15) -> cc -> binario do 15 (tem backend C)
                  -> constroi o vagao 20 EM UMA GERACAO, e emite o teko.c(20)

    quando o N2 fechar
                bootstrap/teko.c (20) -> cc -> binario do 20 (so backend nativo)
                  -> constroi o vagao 20 NATIVAMENTE, sem emitir nada

O critério de parada do dono — *"até termos um teko.c que não gera outro teko.c"* — e o fecho do
degrau N2 são **o mesmo evento visto de dois lados**. Hoje o C versionado gera outro C porque o
compilador dele emite C; no dia em que o nativo construir o compilador, não gera, e o ciclo termina
sozinho, sem ninguém precisar declarar que terminou.

`scripts/no_emitted_c.sh` já existe (escrito para a ordem de zero-C de 26/07) e **nenhum workflow o
chama**. É literalmente o gate que detecta esse evento; ligá-lo é o passo que fecha o ciclo. Ele
exclui `.c` rastreados pelo git por construção, então `bootstrap/teko.c` não conflita com ele.

## Como atualizar

O arquivo não é estático — é o elo mais novo provado. Para trocá-lo:

1. construa a árvore com o compilador em mãos (`--no-verify --release`);
2. o `teko.c` emitido em `OUT_DIR` é o candidato;
3. verifique que ele compila contra o runtime da árvore ALVO e que o binário constrói essa árvore;
4. só então substitua.

O passo 3 não é cerimônia: é exatamente o que separa este arquivo de um binário opaco.

## Quando apagar

Quando o seed publicado voltar a construir o tip — o que acontece no merge para a org, com a `.31`
cortada como release. Ruling do dono: *"podemos apagar o teko.c e voltar a construção normal pegando
a última versão publicada"*. **O que sai é o ARQUIVO, não a ordem em que o rung -1 roda**: ele roda
antes de o seed ser sequer tentado, porque a presença do arquivo É a declaração de que o seed não
serve, e tentar o seed primeiro custa ~94s de fracasso já conhecido por job.

## Tamanho

9,9 MB / 215.598 linhas. O dono ratificou: *"é um fardo histórico"*.
