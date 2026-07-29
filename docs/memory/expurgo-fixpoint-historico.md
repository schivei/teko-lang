# Expurgar os invasores `.fixpoint/` — o que já está feito, o que falta, e o preço

**Estado em 2026-07-29: a parte segura ESTÁ FEITA. A parte restante está PARADA por decisão
informada do dono, não por esquecimento.**

## O que já está feito

- **Zero ficheiros `.fixpoint/` rastreados** (commit `64227bd`, "destrackar .fixpoint/ — 28 MB de
  artefactos gerados, incluindo C de saída"). Medido: `git ls-files | grep -c '^\.fixpoint/'` → `0`.
- **Varrida a família toda** à procura de irmãos. O único C grande rastreado é `bootstrap/teko.c`
  (10.1 MB), que é a semente deliberadamente colhida e protegida por lei do dono, mais
  `src/runtime/teko_rt.c` (144 KB), que é o runtime real. Os restantes `.c` rastreados são fixtures
  de teste de 1–5 KB. **Nenhum invasor sobrou.**
- **Recorrência fechada por DUAS camadas**: a regra `.fixpoint/` explícita, e a regra genérica
  `.*/` + allowlist (`!.github/`, `!.claude/`) que apanha até um nome de rascunho que ninguém previu.

## O que falta, e porque não foi feito

Restam **2 blobs, 13.4 MB, apenas no HISTÓRICO**. Foram introduzidos por dois commits:

| commit | o que é | ancestral de `origin/main`? |
|---|---|---|
| `4e6c4e4` | PR #95, "vagão 20 — lanes por artefato…" | **SIM** |
| `38a8875` | #654, "0.3.0.31-beta — o compilador reconstrói a si mesmo…" | não |

Removê-los exige **reescrever `4e6c4e4`**, e isso colide com três leis do dono ao mesmo tempo:

1. **Perde a origem.** `bootstrap/PROVENANCE` nomeia esse commit explicitamente como âncora da
   cadeia de custódia da semente:
   ```
   run:     30381332577
   commit:  4e6c4e4bee787ce6ea19d80a8f7ca7359c9d5b4d
   ```
   Reescrever muda o SHA; o registo passa a apontar para um commit que não existe. É literalmente o
   que a lei do dono proíbe: *"só podemos trocar o teko.c se ele passar por toda uma cadeia válida,
   senão não há garantia e 'perdemos' a origem."*
2. **Exige force-push em `main`** — medido como RECUSADO pelo token do agente em todos os ramos.
3. **Parte a linearidade `main` ↔ org**, que o dono exigiu explicitamente.

## O facto que corrige a premissa: SQUASH NÃO EXPURGA

O dono escreveu: *"a main só será alterada quando eu squashar ela através do vagão."* Verdade quanto
a QUEM altera a `main` — mas **um squash-merge não remove estes blobs**, e é importante não contar
com isso.

Um squash-merge cria **um commit novo cujo pai é a ponta actual da `main`**. Todo o histórico
anterior continua a ser ancestral desse commit, `4e6c4e4` incluído, e os seus blobs continuam
alcançáveis. Squashar limpa a FORMA da história (muitos commits → um), não os OBJECTOS que ela já
contém. Os 13.4 MB sobrevivem a qualquer número de squashes.

Só duas coisas removem um blob do histórico: reescrever os commits que o contêm, ou abandonar essa
história por completo.

## O procedimento, para o dia em que o dono o quiser correr

**Não é executável por um agente** — o passo 4 exige force-push, recusado pelo token. Fica escrito
para o dono correr, na ordem exacta.

```sh
# 0. BACKUP primeiro. Isto é irreversível e toca a mainline.
git clone --mirror git@github.com:schivei/teko-lang.git teko-backup.git

# 1. Purgar o caminho de TODO o histórico (git-filter-repo, não filter-branch).
pip install git-filter-repo
git clone git@github.com:schivei/teko-lang.git teko-purge && cd teko-purge
git filter-repo --path .fixpoint --invert-paths

# 2. LER o novo SHA do commit que era 4e6c4e4. O filter-repo escreve o mapa em
#    .git/filter-repo/commit-map — a coluna esquerda é o SHA velho, a direita o novo.
grep ^4e6c4e4 .git/filter-repo/commit-map

# 3. RE-ANCORAR bootstrap/PROVENANCE no SHA novo, num commit próprio, e explicando porquê.
#    ESTE PASSO NÃO É OPCIONAL. Saltá-lo é exactamente "perder a origem".
#    (O campo `run:` NÃO muda — a corrida de CI 30381332577 é um facto externo ao git.)

# 4. Force-push. SÓ O DONO. Coordenar com o org antes: a linearidade main <-> org quebra aqui.
git push --force origin main

# 5. Toda a gente re-clona. Clones antigos passam a ter história divergente e não reconciliável.

# 6. Os objectos ficam no GitHub até o GC deles correr; para forçar, é preciso abrir pedido ao
#    suporte do GitHub. Até lá o ganho de espaço não aparece do lado remoto.
```

## Recomendação registada

**Deixar a história em paz.** O custo real é 13.4 MB mortos no tamanho do clone — nada os referencia,
nada os lê, e não voltam a acontecer. O custo do expurgo é re-ancorar a proveniência da semente,
partir a linearidade com o org, e obrigar toda a gente a re-clonar, para poupar 13 MB.

O dono ficou informado dos três choques de lei antes de decidir. Se um dia decidir correr, o
procedimento acima está pronto — e o passo 3 é o que não pode ser esquecido.
