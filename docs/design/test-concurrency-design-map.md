# Test/Concurrency Architecture — Design Source Map

**Scout Recon (2026-08-20):** Full read-only survey of test harness, test runner, concurrency, 
channels, DI, and spawn infrastructure. **Purpose:** map where the design is documented and identify 
DESENHADO-MAS-NÃO-GRAVADO (designed but not recorded in docs/design or .crumbs).

**Report Type:** Tabela mapeando peça → arquivo(s) → tipo de fonte (DOC | CRUMB | DECISION_LOG | código).

---

## Executive Summary

| Category | Status | Primary Source |
|----------|--------|-----------------|
| **Runner de testes paralelo (SW11.4)** | PLANEJADO (pauta) | wave-0.3.1-plan.md:436 + #472 |
| **tkt (unit) — modelo thread-paralelo** | DESENHO 70-85% LANDED | harness-de-testes-gerado.md + crumbs 0115/0116/RT-L6 |
| **tkr (regression) — modelo processo-isolado** | DESENHO LANDED | tkr-regression-format.md + plano-s10-concorrencia-crumbs.md |
| **spawn (thread-only)** | LANDED (S1-S3) | plano-s10-concorrencia-crumbs.md:44-88 + crumbs 0115/0116 |
| **memchan (in-process canal)** | LANDED ~70-85% | crumb 0116 + teko_rt.c:2605+ |
| **oschan (IPC canal)** | LANDED ~70-85% | crumb 0116 + teko_rt.c:2840+ |
| **Canais dependem de DI** | DESIGN (Part B deferred) | crumb 0117-D1-DI + plano-secao7-di-service-svc.md |
| **DI / DI-scoped** | DESIGN (Part A landed, Part B post-0.3.1) | crumb 0117-D1-DI + plano-secao7-di-service-svc.md |
| **Queue<T> / work-queue** | PLANEJADO (M2) | crumb 0082-COL-Q14 + colecoes-memoria-fila-implementacao-0.3.1.md |

---

## Mapa Detalhado: Peça de Desenho → Arquivo(s)

### 1. Runner de Testes Paralelo (#472 / SW11.4)

| Aspecto | Arquivo(s) | Linha(s) | Tipo | Status |
|---------|-----------|---------|------|--------|
| **Plano geral** | `wave-0.3.1-plan.md` | 436 | DOC | PLANEJADO (não implementado) |
| **Especificação de dependências** | `wave-0.3.1-plan.md` | 426-443 | DOC | PLANEJADO |
| **Fixtures owed** | `wave-0.3.1-plan.md` | 442-443 | DOC | NÃO GRAVADO/PLANEJADO |
| **Harness de testes gerado** | `harness-de-testes-gerado.md` | 1-150 | DOC | DESIGN COMPLETO |
| **Recon paralelo-channels** | `parallel-test-channels-recon.md` | 1-255 | DOC | RECON 2026-08-20 |
| **Runtime primitivos** | `.crumbs/0116-S10-RT-…` | 44-85 | CRUMB | 70-85% LANDED |
| **Harness runtime (RT-L6)** | `.crumbs/0064-RT-L6-…` | — | CRUMB | PLANNED (M2) |

**Veredito:** A especificação está DISPERSA entre docs e crumbs.

---

### 2. tkt (Unit) — Modelo de Execução

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Conceito: binário próprio por .tkt + threads paralelo** | `harness-de-testes-gerado.md:14-50` | DOC | DESIGN COMPLETO |
| **Ruling: "testes em threads"** | `harness-de-testes-gerado.md:R1, R8, R10, R13` | DOC | RULINGS DONO |
| **Parser (spawn contextual)** | `.crumbs/0115-S10-SURF` | CRUMB | 70-85% LANDED |
| **Runtime test harness** | `.crumbs/0064-RT-L6-runtime-test-harness.md` | CRUMB | PLANNED (M2) |
| **Código: test harness sequencial** | `src/test/test.tks` | CÓDIGO | SEQUENCIAL HOJE |

**Veredito:** Modelo está DOCUMENTADO; runtime está 70-85% landed.

---

### 3. tkr (Regression) — Modelo de Execução

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Formato Gherkin** | `tkr-regression-format.md` | DOC | RATIFIED |
| **Execution: processos isolados** | `tkr-regression-format.md:§1` | DOC | DESIGN |
| **Runtime: run_pool** | `src/build/regression.tks:176-203` | CÓDIGO | LANDED (sequencial) |
| **Crumb: S10-RT** | `.crumbs/0116-S10-RT` | CRUMB | 70-85% LANDED |

**Veredito:** Especificação está RATIFICADA; implementação é sequencial hoje.

---

### 4. spawn (Thread-Only Primitivo)

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Decision D1** | `plano-s10-concorrencia-crumbs.md:16-25` | DOC | CONSTRAINT-FORCED |
| **S1–S3 spine** | `plano-s10-concorrencia-crumbs.md:44-61` | DOC | DESIGN |
| **Primitivo C: tk_thread_spawn** | `teko_rt.c:2554,2567` | C | LANDED |
| **Leaf discipline** | `plano-s10-concorrencia-crumbs.md:82-88` | DOC | RATIFIED |

**Veredito:** spawn é **SÓ thread**, conforme design.

---

### 5. memchan (In-Process Memory Channel)

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Spike C0b** | `plano-s10-concorrencia-crumbs.md:47` | DOC | DESIGN |
| **Runtime: tk_memchan_*** | `teko_rt.c:2605+` | C | LANDED |
| **Surface: Rx<T>/Tx<T>** | `src/threads/threads.tks` | SRC | 70-85% LANDED |

**Veredito:** memchan está **LANDED ~70-85%**; reutilizável.

---

### 6. oschan (OS IPC Channel)

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Spike C0c** | `plano-s10-concorrencia-crumbs.md:48` | DOC | DESIGN |
| **Runtime: tk_oschan_*** | `teko_rt.c:2840+` | C | LANDED |
| **Mecanismo: AF_UNIX DGRAM** | `plano-s10-concorrencia-crumbs.md:9` | DOC | DESIGN |

**Veredito:** oschan está **LANDED ~70-85%**, mas regressor usa arquivo-based DIY hoje.

---

### 7. Canais Dependem de DI (Service Injection)

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Modelo: canal é serviço singleton** | `.crumbs/0116-S10-RT:70-72` | CRUMB | DESIGN |
| **F2 program-root exception** | `.crumbs/0117-D1-DI:48-50` | CRUMB | DESIGN |
| **DI Part B (binding)** | `.crumbs/0117-D1-DI` | CRUMB | DEFERRED (M5) |

**Veredito:** Dependência é **RATIFICADA**; Part B deferred until S10-RT finals.

---

### 8. DI / DI-Scoped (Dependency Injection)

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Design (Part A + B)** | `plano-secao7-di-service-svc.md` | DOC | DESIGN COMPLETO |
| **Spec §7** | `mudancas-superficie-0.3.1.md:§7 (252-300)` | DOC | SEALED |
| **Spec §8 (arena-lifetimes)** | `arena-especificacao-unica-0.3.1.md:§8 (728-768)` | DOC | SEALED |
| **Crumb Surface (SM-G6)** | `.crumbs/0012-SM-G6-di-service-taint.md` | CRUMB | LANDED (M1) |
| **Crumb Binding (D1-DI)** | `.crumbs/0117-D1-DI-arena-lifetime-binding.md` | CRUMB | DEFERRED (M5) |

**Veredito:** DI está **COMPLETO EM DESIGN**, em duas fases (Part A landed, Part B deferred).

---

### 9. Queue<T> / work-queue (COL-Q14)

| Aspecto | Arquivo(s) | Tipo | Status |
|---------|-----------|------|--------|
| **Design: FIFO sobre Ring<T>** | `.crumbs/0082-COL-Q14-queue-deque.md` | CRUMB | DESIGN |
| **Baseador: Ring<T>** | `colecoes-memoria-fila-implementacao-0.3.1.md` | DOC | DESIGN |
| **Crumb: M2, [dry]** | `.crumbs/0082-COL-Q14-queue-deque.md` | CRUMB | PLANEJADO |

**Veredito:** Queue<T> está **PLANEJADO (M2)**; pronto para uso futuro.

---

## DESENHADO-MAS-NÃO-GRAVADO

### Lacunas Genuínas

| Peça | Descrição | Onde deveria estar |
|------|-----------|-------------------|
| **#472 protocolo de distribuição** | Qual mecanismo: memchan/oschan/arquivo? Qual estrutura: Queue/Ring/Channel? | Crumb SW11.4 (não existe) ou wave expandido |
| **tkt harness final** | Implementação concreta do main sintetizado | Crumb RT-L6 (PLANNED) |
| **tkr run_pool paralelo** | Substituição do sliding-window por worker-pool | Crumb SW11.4 (não existe) |
| **Graceful shutdown (canais)** | Protocolo de close | `plano-s10-channels-batch-detalhe.md` (parcial) |
| **Cancel (CN1) sob-await** | Specifics de `cancel()` dentro de await | Deferred post-S10 |

---

## Recomendações

1. **Existe UM doc canônico?** NÃO — está fragmentado em múltiplos docs + crumbs.

2. **Consolidação recomendada:** Um doc único agregando rulings + spike + binding.

3. **Antes de implementação:** Dono decide SW11.4 protocolo; crumb detalha.

---

## Checklist de Localização

- [x] Runner paralelo: `wave-0.3.1-plan.md` + `harness-de-testes-gerado.md` + crumbs 0115/0116
- [x] tkt model: `harness-de-testes-gerado.md` + crumbs 0115/0116
- [x] tkr model: `tkr-regression-format.md` + `regression.tks`
- [x] spawn: `plano-s10-concorrencia-crumbs.md` + crumbs 0115/0116
- [x] memchan: crumb 0116 + `teko_rt.c:2605+`
- [x] oschan: crumb 0116 + `teko_rt.c:2840+`
- [x] DI: `plano-secao7-di-service-svc.md` + crumbs 0012/0117
- [x] Queue: crumb 0082

