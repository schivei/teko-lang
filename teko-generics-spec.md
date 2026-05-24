# Teko Generics Specification

## 📖 Convenção de Generics

- O **nome base** do tipo é definido pelo **nome do arquivo**.  
- O **backtick + número** (`1`, `2`, etc.) indica quantos parâmetros genéricos o tipo aceita.  
- A **extensão** define a natureza (`record`, `class`, `struct`, `interface`).  
- Não há sintaxe interna para declarar generics — apenas uso.  
- O compilador resolve sobrecarga de nome automaticamente.  

---

## 📖 Estruturas Genéricas

| **Estrutura** | **Arquivo** | **Finalidade** | **Exemplo de Uso** |
|---------------|-------------|----------------|--------------------|
| **[Record](ca://s?q=Detalhar_records_em_Teko)** | `Resposta\`1.record.teko` | Estrutura imutável com 1 parâmetro genérico | `let r1 : Resposta<int> = Resposta(Valor=200, Sucesso=true)`<br>`let r2 : Resposta<string> = Resposta(Valor="OK", Sucesso=true)` |
| **[Class](ca://s?q=Detalhar_classes_em_Teko)** | `Repositorio\`1.class.teko` | Estrutura mutável com 1 parâmetro genérico | `let repoInt : Repositorio<int> = new Repositorio()`<br>`repoInt.adicionar(10)` |
| **[Struct](ca://s?q=Detalhar_structs_em_Teko)** | `Par\`2.struct.teko` | Tipo de valor com 2 parâmetros genéricos | `let coordenada : Par<int,int> = Par(X=10, Y=20)` |
| **[Interface](ca://s?q=Detalhar_interfaces_em_Teko)** | `IService\`2.interface.teko` | Contrato com 2 parâmetros genéricos | `ServicoInt : IService<int,string> { executar(param: int) -> string { return $"Número: {param}" } }` |

---

## 📖 Regras Gerais

- **Arquivo + extensão** definem o tipo e sua natureza.  
- **Backtick + número** define a quantidade de parâmetros genéricos.  
- O compilador trata `Tipo``1`, `Tipo\`2`, etc. como **tipos distintos**.  
- Não existe sintaxe interna para declarar generics — apenas uso.  
- Essa convenção garante clareza e evita caracteres inválidos em nomes de arquivo.  

---
