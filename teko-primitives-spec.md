# Teko Primitives Specification

## 📖 Inteiros
- **[sbyte](ca://s?q=Detalhar_tipo_sbyte_em_Teko)** → 8 bits com sinal  
- **[byte](ca://s?q=Detalhar_tipo_byte_em_Teko)** → 8 bits sem sinal  
- **[short](ca://s?q=Detalhar_tipo_short_em_Teko)** → 16 bits com sinal  
- **[ushort](ca://s?q=Detalhar_tipo_ushort_em_Teko)** → 16 bits sem sinal  
- **[int](ca://s?q=Detalhar_tipo_int_em_Teko)** → 32 bits com sinal  
- **[uint](ca://s?q=Detalhar_tipo_uint_em_Teko)** → 32 bits sem sinal  
- **[long](ca://s?q=Detalhar_tipo_long_em_Teko)** → 64 bits com sinal  
- **[ulong](ca://s?q=Detalhar_tipo_ulong_em_Teko)** → 64 bits sem sinal  
- **[bigint](ca://s?q=Detalhar_tipo_bigint_em_Teko)** → inteiro arbitrário com sinal  
- **[ubigint](ca://s?q=Detalhar_tipo_ubigint_em_Teko)** → inteiro arbitrário sem sinal  

## 🔬 Flutuantes e Decimais
- **[float](ca://s?q=Detalhar_tipo_float_em_Teko)** → 32 bits precisão simples  
- **[double](ca://s?q=Detalhar_tipo_double_em_Teko)** → 64 bits precisão dupla  
- **[decimal](ca://s?q=Detalhar_tipo_decimal_em_Teko)** → 128 bits (default)  
- **[decimal256](ca://s?q=Detalhar_tipo_decimal256_em_Teko)** → 256 bits  
- **[decimal512](ca://s?q=Detalhar_tipo_decimal512_em_Teko)** → 512 bits  

## 📖 Texto e Binário
- **[char](ca://s?q=Detalhar_tipo_char_em_Teko)** → caractere único Unicode  
- **[string](ca://s?q=Detalhar_tipo_string_em_Teko)** → texto imutável  
- **[binary](ca://s?q=Detalhar_tipo_binary_em_Teko)** → sequência de bytes  

## ⏳ Tempo e Identificadores
- **[date](ca://s?q=Detalhar_tipo_date_em_Teko)** → apenas data (ano, mês, dia)  
- **[time](ca://s?q=Detalhar_tipo_time_em_Teko)** → apenas hora (hora, minuto, segundo, nanos)  
- **[datetime](ca://s?q=Detalhar_tipo_datetime_em_Teko)** → data/hora sem fuso  
- **[datetimez](ca://s?q=Detalhar_tipo_datetimez_em_Teko)** → data/hora com fuso explícito  
- **[timespan](ca://s?q=Detalhar_tipo_timespan_em_Teko)** → intervalo de tempo  
- **[guid](ca://s?q=Detalhar_tipo_guid_em_Teko)** → identificador único global  

## ✅ Booleanos
- **[bool](ca://s?q=Detalhar_tipo_bool_em_Teko)** → valores lógicos `true` e `false`  

## 🔗 Ponteiros (Interop)
- **[ptr](ca://s?q=Detalhar_tipo_ptr_em_Teko)** → ponteiro seguro para interoperabilidade externa  
  - Tipado: `ptr<int>`, `ptr<char>`, `ptr<byte>`  
  - Sem aritmética direta, apenas passagem e comparação  

---

## 📖 Tabela Comparativa

| **Tipo** | **Categoria** | **Tamanho** | **Sinal** | **Mutabilidade** | **Exemplo de Uso** |
|----------|---------------|-------------|-----------|------------------|--------------------|
| sbyte    | Inteiro       | 8 bits      | com sinal | mutável/imutável | `let x : sbyte = -5;` |
| byte     | Inteiro       | 8 bits      | sem sinal | mutável/imutável | `let y : byte = 255;` |
| short    | Inteiro       | 16 bits     | com sinal | mutável/imutável | `let z : short = -32000;` |
| ushort   | Inteiro       | 16 bits     | sem sinal | mutável/imutável | `let u : ushort = 65000;` |
| int      | Inteiro       | 32 bits     | com sinal | mutável/imutável | `let idade : int = 30;` |
| uint     | Inteiro       | 32 bits     | sem sinal | mutável/imutável | `let pos : uint = 100;` |
| long     | Inteiro       | 64 bits     | com sinal | mutável/imutável | `let saldo : long = -999999;` |
| ulong    | Inteiro       | 64 bits     | sem sinal | mutável/imutável | `let total : ulong = 999999;` |
| bigint   | Inteiro       | arbitrário  | com sinal | mutável/imutável | `let grande : bigint = 12345678901234567890;` |
| ubigint  | Inteiro       | arbitrário  | sem sinal | mutável/imutável | `let enorme : ubigint = 99999999999999999999;` |
| float    | Flutuante     | 32 bits     | com sinal | mutável/imutável | `let pi : float = 3.14;` |
| double   | Flutuante     | 64 bits     | com sinal | mutável/imutável | `let raiz : double = 2.71828;` |
| decimal  | Decimal       | 128 bits    | com sinal | mutável/imutável | `let preco : decimal = 199.99;` |
| decimal256 | Decimal     | 256 bits    | com sinal | mutável/imutável | `let taxa : decimal256 = 0.000000000123;` |
| decimal512 | Decimal     | 512 bits    | com sinal | mutável/imutável | `let constante : decimal512 = 6.626e-34;` |
| char     | Texto         | 16 bits     | n/a       | imutável         | `let letra : char = 'A';` |
| string   | Texto         | variável    | n/a       | imutável         | `let nome : string = "Elton";` |
| binary   | Binário       | variável    | n/a       | mutável/imutável | `let dados : binary = file.read("img.png");` |
| date     | Tempo         | 32 bits     | n/a       | imutável         | `let aniversario : date = 1988-05-23;` |
| time     | Tempo         | 32 bits     | n/a       | imutável         | `let horario : time = 14:30:00;` |
| datetime | Tempo         | 64 bits     | n/a       | imutável         | `let evento : datetime = now;` |
| datetimez| Tempo         | 64 bits+fuso| n/a       | imutável         | `let evento : datetimez = now("America/Sao_Paulo");` |
| timespan | Tempo         | 64 bits     | n/a       | imutável         | `let duracao : timespan = evento - aniversario;` |
| guid     | Identificador | 128 bits    | n/a       | imutável         | `let id : guid = new guid();` |
| bool     | Booleano      | 1 bit lógico| n/a       | mutável/imutável | `let ativo : bool = true;` |
| ptr      | Ponteiro      | dependente  | n/a       | mutável/imutável | `extern fn printf(fmt: ptr<char>, ...) -> int;` |

---
