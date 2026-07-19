# AL Wave — crumbs (rascunhos de issue versionados na umbrella)

Status: **RATIFICADO (owner, 2026-07-19).** Estes são os textos de crumb/issue da onda
AL, versionados na umbrella `remodel/emit-throughput` (o owner optou por manter o
rascunho aqui, não como issues do GitHub). Design completo: `al-wave-emit-throughput.md`.

Ordem ratificada (REORDENADA pós-AL1, 2026-07-19): **AL1(feito) → [AL0 + AL4a em paralelo,
cedo, sem máquina nova] → F1→F2→F3 (FUNDAÇÃO) → AL3 (ref-push global, o lever) → AL4b
(str-builder/stream) → AL5 (region-per-phase) → AL6 (migração restante).**
**AL2 (endurecer push-cache) SAIU** — AL3 elimina o cache global de vez, e a prova mostra
que os grows >1MB já são saudáveis (a eviction size-aware protege o buffer grande); o
paliativo não compra nada que a ponte de coexistência do AL3 não entregue melhor.
Execução é proof-first: **AL1 fechou os números** (`al1-proof-report.md`).

Legenda: cada crumb roda como WIP na umbrella (commit direto ou branch → PR de volta na
umbrella); bump só quando o owner cortar versão (duplica umbrella → org, PR de seed/bump).

---

## AL1 — Prova do gargalo de emit + censo dos push-sites (proof-first, zero fix) · M

### Contexto
O emit gera o teko.c ~20x mais devagar do que o `cc` o compila. Causa suspeita (a
confirmar, não assumir): `[]byte` é `{ptr,len}` SEM `cap` (`teko_rt.h:59`); a capacidade
vive na tabela global `tk_push_cache` (65536 buckets, `teko_rt.c:2091`); MISS por colisão
de slot força copy-grow do buffer inteiro (incidente "11.5 GB fix", `teko_rt.c:2155` —
~85% do churn).

### Objetivo — SÓ medição, nenhum fix
1. PROVAR/REFUTAR o storm de copy-grow.
2. CENSAR os ~1383 push-sites por nível de conhecimento — base de escopo do AL0/AL6.

### Veredito do storm
- PROVADO sse `copy_amp(emit) > 4·log2(output_bytes)` E a coluna >1MB do histograma de
  miss-reason é dominada por `other-ptr` (colisão) ou `len` (aliasing).
- REFUTADO se dominada por `cap-full` (doublings sãos).

### Método
- Self-build com `TEKO_ARENA_OBS`: histograma de miss-reason
  (`empty|other-ptr|len|cap-full|esz/gen`, coluna >1MB) + tabela dark-matter de str.
- `copy_amp` = bytes copiados / bytes lógicos no emit.
- Timing por fase (parse/check/mono/codegen/emit) — confirmar que emit domina.

### Censo dos push-sites (o número-MANCHETE)
- **CONSTANTE DISFARÇADA** (build-and-return de array fixo, nullary/args-const, sem arg de
  runtime) → const-ificável (AL0). **Número-manchete — a dor real.**
- itens conhecidos → literal `[a,b,c]` (AL0)
- tamanho conhecido, itens dinâmicos → `[N]T` (fundação/AL6)
- genuinamente dinâmico → ref-push (AL3)

### Distinguir causas alternativas (não confirmar a favorita)
- interning O(n²) → timing + dark-matter
- arena sem size-header → miss `esz/gen`
- str-concat (`tk_str_concat`) → dark-matter; se ganhar, o núcleo vira str-builder

### Entregável
Relatório máquina-legível: veredito do storm + causa dominante + censo dos 4 níveis (com
a contagem de constantes disfarçadas em destaque). DECIDE o escopo de AL0. Nenhum código
de produto muda.

---

## Banda EARLY-PARALELA (roda cedo, sem máquina nova, semi-independente do ref-push)

### AL0 — Const-ificar produtores build-and-return de array fixo · S mecânico · HONESTIDADE
**A prova rebaixou AL0 de perf-lever a crumb de HONESTIDADE/W15**: o censo achou só **5
sites** const-ificáveis (~1,4 KB rodata), não a dor de perf. Mantido mesmo assim pelo
princípio owner **"código não mente sobre o que é"**: um produtor determinístico de dado
fixo É `const`, não função — independente do ganho. Sem máquina nova (literais + `const`
T-B6 já no seed). Ritual: fixpoint por sub-lote; onde muda bytes (runtime→rodata),
justificar. Behavior: muda-C-do-compilador (5 sites).
- **Produtor build-and-return** (nullary/args-const, compile-time-constante) → `const X=[...]`.
- **Produtor parametrizado** (arg de runtime) → CONTINUA função com literais internos.
- **Chamador que muta**: `mut c = CONSTANTE` (cópia eager, valor já existente) + `grow(&c,…)`.

### AL4a — Interning/memoização de nomes manglados (ACHADO NOVO da prova) · M · CEDO
**Alvo nomeado (RA-medido):** `cg_variant_typename_str` reconstrói a MESMA string
determinística **6,65 MILHÕES de vezes** (99 MB); `cg_opt_key` 2,85M (20 MB);
`cg_variant_key` 1,03M (22 MB); `checker::qualify` 1,1M (17 MB). O nome manglado de um tipo
é DETERMINÍSTICO → computa 1× por tipo distinto e cacheia (memo table type→str). 6,65M+2,85M+
1,03M allocs → ~milhares. **Semi-independente do ref-push, grande E barato** → entra CEDO, em
paralelo à fundação (como o AL0). Behavior: preserva-tudo (só cacheia; a string é idêntica).
Ritual: fixpoint + probe: allocs de `cg_variant_typename_str` no dark-matter → ~milhares.

---

## FUNDAÇÃO (pré-requisito do ref-push) — a rascunhar

- **F1** — borrow mutável seguro `&x` (grafia A: dessugar pra `Reference` existente,
  spine `is_unique_at` autoriza) · L · preserva-alvo/muda-C-do-compilador
- **F2** — `let` imutável profundo (auto-enforçado; ~7 sites, baixo risco) · S/M · preserva
- **F3** — array Model A `[N]T` `{ptr,len,cap}` sem zero-fill · L · preserva-alvo/muda-rep

## THROUGHPUT (consome a fundação) — a rascunhar

- **AL3** — **A CURA, o lever global**: ref-push `push(&x,v)`. A prova mostrou copy-grow
  DISTRIBUÍDO (inline_rw_block 117MB, type_param_table 108MB, resolve_type 71MB, cg_lift_block
  59MB, mono_block 58MB, type_block 56MB, cb_byte 43MB, cg_name_reaches_byvalue 41MB) — sem
  vilão único, logo o fix é WHOLESALE (migração de todo push-site), não pontual. Ponte de
  nome novo p/ migração gradual (fixpoint verde a cada sub-lote) · L · preserva-alvo/muda-C
- **AL4b** — str-builder "stream-não-concat": o resto dos 15,5M buffers de str/format
  (`cg_format_c` 1,6M, `member_key`) escreve fragmentos num writer/stream e materializa 1× do
  tamanho final conhecido (alloc única), NÃO concatena em memória (concat realoca como push).
  As entradas "2 allocs" grandes (`tk_emit_c_mode`, `run_native_gate`) JÁ são o padrão bom —
  não mexer. Absorve o antigo "writers nativos" (EmitWriter = `[chunk]byte` em stack, F3) · M
  · preserva
- **AL5** — **ELEVADO pela prova** (reclaim 0%, 1,7 GB de root nunca liberado): region-per-
  phase (arena por fase, bulk-drop no fim). Primitivas já existem (`teko_rt.h:148-152`) · M/L
  · preserva

## MIGRAÇÃO — a rascunhar

- **AL6** — migração dos push-sites dinâmicos restantes (itens→literal, tamanho→`[N]T`,
  dinâmico→ref-push) via ponte de coexistência · M mecânico · preserva-alvo/muda-C

> AL2 (endurecer push-cache) foi DESCARTADO pós-prova: AL3 remove o cache global de vez e os
> grows >1MB já são saudáveis (eviction size-aware). Rascunho completo de F1-AL6 conforme
> cada um entra em execução; a prova (`al1-proof-report.md`) já os calibrou.

## Prospecção (PARKADO — design próprio depois da AL)
Unificar a superfície de referência em `&`/`*` (`mut *x: T ≡ mut x: Reference<T>`,
aposentar keyword `ref`, tirar `Ref<T>` da superfície) — ligado ao "repensar `Ref<T>`".
Toca a lei "sigils unsafe-only"; keystone. Ver `al-wave-emit-throughput.md` §12.
