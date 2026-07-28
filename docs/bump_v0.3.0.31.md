0.3.0.31-beta — o compilador reconstrói a si mesmo, e o fixpoint passa a provar isso

## O que esta versão entrega

**O compilador se reproduz.** `gen2 == gen3`, byte a byte, 3.880.552 bytes idênticos. Essa afirmação estava escrita como guardrail permanente em seis documentos deste repositório — README, MASTER_PLAN, HISTORY, DECISION_LOG, BUILDING, ROADMAP_NET_CRYPTO — e **nenhum workflow jamais a computou**. Agora existe `scripts/fixpoint_gate.sh`, ele roda em cada sublane produtora logo após a compilação, e reprova a lane quando o fixpoint não vale.

**As duas rotas de emissão convivem de novo.** A excisão do caminho C foi revertida. O backend nativo continua sendo o padrão e continua sendo exercitado por toda a suíte de regressão; a rota C existe para uma coisa só — permitir que o compilador construa a si mesmo enquanto o backend nativo ainda não fecha esse ciclo. A remoção definitiva do emissor de C é meta declarada da `.33`, com a `.32` completando o backend nativo no meio.

## Por que esta release existe

Sem ela, todo push a este repositório pagava uma **escada de bootstrap**: o seed publicado (0.3.0.30) não constrói mais esta árvore, então o CI encadeava gerações intermediárias — 392 s de 780 s por job no x86_64, seis jobs, em cada push. A escada foi substituída por um C versionado (`bootstrap/teko.c`), que reduz o mesmo trabalho a um `cc` e uma geração: **358 s contra 561 s**, medidos na mesma máquina, sem invocar `git` uma única vez.

Publicar esta versão é o que torna esse arranjo desnecessário: com um seed 0.3.0.31 disponível, o `bootstrap/teko.c` sai do repositório na `.32`, e `teko::c_types` — hoje estacionado em `staged/` porque consome builtins que o seed 0.3.0.30 desconhece — volta para `src/` sem edição.

## Mudanças de linguagem

**`redundant cast` deixa de ser erro e passa a ser aviso.** A diagnose D1 (`cast-width-hygiene-0.3.1.md`) reprovava a compilação de um `to` provadamente redundante. Como erro, ela era insatisfável ao longo de uma cadeia de bootstrap: o seed aplica B.22 (*"operands must be the same type"*) e **exige** o cast; um compilador local tem a W-RULE e **prova o mesmo cast redundante**. Medido: de 18 casos, 17 faziam o compilador local rejeitar a árvore inteira. Como aviso, um único texto compila sob os dois.

A política que a diagnose carregava passou para o gate D4 — ARITH-CAST-RATE — que **reprova o build**. O teto foi ajustado de 2% para 5% para que ele pudesse ser ligado de fato: o corpus mede 3,22%, e um teto de 5% que roda vale mais que um de 2% que nunca rodou. O número só anda para baixo.

## Gates que passaram a existir

| gate | o que faz |
|---|---|
| **fixpoint** | `gen2 == gen3` byte a byte, em cada sublane produtora. Duas saídas: passou ou falhou |
| **sem skips** | uma linha de regressão pulada é falha. Avaliado **depois** da lane, sobre a saída — a semântica de `teko test` fica intacta e nenhum projeto de usuário herda a política |
| **D4 ≤ 5%** | ARITH-CAST-RATE. A métrica é reportada logo após o front-end, então aparece mesmo quando a suíte falha; o veredito fica no fim, para que uma métrica de estilo nunca mascare uma falha de teste |

## Correções

- **`xs = push(xs, v)` devolvia comprimento errado.** Um local gordo `mut` vivia num par de registradores e a reatribuição caía no caminho escalar, guardando só a metade `ptr`. Cinco `push` num laço liam `len == 0`. Agora um `mut` gordo vive num slot de frame de 16 bytes, cujo endereço atravessa merges de fluxo.
- **`teko::list::push`/`empty`/`mem::push_fo`** passam a baixar no backend nativo para elemento escalar, pela convenção de out-slot.
- **Chamada com receptor gordo** passa a baixar no backend nativo.
- **`diagnostics.tkr`**: um caso de parada dura da signature-walk estava dentro do build compartilhado do item-walk, onde um corte duro apaga tudo que vem depois.

## Removido

- **`examples/regressions/cwd_build`** — o regressor tinha um cenário (`teko build .` de dentro do diretório) e o corpo exercitava a família UTF-8. Duas coisas sem relação, batizadas com o nome da menos importante. Compilar na raiz do projeto é o caminho normal, não um modo especial; a cobertura UTF-8 é exercida no corpus principal por `q031_chars_iter`, `q073_encodings_roundtrip` e `q100_json_roundtrip`.

## Limitações conhecidas

- **O backend nativo ainda não constrói o compilador.** Ele para em degraus nomeados, com endereço, e é isso que a `.32` fecha. Enquanto isso, o fixpoint é obtido pela rota C.
- **O emissor de C sai na `.33`, o runtime em C não.** São dois "C" diferentes: `src/runtime/teko_rt.c` (2.578 linhas, 136 funções) é runtime de produção, linkado em todo programa compilado, e não tem prazo de portabilidade definido.
- **`bootstrap/teko.c` permanece no repositório nesta versão.** Ele só pode sair depois que um seed 0.3.0.31 estiver publicado — antes disso o CI ainda provisiona o 0.3.0.30, que não constrói esta árvore.
