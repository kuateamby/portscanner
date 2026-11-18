# PortScanner 🔍

[🇬🇧 English](#-english) · [🇷🇺 Русский](#-русский)

---

## 🇬🇧 English

Fast multithreaded TCP/UDP port scanner & OS detector for Windows. C++17, single file, zero dependencies except `ws2_32.lib`. Async `connect()` engine with `WSAPoll()` batching — thousands of ports per second.

### Features

- ⚡ Async `connect()` + `WSAPoll()` — up to 4096 sockets per batch
- 🌐 TCP scan, 📡 UDP scan, 🖥️ passive OS detection (ports + banners)
- 🧵 Up to 128 threads, auto-tuned to CPU
- 📦 Only `ws2_32.lib` needed (built into Windows)

### Usage

```bash
portscanner.exe -P 192.168.1.1              # TCP scan (1-1024)
portscanner.exe -P 192.168.1.1 1 65535      # full TCP range
portscanner.exe -P -u 192.168.1.1 1 200     # UDP scan
portscanner.exe -O 192.168.1.1              # OS detection
```

### Example

```
Scanning 192.168.1.1 ports 1-1024 (1024 ports, 32 threads, batch=4096, mode=TCP)

[+] TCP 22 OPEN  (ssh)
[+] TCP 80 OPEN  (http)
[+] TCP 445 OPEN  (smb)
[+] TCP 3389 OPEN  (rdp)

Done. Open ports: 4 | Time: 87 ms | Speed: 11770 ports/sec
```


### Limitations

- ❌ No SYN scan
- ❌ No TTL-based OS fingerprint
- ❌ OS detection accuracy 55–95%

### ⚠️ Disclaimer

Use only against hosts you own or have explicit written permission to scan. Unauthorized scanning may violate laws (Art. 272 UK RF, CFAA, CMA). Author is not responsible for misuse.


## 🇷🇺 Русский

Быстрый многопоточный TCP/UDP сканер портов и определитель ОС для Windows. C++17, один файл, ноль зависимостей кроме `ws2_32.lib`. Асинхронный `connect()` с пакетной обработкой через `WSAPoll()` — тысячи портов в секунду.

### Возможности

- ⚡ Асинхронный `connect()` + `WSAPoll()` — до 4096 сокетов на пачку
- 🌐 TCP-скан, 📡 UDP-скан, 🖥️ пассивное определение ОС (порты + баннеры)
- 🧵 До 128 потоков, авто-подстройка под CPU
- 📦 Нужен только `ws2_32.lib` (встроен в Windows)

### Сборка

```bash
# MinGW
g++ -O3 -march=native -std=c++17 portscanner.cpp -o portscanner.exe -lws2_32

# MSVC
cl /O2 /GL /EHsc /std:c++17 portscanner.cpp ws2_32.lib
```

### Использование

```bash
portscanner.exe -P 192.168.1.1              # TCP-скан (1-1024)
portscanner.exe -P 192.168.1.1 1 65535      # весь TCP-диапазон
portscanner.exe -P -u 192.168.1.1 1 200     # UDP-скан
portscanner.exe -O 192.168.1.1              # определение ОС
```

### Пример

```
Scanning 192.168.1.1 ports 1-1024 (1024 ports, 32 threads, batch=4096, mode=TCP)

[+] TCP 22 OPEN  (ssh)
[+] TCP 80 OPEN  (http)
[+] TCP 445 OPEN  (smb)
[+] TCP 3389 OPEN  (rdp)

Done. Open ports: 4 | Time: 87 ms | Speed: 11770 ports/sec
```

### Ограничения

- ❌ Нет SYN-скана 
- ❌ Нет TTL-fingerprint
- ❌ Точность определения ОС 55–95%

### ⚠️ Отказ от ответственности

Используйте только против хостов, которыми владеете, или на которые есть письменное разрешение. Несанкционированное сканирование может нарушать закон (ст. 272 УК РФ, CFAA, CMA). Автор не несёт ответственности за misuse.

### First commit
