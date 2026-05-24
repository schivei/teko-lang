# Documento para o Agent — Análise da Linguagem Teko e Plano de Bootstrap

## Objetivo geral

O projeto **Teko** tem como objetivo criar uma linguagem nativa AOT, inicialmente compilada por um compilador bootstrap escrito em **C puro e portável**, sem dependências de sistema operacional no núcleo, para depois migrar o compilador para a própria linguagem Teko e alcançar **self-hosting**.

O C deve ser usado apenas como ferramenta inicial de bootstrap, não como implementação final permanente.

Fluxo desejado:

```plain text
Compilador bootstrap em C puro
  ↓
Compila subconjunto inicial da linguagem Teko
  ↓
Compilador escrito em Teko
  ↓
Compilador Teko compila a si mesmo
  ↓
C deixa de ser dependência principal
```


---

# 1. Princípios fundamentais

## 1.1 C puro apenas como bootstrap

O compilador em C deve implementar somente o necessário para compilar o primeiro compilador escrito em Teko.

Não deve tentar implementar a linguagem completa logo no início.

Objetivo do C:

```plain text
Lexer
Parser
AST
Name resolver mínimo
Type checker mínimo
Backend C
```


O compilador em C deve ser pequeno, portável e eventualmente descartável.

---

## 1.2 Core do compilador sem dependência de sistema operacional

O núcleo do compilador não deve depender de:

```plain text
stdio.h
dirent.h
unistd.h
mmap
threads do sistema
filesystem
terminal
processos externos
APIs POSIX
APIs Windows
```


O core deve operar apenas sobre bytes em memória.

Exemplo conceitual:

```c++
typedef struct TekoSource {
    TekoString path;
    TekoString type_name;
    TekoSourceKind kind;
    TekoString text;
} TekoSource;
```


A leitura de arquivos, listagem de diretórios e escrita em disco devem ficar em uma camada separada de plataforma/CLI.

Arquitetura recomendada:

```plain text
bootstrap/
  c/
    core/
      lexer
      parser
      ast
      diagnostics
      checker
      backend_c

    platform/
      filesystem opcional
      console opcional

    cli/
      executável de linha de comando opcional
```


---

## 1.3 Compilador core deve receber sources já carregados

O core não deve abrir arquivos diretamente.

Correto:

```plain text
TekoSource[] -> compilador -> TekoOutput[]
```


Incorreto:

```plain text
compilador abre diretório
compilador lê arquivo
compilador imprime erro no terminal
compilador chama gcc
```


A CLI pode fazer isso, mas o core não.

---

# 2. Estratégia de self-hosting

## 2.1 Fases propostas

```plain text
Fase 0 — Consolidar especificação mínima
Fase 1 — Implementar compilador bootstrap em C puro
Fase 2 — Gerar C portável como backend inicial
Fase 3 — Escrever compilador Teko usando Teko Core 0
Fase 4 — Bootstrap C compila compilador Teko
Fase 5 — Compilador Teko compila a si mesmo
Fase 6 — Evolução da linguagem passa a ocorrer em Teko
```


---

## 2.2 Processo de validação do self-hosting

```plain text
bootstrap-c compila compiler-teko -> stage1
stage1 compila compiler-teko      -> stage2
stage2 compila compiler-teko      -> stage3
```


Idealmente, os outputs de `stage2` e `stage3` devem ser equivalentes.

---

# 3. Divisão da linguagem em níveis

A linguagem completa já possui muitas features avançadas. Para não tornar o bootstrap em C grande demais, recomenda-se dividir a linguagem em níveis.

## 3.1 Teko Core 0

Subconjunto mínimo para bootstrap.

Deve ser suficiente para escrever:

```plain text
lexer
parser
AST
diagnostics
symbol table
type checker simples
backend C
```


## 3.2 Teko Core 1

Linguagem inicial mais confortável, já compilada pelo compilador escrito em Teko.

Pode incluir:

```plain text
records
structs completas
enums melhores
interfaces simples
generics fechados
nullable básico
ref básico
```


## 3.3 Teko Full

Linguagem completa planejada.

Pode incluir:

```plain text
classes completas
traits
dependency injection
meta-code
tests
benchmarks
pattern matching
yield
constraints avançadas
lifecycle completo
weak references
operator overload
```


---

# 4. Teko Core 0 recomendado

## 4.1 Extensões suportadas inicialmente

Para o bootstrap, suportar inicialmente:

```plain text
.teko
.struct.teko
.enum.teko
.static.teko
```


Adiar:

```plain text
.class.teko
.record.teko
.interface.teko
.trait.teko
.meta.teko
.test.teko
.spec.teko
.bench.teko
.extension.teko
.subtype.teko
```


---

## 4.2 Tipos mínimos

O Core 0 deve suportar:

```plain text
void
bool
byte
int
uint
long
ulong
usize
isize
ptr<T>
Slice<T>
```


Observação: `usize` e `isize` devem ser adicionados oficialmente à especificação, pois são essenciais para compilador, slices, offsets e tamanhos.

---

## 4.3 Declarações mínimas

Recomendado para Core 0:

```
fn nome(param: Tipo) -> Retorno {
}
```


Exemplo:

```
fn add(a: int, b: int) -> int {
    return a + b
}
```


Embora a linguagem final possa evitar `fn` em métodos, `fn` simplifica o bootstrap para funções livres.

---

## 4.4 Structs

```
struct Token {
    Kind : TokenKind
    Offset : usize
    Length : usize
    Line : uint
    Column : uint
}
```


Ou, seguindo o modelo por arquivo:

```plain text
Token.struct.teko
```


```
Kind : TokenKind
Offset : usize
Length : usize
Line : uint
Column : uint
```


Para o bootstrap, escolher uma forma e manter consistente.

---

## 4.5 Enums

```
enum TokenKind {
    Eof
    Invalid
    Identifier
    IntLiteral
    StringLiteral
}
```


Ou por arquivo:

```plain text
TokenKind.enum.teko
```


```
Eof
Invalid
Identifier
IntLiteral
StringLiteral
```


---

## 4.6 Controle de fluxo mínimo

Suportar:

```
if condition {
}

if condition {
} else {
}

while condition {
}

return expr
```


Adiar:

```plain text
for avançado
for in
for range
do while
while do
laços nomeados
break/continue condicionais
yield
```


---

## 4.7 Expressões mínimas

Suportar:

```plain text
literais
identificadores
chamada de função
acesso a campo
indexação
atribuição
operadores binários básicos
operadores unários básicos
```


Operadores mínimos:

```plain text
=
==
!=
<
<=
>
>=
+
-
*
/
%
&&
||
!
.
[]
```


Operadores verbosos como `is`, `not`, `and`, `or` podem ser adicionados depois ou mapeados inicialmente com cuidado.

---

# 5. Modelo de arquivos da linguagem completa

A linguagem Teko usa fortemente o modelo:

> O nome do arquivo define o tipo.  
> A extensão define a categoria do tipo.

Exemplos:

```plain text
Cliente.class.teko      -> class Cliente
Pessoa.record.teko      -> record Pessoa
Ponto.struct.teko       -> struct Ponto
Cor.enum.teko           -> enum Cor
IRepositorio.interface.teko -> interface IRepositorio
Loggable.trait.teko     -> trait Loggable
String.extension.teko   -> extension para String
```


O parser deve saber o tipo do arquivo através de `TekoSourceKind`.

---

# 6. Extensões oficiais a consolidar

Há inconsistências que precisam ser resolvidas.

## 6.1 Interface

Aparecem:

```plain text
.interface.teko
.iface.teko
```


Recomendação:

```plain text
.interface.teko
```


Opcionalmente aceitar `.iface.teko` como alias legado.

---

## 6.2 Extension

Aparecem:

```plain text
.extension.teko
.ext.teko
```


Recomendação:

```plain text
.extension.teko
```


Opcionalmente aceitar `.ext.teko` como alias legado.

---

## 6.3 Subtype

Aparecem:

```plain text
.subtype.teko
.type.teko
```


Recomendação:

```plain text
.subtype.teko
```


Evitar `.type.teko` por ser genérico demais.

---

# 7. Modelo de membros, campos e propriedades

## 7.1 Regra de visibilidade por nome

A linguagem usa convenção de nome para visibilidade:

```plain text
PascalCase      -> público
camelCase       -> internal
_nome           -> privado
```


Interfaces devem ter membros sempre públicos.

---

## 7.2 Campos privados e propriedades

Decisão consolidada:

> Propriedades podem existir por convenção usando getter e setter.  
> Se um campo inicia com `_`, ele é privado e não pode ser getter/setter.

Regra formal:

```plain text
1. Membro iniciado com "_" é privado.
2. Membro iniciado com "_" é sempre storage privado ou método privado.
3. Membro iniciado com "_" não pode declarar get/set.
4. Membro de dados sem "_" é propriedade por padrão.
5. Propriedade pode ser auto-property ou custom-property.
6. PascalCase define propriedade pública.
7. camelCase define propriedade internal.
8. Interfaces não podem declarar membros iniciados com "_".
9. required só pode ser usado em propriedades, não em campos privados.
```


---

## 7.3 Campo privado válido

```
_nome : string
_saldo : decimal
_items : Slice<Item>
```


Esses campos:

```plain text
são privados
não têm getter/setter
não participam diretamente da API pública
não podem ser required
```


---

## 7.4 Propriedade automática

```
Nome : string
Saldo : decimal = 0
Id : guid required
```


Como não começam com `_`, são propriedades por padrão.

O compilador pode gerar storage interno oculto.

---

## 7.5 Propriedade customizada

```
_nome : string

Nome : string {
    get {
        return _nome
    }

    set {
        _nome = value
    }
}
```


Válido.

`Nome` é propriedade pública.

`_nome` é storage privado.

---

## 7.6 Exemplo inválido

```
_nome : string {
    get {
        return _nome
    }

    set {
        _nome = value
    }
}
```


Erro:

```plain text
Membro privado iniciado com "_" não pode declarar get/set.
```


---

## 7.7 Interface inválida

```
_id : guid
```


Erro:

```plain text
Interface não pode declarar membro privado.
```


---

# 8. Sintaxe canônica de membros

Há duas formas aparecendo nos documentos:

```
Nome : Tipo
```


e:

```
Tipo Nome;
```


Recomendação:

> Adotar oficialmente `Nome : Tipo`.

Para métodos:

```
calcularTotal(valor: decimal) -> decimal {
    return valor
}
```


A forma C-like deve ser tratada como legado ou experimental.

---

# 9. Generics

## 9.1 Regra principal

Teko usa generics fechados.

```plain text
Generics são sempre fechados.
Não existem tipos abertos.
O compilador gera tipos concretos.
```


Exemplo:

```
List<int>
Map<string, User>
```


Pode gerar internamente:

```plain text
List__int
Map__string__User
```


---

## 9.2 Aridade por nome do arquivo

A aridade vem do nome do arquivo:

```plain text
Resposta`1.record.teko
Par`2.struct.teko
Repositorio`1.class.teko
```


---

## 9.3 Header interno para nomes dos parâmetros

Apesar da aridade vir do arquivo, o header interno pode nomear parâmetros:

```
type<T>
where T : IComparable
```


Interpretação recomendada:

```plain text
Arquivo define aridade.
Header define nomes e constraints.
```


---

# 10. Constraints e `has`

A linguagem prevê constraints avançadas:

```
where T : interface not ITransient
where K : struct and not string
where T : class or record and not Disposable
where T : not (UserRecord, AdminRecord)
where T : has method GetNumber(string) -> int
where T : has { Content: string }
```


Também há assertividade inline:

```
if obj is { Somar(int, int) -> int } as calc {
    return calc.Somar(10, 20)
}
```


Recomendação:

```plain text
Não implementar no bootstrap C completo.
Adicionar no compilador Teko posterior.
```


---

# 11. Interfaces indiretas e sintéticas

A linguagem suporta compatibilidade estrutural inspirada em Go:

```plain text
Um tipo é compatível se possuir os métodos exigidos.
Não precisa declarar implements explicitamente.
```


Recomendação:

```plain text
implements pode existir como assertion explícita opcional.
Compatibilidade real pode ser estrutural.
```


Quando `has` é usado, o compilador pode criar interfaces sintéticas apenas em tempo de compilação.

Essas interfaces:

```plain text
não existem em runtime
não geram metadados obrigatórios
servem apenas para validação
```


---

# 12. Nulabilidade

Regra definida:

```plain text
Tipos são not-null por padrão.
Nullable usa ?.
```


Exemplos:

```
Nome : string
Telefone : string?
```


Regras:

```plain text
T para T?         -> permitido
T? para T         -> erro de compilação, salvo se validado
null para T       -> erro
null para T?      -> permitido
```


Acesso direto sobre nullable pode inserir check runtime, mas isso precisa ser formalizado.

Recomendação:

```plain text
Core 0: sem nullable ou nullable muito limitado.
Core 1: nullable básico.
Teko Full: análise completa.
```


---

# 13. Lifecycle

Recursos definidos:

```plain text
required
default/init
?
ref
weak
init {}
finalize {}
on before
on after
on clone
clone
deep clone
```


## 13.1 `required`

```
Id : guid required
Nome : string required
```


Regra:

```plain text
required só vale para propriedades.
required não vale para campos privados iniciados com "_".
```


---

## 13.2 `init`

```
init {
    // inicialização interna
}
```


Chamado automaticamente pelo runtime/compilador.

---

## 13.3 `finalize`

```
finalize {
    // descarte
}
```


Usado para limpeza/descarte.

---

## 13.4 Clone

```
var clone = clone obj
var clone = clone obj with (OutraPropriedade: 3)
var deepClone = deep clone obj
```


Adiar para Teko Full.

---

# 14. Referências seguras

## 14.1 `ref`

`ref` representa referência segura:

```
Endereco : Endereco ref
```


Também pode ser usado em variáveis:

```
ref var nome = ent.Nome
```


Separar semanticamente:

```plain text
Tipo ref      -> tipo referência
ref var       -> binding local por referência
```


---

## 14.2 `weak ref`

Weak references:

```
Responsavel : Pessoa weak ref
```


Recomendação de sintaxe oficial:

```plain text
Tipo weak ref
```


Evitar variações como:

```plain text
Pessoa weak
weak Pessoa
weak ref Pessoa
```


A regra:

```plain text
weak ref é sempre nullable
não impede desalocação
vira null se o objeto for desalocado
```


---

# 15. Primitivos

A especificação atual inclui:

```plain text
sbyte
byte
short
ushort
int
uint
long
ulong
bigint
ubigint

float
double
decimal
decimal256
decimal512

char
string
binary

date
time
datetime
datetimez
timespan
guid

bool
ptr<T>
```


Adicionar:

```plain text
usize
isize
```


Motivo:

```plain text
necessários para compilador, slices, offsets, tamanhos e portabilidade
```


---

# 16. `ptr<T>`

`ptr<T>` existe para interoperabilidade externa.

Exemplo:

```
extern fn printf(fmt: ptr<char>, ...) -> int
```


Regras:

```plain text
ptr<T> não deve ter aritmética direta
pode ser comparado
pode ser passado para interop
não é o mesmo que ref
```


Para Core 0, `ptr<T>` ou `Slice<T>` serão importantes para manipular buffers.

---

# 17. Estruturas da linguagem completa

## 17.1 Record

```plain text
imutável
comparação por valor
bom para DTOs/value objects
```


## 17.2 Class

```plain text
mutável
identidade
ciclo de vida completo
ref/weak/init/finalize
```


## 17.3 Struct

```plain text
tipo de valor
copiado por valor
sem herança
```


## 17.4 Enum

```plain text
conjunto nomeado
from int32 por padrão
flags opcionais
```


## 17.5 Interface

```plain text
contrato formal
membros públicos
compatibilidade estrutural possível
```


## 17.6 Trait

```plain text
comportamento reutilizável
composição horizontal
implementações padrão
```


## 17.7 Meta

```plain text
metaprogramação
execução em tempo de compilação
configuração de compilação
```


## 17.8 Test

```plain text
testes unitários e integração
```


## 17.9 Extension

```plain text
adiciona métodos a tipos existentes
usa this implícito
```


---

# 18. Operadores

## 18.1 Operadores simbólicos tradicionais

```plain text
== !=
< <= > >=
+ - * / %
&& || !
```


## 18.2 Operadores verbosos

```plain text
is
not
and
or
```


Exemplo:

```
if usuario is not null and usuario.Idade > 18 {
}
```


---

## 18.3 Pattern matching estrutural

Planejado:

```
if pedido is { Valor: > 100, Status: "Pago" } {
}
```


Adiar para Teko Full.

---

## 18.4 Inline conditional expression

Planejado:

```
var resultado = "OK" if (status == 200) else "Erro"
```


Adiar para depois do Core 0.

---

## 18.5 Operadores especiais

Planejados:

```plain text
*x duplicação
/x inverso multiplicativo
^x exponenciação
~x raiz quadrada
```


E operadores aditivos:

```
x += 3
x -= 2
x *= 4
x /= 2
x ^= 2
x ~= 2
```


Atenção: `^` também aparece como XOR para flags/enums. Resolver semanticamente via tipo ou revisar símbolo.

---

## 18.6 Operator overload

Planejado:

```
operator ++(a: MinhaEntidade) -> MinhaEntidade {
}
```


Adiar para depois do self-hosting inicial.

---

# 19. Laços

A linguagem completa prevê:

```plain text
for
for in
for range
while
do while
while do
laços nomeados
break/continue com alvo
break/continue condicionais
```


Exemplo planejado:

```
continue if (cond)
continue loopName if (cond)
break if (cond)
break loopName if (cond)
```


Para Core 0:

```plain text
implementar apenas while
talvez break/continue simples
```


---

# 20. Yield

Planejado:

```
GerarNumeros() -> iterator<int> {
    yield 1
    yield 2
    yield 3
}
```


Exige transformação em state machine.

Adiar para Teko Full.

---

# 21. Backend inicial

O backend inicial deve gerar **C portável**.

Não gerar assembly inicialmente.

Motivos:

```plain text
evita ABI
evita ELF/Mach-O/PE
evita linker
evita registradores
evita calling conventions
acelera self-hosting
```


Pipeline inicial:

```plain text
Teko Core 0
  ↓
bootstrap C
  ↓
AST
  ↓
type checker mínimo
  ↓
C backend
  ↓
compilador C externo
```


---

# 22. Organização recomendada do repositório

```plain text
bootstrap/
  c/
    core/
      teko_base.h
      teko_string.h
      teko_memory.h
      teko_source.h
      teko_diag.h
      teko_token.h
      teko_lexer.h
      teko_parser.h
      teko_ast.h
      teko_checker.h
      teko_c_backend.h

    platform/
      teko_platform_file_stdio.c

    cli/
      teko_cli.c

compiler/
  teko/
    base.teko
    source.teko
    token.enum.teko
    lexer.static.teko
    parser.static.teko
    ast.struct.teko
    checker.static.teko
    c_backend.static.teko
    main.teko

spec/
  TEKO_CORE0.md
  TEKO_LANGUAGE.md
```


---

# 23. Arquivos de especificação a criar ou consolidar

## 23.1 Criar `TEKO_CORE0.md`

Deve definir:

```plain text
objetivo do Core 0
sintaxe suportada
tipos suportados
declarações suportadas
controle de fluxo
expressões
features proibidas
mapeamento para C
```


---

## 23.2 Criar ou consolidar `TEKO_LANGUAGE.md`

Deve unificar os documentos atuais:

```plain text
README.md
INITIAL_SPEC.md
add_spec.txt
assertion.txt
cycle.txt
refs.txt
teko-generics-spec.md
teko-lifecycle-spec.md
teko-primitives-spec.md
teko-structures-spec.md
```


---

# 24. Instruções diretas para o Agent

## 24.1 Antes de codificar

O agent deve primeiro:

```plain text
1. Consolidar TEKO_CORE0.md
2. Resolver nomes oficiais de extensões
3. Definir sintaxe canônica de campo/propriedade/método
4. Definir lista mínima de tokens
5. Definir gramática mínima do parser
```


---

## 24.2 Ao implementar o compilador C

O agent deve:

```plain text
1. Manter core sem stdio/filesystem
2. Usar TekoString com ponteiro + tamanho
3. Não depender de string terminada por \0 no core
4. Não usar ctype.h no lexer
5. Implementar classificação ASCII manual
6. Usar allocator injetável
7. Acumular diagnósticos em lista, não imprimir direto
8. Separar core, platform e cli
9. Gerar C como backend inicial
10. Manter bootstrap pequeno
```


---

## 24.3 O que não implementar no bootstrap C inicialmente

Não implementar de início:

```plain text
traits
DI
meta-code
yield
operator overload
pattern matching estrutural
generics avançados
constraints avançadas
weak ref
lifecycle completo
test runner
bench runner
LSP
backend nativo
linker
```


---

## 24.4 O que implementar primeiro

Ordem sugerida:

```plain text
1. Base portável
2. String/slice
3. Allocator
4. Diagnostics
5. Source model
6. Token definitions
7. Lexer
8. Parser Core 0
9. AST
10. Name resolver simples
11. Type checker mínimo
12. C backend
13. CLI opcional
14. Testes com exemplos Core 0
```


---

# 25. Decisões finais consolidadas

## 25.1 C é temporário

```plain text
C puro é apenas bootstrap.
Compilador real será escrito em Teko.
```


## 25.2 Core sem SO

```plain text
Compilador core não acessa filesystem nem terminal.
```


## 25.3 Tipos por arquivo

```plain text
Nome do arquivo define tipo.
Extensão define categoria.
```


## 25.4 Propriedades

```plain text
Membros sem "_" são propriedades por padrão.
Membros com "_" são storage privado.
Membros com "_" não podem ter getter/setter.
```


## 25.5 Visibilidade

```plain text
PascalCase público.
camelCase internal.
_nome privado.
```


## 25.6 Generics

```plain text
Generics fechados.
Aridade no nome do arquivo com backtick.
Monomorfização.
```


## 25.7 Nulabilidade

```plain text
Not-null por padrão.
Nullable com ?.
```


## 25.8 Referências

```plain text
ref para referência segura.
weak ref para referência fraca nullable.
ptr<T> para interop.
```


## 25.9 Backend inicial

```plain text
Gerar C portável.
Backend nativo fica para depois.
```


---

# 26. Resumo executivo

A Teko já possui uma visão bem definida e ambiciosa:

```plain text
AOT
tipos por arquivo
propriedades por convenção
nulabilidade forte
generics fechados
constraints estruturais
safe refs
lifecycle controlado
traits
meta-code
test/bench integrados
```


Mas para alcançar self-hosting rapidamente, o caminho correto é:

```plain text
1. Definir Teko Core 0
2. Implementar compilador bootstrap mínimo em C puro
3. Gerar C portável
4. Escrever compilador em Teko Core 0
5. Compilar o compilador Teko com o bootstrap C
6. Fazer o compilador Teko compilar a si mesmo
7. Evoluir a linguagem completa dentro da própria Teko
```


A prioridade agora não é implementar todas as features, mas sim criar uma base mínima e sólida que permita abandonar a dependência do C o mais cedo possível.