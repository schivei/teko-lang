# Teko Structures Specification

## 📖 Estruturas Avançadas

| **Estrutura** | **Extensão de Arquivo** | **Finalidade** | **Exemplo** |
|---------------|--------------------------|----------------|-------------|
| **[Record](ca://s?q=Detalhar_records_em_Teko)** | `.record.teko` | Estruturas imutáveis, comparação por valor | **Pessoa.record.teko**<br>`Id : guid required`<br>`Nome : string`<br>`Idade : int` |
| **[Class](ca://s?q=Detalhar_classes_em_Teko)** | `.class.teko` | Estruturas mutáveis, ciclo de vida completo | **Cliente.class.teko**<br>`Id : guid required`<br>`Nome : string`<br>`Saldo : decimal`<br>`atualizarSaldo(valor: decimal) { Saldo += valor }` |
| **[Struct](ca://s?q=Detalhar_structs_em_Teko)** | `.struct.teko` | Tipos de valor, copiados por valor | **Ponto.struct.teko**<br>`X : int`<br>`Y : int` |
| **[Enum](ca://s?q=Detalhar_enums_em_Teko)** | `.enum.teko` | Conjunto de valores nomeados | **Cor.enum.teko**<br>`Vermelho`<br>`Verde`<br>`Azul` |
| **[Interface](ca://s?q=Detalhar_interfaces_em_Teko)** | `.interface.teko` | Contratos formais de implementação | **IRepositorio.interface.teko**<br>`salvar(entidade: object)`<br>`buscar(id: guid) -> object` |
| **[Trait](ca://s?q=Detalhar_traits_em_Teko)** | `.trait.teko` | Comportamentos reutilizáveis, com implementações padrão | **Loggable.trait.teko**<br>`logInfo(mensagem: string) { print("[INFO] " + mensagem) }` |
| **[Meta](ca://s?q=Detalhar_meta_em_Teko)** | `.meta.teko` | Metaprogramação e configuração de compilação | **Assembly.meta.teko**<br>`target : "net8.0"`<br>`optimize : true`<br>`author : "Elton"` |
| **[Test](ca://s?q=Detalhar_test_em_Teko)** | `.test.teko` | Testes unitários e de integração | **Cliente.test.teko**<br>`criarCliente() { let c = Cliente(Id=new guid(), Nome="Teste", Saldo=0) assert c.Nome == "Teste" }` |
| **[Extension](ca://s?q=Detalhar_extension_em_Teko)** | `.extension.teko` | Extender tipos existentes sem modificar definição original | **String.extension.teko**<br>`isNullOrEmpty() -> bool { return this == null || this == "" }` |

---

## 📖 Regras Gerais

- O **nome do tipo** é sempre definido pelo **nome do arquivo**.  
- A **extensão** define a **natureza da estrutura** (`record`, `class`, `struct`, etc.).  
- Métodos são declarados **sem palavra-chave** (`fn`, `def`, etc.), apenas pela assinatura ou corpo.  
- `trait` permite **composição de comportamento**.  
- `meta` controla **configuração e metaprogramação**.  
- `test` garante **qualidade e validação**.  
- `extension` adiciona **funcionalidades extras** a tipos existentes.  

---
