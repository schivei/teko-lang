---
seq: 0127
crumb-id: NAT-B3
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [NAT-N1]        # variant/nullable machine model lands first; this fixes its wrong invariant + the crashes
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-999"   # §1 classe (b), §3 P1.1-P1.3
  - "src/lir/lower.tks:3595-3639"                                       # variant_member_index_of / variant_member_type_matches
  - "src/backend/objfile_elf.tks:425"                                   # emit_elf_shdrs — fat []byte-return chain hang/OOM
  - "src/backend/dwarf.tks:508"                                         # debug_info_locals — aggregate sret stack-smash arm64
  - "src/backend/abi_aapcs64.tks:0-999"                                 # AAPCS64 sret classification
  - "scratchpad/ci-1135-native-extract.txt:5-22"                       # x86@934, arm64@90 frontier
---

# 0127 · NAT-B3 — classe (b): crashes de lowering (hang/OOM/stack-smash) + invariante-errado

> Fechar a classe de falha que MATA CEDO no self-host native — não são honest-stops "not yet lowered",
> são bugs de lowering: hang/OOM no retorno-fat `[]byte` em cadeia (x86_64@934 `emit_elf_shdrs`),
> stack-smash no retorno-agregado sret AAPCS64 (arm64@90 `debug_info_locals`), e o invariante-ERRADO de
> membro-de-variante (`lower.tks:3602/3819`) que falha ao casar `Null`/`Func`/`Void`. São elas que
> impedem gen2 native de EXISTIR. Byte-preserving na rota C (native-only) → `fixpoint-rebuild`.

## Goal

O CI #1135 prova aderência native <12%: x86_64 trava no item 934, arm64 CRASHA no item 90. Nenhum é
honest-stop; são bugs. Este crumb corrige os três primeiros-ofensores concretos + varre o resíduo
frontier-driven dos 77 error-paths `(internal)` de `lower.tks`. A regra: um construto que o checker
prova válido NUNCA pode travar, vazar memória ou corromper a pilha no lowering — ou lowera certo, ou
honest-stop NOMEADO (nunca hang/crash/silêncio). Completeness + correção, não capability.

## Where

- `src/lir/lower.tks:3595` `variant_member_index_of` + `:3605` `variant_member_type_matches` — o
  invariante-errado. Estender o `match member`/`match value_ty` para os membros que hoje caem no
  `_ => false`: `Null` (null-como-união), `Func` (closure/func-value), `Void`, e agregado
  (struct/class por nome já via `Named`, mas confirmar layout-anônimo). Remove os falsos-erros
  `:3602`/`:3819`. Desbloqueia `collect_const_sig` (item ~2163) e a família union/nullable.
- `src/backend/objfile_elf.tks:425` `emit_elf_shdrs` — **NÃO muda a fonte** (é código correto que a
  rota C compila bem); o defeito está no LOWERING do retorno-fat `[]byte` em cadeia. Corrigir em
  `src/lir/lower.tks` (o threading DPS/dest-passing do valor `[]byte` retornado por chamada encadeada)
  + `src/backend/abi_sysv64.tks` (classificação do retorno `{ptr,len}`). Cobre também os honest-stops
  `lower.tks:2824/2859/6020` "fat-typed lambda return".
- `src/backend/dwarf.tks:508` `debug_info_locals` — idem, fonte intocada; o defeito é o retorno de
  `DwLocalsInfo` (agregado) por `sret` na ABI arm64. Corrigir `src/backend/abi_aapcs64.tks` (regra
  sret/HFA/register-pair) + `src/backend/isel_arm64.tks`/`regalloc.tks` (guard de overrun de slot
  `[]byte`/`[]u32`).
- `src/lir/lower.tks` (77 sítios `(internal)`) — a lista latente: à medida que o frontier avança
  (R#1→R#3), cada novo ofensor é um destes; converter invariante-errado→certo ou hang→lowering-correto.

## How

Frontier-driven, na ordem do CI (pior/mais-cedo primeiro). Cada correção → re-emite gen2 native
(write-only) → o próximo ofensor aparece mais fundo → repete.

1. **P1.3 · invariante membro-de-variante (o mais barato, destrava a maior família).** Estender
   `variant_member_type_matches`:

```teko
/**
 * variant_member_type_matches — does `value_ty` name (or nest into) `member`, one arm of a declared
 * variant? Extended to cover every arm shape the checker can produce: named/error/prim/str/byte/slice
 * (already), PLUS `null` (the null-as-union arm), a function/closure arm, `void`, and an anonymous
 * aggregate arm — so `variant_member_index_of` never falsely reports "not a member" on a
 * checker-valid value (the `collect_const_sig` internal-error frontier).
 *
 * @param member   one arm type of the declared variant
 * @param value_ty the static type of the value being placed into / compared against the variant
 * @param variants the lowering's variant-def table for nested-union recursion
 * @return         true when `value_ty` is (or nests into) `member`
 * @since 0.3.1
 */
fn variant_member_type_matches(member: @Type(), value_ty: @Type(), variants: []LVariantDef): bool
```

2. **P1.1 · retorno-fat `[]byte` em cadeia (x86@934).** Ensinar o lowering a passar o `[]byte`
   retornado por DEST-PASSING na arena do CALLER (não realocar/copiar por chamada) — o idioma que o
   `emit_elf_shdrs` estressa (9 `b = emit_elf_shdr(b, …)` seguidos). Confirmar contra o codegen C que
   JÁ funciona (a rota C sret-a o `{ptr,len}` e reusa o buffer): o native tem de emitir a MESMA
   semântica (arena do caller, sem cópia). Fecha os honest-stops de fat-return (2824/2859/6020).
3. **P1.2 · retorno-agregado sret AAPCS64 (arm64@90).** Corrigir a classificação de `DwLocalsInfo`
   (>16 bytes → sret por ponteiro em `x8`; ≤16 → register-pair) no `abi_aapcs64.tks`; adicionar guard
   de escrita-em-slot no encoder arm64 (o stack-smash é escrita além do slot). Auditar `[]byte`/`[]u32`
   locais da função contra o mesmo padrão.
4. **Sweep frontier-driven.** Após 1–3, re-medir a fronteira; cada novo ofensor `(internal)` é
   convertido (invariante-errado→certo, ou hang→correto, ou — se for capability genuína — honest-stop
   NOMEADO, nunca silêncio). Inclui o windows@1145 `asm_module_items` (auditar se é (a) ou (b) no sítio).

## Rulings & laws

- **Teko-only:** `src/lir/*.tks` + `src/backend/*.tks`; sem C-twin. As fontes `objfile_elf.tks`/
  `dwarf.tks` NÃO mudam (código correto — o bug é o lowering delas).
- **Comment convention (W15):** `/** */` só em `exp`; sem `//`; doc nunca maior que o código.
- **Fork protocol:** o modelo de máquina (sret/register-pair por ABI; DPS na arena do caller; union
  `{tag,payload}`) já é ratificado (abi_*, D1-T1 union→largest-slot, DPS-crumb). Sem fork; NÃO HALT.
- **Fail-loud:** um construto não-lowerável honest-stop NOMEADO; NUNCA hang/OOM/stack-smash/silêncio.
- **Native write-only:** medir por "gen1 emite gen2 native completo" (item→TOTAL); NÃO rodar o binário.
- **Layout parity:** o sret/fat-return native tem de casar byte-a-byte o que a rota C emite (o
  fixpoint C-vs-native pós-F9 concorda por construção).
- **Safety:** NUNCA `teko test .`; build subshell `ulimit -v 4718592`; reseed só no [fixpoint] da fase;
  `gen2==gen3` (rota C) byte-idêntico; MEM_PARANOID 0; ratchet D100.

## Fixtures

`none — the fixpoint self-build exercises this` — o próprio `src/` do compilador é o corpus: `emit_elf_shdrs`,
`debug_info_locals`, `collect_const_sig` SÃO o teste; a métrica "gen1 emite gen2 native completo" é o
oráculo. Sem fixture afirmativo novo (lei do dono; a validação de RUNTIME native é pós-F9).

## Gate

`[fixpoint]` — rides os reseeds R#1 (destrava) e R#3 (resíduo). "Green" = os três primeiros-ofensores
não travam/crasham; o frontier de `gen1`-emite-gen2-native avança além dos itens 90/934/1145 nas 4
pernas; nenhum `(internal)` alcançado é falso; o reseed de fase da rota C é `gen2==gen3` byte-idêntico.
Reseed-class: `fixpoint-rebuild`.

## Deps

`NAT-N1` (0113) — o modelo `{tag,payload}` de união pousa primeiro; este crumb corrige o invariante
errado que 0113 assume "inalcançável" mas o CI prova alcançado, e as travas fat/sret.

## Done when

`variant_member_index_of` nunca falso-erra em valor checker-válido; `emit_elf_shdrs` e
`debug_info_locals` lowerаm sem hang/OOM/stack-smash nas suas arches; o frontier de gen1-emite-gen2-native
passa dos primeiros-ofensores em todas as 4 pernas; reseed de fase byte-idêntico.
