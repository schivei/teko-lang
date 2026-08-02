# Fechar o LINK do self-host nativo — os dois gaps pré-existentes do backend (0.3.1.0)

Arquiteto, 2026-08-02. Worktree `/home/user/wt-link-nativo`, ramo
`cargo/0.3.1.0-link-nativo-arq` (de `origin/fix/union`, HEAD `e399db23` = memória-por-escopo C1-C3).
Documento de DESENHO — nenhuma linha de produto aqui. `bootstrap/teko.c` e `teko_rt.{c,h}` são
saída/exceção. Regra do dono honrada: **proposta com arquivo:linha, não contra-argumento; alarme só
se provado.**

O fix de memória (C1-C3, `e399db23`) destravou a COMPILAÇÃO nativa: os 5034 fns compilam, o pico
caiu de ~15,8 GB (OOM) para 8,06 GB, e o objeto emitido é byte-idêntico entre dois emits. O objeto
**não linka** por dois gaps do backend nativo que só ficaram alcançáveis agora (o OOM batia antes
deles). Este documento diagnostica ambos com arquivo:linha, dá a correção law-first de cada, o
portão (fixpoint BINÁRIO), os crumbs ordenados com colisões, e a relação com o endgame "sem C".

---

## 0. Enquadramento — os dois gaps são independentes e de naturezas diferentes

- **Gap (a) — relocation incompatível com PIE.** Um problema de *tipo de relocation*: o linker
  default é PIE (`build_cc_argv` não passa `-no-pie`, `project.tks:1008-1044`), e o `.text`/`.rodata`
  emitido carrega ao menos uma relocation que o `ld` recusa sob PIE. É INDEPENDENTE dos símbolos
  indefinidos: `-no-pie` fá-lo linkar mesmo com o gap (b) presente, o que PROVA que (a) é uma questão
  de position-independence, não de resolução de símbolo.
- **Gap (b) — símbolos `teko::mem::*` indefinidos.** Um problema de *lowering ausente*: `free`,
  `region_new` e `region_alloc` recebem do checker `call_ns == "teko::mem"` e, no `call_symbol` do
  backend nativo, caem no MANGLER em vez de serem interceptados como builtins — emitindo referências a
  `teko_teko__mem__free` / `teko_teko__mem__region_new` / `teko_teko__mem__region_alloc`, símbolos que
  função nenhuma define. A rota C intercepta-os type-directed; o nativo não.

Fecham em qualquer ordem, mas o portão só verde quando OS DOIS fecharem (o link precisa de ambos).

---

## 1. Gap (a) — a relocation que o `ld` recusa sob PIE

### 1.1 O que o backend emite hoje em `.text` (arquivo:linha)

O `.text` nativo carrega EXATAMENTE três formas de relocation, todas em
`src/backend/encode_x86_64.tks` + `src/backend/isel_x86_64.tks`:

| origem (isel) | instrução | RelocKindX86 | R_X86_64_* | psABI-PIE? |
|---|---|---|---|---|
| `select_call_x86` (`isel_x86_64.tks:1700`) → `encode_call_x86` (`encode_x86_64.tks:1092-1095`) | `call rel32` (`E8`) | `Plt32` | `R_X86_64_PLT32` | **OK** (o `ld` cria PLT p/ externo, resolve direto p/ local) |
| `select_global_addr_x86` (`isel_x86_64.tks:1195-1198`) → `encode_lea_rip_x86` (`encode_x86_64.tks:1048-1053`) | `lea dst,[rip+disp32]` | `Pc32` | `R_X86_64_PC32` | OK **se** o alvo for DEFINIDO-local (rodata do módulo) |
| `select_func_addr_x86` (`isel_x86_64.tks:1214-1217`) → `encode_lea_rip_x86` | `lea dst,[rip+disp32]` | `Pc32` | `R_X86_64_PC32` | **suspeito** p/ símbolo de FUNÇÃO externo (ver 1.3) |

O `Abs64` (`R_X86_64_64`) existe SÓ para relocations rodata-internas (`rodata_relocs_x86`,
`encode_x86_64.tks:2449-2454`; `elf_build_rodata_relas`, `objfile_elf.tks:544-553`) e está
honest-stopped em todo compile real (`encode_module_x86:2499` recusa `m.globals.len > 0`; o
doc-comment de `elf_build_rodata_relas` diz "Empty today ... never emitted in a real compile"). O
`movabs r64,imm64` (`push_imm64_x86`, `encode_x86_64.tks:222-230`) carrega inteiros SEM relocation.

### 1.2 As duas causas-raiz candidatas — e por que ambas apontam para RIP-relative-a-externo

O `ld` recusa uma relocation sob PIE por UM de dois motivos, e a mensagem distingue-os:

- **(A) Relocation ABSOLUTA (`R_X86_64_64/32/32S`) numa secção alocável não-gravável.** Precisa de
  fixup em load-time; sob PIE vira `DT_TEXTREL` (ou o `ld` recusa `R_X86_64_32` de todo). `-no-pie`
  fixa porque o endereço de carga passa a ser fixo. **No corpus atual não há absoluta em `.text`**
  (só as rodata-Abs64 honest-stopped), então (A) só arma se o self-host começou a EMITIR
  rodata-relocs — o que o emit byte-idêntico permite verificar (crumb LA1).
- **(B) `R_X86_64_PC32` contra símbolo que o `ld` não resolve estaticamente sob PIE.** Para um
  símbolo de DADOS externo/preemptível, o `ld` erra `relocation R_X86_64_PC32 against undefined
  symbol ... can not be used when making a PIE object; recompile with -fPIC`. `-no-pie` fixa porque o
  símbolo passa a ter endereço fixo alcançável. Para um símbolo de FUNÇÃO, o `ld` moderno (binutils
  ≥2.26) trata `R_X86_64_PC32` a função como `PLT32` e resolve — mas versões/caminhos há em que uma
  função tomada por ENDEREÇO (não chamada) via `PC32` ainda cai no mesmo erro.

**Correção de enquadramento honesta:** o enunciado nomeia "DT_TEXTREL", que é a assinatura de (A). A
análise estática diz que (A) exige uma rodata-reloc que o honest-stop deveria barrar — logo há um de
dois factos a MEDIR, e é barato: `readelf -r` + `readelf -d` do objeto que o self-host já emite. O
crumb **LA1** faz esse dump e PINA qual causa arma. O desenho abaixo dá a correção law-first para
CADA uma, para o implementador não esperar pela medição: a que a LA1 confirmar é a que se aplica; a
outra fica como scaffolding documentado.

### 1.3 A correção law-first — emitir PIE-compatível, NUNCA marcar o objeto non-PIE

O dono decretou o caminho nativo como o futuro toolchain-independente (Fase E, `own-backend-
architecture.md`) e a segurança-norte (`docs/memory/teko-security-north.md`) valoriza ASLR. Marcar o
objeto/binário non-PIE (passar `-no-pie` no `build_cc_argv`) **desliga ASLR de todo binário que o
compilador produz** — uma regressão de segurança global para contornar um bug de emissão localizado.
Além disso divergiria a linha de link entre a rota C (PIE) e a nativa (non-PIE), tornando o binário
nativo estruturalmente diferente do C num eixo que não é código. **Rejeitado.** A resposta é emitir
relocations que o PIE aceita — o que o backend já faz para calls (`PLT32`) e para rodata local
(`PC32`), estendido ao sítio que falta.

**Correção para causa (B) — a recomendada pela análise estática:**
`select_func_addr_x86` (`isel_x86_64.tks:1214-1217`) materializa o ENDEREÇO de uma função com
`RelocKindX86::Pc32`. Trocar para `RelocKindX86::Plt32` — a MESMA escolha que `encode_call_x86` já
faz para a chamada direta. Justificação psABI: `R_X86_64_PLT32` a um símbolo de função é a forma
canónica PIE-safe de referenciar uma função por PC-relative; o `ld` resolve-a direto quando a função
é local (sem stub PLT, endereço real) e via PLT quando é externa/preemptível — exatamente o
comportamento que os CALLS já provam funcionar neste mesmo objeto. É uma mudança de UMA linha, no
sítio que o enunciado nomeou. `select_global_addr_x86` (dados, `:1198`) fica em `Pc32`: rodata local
é definida-no-módulo e `PC32` a ela é PIE-safe.

**Correção para causa (A) — se a LA1 revelar uma rodata-Abs64 viva:**
Uma relocation `R_X86_64_64` numa secção rodata read-only é o que gera `DT_TEXTREL` sob PIE. A forma
PIE-correta de um ponteiro rodata→rodata (ou rodata→símbolo) é `R_X86_64_RELATIVE` (o loader soma a
base) — mas o `.o` NÃO emite dynamic relocs; num `.o` a escolha é: (i) manter `R_X86_64_64` e deixar
o `ld` convertê-la em `R_X86_64_RELATIVE` na secção `.data.rel.ro` (o `ld` re-secciona rodata com
relocs para um segmento gravável-após-relro — PIE-safe, sem `DT_TEXTREL`), o que exige que a secção
que HOSPEDA o ponteiro seja marcada `SHF_WRITE`-elegível (`.data.rel.ro`), NÃO `.rodata` pura; ou
(ii) materializar o ponteiro em runtime (um `lea rip`/`Pc32` no `.text` de init) em vez de o gravar
absoluto em rodata. Sítio a editar se (A) armar: `elf_build_rodata_relas`/`elf_section_names`
(`objfile_elf.tks:544-553`, `:567-574`) para emitir os ponteiros-com-reloc numa secção
`.data.rel.ro` em vez de `.rodata`. **Mas** o caminho honesto primeiro é confirmar que o honest-stop
de `encode_module_x86:2499`/`rodata_relocs` está mesmo a segurar; se está, causa (A) não arma e (B)
é a única — nesse caso esta sub-secção é scaffolding para o dia que rodata-relocs vivam.

### 1.4 A prova de que (a) não é dos CALLS nem da rodata-string comum

- Calls já usam `PLT32` (`encode_x86_64.tks:1095`), PIE-safe — não são o gap. Confirmado: o mesmo
  objeto que falha o link tem milhares de calls e o `-no-pie` (que não muda calls) fá-lo linkar,
  logo o gap não está nos calls.
- Strings-literais materializam o endereço via `lea rip`/`Pc32` a um símbolo rodata LOCAL
  (`global_addr_inst`, `lower.tks:1361`/`:10519`/`:10608`), definido-no-módulo — `PC32` a definido-
  local é PIE-safe. Não é o gap.

---

## 2. Gap (b) — os `teko::mem::*` não lowerados no nativo

### 2.1 O diagnóstico exato (arquivo:linha)

`type_mem_free` (`checker/typer.tks:913-933`), `type_region_new` (`:956-964`) e `type_region_alloc`
(`:986-1001`) carimbam o `TCall` com `call_ns = "teko::mem"` (linhas 933, 964, 1001). No backend
nativo, `call_symbol` (`lir/lower.tks:4304-4321`) só consulta a resolução-de-builtin
(`native_builtin_symbol`, `assert_seed_symbol`) QUANDO `c.call_ns.len == 0` (linha 4308). Com
`call_ns == "teko::mem"` (≠ vazio), salta esse ramo, falha o `find_extern_symbol` (não há `extern
fn`) e cai em `mangle_fn_symbol(c.call_ns, last, flat)` (linha 4319):

- `mangle_fn_symbol("teko::mem", "free")` = `"teko_"` + `mangle_ns("teko::mem")`=`"teko__mem"` +
  `"__"` + `"free"` = **`teko_teko__mem__free`** (`lower.tks:885-902`, `TEKO_USER_PREFIX="teko_"`,
  `:856`).
- idem `teko_teko__mem__region_new`, `teko_teko__mem__region_alloc`.

Nenhuma função Teko define esses símbolos ⇒ `ld: undefined reference`. A rota C intercepta os três
ANTES do resolver, por `mem_op = segs[-2]=="mem"` (`codegen/codegen.tks:4161-4170`):
`free`→`emit_mem_free`, `region_new`→`(uintptr_t)tk_region_new(tk_region_root())`,
`region_alloc`→`emit_region_alloc`. O nativo não tem interceção equivalente.

### 2.2 O INVENTÁRIO COMPLETO de `teko::mem::*` — C lowera vs nativo lowera

| builtin | assinatura (checker) | rota C (codegen.tks) | rota nativa (lower.tks) | alcançável no self-host? |
|---|---|---|---|---|
| `free` | `(T) -> void` (T = `[]E` \| class \| region-handle) | `emit_mem_free` type-directed (`:3932`) | **AUSENTE → undefined** | **SIM** (typer, resolve, spine, assemble, arena) |
| `region_new` | `() -> uptr` | inline `tk_region_new(tk_region_root())` (`:4166`) | **AUSENTE → undefined** | **SIM** (arena.tks, via checker) |
| `region_alloc` | `(uptr, init:T) -> ptr<T>` | `emit_region_alloc` (`:3821`) | **AUSENTE → undefined** | **SIM** (arena.tks) |
| `buf_ptr` | `(u64) -> ptr<byte>` | `emit_buf_ptr` (`:3794`) | `lower_buf_ptr_call` (`:3423`) ✔ | sim (rawbuf) |
| `bytes_from_ptr` | `(ptr<byte>,u64) -> []byte` | `emit_host_ffi tk_bytes_from_ptr` | `lower_bytes_from_ptr_call` (`:3376`) ✔ | sim |
| `append_fo` | `([]byte,str) -> []byte` | inline (`:4203`) | `lower_append_fo` via `lower_call_fat` ✔ | sim (coverage, codegen) |
| `push_fo` | genérico `list::push` | `emit_list_push @fo` (`:4155`) | `is_list_builtin_call` → honest-stop N2 (`:4311`) | sim — **honest-stop hoje** (ver R2) |
| `str`/`str_of_bytes` | `([]byte) -> str` | `tk_str_of_bytes` (`:4221`) | `builtin_str_of_bytes_symbol` ✔ | sim |
| `as_cstr` | `(str) -> ptr<byte>` | `emit_as_cstr` (`:3861`) | **AUSENTE → unresolved_builtin_stop** | NÃO (só codegen def) |
| `str_from_c` | `(ptr<byte>,u64) -> str` | `emit_str_from_c` (`:3886`) | **AUSENTE → unresolved_builtin_stop** | NÃO |
| `region_buf` | `(uptr,u64,str) -> ptr<byte>` | `emit_region_buf` (`:3906`) | **AUSENTE → unresolved_builtin_stop** | NÃO |
| `region_drop` | (não é builtin exposto; `tk_region_drop` é primitiva interna) | — | — | N/A |
| `copy`/`alloc` | (não existem como builtin `teko::mem::*` — o issue lista-os como possíveis; não há assinatura em `scope.tks`) | — | — | N/A |

**Fronteira das duas colunas "AUSENTE":** as três de baixo (`as_cstr`/`str_from_c`/`region_buf`)
caem num honest-stop de COMPILE (`unresolved_builtin_stop`/`float_parse_result_stop` não; a rota é
`native_builtin_symbol`→null com `call_ns` vazio → `unresolved_builtin_stop`), então NÃO emitiriam
objeto — como o objeto emite, elas não são alcançadas no corpus (confirmado: só aparecem como
DEFINIÇÕES do emissor em `codegen.tks`, nunca como CHAMADAS em `src/**/*.tks`). As três de cima
(`free`/`region_new`/`region_alloc`) têm `call_ns` NÃO-vazio, então escapam ao honest-stop e emitem
uma referência a símbolo — daí o LINK falhar em vez do COMPILE parar. **Esta é a diferença que faz
(b) ser um erro de link e não de compile.**

**MUST-FIX-NOW (alcançável, quebra o link): `{free, region_new, region_alloc}`.**
**DESIGN-AHEAD (não alcançável, mas mesma classe): `{as_cstr, str_from_c, region_buf}`** — desenhadas
abaixo e deixadas como interceção que compila e honest-stop nomeado até um chamador existir.

### 2.3 A correção law-first — interceptar em `lower_call` ANTES do `call_symbol`, espelhando o C

O sítio já existe e o padrão já está estabelecido: `lower_call` (`lower.tks:2490-2502`) intercepta
`buf_ptr`/`bytes_from_ptr`/`last_index_of`/`str_from_utf8`/`err_loc`/`err_typed`/`f64_bitcast`/
`float_parse` por predicado de path ANTES de `call_symbol`. Acrescentar três interceções irmãs, cada
uma com o seu `is_*_call` + `lower_*_call`, espelhando a semântica exata do `emit_*` da rota C. A
chave de reconhecimento é `call_ns == "teko::mem"` + o nome do último segmento (não o path bare, pois
estes têm `call_ns` carimbado), o mesmo que a rota C usa (`mem_op = segs[-2]=="mem"`).

#### 2.3a `region_new` — o mais simples (mero forwarder)

`teko::mem::region_new() -> uptr` ⇒ `tk_region_new(tk_region_root())` como um `uptr`. A rota C emite
`(uintptr_t)tk_region_new(tk_region_root())`. No nativo, duas `LCall` encadeadas (exatamente o molde
de `lower_buf_ptr_call`, `:3423-3439`): chamar `tk_region_root()` → passar o resultado a
`tk_region_new(root)`. O resultado é um ponteiro/uptr num registo inteiro — nenhuma classe de
resultado especial, ao contrário de `float::parse`. **Reusa** as primitivas de runtime já
existentes (`teko_rt.h:149,153`); NÃO adiciona runtime.

#### 2.3b `region_alloc` — bump-aloca UM T e copia `init`

`teko::mem::region_alloc(region: uptr, init: T) -> ptr<T>`. A rota C
(`emit_region_alloc`, `:3821-3841`): `p = (T*)tk_region_alloc((tk_region*)region, sizeof(T)); *p =
init; p`. No nativo: lower do `region` (uptr) e do `init` (valor de T); `LCall tk_region_alloc(region,
size)` com `size = ltype_size(elem)` (o tamanho vem do `ptr<T>` no `e.type`, como o C lê `p.inner`);
depois um `store` de `init` no endereço devolvido; o resultado é o ponteiro. O `sizeof(T)` do nativo
é `ltype_size` do elemento (`lower.tks` já o computa para field-addr/alloca). O `store` reusa
`store_inst`/`lower` de escrita-em-endereço que o backend já tem (usado em box/struct-init).

#### 2.3c `free` — TYPE-DIRECTED, o de mais substância (e o mais chamado)

`teko::mem::free(x)` é `void` e ramifica no TIPO de `x` (o checker provou `x` ser variável mutável de
tipo heap). A rota C (`emit_mem_free`, `:3932-3960`) tem três braços:

1. **`[]E` (slice):** `tk_free_block((void*)x.ptr, x.len * sizeof(E))`, depois SCRUB `x = (slice){0}`.
2. **class (`Named` não-region-handle):** `tk_region_drop(x->__region)`, depois SCRUB `x = NULL`.
3. **region-handle (`Named` + `is_region_handle_name`):** `tk_region_drop_subtree((tk_region*)x.field)`,
   depois SCRUB `x.field = 0` (`emit_region_free`, `:3983-3993`).

O nativo tem de replicar OS TRÊS braços E o scrub. O scrub é uma escrita-de-volta ao LUGAR (lvalue)
de `x` — o nativo já sabe endereçar lugares (`lower_addr_of_place`, `:2790`; `lower_assign_*`,
`:12083-12402`). O plano de lowering, por braço:

- Slice: carregar `x.ptr` e `x.len` (field 0/8 do fat), computar `len*size`, `LCall
  tk_free_block(ptr, bytes)` (void), depois escrever o slice-vazio de volta ao lugar de `x` (dois
  stores: ptr=0, len=0 — ou reusar o caminho de atribuição de slice).
- Class: carregar `x->__region` (o campo region do objeto — o backend conhece o offset via layout),
  `LCall tk_region_drop(region)` (void), depois `x = null` (store 0 no lugar de `x`).
- Region-handle: ler o campo `uptr` do handle (nome via `checker::region_handle_field_name`, o MESMO
  probe que a rota C usa, `:3984`), `LCall tk_region_drop_subtree(field)` (void), depois `field = 0`.

**Primitivas de runtime — todas JÁ existem, ZERO adição:** `tk_free_block` (`teko_rt.h:1162`),
`tk_region_drop` (`:151`), `tk_region_drop_subtree` (`:152`). O trabalho é `.tks` de lowering, não de
runtime.

**Decisão de escopo do scrub (law-first, PARIDADE):** o scrub deve ser emitido no nativo, não
omitido. Razão: o diff C-vs-own compara o COMPORTAMENTO do compilador nas duas rotas até ao ponto de
paragem honesto; se o `free` nativo não fizer scrub, uma leitura pós-free (que o `#must_free`
permite em caminhos que o consume-dataflow não cobre) devolve o buffer parqueado em vez de
vazio/null, divergindo do C e podendo corromper `tk_free_block`'s free-list. O scrub é a semântica,
não um extra. É o que torna o braço `free` mais que um forwarder — e a razão de ele ser um crumb
próprio.

---

## 3. O PORTÃO — fixpoint BINÁRIO, o próximo nível acima do fixpoint de objeto

O fix pontual (C1-C3) provou byte-identidade do OBJETO entre dois emits. Agora que linka, o portão
sobe para o BINÁRIO linkado. Rotina (correção do dono; sob gen2 `TEKO_BACKEND=native`):

1. **Build gen2-native.** `teko . -o gen2 --no-verify --release` com `TEKO_BACKEND=native`. Tem de
   COMPLETAR (não parar em `undefined reference` nem em relocation-recusada). É a asserção principal
   (`scripts/native_dry_gate.sh`): o "known-stop" LEVANTA — atualizar o gate conforme o seu próprio
   texto manda quando `RC==0`.
2. **`teko test .` com gen2-native — verde** (as 4 falhas pré-existentes reproduzem idênticas na
   base; zero NOVAS).
3. **FIXPOINT BINÁRIO gen2 == gen3.** O gen2-native recompila-se a si → gen3; `cmp -s gen2 gen3`
   byte-a-byte. É o nível novo: o objeto já era idêntico (C1-C3), agora o BINÁRIO linkado tem de o
   ser — prova que nem o novo lowering de `mem::*` nem a troca de relocation introduzem não-
   determinismo no link. `scripts/fixpoint_gate.sh` já faz build_gen; estender o alvo dele para o
   binário nativo.
4. **Diff C-vs-own no ponto de paragem honesto** (`scripts/diff_c_own.sh`) inalterado — a rota C e a
   nativa concordam no output até à paragem. As fixtures assertam **STDOUT, nunca exit-code**.

**Pontos rituais (gate completo OBRIGATÓRIO):**
- o crumb que troca a relocation de `func_addr` (LA2) — pode mudar bytes emitidos, então FIXPOINT +
  diff C-vs-own + `native_dry_gate` na base vs mudança (assinatura de paragem IGUAL ou MAIS ADIANTE,
  nunca nova-no-mesmo-sítio).
- o crumb que liga o lowering de `free` (LB3) — é o de maior superfície semântica (scrub); FIXPOINT
  BINÁRIO + diff C-vs-own.
- o crumb final de integração (LC1) — o primeiro em que o link FECHA: ritual completo com o fixpoint
  binário como âncora de aceitação.

---

## 4. Os CRUMBS ordenados (com colisões e rituais)

Sequência bootstrap-segura. O seed é o `teko` lançado anterior; nenhum crumb usa feature fora do
seed (as primitivas de runtime — `tk_free_block`/`tk_region_*` — já existem no seed). Cada crumb é
gate-able isolado. Dois eixos independentes (LA = gap a, LB = gap b) que convergem em LC.

**L0 — [FEITO] commit vazio + push** (proteção contra restart). Sem código.

### Eixo A — a relocation PIE

**LA1 — MEDIR (dump de relocations do objeto que já emite).** `readelf -r obj.o` + `readelf -d`
(ou `-r` do `.o`, que não tem `.dynamic` — usar `readelf -r`/`objdump -r` no `.o` e depois tentar o
link e capturar a mensagem exata do `ld`). PINA a causa: (B) `R_X86_64_PC32 against <fn> ...` ou (A)
`DT_TEXTREL`/`R_X86_64_64` numa rodata viva. **Sem produto** (é medição). **Gate:** a mensagem do
`ld` gravada como âncora. Ritual: NÃO.

**LA2 — CORRIGIR a relocation.** Conforme LA1:
- se (B): `select_func_addr_x86` (`isel_x86_64.tks:1217`) `RelocKindX86::Pc32` → `RelocKindX86::Plt32`
  (uma linha). **Colisão:** `isel_x86_64.tks` (agentes de isel) — edição mínima, uma constante.
- se (A): `elf_build_rodata_relas`/`elf_section_names` (`objfile_elf.tks:544-574`) emitem os
  ponteiros-com-reloc em `.data.rel.ro` em vez de `.rodata`. **Colisão:** `objfile_elf.tks`.
Atualizar o teste de unidade do encode que fixa o kind (`encode_x86_64_test.tkt:87` usa
`Plt32`/`Pc32`/`Abs64` — o teste de `func_addr` que hoje espera `Pc32` passa a esperar `Plt32`).
**Gate — RITUAL:** `native_dry_gate` base-vs-mudança (assinatura ≥); FIXPOINT objeto; diff C-vs-own.

### Eixo B — o lowering de `mem::*`

**LB1 — `region_new` (forwarder).** `is_region_new_call` + `lower_region_new_call` em `lower.tks`,
interceção em `lower_call` (`:2490-2502`, ao lado de `is_buf_ptr_call`). **Colisão:** `lower.tks`
(MUITO quente — ver §5). Edição ADITIVA (novo predicado + nova fn + uma linha de interceção). **Gate:**
compila; um fixture `mem_region_new_ok` (STDOUT) verde. Ritual: NÃO (aditivo, mas ver §5-coord).

**LB2 — `region_alloc` (bump + store).** `is_region_alloc_call` + `lower_region_alloc_call`, mesma
interceção. Reusa `ltype_size`/`store`. **Colisão:** `lower.tks`. **Gate:** fixture
`mem_region_alloc_ok` (STDOUT). Ritual: NÃO.

**LB3 — `free` type-directed (o de substância).** `is_mem_free_call` + `lower_mem_free_call` com os
três braços (slice/class/region-handle) + o scrub via `lower_addr_of_place`/store. Reconhecimento por
`call_ns=="teko::mem"` + `last=="free"`; o braço escolhe por `arg.type` (`checker::Slice` /
`checker::Named` + `is_region_handle_name`). **Colisão:** `lower.tks` + LEITURA de
`checker::is_region_handle_name`/`region_handle_field_name` (já `pub`, usadas pela rota C). **Gate —
RITUAL:** fixtures `mem_free_slice_ok`/`mem_free_class_ok`/`mem_free_arena_ok` (STDOUT); FIXPOINT
BINÁRIO; diff C-vs-own.

**LB4 — [DESIGN-AHEAD, compila hoje] `as_cstr`/`str_from_c`/`region_buf`.** Não alcançados no corpus,
mas mesma classe. Interceção + `lower_*_call` espelhando `emit_as_cstr`/`emit_str_from_c`/
`emit_region_buf`, OU (mínimo bootstrap-safe) um honest-stop NOMEADO por builtin
(`unresolved` genérico → um stop específico "`as_cstr` not yet lowered (N2)") para o dia que um
chamador surja não cair num undefined silencioso. **Gate:** compila; nenhum chamador ⇒ FIXPOINT
trivial. Ritual: NÃO.

### Convergência

**LC1 — O LINK FECHA (integração).** Com LA2 + LB1-LB3, buildar gen2-native até ao BINÁRIO. É o
primeiro crumb em que o link tem sucesso. **Gate — RITUAL COMPLETO:** `native_dry_gate` RC==0
(known-stop levanta — promover o gate); `teko test .` verde; **FIXPOINT BINÁRIO gen2==gen3**; diff
C-vs-own. Se o gen2-native parar num NOVO honest-stop native-N1 mais adiante (degrau seguinte da
escada), isso é PROGRESSO — reportar, não é regressão.

---

## 5. Colisões — `lower.tks` é MUITO quente; a fronteira com memória-modelo e o fix pontual

`lower.tks` é editado por: (i) este trabalho (LB1-LB3, interceções aditivas de `mem::*`); (ii) o
modelo de memória por escopo — o C6/Tier-2 do `backend-memoria-por-funcao-0.3.1.md` (§C6, `:279-285`)
estende a janela escopada a `lower_function` e mexe na RESIDÊNCIA (separar setup global de
acumulação por-item em `lower_program`, `:12997-13022`); (iii) o modelo geral
`modelo-de-memoria-por-escopo-0.3.1.md`. **A regra é reusar, não duplicar; a fronteira:**

- **Este trabalho NÃO toca residência nem `lower_program`/`lower_function`.** As três interceções
  vivem em `lower_call` (`:2490-2502`) e em novas `fn` locais de lowering-de-chamada, ao lado das
  irmãs `buf_ptr`/`bytes_from_ptr`. São puramente ADITIVAS na zona de DISPATCH de chamada — a zona
  MENOS quente para o eixo-memória (que mexe em setup/residência, não em `lower_call`).
- **Reuso, não duplicação, das primitivas de região:** o lowering de `free`/`region_new`/
  `region_alloc` emite `LCall` para as primitivas de runtime JÁ existentes (`tk_free_block`,
  `tk_region_new`, `tk_region_alloc`, `tk_region_drop`, `tk_region_drop_subtree`, `tk_region_root`).
  NÃO introduz `tk_region_enter/leave` (essas são do eixo-memória C1, para o SCRATCH do backend, um
  conceito ortogonal ao `mem::*` do PROGRAMA compilado). A fronteira é limpa: `enter/leave` escopa a
  memória DO COMPILADOR; `free`/`region_*` lowera a memória DO PROGRAMA que o compilador compila.
- **Coordenação de sequência:** se o eixo-memória C6 aterrar primeiro e refatorar `lower_program`,
  as interceções de `mem::*` não colidem (zona diferente). Se este aterrar primeiro, C6 não toca
  `lower_call`. Ambos podem avançar em paralelo; o merge é trivial (zonas disjuntas). Documentado
  aqui para o integrador não serializar desnecessariamente.

Outras colisões: LA2 toca `isel_x86_64.tks` (uma constante) ou `objfile_elf.tks` — fora de
`lower.tks`, sem sobreposição com o eixo-memória. Os testes de unidade do encode
(`encode_x86_64_test.tkt`) atualizam junto com LA2.

---

## 6. A relação com o endgame "sem emissão de C" (CgMode / tk_emit_c)

Este fix é o penúltimo bloqueio do caminho crítico do endgame
(`docs/design/expurgo-do-c-e-a-busca-por-linker-0.3.1.md`). O que falta DEPOIS de o nativo linkar e
o fixpoint binário fechar, para remover a emissão de C:

1. **Rotear os DOIS sítios de emissão-de-C do motor de testes por `m.backend==Native`** (o item que
   o `backend-memoria` §6 nomeou e não projetou): `native_gate_build` (`project.tks:3549` →
   `codegen::tk_emit_c_test` + `run_cc`) e `build_regression_cov_exe` (`:5523` →
   `codegen::tk_emit_c_cov` + `run_cc`). Hoje NENHUM lê `m.backend`; `scripts/no_c_in_tests_gate.sh`
   já regista `run_native_gate` como "separately-scoped". Com o link nativo fechado, estes passam a
   poder emitir objeto nativo + `link_object` em vez de C+cc. É o próximo arquiteto.
2. **O runtime dos 11 símbolos vivos vira Teko** (`expurgo-do-c` §2-3): `teko_rt.tks` declara
   `extern fn write/_exit/abort` (o commit `afdb1fd8` já fez `extern fn` mirar o símbolo C, provado
   com `c_getpid`), e os 11 (`tk_print`, `tk_println`, …, `tk_i64_to_str`) tornam-se `exp fn`
   compiladas no próprio objeto. Sem runtime separado para linkar ⇒ sem runtime para embutir.
3. **O arena vai para Teko** (`tk_region_*`, `tk_arena_*`) — decreto do dono: "é projeto, não
   tradução". É o maior item residual e o gap (b) deste doc é o PRIMEIRO passo dele: o lowering de
   `free`/`region_*` que aqui se desenha é a prova de que a superfície de arena do PROGRAMA fecha no
   nativo, pré-requisito de mover a IMPLEMENTAÇÃO da arena para Teko.
4. **Remover `tk_emit_c`/`tk_emit_c_test`/`tk_emit_c_cov`/`tk_emit_c_export`** (`codegen.tks`) e o
   `run_cc`; a escada `seed→gen1` e o provisionamento de runtime no CI
   (`ci_provision_teko.sh:243-271` — não baixa `teko-bootstrap-src.tar.gz`) precisam de acerto para
   não quebrar no passo `cc`. Fora do escopo deste doc; nomeado.

**Este doc entrega o item (3)-passo-1 e desbloqueia o item (1).** Uma vez que o nativo linka e o
fixpoint binário fecha, o item (1) (rotear o motor de testes) é o próximo trabalho a jusante.

---

## 7. Assinaturas Teko que o implementador adiciona (full Javadoc — copiar verbatim)

Formas, não corpos. Estilo Javadoc completo (W15). Todas em `src/lir/lower.tks`, na zona de
interceção de `lower_call` (ao lado de `is_buf_ptr_call`/`lower_buf_ptr_call`, `:3398-3439`).

```teko
/**
 * is_region_new_call — reconhece `teko::mem::region_new`: o abre-região manual sem argumentos que
 * a rota C emite como `(uintptr_t)tk_region_new(tk_region_root())` (`codegen.tks:4166`). Carimbado
 * pelo checker com `call_ns == "teko::mem"` (`typer.tks:964`), por isso é reconhecido pelo par
 * (namespace, último segmento) e NÃO pelo path bare — ao contrário de `buf_ptr`, cujo `call_ns`
 * fica vazio. Interceptado em `lower_call` ANTES de `call_symbol`, senão cai no mangler e emite o
 * símbolo indefinido `teko_teko__mem__region_new`.
 *
 * @param c  a chamada tipada (o seu `callee` + `call_ns`)
 * @return   true sse `c` nomeia `teko::mem::region_new`
 * @since 0.3.1
 */
fn is_region_new_call(c: checker::TCall) -> bool

/**
 * lower_region_new_call — `teko::mem::region_new() -> uptr`: abre uma nova região-filha da raiz do
 * processo e devolve o seu handle opaco. Espelha o `emit`-inline da rota C
 * (`(uintptr_t)tk_region_new(tk_region_root())`, `codegen.tks:4166`) com duas `LCall` encadeadas,
 * no molde de `lower_buf_ptr_call` (`:3423`): `tk_region_root()` → `tk_region_new(root)`. O
 * resultado é um `uptr` num registo inteiro — sem classe de resultado especial. Reusa as
 * primitivas de runtime existentes (`teko_rt.h:149,153`); não adiciona runtime.
 *
 * @param ctx  o contexto de lowering
 * @param e    a expressão-chamada (a sua posição carimba as instruções; o seu tipo é `uptr`)
 * @param c    a chamada tipada (zero argumentos)
 * @return     o contexto avançado + o VReg do handle, ou um erro propagado
 * @since 0.3.1
 */
fn lower_region_new_call(ctx: LowerCtx, e: checker::TExpr, c: checker::TCall) -> Lowered | error

/**
 * is_region_alloc_call — reconhece `teko::mem::region_alloc`: bump-aloca UM `T` na região escolhida
 * pelo chamador (`call_ns == "teko::mem"`, `typer.tks:1001`). Interceptado em `lower_call` antes de
 * `call_symbol` pela mesma razão de `is_region_new_call`.
 *
 * @param c  a chamada tipada
 * @return   true sse `c` nomeia `teko::mem::region_alloc`
 * @since 0.3.1
 */
fn is_region_alloc_call(c: checker::TCall) -> bool

/**
 * lower_region_alloc_call — `teko::mem::region_alloc(region: uptr, init: T) -> ptr<T>`: bump-aloca
 * `sizeof(T)` bytes na `region` dada, copia `init` para lá e devolve o `ptr<T>`. Espelha
 * `emit_region_alloc` (`codegen.tks:3821`): `p = tk_region_alloc((tk_region*)region, sizeof(T)); *p
 * = init; p`. O `sizeof(T)` é `ltype_size` do elemento, lido do `ptr<T>` em `e.type` (como o C lê
 * `p.inner`). O store de `init` reusa o caminho de escrita-em-endereço do backend. Reusa
 * `tk_region_alloc` (`teko_rt.h:150`); não adiciona runtime.
 *
 * @param ctx  o contexto de lowering
 * @param e    a expressão-chamada (o seu tipo é `ptr<T>`, de onde vem o `T`)
 * @param c    a chamada tipada (args[0] = region uptr, args[1] = init)
 * @return     o contexto avançado + o VReg do ponteiro, ou um erro propagado
 * @throws     quando o `e.type` não é `ptr<T>` (invariante do checker) ou a aridade não é 2
 * @since 0.3.1
 */
fn lower_region_alloc_call(ctx: LowerCtx, e: checker::TExpr, c: checker::TCall) -> Lowered | error

/**
 * is_mem_free_call — reconhece `teko::mem::free`: a desalocação manual type-directed (`call_ns ==
 * "teko::mem"`, `typer.tks:933`). Interceptado em `lower_call` antes de `call_symbol`, senão emite
 * o símbolo indefinido `teko_teko__mem__free` — a causa medida do gap-de-link (b).
 *
 * @param c  a chamada tipada
 * @return   true sse `c` nomeia `teko::mem::free`
 * @since 0.3.1
 */
fn is_mem_free_call(c: checker::TCall) -> bool

/**
 * lower_mem_free_call — `teko::mem::free(x) -> void`: recupera o bloco heap de `x` e SCRUBA o
 * binding, ramificando no tipo de `x` (o checker provou ser variável mutável de tipo heap). Espelho
 * exato de `emit_mem_free`/`emit_region_free` (`codegen.tks:3932-3993`), os três braços:
 *
 *   * `[]E`  → `tk_free_block((void*)x.ptr, x.len*sizeof(E))`, depois `x = (slice){0}`;
 *   * class  → `tk_region_drop(x->__region)`, depois `x = null`;
 *   * region-handle (`is_region_handle_name`) → `tk_region_drop_subtree((tk_region*)x.<campo>)`,
 *     depois `x.<campo> = 0` (`<campo>` via `checker::region_handle_field_name`, o MESMO probe da
 *     rota C).
 *
 * O SCRUB é uma escrita-de-volta ao LUGAR de `x` (`lower_addr_of_place` + store), NÃO um extra: sem
 * ele uma leitura pós-free (que o `#must_free` permite em caminhos não cobertos pelo consume-
 * dataflow) devolveria o buffer parqueado, divergindo do C e corrompendo a free-list de
 * `tk_free_block`. Reusa `tk_free_block`/`tk_region_drop`/`tk_region_drop_subtree` (todas em
 * `teko_rt.h`); não adiciona runtime.
 *
 * @param ctx  o contexto de lowering
 * @param e    a expressão-chamada (posição; o seu tipo é `void`)
 * @param c    a chamada tipada (args[0] = o lugar mutável a libertar)
 * @return     o contexto avançado (sem VReg de resultado — void), ou um erro propagado
 * @throws     quando `arg.type` não é slice/class/region-handle (o checker deve ter barrado)
 * @since 0.3.1
 */
fn lower_mem_free_call(ctx: LowerCtx, e: checker::TExpr, c: checker::TCall) -> Lowered | error
```

Interceção em `lower_call` (`lower.tks:2490-2502`), ao lado das existentes:

```teko
    if is_region_new_call(c) { return lower_region_new_call(ctx, e, c) }
    if is_region_alloc_call(c) { return lower_region_alloc_call(ctx, e, c) }
    if is_mem_free_call(c) { return lower_mem_free_call(ctx, e, c) }
```

Funções existentes que o novo lowering TOCA (chama/lê, não edita): `lower_expr`, `ctx_alloc`,
`ctx_append`, `call_inst`, `two_args`, `ltype_size`, `lower_addr_of_place` (`:2790`), o store-em-
endereço; `checker::is_region_handle_name`, `checker::region_handle_field_name` (já `pub`). Editado
de facto para LA2: `select_func_addr_x86` (`isel_x86_64.tks:1217`) OU `elf_build_rodata_relas`
(`objfile_elf.tks:544`), conforme LA1.

---

## 8. Fixtures de regressão (input → STDOUT esperado; NUNCA exit-code)

Sob gen2 `TEKO_BACKEND=native`. Asserção de STDOUT (a lei das fixtures desta lane). Junto às fixtures
native existentes (`examples/regressions/`, molde `bmem_*` do `backend-memoria`).

| fixture | input | STDOUT esperado | o que prova |
|---|---|---|---|
| `mem_region_new_ok` | `let r = teko::mem::region_new(); println(...)` (uptr não-zero) | linha fixa (ex. `ok`) | LB1: `region_new` lowera para `tk_region_new(tk_region_root())`, não p/ símbolo indefinido |
| `mem_region_alloc_ok` | abre região, `region_alloc` de um struct, lê um campo, imprime | o campo escrito | LB2: bump+store; `sizeof(T)` correto; `init` copiado |
| `mem_free_slice_ok` | constrói `[]byte`, imprime, `mem::free`, reconstrói e imprime | as duas linhas | LB3 braço slice: `tk_free_block` + scrub; free-list reusa sem corromper |
| `mem_free_class_ok` | cria uma class heap, imprime um campo, `mem::free` | a linha | LB3 braço class: `tk_region_drop(__region)` + scrub null |
| `mem_free_arena_ok` | cópia standalone do shape `#must_free` single-`uptr` (como `arena.tks`); `region_new`→`region_alloc`→ler→`free` | o valor lido | LB3 braço region-handle: `tk_region_drop_subtree` + scrub 0; o round-trip completo do `Arena` |
| `mem_free_arena_leak` (NEGATIVO) | o mesmo shape mas SEM `free` num caminho | COMPILE-reject (`#must_free`) | o consume-dataflow do checker inalterado pelo novo lowering |
| `link_pie_ok` | qualquer `.tks` que exercite `func_addr` (uma closure sobre uma fn) | a saída da closure | LA2: o binário LINKA sob PIE (o teste é o link ter sucesso + rodar) |
| `self_host_link_fixpoint` | o próprio `src/` (self-build nativo) | gen2==gen3 BINÁRIO + link fecha | LC1: o portão — o link nativo fecha e é determinístico |

Os `mem_free_arena_ok`/`leak` reusam o shape de `src/mem/unsafe/arena.tks` numa cópia standalone (um
fixture é o seu próprio projeto e não pode `use` a stdlib do compilador), lowerando idêntico via
`is_region_handle_name`. A âncora de aceitação do portão é `self_host_link_fixpoint` (o link fecha) +
`native_dry_gate` RC==0.

---

## 9. Riscos e tensões de lei — com resolução

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — LA1 revela causa (A) e não (B)** | O desenho dá a correção de AMBAS (§1.3); a que a medição pinar aplica-se, a outra é scaffolding. O crumb LA1 é medição barata (`readelf`), não bloqueia o eixo B. Sem tensão — é sequência. |
| **R2 — `push_fo` honest-stop (N2) é alcançável?** | `is_list_builtin_call` manda `teko::list::*`/`mem::push_fo` para um honest-stop N2 (`lower.tks:4311-4313`). Se o corpus o alcança, é um TERCEIRO gap (família list-builtin genérica), FORA do escopo deste issue (que é `mem::*` de arena). Como o objeto EMITE, ou não é alcançado, ou já é interceptado por `lower_call_fat`. REPORTADO ao dono, não transformado em issue novo por mim. |
| **R3 — scrub do `free` muda bytes → quebra FIXPOINT** | O scrub é a SEMÂNTICA correta (paridade com C). O FIXPOINT BINÁRIO é gen2==gen3 (AMBOS com o novo lowering), não gen2-C==gen2-nativo — logo o scrub estar presente nos dois lados mantém o fixpoint. O diff C-vs-own compara COMPORTAMENTO, e o scrub é o que os IGUALA. Sem tensão. |
| **R4 — `-no-pie` seria mais simples** | REJEITADO law-first: desliga ASLR de todo binário produzido (regressão de segurança, `teko-security-north.md`) e diverge a linha de link C-vs-nativo num eixo não-código. A correção de relocation é localizada (1 linha) e psABI-correta. |
| **R5 — colisão em `lower.tks` com o eixo-memória** | Zonas disjuntas: este trabalho é aditivo em `lower_call` (dispatch); o eixo-memória mexe em `lower_program`/residência. Merge trivial; paralelizável. Documentado §5 para o integrador não serializar. |
| **R6 — Teko-only / Javadoc / runtime C** | Todo produto novo é `.tks` (lowering); snippets já em Javadoc completo. ZERO adição de runtime — reusa `tk_free_block`/`tk_region_*` existentes. Sem exceção-de-congelamento invocada. |
| **R7 — `as_cstr`/`str_from_c`/`region_buf` alcançados por um caller futuro** | Não alcançados hoje (só definições em `codegen.tks`). LB4 dá interceção-que-compila OU honest-stop nomeado, para nunca cair num undefined silencioso. Design-ahead entregue. |

**Nenhuma tensão de lei genuína permanece. Não há HALT.** O único ponto que precisa do DONO é uma
DECISÃO de sequência que reporto abaixo (não um bloqueio): se `push_fo`/família-list-genérica (R2)
for alcançada no self-host, é um terceiro gap adjacente — reportado, não convertido em issue por mim.
```
