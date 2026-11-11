# Распаковщик F.E.A.R. 3 dsPack

Скрипт `dspack_unpack.py` быстро читает индексы dsPack и извлекает данные каждой записи из исходных смещений. Он поддерживает варианты `mgf ` (little-endian) и ` fgm` (big-endian), строит пути по таблице папок, не изменяет контейнер и создает `manifest.json` с исходными метаданными.

## Использование

Посмотреть индекс:

```bash
python3 dspack_unpack.py resources/Patch.dsPack --list
```

Извлечь содержимое в новый каталог:

```bash
python3 dspack_unpack.py resources/Patch.dsPack --extract /tmp/Patch-unpacked
```

Включить декодирование MiniPack для framed entries в новый каталог:

```bash
python3 dspack_unpack.py resources/Patch.dsPack --extract /tmp/Patch-decoded --decompress-minipack
```

Без флага `--decompress-minipack` режим остается консервативным: неизвестные compressed payload сохраняются как `.cmp`. С флагом decoder применяется только к size-different entries с валидным marker/frame. Blocks, для которых stored size равен expected output size, копируются как raw и явно отмечаются в manifest. Entries с незнакомым framing остаются `.cmp`.

Каталог назначения должен быть пустым или отсутствовать. Непустой каталог отвергается; уже существующие файлы никогда не перезаписываются. Имена проверяются на абсолютные пути и traversal (`..`), циклические ссылки родительских папок, выход за границы и symlink-компоненты каталога назначения. Повторяющиеся пути получают уникальный суффикс с индексом записи.

## Поведение с компрессией

Обычный режим извлекает **хранимые байты**, не пытаясь угадать алгоритм декомпрессии. В явно выбранном режиме `--decompress-minipack` известные MiniPack frames проходят decoder с ожидаемым output size и block bounds. Неизвестные/не framed compressed entries по-прежнему сохраняются с `.cmp`. Равные размеры сами по себе не означают отсутствия трансформации; raw passthrough разрешен только для block record внутри подтвержденного frame и отражается в manifest.

Ранние проверки стандартных гипотез `zlib`, raw DEFLATE, gzip, bzip2 и xz/LZMA не нашли точных декодирований. Сторонняя MiniPack-гипотеза тоже оказалась неполной. Позднее статическое исследование EXE позволило восстановить tree model и token grammar; результаты opt-in реализации и проверки приведены ниже. Публичные QuickBMS/экстракторы не считаются эталонной реализацией.

### Наблюдаемое framing блоков

У 11 153 сжатых записей из 31 835 по всем игровым контейнерам на смещении payload `+0x04` обнаружен маркер `c5 73 5a 11`; у оставшихся 20 682 записей такой валидный frame не обнаружен. В частности, он найден у 28/40 записей `Patch`, 198/446 `Preloads`, 337/2 248 `Boot`, 281/615 `DLC_MP1`, 462/767 `DLC_MP2` и 9 847/27 719 `Resource`. Всего таблицы описывают 94 325 блоков. Во всех 11 153 случаях проверены поля заголовка и таблицы:

| Смещение в payload | Наблюдение |
|---:|---|
| `+0x00` | Неизвестное 32-битное слово |
| `+0x04` | Маркер `c5 73 5a 11` |
| `+0x08` | Число блоков, big-endian |
| `+0x0c` | Полный выходной размер, big-endian; равен `unpacked_size` dsPack для всех 226 записей |
| `+0x10` | Неизвестное size-like слово; совпало с длиной первого блока только в 65/226 записей |
| `+0x14` | Размер блока `0x8000`; подтвержден во всех 11 153 валидных frames |
| `+0x18` | Длина таблицы big-endian; равна `число_блоков * 12` |
| `+0x1c` | Ноль во всех проверенных записях |
| `+0x20` | Строки таблицы по 12 байт: big-endian offset, length, неизвестное слово |

Число блоков во всех 11 153 случаях точно равно `ceil(output_size / block_size)`. Каждый блок кроме последнего соответствует 32 768 байтам ожидаемого выхода; последний покрывает остаток. Смещения блоков непрерывны, первый блок начинается после таблицы, последний заканчивается ровно на конце payload. Распаковщик теперь распознает и структурно валидирует этот framing во время извлечения, добавляет ожидаемый размер выхода каждого блока и записывает таблицу в `manifest.json` как `block_frame`. Просмотреть обнаруженные таблицы отдельно можно командой `python3 dspack_unpack.py resources/Preloads.dsPack --frames`. Поле строки таблицы намеренно названо `unknown_word`, а не checksum: алгоритм и область покрытия значения не установлены. Фрейминг не является декодером.

Пример: `Persistent.dsPreload` в `Preloads.dsPack`, запись 2: offset в архиве `0xb6c513`, stored size 34 652, unpacked size 118 320; 4 блока, table size `0x30`, payload-relative начало данных `0x50`. Строки `(offset, length, unknown_word)`:

```text
(0x50,   0x24e0, 0x8a5d6c3a)
(0x2530, 0x2428, 0x8e108869)
(0x4958, 0x242c, 0xec3cc734)
(0x6d84, 0x19d8, 0xd9e91518)
```

На этих и остальных блоках из `Patch`/`Preloads` второй независимый анализатор проверил 26 829 попыток: raw/zlib DEFLATE, gzip, bzip2, xz, LZMA-alone, raw LZ4, LZ4 frame, Zstandard frame и варианты LZ4 с 32-битным префиксом размера. Требовались точный ожидаемый размер выхода и полное потребление потока; точных совпадений не найдено. Отдельная проверка на всех 11 153 frames подтвердила структурную схему выше.

Дополнительно проверялись контрольные поля для всех 94 325 блоков во всех 13 архивах без декомпрессии. Для block `unknown_word` испытаны compressed block bytes, сама table row без поля, таблица блоков, frame header, header + table, полный frame, предыдущий/следующий block и смежные пары. Для dsPack `unknown_field` проверены хранимые payload всех 52 863 непустых записей, а для framed записей также header/table/full frame. IEEE CRC-32 и Adler-32 сравнивались как 32-битные значения и byte-swapped; CRC-16/CCITT-FALSE и XMODEM — как точные 16-битные значения в обеих byte order и в обеих половинах поля. **Точных совпадений не найдено.** CRC32C не проверялся (реализация отсутствовала); CRC-16/ARC/MODBUS не имеет результата, так как проверка превысила отведенное время. Это не устанавливает семантику unknown полей и не исключает другие алгоритмы/области. `codec` остается `unknown`, блоки не декодируются, поля намеренно не называются checksum.

### Статически найденный путь декодирования

После неудачного полного импорта PE в Ghidra код был локально изучен через `objdump` и RTTI/vtable-связи, без запуска EXE. PE32 x86 использует preferred image base `0x00400000`. RTTI `Despair::StreamCompression` находится по VA `0x015C4B04`, TypeDescriptor — `0x015C4AFC`; его vtable — `0x017C3584`. В vtable есть entry `0x64A6A0` (проверка/подготовка буферов), `0x64A860` (копирование выходных данных) и deleting-destructor entry `0x64AAB0`; constructor candidate `0x64A5B0` устанавливает эту vtable. Функция `0x64A280` проверяет номер 32-KiB блока, индексирует 12-байтовые записи, берет смещение и длину compressed block, выбирает размер последнего выхода как остаток общего размера и вызывает специализированный путь, когда поле объекта `+0x10` равно `0xc5735a11`. На этом пути вызывается `0x64AE90`.

RTTI `Despair::MiniPackFileSystem` находится по VA `0x015C4A88`, TypeDescriptor — `0x015C4A80`, vtable — `0x017C2DC4`. Constructor candidate `0x641930` устанавливает vtable, destructor candidate — `0x6412F0`, deleting-destructor entry — `0x6415E0`. Путь `0x641F00` выделяет объект размером `0x6C`, вызывает constructor и затем setup candidate `0x6413C0`. Строка `IO\\MiniPack` расположена по VA `0x017C2D64` и имеет прямые ссылки, в том числе, из `0x63FEB8`, `0x640672`, `0x640ADD` и `0x641F0A`; строка регистрации `+ Adding MiniPack file system OS(%s%c)` по VA `0x017C2D70` используется кодом около `0x640E18`.

PE содержит CodeView RSDS ссылку на `c:\FEAR3_code\Code\output\bin\Game_wf.pdb`, raw GUID bytes `0534be75b05047ac9f08632137127566` (canonical GUID `{75BE3405-B050-AC47-9F08-632137127566}`), age `26`; локальной копии этого PDB под `/home/drcerg` или в проекте нет. Поэтому source-level имена/типы для бинарника недоступны в текущем наборе файлов.

## Воспроизводимая модельная диагностика

`minipack_model.py` содержит bounded model builder, fast-lookup consistency check и token decoder. Decoder пока включается только явно через `--decompress-minipack`.

На всех семи blocks трех записей `Persistent.dsPreload` и четырех записей `UI/F2MenuTree.dsMenuFlow` получены 1024/1024 заполненных fast entries и ноль mismatches между fast-table lookup и медленным обходом построенного node tree; все node references указывают на созданные records. Число recursive calls/control bytes равно `N`, символов потреблено `N+1`. Заголовки `token_count/output_size/literal_offset` проходят проверки на тех же blocks. Суммарно проверено 7 моделей, 7 168 fast slots, 1 746 builder calls и 11 894 заявленных token iterations. Это верифицирует tree construction и fast/node consistency на игровых данных; успешный token/output decode все еще не получен.

В вызывающем методе `0x64A280` индекс блока берется из текущего output offset, деленного на block size; индекс выбирает 12-байтовую строку, из которой берется stored offset/length. До декодирования проверяется полное чтение указанной compressed длины. Ожидаемый output count равен полному 32-KiB block size, кроме последнего неполного блока, где используется `total_output_size % block_size`. Кешируемый output interval задается как `[block_index * block_size, block_index * block_size + out_size)`, после чего `0x64A860` копирует запрошенный slice из кеша вызывающему буферу. Это устанавливает ожидаемую длину/интервал, но не доказывает, что декодер успешно выдал корректное содержимое; checksum и семантика неизвестного третьего слова таблицы также не установлены.

Уточнение ABI специализированного вызова из `0x64A3E1` в `0x64AE90`: это stack-argument функция с пятью аргументами и обычным `ret` (caller очищает 20 байт). В порядке push передаются выбранная table entry `+4`, object `+0x34`, адрес stack local, object `+0x30` и адрес другого stack local; `ECX` тоже указывает на последний local, но его нельзя автоматически трактовать как C++ `this`. Внутри `0x64AE90` значения используются для model prefix, границы bitstream и инициализации reader, однако source-level роли двух object buffer fields не подтверждены. До token decoder вызываются `0x6569A0` и извлекаются три 32-битных поля из reader. На двух игровых образцах эти big-endian слова точно совпали с ожидаемыми значениями: token count, output size, literal-data offset. В `0x64AE90` token count маскируется глобалом `0x17C3AE8`, который статически равен `0xFFFFFFFF`; значит mask оставляет count неизменным. `0x64AAD0` уменьшает его раз за outer iteration. Это **не dsPack block count**, который используется только для выбора table row. Все три header-поля найдены на реальных blocks и проверены: смещение literal-data указывает на читаемые raw bytes, output size равен frame block output.

Уточнение по независимому статическому разбору: это **битовый decoder по сериализованному дереву**, не adaptive arithmetic/range coder. `0x64ae90` очищает модель, строит дерево, инициализирует reader и читает три 32-bit поля; `0x64aad0` декодирует токены. Helpers `0x6569f0` / `0x6569a0` читают endian-swapped 32-bit words, `0x656be0` ходит по дереву. В `0x64aad0` обнаружены четыре класса токенов:

| `token & 0xC0` | Операция | Поля |
|---:|---|---|
| `0x00` | Копировать literal-run | длина `token + 3` |
| `0xC0` | Дальний match | distance `((token << 8) | next_symbol) & 0x3FFF`; length `next_symbol + 6` |
| `0x80` | Ближний match | distance `token & 0x3F`; length `next_symbol + 6` |
| `0x40` | Match фиксированной длины | length `(token & 0x3F) + 6`; distance `next_symbol` |

Копирование match выполняется forward bytewise с поддержкой overlap. После `token_count` decoder читает дополнительный terminal literal count. При реализации этого хвоста есть отдельный caller-supplied source pointer (arg5 `0x64AAD0`), а обычные literal runs используют cursor, переданный по адресу arg1; эти указатели нельзя отождествлять только по high-level pseudocode. Текущий diagnostic decoder читает оба вида literal bytes из raw literal-data region по header offset, потому что так восстанавливаются ожидаемые outputs для tested blocks; отдельный статический аудит caller arg5 еще нужен, прежде чем утверждать идентичность source pointers для всех contexts. Bounds/длины ограничены ожидаемым block output.

### Проверка границы модель/битовый поток

Для записи `Persistent.dsPreload` (`Preloads.dsPack`, entry 2), block 0, статически проверено выравнивание модельного префикса с битовым reader:

- block начинается по абсолютному смещению архива `0xB6C563`, длина `0x24E0` (9 440 байт); первые байты `FE ED 29 2D 39 0F F2 96 ...`.
- `0x656B90` берет первый байт `n = 0xFE`; выражение возврата указывает на `block_start + 2*n + 2 = block_start + 0x1FE`.
- битовый поток начинается по смещению `0xB6C761`; первые 8 байт `00 00 07 A1 00 00 80 00`.
- reader разворачивает порядок байтов внутри 32-битных слов; два preload-слова получаются `0xA1070000` и `0x00008000`, initial 64-bit state `0x00008000A1070000`.
- до конца блока остается `0x22E2` байта, что позволяет reader сделать два 32-битных preloads.

Это согласуется с последовательностью вызовов в `0x64AE90`, но не доказывает успешную декомпрессию: конкретное дерево полностью не разобрано, символы не декодированы до проверенного содержимого, а ожидаемые 118 320 выходных байт не восстановлены. Размеры внутренних массивов (`0x2000` и `0x1000`) и параметр recursive builder (`n+0xFF = 0x1FD`) не выдаются за фактические числа узлов/листьев.

Проверка известных игровых записей не выявила ни одного подтвержденного выходного префикса/сигнатуры. `Script/luasense.lua`, `UI/F2MenuTree.dsMenuFlow`, `.dsworld` и `.dsMenu` дают полезные типовые гипотезы по имени, но не являются доказательством содержимого. Для Lua дополнительно возможна версия-зависимая bytecode форма. У двух коротких последних блоков записей `.dsPreload` stored size равен ожидаемому размеру блока (87 и 58 байт), и байты выглядят как читаемый путь; это не признак декодированного файла и не дает основания пропускать их через tree decoder как raw/decoded output.

Wrapper `0x656B90` читает первый байт модели `n`, вызывает recursive builder `0x656A30` с указателем `input+1` и девятью stack аргументами `(input+1, n+1, 0, context+0x2000, n+0xFF, n, context, 0, 0)`, передает `ECX=context`, записывает `n+0xFF` в `context+0x3000` и возвращает `input+2*n+2`. Внутри builder stack offsets на входе соответствуют `a1..a9` по порядку push; регистры сохраняют `a5` в `EDI`, `a7` в `ESI`, `a8` в `EBP`, `a9` в `EDX`.

Подтвержденная zero-control leaf ветка читает `control = input[a2]`; если control равен нулю, читает symbol `input[a3]`, увеличивает `a3` и пишет symbol в `a4 + a5*8`. Если `a8 < 10` и `a9 < 0x400`, заполняет `a7 + index*8`, начиная с `a9`, шагом `1 << (a8+1)` до индекса `< 0x400`; каждая запись хранит symbol в `+0` и `a8+1` в `+4`. При `a8 == 10` есть специальная запись `(a5, 10)` в `a7 + a9*8`.

Nonzero-control ветка сначала пишет `a5-1` в `+0` записи `a4 + a5*8`, затем делает первый recursive call. Для parent аргументов `(A1..A9)` и `c = input[A2] != 0`, точный первый child-call `0x656ADD` получает `(A1, A2+1, A3, A4, A5-1, c, A7, A8+1, A9)`; `ECX_child=A1`. Push order: `A9, A8+1, A7, c, A5-1, A4, A3, A2+1, A1`. Аргумент 6 — сохраненный control byte `c`, а не `A8`; ранее опубликованное в отчете `A8` было ошибкой stack-offset интерпретации.

После возврата parent вычисляет `span=A6-c` и перезаписывает stack-slot `A3` этим span (`0x656AFC`). При span `> 1` записывает индекс `A5-c-1` и делает второй recursive call `0x656B2D`; child args: `(A1, A2+c+1, r1, A4, A5-c-1, span-1, A7, A8+1, A9 | (1 << A8))`, где `r1` — возвращенный первым child symbol cursor. Ранее формула `A3-1` ошибочно использовала исходный, а не перезаписанный stack-slot; корректное значение равно `A6-c-1`. При span `<= 1` parent берет symbol из возвращенного cursor, пишет в `+4` записи `A4 + A5*8`, затем при допустимых depth/index выполняет strided fill. Zero-control ветка после заполнения первого leaf **не возвращает немедленно**, а идет в общее продолжение для правой ветви; terminal fill использует ровно depth+1, а не depth+2. Эти три исправления необходимы для валидной реконструкции.

`context` содержит две раздельные таблицы: fast lookup на 1024 восьмибайтовые записи (8 KiB) и node records с base `context+0x2000` на 512 восьмибайтовых записей (4 KiB). `0x64AAD0` использует low 10 bits для fast lookup; leaf `<0x100` возвращается сразу, а второй dword leaf-записи задает число потребляемых битов. Значение `>=0x100` передается в `0x656BE0`, который выбирает ветви из node records по следующему биту до leaf `<=0xFF`. Инициализация очищает соответствующие `0x2000` и `0x1000` байт. Структурные размеры соответствуют индексам; семантическое соответствие каждой построенной записи реальной таблице пока требует lookup-теста.

**Уточнение адресного соответствия по decoder disassembly:** 8-KiB fast lookup — это `context[0x0000:0x1fff]` (1024 записи по 8 байт), а 4-KiB node records — `context+0x2000[0x0000:0x0fff]` (512 записей по 8 байт). Builder пишет leaf/node routing slots через `a4=context+0x2000` индексом `a5` (root index `n+0xFF`, максимум 510), а strided fast lookup fill выполняет через `a7=context` индексом до `0x3ff`. `0x64AAD0` вначале читает `context + ((state & 0x3ff) * 8)`; если значение `>= 0x100`, `0x656BE0` использует тот же context base, но адресует node record через `context + 0x2000 + node_index*8`. Таким образом, apparent OOB от смешения этих двух base исчезает: быстрый индекс имеет 1024 записи, индекс дерева — 512. Отдельные рекурсивные call formulas всё равно должны держать node index в допустимом диапазоне; предыдущий bounded probe в обоих block samples не наблюдал OOB.

Проверка границ контекста: `0x64AE90` резервирует `0x3024` байта, а передает context по адресу `ESP+0x30`; следовательно, перед context есть `0x20` байт локального префикса, и конец очищенной области `context+0x3003` помещается ровно в выделение. Это не переполнение. Decoder fast base соответствует 8-KiB области `context`; node base — смещению `+0x2000`, и ее 4-KiB диапазон соответствует 512 индексам. Bounded probe двух образцов не наблюдал out-of-bounds accesses; отдельная семантическая проверка всех branch targets еще ожидается.

Cursor расчет повторен на двух игровых blocks:

| Архивная запись | Block 0: archive offset / stored length | `n` | Cursor delta | Байты на cursor |
|---|---|---:|---:|---|
| `Preloads.dsPack`, entry 2, `Persistent.dsPreload` | `0xB6C563` / `0x24E0` | `0xFE` | `0x1FE` | `00 00 07 A1 00 00 80 00` |
| `Patch.dsPack`, entry 35, `UI/F2MenuTree.dsMenuFlow` | `0x2AA2A` / `0x1AF4` | `0xF6` | `0x1EE` | `00 00 06 72 00 00 80 00` |

В обоих случаях cursor равен `block_start+2*n+2`; с frame table это дает границу модели/bitstream. Reader endian-swaps 32-bit words; первые три successive 32-bit reads после границы являются token count, output size, literal offset. После них token code bits начинаются на boundary+12, а literal data начинается по независимому `block_base+literal_offset`.

Ограниченный Python-перенос аргументной рекурсии дерева на раннем этапе дал `-1` / `0xffffffff` из-за неверного stack mapping. После исправления второго child `a6=span-1`, zero-control fall-through и глубины stride точный probe на семи блоках дал следующую таблицу:

| Sample blocks | Recursive calls | Control bytes | Symbol bytes / cursor | Fast entries | Leaves | Node refs | Node IDs |
|---|---:|---:|---:|---:|---:|---:|---|
| `Persistent.dsPreload`, 4 blocks | 251–255 | calls | `N+1` | 1024/1024 each | 1011–1018 | 6–13 | 256–510 |
| `F2MenuTree.dsMenuFlow`, 3 blocks | 237–248 | calls | `N+1` | 1024/1024 each | 1004–1014 | 10–20 | 256–503 |

Для каждого из этих 7 blocks все 1024 fast-table entries совпали с независимым медленным обходом node tree для соответствующего 10-bit prefix; node references имели допустимые индексы. Это подтверждает построение модели и согласование fast lookup с node tree по статическим правилам decoder-а. В дальнейшем token decoder также прошел на всех блоках этих двух entries.

Recursive builder `EAX` и wrapper cursor — два разных значения. Wrapper вызывает `0x656A30`, **игнорирует его EAX** и сам вычисляет `input+2*N+2`; именно этот cursor передается далее в `0x6569F0`. Builder return равен `N+1` symbol bytes; это не bitstream boundary. Control bytes адресуются отдельно через `a2=N+1`, leaf symbols идут через `a3`, начинающийся с нуля.

В одном из временных extraction-путей ранее указывался prefix `00 00 A0 74` для `UI/F2MenuTree.dsMenuFlow.cmp`. Повторная проверка исходного `Patch.dsPack` этому не соответствует: entry 35 имеет frame-data начало `+0x44`, а первый блок начинается `f6 08 bf fb ...`; его byte `n=0xF6` и cursor `+0x1EE`. Поэтому prefix-пример не используется как доказательство parser-а; соответствующий временный файл, вероятно, был получен иной/устаревшей версией extraction. Совпадение cursor и preload для `Persistent.dsPreload` проверено отдельно и точно.

### Кандидатные выходные сигнатуры

Сопоставление одинаковых basename между архивами обнаружило 76 имен с size-mismatched и equal-size вариантами. Это только oracle-кандидаты: basename не доказывает совпадение asset/version/path. Примеры:

| Сжатая запись | Stored / declared output | Равноименный несжатый пример | Наблюдаемый prefix |
|---|---:|---|---|
| `DLC_MP2.dsPack: rocks_rock_sml.dsmodel` | 4 788 / 5 083 | `Resource.dsPack`, вариант 10 754 байта | `52 4f 52 46 77 0a 00 00` (`RORF...`) |
| `Boot.dsPack` или `Resource.dsPack: overheat.dsa` | size differs | `Resource.dsPack`, 617 байт | `52 4f 52 46 2d 02 00 00` (`RORF...`) |
| `Resource.dsPack: wall_panel_a_dif.tif` | output 87 628 | другой path в `Resource.dsPack`, также 87 628 | `66 4c 56 01` |

Это не ground truth для сжатых вариантов: разные пути/версии могут содержать разные assets. `RORF` повторяется у просмотренных `.dsa/.dsmodel/.dsobject`; `.tif` показывает неодинаковые префиксы, поэтому extension не задает универсальную сигнатуру. Проверкой decoder будет точная длина плюс структурное/full-content совпадение с доказанно идентичной записью, а не один похожий prefix.

Уточнение по RTTI: точного класса `MiniPackResourceComponent` в RTTI нет. Статически подтверждены интерфейс `IMinipackResourceComponent` и то, что `HardwareLightComponent` содержит его base subobject по смещению `this+0x70` и записывает интерфейсную vtable. Это не доказывает, что `HardwareLightComponent` — единственная реализация интерфейса.

Это существенно сужает неизвестность, но пока не является готовой совместимой реализацией: точный формат сериализованного дерева, полный token grammar, правила длины/дистанции и условия остановки требуют точного переноса. Нельзя считать отдельный извлеченный блок успешно восстановленным до полного совпадения размера и повторяемых структурных сигнатур. Проверки стандартных codec и локального decoder-кандидата, описанные выше, остаются отрицательными и согласуются с тем, что используется собственный формат битового дерева.

Полученные файлы `.cmp` являются полезным безопасным промежуточным результатом, но сами по себе не являются исходными восстановленными файлами. Следующий шаг для декомпрессии — определить codec каждого блока, назначение size-like и unknown полей, а также область checksum, затем получить совпадение нескольких независимых записей по точной выходной длине и сигнатуре/структуре результата.

## Проверка на поставленных игровых архивах

На всех 13 контейнерах успешно разобраны заголовки, таблицы имен и обе таблицы записей: **52 927 записей файлов**, **6 726 папок**. Проверены оба byte order. Все payload извлечены в отдельное временное дерево `/tmp/kilo/dspack-extracted`, а каждый выведенный файл побайтно сравнен с диапазоном `[offset, offset + stored_size)` исходного архива.

Итог проверки: **52 927 / 52 927** совпадений, **3 058 706 908** байт извлеченных stored payload; **31 835** записей помечены как возможные сжатые (`.cmp`) и оставлены без декомпрессии. Архивы в `resources/` остались неизменными. Временные распаковки не входят в репозиторий/каталог игры.

Проверка payload доказывает правильность копирования диапазонов по таблице, но не подлинность оригинальных имен/размеров, целостность содержимого файла на уровне игрового формата или корректность декомпрессии. Табличное поле `unknown_field` намеренно не называется CRC и не используется как checksum.

## Результат MiniPack декомпрессии

`minipack_model.py` содержит in-memory tree builder, validated fast lookup, word bit reader и token decoder. `dspack_unpack.py --decompress-minipack` запускает его только явно; default CLI остается консервативным. Каждый block проверяется по frame output length, token-bit cursor не должен заходить в literal-data region, literal source и match distance/length ограничены, match-copy корректно поддерживает overlap. Entry публикуется только после успешной декомпрессии всех blocks и точного совпадения суммарной длины; temp file создается рядом с output и атомарно публикуется без overwrite.

Полные результаты на игровых контейнерах:

| Архив | Framed blocks | Size-different entries декодированы | Неизвестные compressed entries `.cmp` | Equal-size raw blocks | Entries с проверенной итоговой длиной |
|---|---:|---:|---:|---:|---:|
| `Patch.dsPack` | 1 681 | 28 | 12 | 0 | 40/40 |
| `Preloads.dsPack` | 1 300 | 198 | 248 | 2 blocks in 2 entries | 557/557 |

Итого 2 979 framed blocks обработаны; декодированы 226 compressed entries, и каждая собрана в точный заявленный dsPack uncompressed size. Два коротких Preloads blocks (87 и 58 байт) внутри валидных frame имели equal stored/expected size и копировались как raw; их block mode фиксируется в manifest. Остальные 260 compressed entries без подтвержденного MiniPack framing остаются `.cmp`.

Разбор остатка `Patch.dsPack`: все 12 opaque entries не имеют валидного MiniPack marker/frame; по типам это 7 SWF, 1 Lua, 1 `.map`, 3 `.dsobject`. В stored prefixes SWF не найдено `FWS/CWS/ZWS`, поэтому SWF CWS/zlib распаковка не применима напрямую. Стандартные zlib/raw DEFLATE/gzip/bzip2/XZ/LZMA/LZ4-frame/Zstd попытки не дали точных полных streams с declared output length.

| Entry | Stored / declared output | Stored prefix | Наблюдение |
|---|---:|---|---|
| `crc.map` | 1 032 / 17 036 | `29 88 3c 15 19 00 29 01` | custom/unknown |
| `Script/Game/Camera/AdjustFOV.lua` | 3 148 / 4 636 | `8e 78 7e 2a 2c cf 0c 76` | не начинается с Lua bytecode/text signature |
| `Flash/UI/HUD/MP/Export/Kos_Score_HUD.swf` | 6 052 / 8 111 | `be 1d 8a b9 c6 99 95 cb` | нет standard SWF header |
| `Flash/UI/HUD/DynamicContent/Export/MPMessageQueue.swf` | 5 992 / 10 775 | `e7 0a 8f 89 cc c4 14 50` | нет standard SWF header |
| `Flash/UI/HUD/DynamicContent/Export/MPMessageMarquee.swf` | 3 328 / 5 251 | `b1 04 06 44 43 c7 cb 0f` | нет standard SWF header |
| `Flash/UI/HUD/Global/Export/VehicleBlackOut.swf` | 1 564 / 2 257 | `56 00 01 42 bd 07 9b 08` | нет standard SWF header |
| `Flash/UI/HUD/Global/Export/GlobalHUD.swf` | 6 200 / 10 948 | `e8 b5 ad f5 8b a7 12 41` | нет standard SWF header |
| `Flash/UI/HUD/Global/Export/BlackOut.swf` | 1 560 / 2 250 | `57 00 01 0b 43 bd ae 98` | нет standard SWF header |
| `Flash/UI/HUD/EnergyBar/Export/EnergyBar.swf` | 7 872 / 14 620 | `f4 c4 91 de f9 87 83 15` | нет standard SWF header |
| `Objects/GameObjects/Cameras/AssaultRifle_Zoom.dsobject` | 1 768 / 2 608 | `85 c8 7f 77 e8 0f 18 42` | custom/unknown |
| `Objects/GameObjects/Cameras/Penetrator_zoom.dsobject` | 1 768 / 2 603 | `83 c1 40 06 00 41 08 09` | custom/unknown |
| `Objects/GameObjects/Cameras/SMG_Zoom.dsobject` | 1 756 / 2 596 | `82 c3 83 7e ab 42 07 0a` | custom/unknown |

Равноименных побайтно совпадающих uncompressed counterparts для этих Patch остаточных записей в локальных decoded trees не найдено. Исключение повторяемости между контейнерами: `EnergyBar.swf` в Patch и Boot имеет одинаковый stored payload; без decoder это не ground truth uncompressed output.

Остаток всех остальных архивов также инвентаризирован статически: `Audio.dsPack` и все 6 localized audio packs не содержат residual compressed entries; `Boot` — 1 911, `DLC_MP1` — 334, `DLC_MP2` — 305, `Resource` — 17 872, всего 20 422 записи без validated MiniPack frame и с size mismatch. Они охватывают 40+ engine resource extensions: наиболее частые `.dsobject` (6 606), `.dsmaterial` (3 062), `.dsa` (2 113), `.dsmodel` (1 797), `.dssoundcue` (1 644), `.dsparticle` (909), `.tif` (889), `.dscinema` (534), `.lua` (520), `.dsworld` (499), `.dsanimation` (497); WAV residue есть только в `Boot` (251). По 4-byte payload prefix получено 19 350 уникальных значений для 20 422 entries; общего wrapper signature не наблюдается. Только 31 payload начинается со структурно допустимого zlib header candidate (3 Boot, 28 Resource), и ни один не декодировался как завершенный zlib stream с точным declared output size. Остальные не имеют известных gzip/bzip2/xz/zstd/LZ4/ZIP/zlib-at-start signatures. Наблюдается 24 группы точных повторов (84 entries), включая одинаковые language-specific fonts в Boot и shared assets между DLC/Resource; это согласуется с обычным дублированием ресурсов, не с единым аудиокодеком.

Из 248 residual entries `Preloads.dsPack`: 123 `des.dsPreload`, 92 `per.dsPreload`, 31 menu preload, одна `Boot.dsPreload` и один `crc.map`. Их stored bytes составляют 677 020, заявленный output — 2 315 158 байт. MiniPack marker и signatures gzip/zstd/LZ4-frame/xz/bzip2 не найдены. Одна запись имеет валидный zlib header с preset-dictionary flag; строгий inflate завершился `Z_NEED_DICT`, поэтому без известного словаря это не подтвержденная декомпрессия. Найдены две пары точных повторов stored bytes: `checkpoint_10_RequirePilotDesign2_nosave/per.dsPreload` ↔ `checkpoint_11_tvsection/des.dsPreload` (6 948→25 011 байт) и `checkpoint_23_ToGardenCenter_nosave/des.dsPreload` ↔ `checkpoint_21_officesenter/per.dsPreload` (176→217 байт). Совпадения подтверждают повторяемость opaque payload, но не раскрывают codec.

Поэтому эти 20 422 residual entries нельзя безопасно прогонять через MiniPack decoder только по признаку `stored_size != unpacked_size`: неизвестно, относятся ли они к другому алгоритму, сериализации по типу ресурса или к комбинации. Нужен отдельный статический путь загрузчика residual resource types в EXE/engine DLL и точная проверка на known-good exemplar. До этого остаются `.cmp`.

Наблюдаемая структура decoded output: `Persistent.dsPreload` начинается с `/Animations/Characters/Player/...` и далее содержит resource paths; `UI/F2MenuTree.dsMenuFlow` содержит `MainMenu`, `menuControllerObjectTable`, `transitions`, `FEARMainMenu` и связные значения меню. Это сильная содержательная валидация на реальных game blocks, вместе с точными длинами всех записей. Пользовательский `/home/drcerg/Documents/luasense.lua` стал точным full-output oracle для `Patch.dsPack:Script/luasense.lua`: оба файла являются Lua 5.1 bytecode version `0x51`, имеют размер `939546`, SHA-256 `5638c4cea924c34d6b5150b7d3db781c5eb8c7a714656ba64c240a4a4b930120`, а `cmp` подтвердил byte-for-byte идентичность. Системный `luac5.1` 5.1.5 отверг оригинал с `bad header in precompiled chunk`; bytecode не исполнялся. Это валидирует также третий token branch/decoder sample. Для остальных asset full-output oracle отсутствует, и terminal-source pointer еще статически сверяется, поэтому decoder остается opt-in.

После полного CLI прогона с флагом получено: `Patch.dsPack` — 28 MiniPack entries декодированы, 12 unknown entries сохранены `.cmp`, все 40 выходных размеров совпали; `Preloads.dsPack` — 198 MiniPack entries декодированы, 248 unknown `.cmp`, 111 изначально uncompressed entries скопированы, все 557 выходных размеров совпали. Два смешанных Preloads entries содержали по одному equal-size raw block; `block_output_modes` в manifest различает raw passthrough и декодированные блоки. Полные тесты включают реальные игровые записи `F2MenuTree.dsMenuFlow` и `Persistent.dsPreload`.
