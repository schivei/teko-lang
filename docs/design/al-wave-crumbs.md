# AL Wave — crumbs (rascunhos de issue versionados na umbrella)

Status: **RATIFICADO (owner, 2026-07-19).** Estes são os textos de crumb/issue da onda
AL, versionados na umbrella `remodel/emit-throughput` (o owner optou por manter o
rascunho aqui, não como issues do GitHub). Design completo: `al-wave-emit-throughput.md`.

Ordem ratificada: **AL1 → AL0 (paralelo) → F1→F2→F3 → AL2→AL3→AL4→AL5 → AL6.**
Execução é proof-first: **AL1 fecha os números antes de qualquer código de produto.**

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

## AL0 — Const-ificar produtores build-and-return de array fixo · M mecânico

### Contexto
Muitos push-sites não são dinâmicos — são funções que CONSTROEM-E-RETORNAM um array fixo,
chamadas repetidamente, reconstruindo a mesma constante em runtime. A dor real (owner).
Roda EM PARALELO à fundação/AL1 — sem máquina nova (literais + `const` T-B6 já no seed).

### Escopo (lista de sites vem do censo do AL1)
- **Produtor build-and-return** (nullary/args-const, resultado compile-time-constante) →
  `const X = [...]` em rodata. Construído ZERO vezes.
- **Itens conhecidos, não-const** → literal `[a,b,c]` (já lowera a um malloc único).
- **Produtor parametrizado** (arg de runtime) → CONTINUA função com literais internos.

### Chamador que muta (padrão owner-ratificado)
Const compartilhada/imutável em rodata; leitor comum referencia direto (zero cópia). Quem
muta: `mut c = CONSTANTE` (cópia eager, valor já existente) + `teko::list::grow(&c, …)`.

### Invariante
Behavior-preserving: fixpoint gen2==gen3. Onde muda bytes (runtime → rodata), justificar e
medir ganho. Convenção W15: "itens → literal; build-and-return const → `const`;
parametrizado → função".

---

## F1 · F2 · F3 (FUNDAÇÃO) — a rascunhar após o AL1

- **F1** — borrow mutável seguro `&x` (grafia A: dessugar pra `Reference` existente,
  spine `is_unique_at` autoriza) · L
- **F2** — `let` imutável profundo (auto-enforçado; ~7 sites, baixo risco) · S/M
- **F3** — array Model A `[N]T` `{ptr,len,cap}` sem zero-fill · L

## AL2 · AL3 · AL4 · AL5 (THROUGHPUT) — a rascunhar após o AL1

- **AL2** — endurecer o push-cache (paliativo/ponte) · S/M
- **AL3** — cura ref-push `push(&x,v)` (ponte de nome novo p/ migração gradual) · L
- **AL4** — writers nativos (stream, sem buffer de 8.5MB) · M
- **AL5** — region-per-phase (arena por fase) · M/L

## AL6 (MIGRAÇÃO) — a rascunhar após o AL1

- **AL6** — migração dos push-sites dinâmicos restantes via ponte · M mecânico

> Os crumbs F1-AL6 nascem calibrados pela prova: o censo do AL1 reescopa AL0/AL6 e o
> blast radius de runtime confirma a prioridade do ref-push. Rascunho completo deles
> quando o AL1 fechar os números.

## Prospecção (PARKADO — design próprio depois da AL)
Unificar a superfície de referência em `&`/`*` (`mut *x: T ≡ mut x: Reference<T>`,
aposentar keyword `ref`, tirar `Ref<T>` da superfície) — ligado ao "repensar `Ref<T>`".
Toca a lei "sigils unsafe-only"; keystone. Ver `al-wave-emit-throughput.md` §12.
