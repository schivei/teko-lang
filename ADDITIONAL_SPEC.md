TEKO LANGUAGE SPECIFICATION (Draft)

====================================
1. File Header & Declaration Model
====================================
- Nome do arquivo define o tipo.
- Extensão define a categoria (.class.teko, .struct.teko, .record.teko, .interface.teko, .enum.teko, .delegate.teko, .trait.teko, .subtype.teko, .static.teko).
- Não há blocos obrigatórios {} exceto para métodos, init, finalize, getter/setter.
- Header define comportamento: visibilidade, herança, traits, implementações.

Exemplo:
MeuServico.class.teko
extends Servico implements IMeuServico
_message : IMessage
Message { get }
Process() -> void { _message.Send("Processing...") }

====================================
2. Visibility by Naming Convention
====================================
- PascalCase → público
- camelCase → internal (somente dentro do projeto)
- _snakeCamelCase → privado (restrito ao namespace)
- Interfaces → membros sempre públicos
- Não existem propriedades privadas

Exemplo:
_message : IMessage   // privado
Message { get }       // público
processData() -> void // internal

====================================
3. Abstract & Virtual
====================================
- abstract mantido apenas para classes e membros.
- getter/setter podem ser abstract.
- Membros são selados por padrão, override exige virtual.
- Tipos também são selados por padrão, herança exige virtual.

Exemplo:
getter MinhaPropriedade { return field / 3 }
abstract setter MinhaPropriedade
virtual Process() -> void { ... }

====================================
4. Properties
====================================
- Sintaxe simplificada: Nome : Tipo
- getter/setter definidos em blocos.
- Propriedades sem getter/setter se comportam como campos.
- Interfaces seguem padrão C#: { get; set; }

Exemplo:
MinhaPropriedade : int
getter MinhaPropriedade { return field }
setter MinhaPropriedade { field = value * 3 }

====================================
5. Initialization & Hooks
====================================
- Sem construtores com parâmetros.
- init {} chamado automaticamente pelo runtime.
- finalize {} para descarte.
- Hooks: on before, on after, on clone.
- Permitem decorar métodos, propriedades, inicialização e descarte.

Exemplo:
init { log info "Objeto inicializado" }
finalize { log info "Objeto destruído" }
on before Process { log info "Starting..." }
on after Process { log info "Finished." }

====================================
6. Object Initialization & Cloning
====================================
- Inicialização direta:
var obj = MinhaClasse(Propriedade: "valor")
- Clone raso:
var clone = clone obj
- Clone com alteração:
var clone = clone obj with (OutraPropriedade: 3)
- Clone profundo:
var deepClone = deep clone obj
- Hook on clone:
on clone {
  new.Id = Guid.New()
  this.CloneCount += 1
}

====================================
7. Required Properties
====================================
- keyword required para propriedades obrigatórias.
- Compilador exige inicialização na construção.
- Não há construtores, apenas inicialização declarativa.

Exemplo:
Nome : string required
Idade : int

var pessoa = Pessoa(Nome: "Elton", Idade: 40) // válido
var pessoa = Pessoa(Idade: 40)                // erro

====================================
8. Closed Generics
====================================
- Generics permitidos, mas sempre fechados.
- Não é permitido Type<> vazio.
- Não existem tipos abertos (dynamic ou object).
- Compilador gera tipos concretos (ListInt, MapStringUserRecord).
- Constraints avançadas: positivas, negativas, compostas, bloqueios.

Exemplo:
List<T> where T : IComparable and not ITransient
Map<K,V> where K : struct and not string

====================================
9. Advanced Constraints
====================================
- where T : interface not ITransient
- where K : struct and not string
- where T : class or record and not Disposable
- where T : not (UserRecord, AdminRecord)

====================================
10. Subtypes
====================================
- Definidos com .type.teko
- from tipos simples (string, int, struct).
- Não podem derivar de class, record, interface.
- Métodos e propriedades viram extensões exclusivas.

Exemplo:
Email.type.teko
from string
IsValid() -> bool = this.Contains("@")

====================================
11. Enums
====================================
- Definidos com .enum.teko
- Por padrão from int32.
- Podem ser flagged, compilador decide tamanho automaticamente.
- Proíbe duplicação de flags.
- Operadores bitwise nativos: |, &, ^, ~
- Propriedades nativas: HasFlag(), GetFlags(), CountFlags
- Extensões permitidas via extensions {}

Exemplo:
MeuEnumerado.enum.teko
flagged:
  Um,
  Dois,
  Três;

extensions {
  ToString() =>
    value switch {
      Um => "Um",
      Dois => "Dois",
      Três => "Três",
      _ => "Zero"
    }

  static MeuEnumerado FromString(string content) =>
    content switch {
      "Um" => Um,
      "Dois" => Dois,
      "Três" => Três,
      _ => Um
    }
}

Uso:
var estado = MeuEnumerado.Um | MeuEnumerado.Dois
if estado.HasFlag(MeuEnumerado.Um) { log info "Flag Um está ativa" }
var todas = estado.GetFlags() // retorna [Um, Dois]

====================================
12. Switch Expressions
====================================
- Sintaxe moderna inspirada no C#.
- Exemplo:
result = value switch {
  1 => "Um",
  2 => "Dois",
  _ => "Outro"
}

====================================
13. Method Declaration (No func Keyword)
====================================
- Métodos declarados diretamente, sem keyword func.
- Apenas nome, parâmetros e retorno.
- Visibilidade inferida pela convenção de nomes.
- Podem ser abstract ou virtual.
- Podem ter hooks.

Exemplo:
Process() -> void {
    _message.Send("Processing...")
}

Somar(int a, int b) -> int {
    return a + b
}
