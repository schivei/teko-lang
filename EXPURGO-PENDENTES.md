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
