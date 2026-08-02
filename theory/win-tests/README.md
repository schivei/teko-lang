# theory/win-tests — localizar o travamento de ~1h do Windows nos testes

Enquadramento do dono, 2026-08-02:

> *"O Windows ainda esta com problemas, fica quase 1h em execucao sem log algum nos testes.
> Para o Windows, use uma theory/** com um CI personalizado e um agente para corrigir."*

## Por que "sem log"

O passo do `pr.yml` (`test / windows-x86_64`) roda:

```bash
"$art" test . > teko-test.log 2>&1
rc=$?
cat teko-test.log        # <- so imprime DEPOIS que o processo termina
```

Se `teko.exe test .` trava, o `cat` nunca chega, e o `timeout-minutes: 60` mata o job sem
uma linha. **A invisibilidade e do redirecionamento, nao do teste** — o teste pode estar travando
num ponto especifico e ninguem ve qual.

## O que este CI faz de diferente

`.github/workflows/theory-win-tests.yml` roda `teko test` em **ESTAGIOS**, com um **marcador
impresso ANTES e DEPOIS de cada** e um **timeout curto por estagio**. Assim o ponto de travamento
aparece mesmo que o teko bufferize a saida: o ultimo marcador impresso nomeia o estagio que travou.

Estagios (cada um com seu proprio timeout):
1. `--version` — o binario sobe?
2. suite unitaria isolada (se der para invocar so ela)
3. tier de regressao isolado
4. um unico projeto de regressao por vez, para bissectar

## A hipotese a testar primeiro

O Windows usa named pipes / mailslots para os canais (ver `theory/canais-nativos`). O gate de teste
e SHARDED (`TEKO_TEST_JOBS`) e o tier de regressao roda **4 filhos por omissao**
(`REGR_JOBS_DEFAULT = 4`, `src/build/regression.tks:242`). Um deadlock de pipe entre pai e filho no
Windows explicaria "vivo mas sem progresso". **Testar com `TEKO_TEST_JOBS=1` e `TEKO_REGR_JOBS=1`**
e o primeiro experimento: se destrava, o bug e na orquestracao de processos no Windows, nao num teste.

## Regra

Isto e `theory/**`: sandbox, sem promocao. O CI aqui e do integrador. O agente corrige com base no
que o integrador COLETA do CI e repassa — o agente NAO ve o CI e NAO espera por ele.
