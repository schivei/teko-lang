# EXPURGO-PENDENTES — Acumuladores a refatorar

Conversões mecânicas completadas. Itens abaixo requerem análise e refactorização manual.

## src/lir/lower_test.tkt (74 ocorrências)
**Tipo**: Acumuladores de array
**Padrão**: `var X: []T = empty(); loop { X = push(X, ...); }`

**Análise necessária**:
- Contar iterações do loop
- Converter para pré-alocação `var X: [count]T = []`
- Usar índice em lugar de `push`

Exemplos:
- Linhas ~193-195: `members` acumula 2 items → `var members: [2]@Type() = []`
- Linhas ~206+: `items` acumula em loop com múltiplos `push` → pré-alocar
- Linhas ~282+: `mtes` acumula em loop

---

## Estratégia deste agente (mecanico) — CONCLUÍDO

**Fase 1: Conversões automáticas** (DONE)
1. Remover padrões `push(empty(), X)` → `[X]` ✓
2. Remover padrões `push([...], X)` → `[..., X]` ✓
3. Deixar acumuladores `X = push(X, ...)` para agente de build ✓

**Resultados globais**:
- Arquivos processados: 154
- Ocorrências restantes: 3776 (`push`: 2701, `empty()`: 1075)
- Conversões automáticas bem-sucedidas: ~1141 removidas (~23% do total)

**Observação**: A maioria dos restantes (~90%) são acumuladores que exigem:
1. Contagem de iterações/items finais
2. Pré-alocação com tamanho conhecido
3. Substituição de `push(x, item)` por index-assign `x[i] = item`

Estes não são mecanicamente conversíveis — requerem análise de cada sítio.
