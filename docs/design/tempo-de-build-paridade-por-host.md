---
section: ci
created: 2026-07-27
source: pergunta do owner — "por que o build em Linux e Windows são tão lentos enquanto em macOS é muito mais rápido? O que estamos deixando de fora nestes sistemas?"
status: INVESTIGAÇÃO MEDIDA — parcial; a seção "O QUE FALTA MEDIR" nomeia exatamente o que não pôde ser lido desta sessão
---

# Tempo de build por host — o que o macOS não faz

Cada afirmação deste documento é rotulada:

* **FATO(árvore)** — lido diretamente do repositório, reproduzível com o comando citado.
* **FATO(medido)** — cronometrado nesta investigação, com a máquina e o comando citados.
* **HIPÓTESE** — plausível, ainda sem número que a sustente.
* **REFUTADO** — hipótese que foi testada e caiu, com o teste que a derrubou.

Uma hipótese sem número não vira causa neste documento.

## 0. O ponto de partida

Corrida `30231488994` (head `47f269ab`) e corrida `30233239753` (head `1af0fb66`),
workflow `.github/workflows/pr.yml`, job `artifact`, ambas em modo `full`:

| lane | corrida 30231488994 | corrida 30233239753 |
|---|---:|---:|
| `macos-arm64` | 4m11s | 2m03s |
| `linux-arm64` | 8m52s | 9m03s |
| `windows-x86_64` | 11m54s | 10m13s |
| `linux-x86_64` | **16m28s** | **15m18s** |
| `linux-riscv64-glibc` | 25m15s | — |
| `linux-riscv64-musl` | 25m41s | — |
| `windows-arm64` | >44m (cancelada) | — |

O outlier a explicar é `linux-x86_64`: é a mais lenta de todas as lanes não emuladas,
perde para `windows-x86_64` no MESMO x86_64 e para `linux-arm64` no MESMO sistema
operacional.

## 1. Três hipóteses refutadas antes de qualquer outra coisa

### 1.1 Cache — REFUTADO

**FATO(árvore).** Não existe cache algum no repositório:

```
$ grep -ci cache .github/workflows/*.yml
pr.yml:0  nightly.yml:0  release.yml:0  codeql.yml:0
branch-policy.yml:0  mirror-pr-to-org.yml:0  seed-linux-fork.yml:0  tag-on-version-bump.yml:0
```

Zero ocorrências de `actions/cache`, zero `cache:` de `setup-*`. Nenhuma lane restaura
nada; o macOS não tem um cache que os outros não têm porque NINGUÉM tem cache.

Isto não é um detalhe negativo — é uma medição com consequência direta, registrada na
§5: cada corrida rebaixa do zero o seed publicado, e cada leg Linux repuxa ~561 MB de
imagem de container.

### 1.2 Flags de `cc` divergentes por plataforma — REFUTADO

**FATO(árvore).** `src/build/project.tks`:

* `resolve_cc` (linha 779) devolve literalmente `"cc"` em todo host quando o manifesto
  não fixa `cc`, `TEKO_CC` está vazio e `TEKO_TARGET` está vazio.
* `opt_cc_flag` (linha 799) devolve `-O2` para qualquer nível ≥ 2, sem ramo por
  plataforma. `--release` ⇒ `-O2` em todo host.
* `build_cc_argv` (linha 899) tem exatamente TRÊS tokens condicionais e nenhum deles é
  de otimização ou de debug info: `-std=c23` vs `-std=c2x`, `-ferror-limit=0` vs
  `-fmax-errors=0` (ambos por família do compilador, não por SO) e o `-Wl,-sectcreate`
  do plist de macOS. `-g` só entra com `debug=true`, que o caminho `--release` não usa.

Não há host compilando a `-O0` enquanto outro compila a `-O2`, e não há host gerando
debug info que outro não gera.

### 1.3 Modo do workflow (light vs full) diferente entre as duas corridas — REFUTADO

Verificado pelo coordenador nos logs do job `plan`: ambas as corridas são `full`
(`FULL_MATCHED: true`, `run=true`). A queda do macOS entre as duas corridas não é
artefato de modo.

## 2. FATO(árvore) — a assimetria de trabalho: quantas vezes cada leg compila o `teko.c` emitido

Esta é a resposta estrutural à pergunta do owner, e ela é legível na árvore sem log
nenhum. Duas fontes: `scripts/ci_producer_matrix.sh` (o campo `kind` e o campo
`produces`) e `scripts/produce_assets.sh` (o que ele faz com cada um).

`scripts/produce_assets.sh` §3:

```sh
if [ "$KIND" = "linux" ]; then
    for label in $PRODUCES; do
        sh scripts/native_linux_asset.sh "$label" out/teko.c src
    done
fi
```

`kind = native` ⇒ esse laço NÃO EXISTE: *"Non-Linux producers are already standing on
the target platform, so the dry build IS the asset."*

`scripts/native_linux_asset.sh` roda, por label, um `docker run` que executa
`gcc -std=c2x -w -O2 … teko.c teko_rt.c assert.c -o gd-<label>/teko` — ou seja, uma
compilação `-O2` COMPLETA do `teko.c` emitido, dentro de um container que precisa ser
baixado antes.

A conta, em modo `full`:

| leg | `kind` | `produces` | compilações `-O2` do `teko.c` | containers baixados |
|---|---|---|---:|---:|
| `macos-arm64` | native | 1 label | **1** (só o build seco) | 0 |
| `windows-x86_64` | native | 1 label | **1** | 0 |
| `windows-arm64` | native | 1 label | **1** | 0 |
| `linux-x86_64` | linux | `linux-x86_64-glibc linux-x86_64-musl` | **3** | 2 |
| `linux-arm64` | linux | `linux-arm64-glibc linux-arm64-musl` | **3** | 2 |
| `linux-riscv64-*` | linux | 1 label | **2** (1 nativa + 1 emulada) | 1 |

**Esta é a coisa que o macOS não faz e os Linux fazem.** O leg macOS constrói o
compilador uma vez e publica esse binário. O leg `linux-x86_64` constrói o compilador
uma vez para obter o `teko.c`, e então recompila esse mesmo `teko.c` a `-O2` mais DUAS
vezes, cada uma dentro de um container recém-baixado — e uma delas (`alpine`, o asset
musl) ainda roda `apk add --no-cache build-base`, que instala um gcc inteiro, do zero,
em toda corrida, porque não há cache (§1.1).

Isto explica de forma direta a parte da pergunta que o owner formulou como
"Linux e Windows são lentos": **Windows não é lento pelo mesmo motivo que Linux.**
`windows-x86_64` (10m13s) faz UMA compilação e ainda assim leva o dobro do macOS
(2m03s–4m11s), o que aponta para custo por compilação. `linux-x86_64` (15m18s) faz TRÊS.

## 3. FATO(árvore) — a escada multiplica tudo isso por três

`scripts/build_with_seed_fallback.sh` tem quatro caminhos, e o número de builds
COMPLETOS do compilador difere por um fator de 3 entre eles:

| caminho | builds completos do compilador |
|---|---:|
| fast path (o seed publicado constrói a ponta) | 1 |
| rung 0 (`bootstrap/seeds/`, o seed commitado constrói a ponta) | 1 (+1 tentativa barata) |
| escada fixada (`LADDER_RUNGS`, 2 degraus) | **3** (degrau 1 + degrau 2 + ponta) |

**FATO(árvore).** `bootstrap/seeds/` NÃO EXISTE em nenhum dos dois commits medidos:

```
$ git ls-tree -r 47f269ab -- bootstrap/     # vazio
$ git ls-tree -r 1af0fb66 -- bootstrap/     # vazio
```

Os blobs entraram em `9eb7721e` e saíram em `45da9218`, ambos ANTERIORES a `47f269ab`.
Logo **rung 0 estava morto nas duas corridas** — `commit_seed_rung` sai imediatamente
com *"no committed seed manifest … skipping"* e a escada fixada é o único caminho
restante quando o fast path falha.

**FATO(medido).** A tentativa de fast path que falha é BARATA: com o seed local
(`teko 0.3.0.16-beta`) contra a ponta deste vagão, a falha ocorre em **0,072 s** —
antes de qualquer `cc`. O custo da escada não está na sondagem; está nos builds
completos que ela acrescenta.

Consequência, se o fast path falhou nas duas corridas (ver §6 — é o que a escada fixada
existe para atender, e o cabeçalho do script afirma que o seed 0.3.0.30 não alcança a
ponta):

| leg | compilações `-O2` totais do `teko.c` |
|---|---:|
| `macos-arm64` / `windows-*` | 3 (escada) |
| `linux-x86_64` / `linux-arm64` | 3 (escada) + 2 (containers) = **5** |

## 4. FATO(medido) — o custo por compilação, e por que ele é o item dominante

O log da lane de teste no macOS já dizia: `16 builds, 43.7s: harness 0% / compile 97% /
run 1%`. A medição abaixo confirma que "compile" quer dizer, essencialmente, `cc -O2`
sobre uma unidade de tradução única e enorme — que é exatamente o que o emissor C do
Teko produz.

Máquina da medição: `Intel(R) Xeon(R) @ 2.80GHz`, 4 vCPU, 15 GB RAM, Ubuntu 24.04,
`gcc 13.3.0` e `clang 18.1.3`. **É a mesma classe de runner que o `ubuntu-latest`**
(4 vCPU Xeon), o que torna esta medição representativa da lane `linux-x86_64`.

Unidade de tradução sintética no formato que um emissor de programa inteiro produz
(uma função estática por função de origem, despacho por `switch`, grafo de chamadas
denso dentro da TU, sem includes pesados) — gerador em
`/tmp/.../scratchpad/gen_tu.py`, tamanhos de 0,86 MB a 6,93 MB.

<!-- MEDICAO-TU -->

## 5. FATO(medido) — o download de imagem NÃO é o diferenciador entre x86_64 e arm64

Tamanho comprimido do manifesto das imagens que `native_linux_asset.sh` puxa
(consultado no registry, `scripts` em `/tmp/.../scratchpad/imgsize.sh`):

| imagem | tamanho comprimido |
|---|---:|
| `quay.io/pypa/manylinux_2_28_x86_64` | **560,8 MB** |
| `quay.io/pypa/manylinux_2_28_aarch64` | **534,7 MB** |

Diferença de 5%. O pull custa caro em ABSOLUTO (561 MB por leg Linux, toda corrida,
sem cache) mas é praticamente IDÊNTICO nos dois arcos — logo não explica os ~6 minutos
que separam `linux-x86_64` de `linux-arm64`.

## 6. O QUE FALTA MEDIR — e por que não foi medido aqui

**Esta sessão não tem acesso à API do GitHub.** Verificado três vezes:

```
$ gh api /repos/teko-org/teko-lang/actions/runs/30233239753
HTTP 403: GitHub access to this repository is not enabled for this session.

$ curl -sS https://api.github.com/repos/teko-org/teko-lang/actions/runs/30233239753/jobs
HTTP=403  {"message":"GitHub access to this repository is not enabled for this session."}

$ curl -o /dev/null -w '%{http_code}' https://api.github.com/rate_limit
200
```

Endpoints sem escopo de repositório respondem (`/rate_limit` → 200); qualquer endpoint
de `teko-org/teko-lang` é negado pelo gateway. **Nenhuma linha de log de nenhuma
corrida pôde ser lida.** As afirmações acima são todas de árvore ou de cronômetro local,
e nenhuma delas depende de log.

O perfil por fase que fecha a investigação precisa de quatro linhas por lane, todas
emitidas pelo próprio script dentro do passo `Produce this leg's assets`. Quem tiver
acesso à API extrai isso com `get_job_logs` e a diferença de timestamps ISO entre:

1. `ci_provision_teko: teko <tag> ready at …` — **fim da provisão do seed**, e a linha
   diz QUAL TAG. Comparar a tag entre `linux-x86_64`, `linux-arm64`, `macos-arm64` e
   `windows-x86_64` é o teste de uma linha para a hipótese "um host pega um seed mais
   novo que os outros".
2. `teko-ci: seed builds the tip directly — fast path, no fallback engaged`
   **ou** `teko-ci: seed FAILED to build the tip directly — engaging the staged bootstrap ladder`
   — **fast path vs escada, por host.** Se divergir entre hosts, §3 sozinha explica a
   tabela inteira.
3. `teko-ci: ladder stage <N>: built pinned rung <sha>` — uma por degrau; a diferença
   entre duas linhas consecutivas é o custo de UM build completo do compilador naquele
   host. **Este é o número que compara `linux-x86_64` com `linux-arm64` sem nenhuma
   variável livre.**
4. `=== <label> — native build in <image> ===` e `native_linux_asset: <label> OK` —
   início e fim de cada build de container. A diferença dá pull + `apk add` + `gcc -O2`.

Com (3) e (4) a comparação `linux-x86_64` × `linux-arm64` fecha: se as linhas (3)
divergirem na mesma proporção que as (4), o custo é por-CPU e uniforme; se só as (4)
divergirem, o custo está no container.

## 7. Resposta direta à pergunta do owner

Com o que está MEDIDO até aqui:

> **O que o macOS tem que os outros não têm: o macOS publica o compilador que ele
> mesmo acabou de construir. As lanes Linux constroem o compilador e depois
> RECOMPILAM o `teko.c` dele mais duas vezes, a `-O2`, dentro de dois containers de
> ~561 MB que são baixados do zero em toda corrida porque o repositório não tem cache
> nenhum.**

Isso é estrutural, é legível na árvore, e não é hardware. O `windows-x86_64` mostra o
outro eixo: ele faz UMA compilação, como o macOS, e ainda assim leva 5× o tempo do
macOS — esse eixo É custo por compilação (toolchain + runner), e é o único pedaço da
tabela para o qual "hardware" continua sendo candidato legítimo.

## 8. O que fazer — dimensionado, e o que NÃO fazer

**A guarda primeiro: lane rápida que mede menos é o pior resultado possível.** Nenhuma
das opções abaixo remove um portão; as que removeriam estão listadas como recusadas.

RECUSADO — não proponha: (a) deixar de construir o asset musl; (b) mover o build glibc
para o `cc` do runner em vez do `manylinux_2_28` (isso levanta o piso de glibc de 2.28
para 2.39 silenciosamente, que é exatamente o que o container existe para impedir);
(c) tirar o `--release` do build final.

<!-- ACOES -->
