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

## Estratégia deste agente (mecanico)
1. Remover padrões `push(empty(), X)` → `[X]` (DONE)
2. Remover padrões `push([...], X)` → `[..., X]` (DONE)
3. Deixar acumuladores `X = push(X, ...)` para agente de build

Total: 367 → 74 conversões (293 removidas, 80% redução)
