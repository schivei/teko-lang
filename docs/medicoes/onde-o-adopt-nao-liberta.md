# Onde o bloco escopado dá problema — evidências medidas

Pedido do dono, 2026-07-31: *"Traga-me evidências reais de onde pode dar problema e te aponto a
direção."*

Três sondas, todas na rota C, todas com `TEKO_ARENA_OBS` ligado. O `adopt { }` é a única forma de
bloco com escopo próprio que existe hoje — logo é o que se mede quando se pergunta o que um bloco nu
faria.

## As três medições

| o que se aloca DENTRO do `adopt` | quanto se alocou | recuperado ao fim do bloco |
|---|---|---|
| `str` por concatenação em ciclo (5000×) | **238,4 MB** (`MALLOC str`, 10 005 buffers) | **0,0 MB** |
| `[]No` com 200 000 structs nomeadas | **8,0 MB** (região **root**, 8 chunks) | **0,0 MB** |
| `str` construída lá dentro e atribuída a um binding de FORA | 14 bytes | **sobrevive intacta** ✔ |

O terceiro caso é a boa notícia e vale registá-la: **o escape não corrompe.** Li a `str` escapada
depois de 20 000 alocações posteriores (o amplificador clássico de use-after-free), com e sem
`TEKO_MEM_PARANOID=1`, e o valor e o comprimento vieram certos nas duas. Não há libertação de storage
vivo — porque não há libertação nenhuma.

E é esse o problema: em ambos os casos que alocam a sério, **a região do `adopt` foi largada e
recuperou zero**:

```
reclaimed by region drops: 0.0 MB   (1 of 2 regions dropped)
CHUNKS: 1 regions, 8 chunks, malloc'd cap 8.0 MB, used 8.0 MB
root (process-lifetime, never freed): 8.0 MB
```

**Os 8 MB foram para a `root`, não para o adotante.** A região abriu, fechou, e não tinha nada
dentro.

## Porquê — e são duas linhas do runtime

**1. `tk_alloc` está preso à raiz, e o comentário di-lo por escrito** (`teko_rt.c:1711`):

```c
void *tk_alloc(size_t n) {
    // (S1) Route through the process root region: bump-allocated, never dropped = today's
    // malloc-everywhere leak (M.5).
    …
    return tk_region_alloc(tk_region_root(), n);
}
```

O alocador geral **não consulta região nenhuma** — vai sempre à raiz. Tudo o que passa por ele é
irrecuperável por qualquer drop de região, esteja dentro de que bloco estiver.

**2. O crescimento de lista ACEITA uma região, e não a recebe** (`teko_rt.c:3424` e `:3559`):

```c
void *buf = (region == tk_g_root) ? tk_alloc(cap * esz) : tk_region_alloc(region, cap * esz);
```

O ternário existe: se lhe passarem uma região não-raiz, ela é usada. **Na sonda, os 8 MB foram para a
raiz — logo o codegen não passou o adotante ao crescimento da lista.** A capacidade está no runtime e
não é exercida pelo emissor.

**3. E a `str` não vive em região de todo.** Os 238,4 MB aparecem na linha `MALLOC str total`, um
contador separado, fora da árvore de regiões. Nenhum drop de região a pode alcançar — nem hoje, nem
com um bloco nu, nem com o predicado alargado.

## O que isto significa para as três propostas em cima da mesa

| proposta | o que a medição diz |
|---|---|
| **bloco nu `{ }` reusando o `emit_adopt`** | **não resolve nada hoje** — o `adopt` já abre e larga região, e recupera 0,0 MB. Ganhava-se sintaxe, não memória |
| **alargar o predicado `Named`** | necessário, **e insuficiente sozinho**: mesmo qualificando, a alocação vai por `tk_alloc` → raiz |
| **rotear as alocações para a região envolvente** | **é aqui que está o ganho**, e o ternário do `:3424` mostra que metade do caminho já existe |

## O que NÃO medi, e é onde eu pararia antes de propor

1. **A rota nativa.** Tudo isto é rota C. O oráculo exige as duas medidas antes de se invocar.
2. **O que acontece se a rota passar a funcionar.** Hoje o escape é seguro *porque nada é libertado*.
   No dia em que a lista for alojada no adotante, a `str` escapada do caso 3 passa a ser uma
   libertação de storage vivo — **e o caso 3 passa de prova de segurança a prova de defeito.** É a
   inversão que qualquer proposta aqui tem de trazer, e não é opcional.
3. **`tk_alloc` tem 3,48 M chamadas no build do compilador** e é a fronteira: mudá-lo mexe em tudo ao
   mesmo tempo. Não medi o custo de lhe dar uma região corrente.

Sondas em `/tmp/.../esc` — reprodutíveis com `TEKO_ARENA_OBS=<path> ./out/esc`.
