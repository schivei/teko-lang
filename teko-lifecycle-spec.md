# Teko Lifecycle Modifiers Specification

## 📖 Modificadores de Ciclo de Vida

| **Modificador** | **Uso** | **Finalidade** | **Exemplo** |
|-----------------|---------|----------------|-------------|
| **[required](ca://s?q=Detalhar_required_em_Teko)** | Propriedades obrigatórias | Garante inicialização explícita pelo desenvolvedor | `Id : guid required` |
| **[default/init](ca://s?q=Detalhar_default_init_em_Teko)** | Propriedades sem required | Devem ser inicializadas no construtor (`init`) ou com valor default | `Saldo : decimal = 0` |
| **[?](ca://s?q=Detalhar_operador_nullable_em_Teko)** | Operador de nulabilidade | Permite ausência de valor, substitui o conceito de "optional" | `Telefone : string?` |
| **[ref](ca://s?q=Detalhar_ref_em_Teko)** | Referência forte | Mantém vínculo direto entre objetos, ciclo de vida compartilhado | `Endereco : Endereco ref` |
| **[weak](ca://s?q=Detalhar_weak_em_Teko)** | Referência fraca | Evita ciclos de referência, não impede coleta de lixo | `Responsavel : Pessoa weak` |

---

## 📖 Regras Gerais

- `required` força inicialização obrigatória.  
- Propriedades sem `required` devem ser inicializadas no `init` ou com valor default.  
- O operador `?` é usado para nulabilidade controlada.  
- `ref` e `weak` controlam ciclo de vida e gerenciamento de memória.  
- O compilador valida automaticamente o uso correto.  

---

## 📖 Exemplo Consolidado

```teko
Cliente.class.teko
Id : guid required
Nome : string required
Telefone : string?
Saldo : decimal = 0
Endereco : Endereco ref
Responsavel : Pessoa weak
