# Teko Lifecycle Modifiers Specification

## 📖 Modificadores de Ciclo de Vida

| **Modificador** | **Uso** | **Finalidade** | **Exemplo** |
|-----------------|---------|----------------|-------------|
| **[required](ca://s?q=Detalhar_required_em_Teko)** | Atributos obrigatórios | Garante que o valor seja sempre inicializado e nunca nulo | `Id : guid required` |
| **[optional](ca://s?q=Detalhar_optional_em_Teko)** | Atributos opcionais | Permite ausência de valor, representando nulabilidade controlada | `Telefone : string optional` |
| **[ref](ca://s?q=Detalhar_ref_em_Teko)** | Referência forte | Mantém vínculo direto entre objetos, garantindo ciclo de vida compartilhado | `Endereco : Endereco ref` |
| **[weak](ca://s?q=Detalhar_weak_em_Teko)** | Referência fraca | Evita ciclos de referência, não impede coleta de lixo | `Pai : Pessoa weak` |

---

## 📖 Regras Gerais

- Os modificadores são aplicados diretamente na **declaração dos atributos**.  
- `required` e `optional` controlam **presença e nulabilidade**.  
- `ref` e `weak` controlam **gestão de memória e ciclo de vida**.  
- Podem ser combinados com qualquer tipo (`record`, `class`, `struct`, `interface`).  
- O compilador valida automaticamente o uso correto dos modificadores.  

---

## 📖 Exemplo Consolidado

```teko
Cliente.class.teko
Id : guid required
Nome : string required
Telefone : string optional
Endereco : Endereco ref
Responsavel : Pessoa weak
