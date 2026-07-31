# As formas dos canais nativos nas três plataformas

Levantamento pedido pelo dono em 2026-07-30, para além do caso dos testes: *"unix dá suporte
full-duplex, e no Windows tem suporte broadcast… quais as formas suportadas pelos canais nativos das
3 plataformas, e se há como dizer se quer uma conexão de mão dupla, broadcast, só leitura ou só
escrita"*.

**O que está MEDIDO nesta caixa é só Linux.** macOS e Windows são conhecimento e estão marcados como
tal — a distinção importa e já custou caro nesta lane.

## Medido (Linux, x86_64)

```
socketpair(AF_UNIX, SOCK_STREAM)   A->B ok, B->A ok          full-duplex: SIM
shutdown(fd, SHUT_WR)              write -> EPIPE
                                   par lê 0 (EOF)
                                   par AINDA escreve de volta
                                   e o primeiro AINDA LÊ      meia-direção: SIM
pipe()                             write na ponta de leitura -> EBADF   half-duplex: SIM
bind duplo ao mesmo nome           EADDRINUSE
uma sendto para o nome 1           leitor1: 1 byte, leitor2: -1
                                                              broadcast por nome: NÃO EXISTE
SOCK_SEQPACKET  3+5 -> 3 e 5       fronteira PRESERVADA
SOCK_STREAM     3+5 -> 8           fronteira PERDIDA (colou)
```

## A tabela das três plataformas

| capacidade | Linux | macOS | Windows |
|---|---|---|---|
| **full-duplex** | `AF_UNIX` **sim** (medido) | `AF_UNIX` sim *(conhecimento)* | pipe **nomeado** com `PIPE_ACCESS_DUPLEX` sim; pipe **anônimo** não *(conhecimento)* |
| **half-duplex** | `pipe()` **sim** (medido) | `pipe()` sim | pipe anônimo sim |
| **declarar só-leitura / só-escrita** | `shutdown(SHUT_RD/WR)` **em execução** (medido) | idem | `PIPE_ACCESS_INBOUND` / `OUTBOUND` **na criação** *(conhecimento)* |
| **fronteira de mensagem** | `SEQPACKET`/`DGRAM` **sim**, `STREAM` **não** (medido) | `DGRAM` sim, **`SEQPACKET` NÃO existe** *(conhecimento)* | `PIPE_TYPE_MESSAGE` sim; `AF_UNIX` de Windows é `STREAM`-only *(conhecimento)* |
| **broadcast (uma escrita, N leitores)** | **NÃO EXISTE** em `AF_UNIX` (medido) | não existe | **mailslot** `\\.\mailslot\` — a **única** primitiva de broadcast das três *(conhecimento)* |
| **muitos clientes** | um `listen`, N `accept` — conexões separadas | idem | pipe nomeado com N **instâncias** — conexões separadas, **não** broadcast |

## As duas coisas que a tabela desfaz

**1. "Muitas instâncias" não é broadcast.** Um pipe nomeado de Windows com `nMaxInstances` serve N
clientes, mas **cada conexão é separada** — para alcançar todos escreve-se N vezes. É fan-out de
*conexões*, não difusão de *mensagens*. O mesmo vale para `listen`/`accept` no POSIX.

**2. O broadcast de Windows existe, mas é outra primitiva e tem preço.** É o **mailslot**: uma
escrita em `\\*\mailslot\nome` chega a todos os servidores de mailslot com esse nome no domínio. É
**datagrama, não confiável**, e **a mensagem de broadcast é limitada a 424 bytes** *(conhecimento,
não medido)*. Não é um pipe com outra flag — é uma família à parte, sem equivalente POSIX.

## Como se declara a intenção

- **POSIX**: a direção é **retirada em execução** — cria-se full-duplex e fecha-se um lado com
  `shutdown`. Medido: depois do `SHUT_WR`, o par lê EOF **e continua a poder escrever de volta**.
- **Windows**: a direção é **declarada na criação** — `PIPE_ACCESS_DUPLEX` / `INBOUND` / `OUTBOUND`.
- **Consequência de desenho**: as duas exprimem a mesma intenção, em **momentos diferentes**. Uma
  superfície de linguagem que queira as três formas tem de decidir se o modo é parâmetro de criação
  (e então o POSIX simula com `shutdown` logo a seguir) ou operação (e então o Windows tem de o
  saber antes, e não pode).

## O que isto NÃO cobre

- **Passar descritor pelo canal** — `SCM_RIGHTS` no POSIX, **sem equivalente** em Windows
  (`DuplicateHandle` exige o PID do destino, é outro mecanismo).
- **N produtores em disputa** sobre o mesmo socket: não medido.
- **Broadcast em rede** (fora da máquina): mailslot é o único das três, e com o limite acima.
