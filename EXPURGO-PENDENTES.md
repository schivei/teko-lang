# Expurgo — sítios pendentes (posse/design ambíguo, não forçados)

Regra: sítio genuinamente ambíguo de posse/design NÃO é forçado; é anotado aqui
para o dono/integrador decidir (não é follow-up deferido, é in-wave).

## src/fmt/fmt.tks — buffer de SAÍDA do formatador
- `format(...)` acumula `out: []byte` byte-a-byte sobre os tokens (via `emit_str`,
  laços de `gap` de espaços, `out = push(out, b'\n')`). É exatamente o caso
  "BUFFER DE SAÍDA" (classe do `cb`/codegen dos 93%): o tamanho final NÃO é
  conhecido barato (depende da reformatação token-a-token).
- Converter a spread-em-laço seria "crescimento disfarçado" (proibido); pré-
  dimensionar exige uma passada extra somando `text.len`+espaços+quebras de todos
  os tokens (redesenho não-mecânico).
- O TOKENIZADOR (`toks`) e `collect_files`/`paths` são tratáveis (limite superior
  `source.len` / lista de arquivos), MAS deixá-los prontos com o `format()` ainda
  sujo não limpa o arquivo — aguardando a decisão do desenho do buffer de saída
  (mesmo desenho central do codegen `cb`).

## src/iter/{byte_iter,int_terminals,str_iter}.tks — collect de iterador lazy
- `collect_bytes`/`collect`/`collect_strs` acumulam de um ITERADOR (`src()`) de
  tamanho DESCONHECIDO e sem limite superior barato (o iterador é lazy/one-shot,
  então duas-passadas não é possível — consumir uma vez esgota). É o único caso em
  que crescimento dinâmico parece intrínseco; precisa de RULING do dono (ex.: um
  builder/segment-list interno, ou API de collect com capacidade dada pelo chamador).
- `str_iter::split_lines` é sobre entrada DIMENSIONADA (`bytes`), logo tratável
  (linhas ≤ bytes.len); mas o arquivo não fica limpo enquanto `collect_strs` estiver
  no mesmo módulo — segue junto do ruling de collect.

## src/compress/inflate.tks — buffer de SAÍDA da descompressão
- O `out`/`state.output` do inflate cresce à medida que os símbolos são decodificados;
  o tamanho descomprimido NÃO é conhecido a priori (é o próprio propósito do inflate).
  Mesma classe do `collect`: crescimento intrínseco, uma passada, sem limite superior
  barato. Precisa de ruling (ex.: buffer com dobra controlada / API com tamanho
  descomprimido conhecido do gzip ISIZE). As tabelas de Huffman internas são tratáveis,
  mas não limpam o arquivo sozinhas.
- (deflate/zlib/gzip — a COMPRESSÃO — foram convertidos: saída = cabeçalho + payload +
  checksum, tamanho calculável.)
