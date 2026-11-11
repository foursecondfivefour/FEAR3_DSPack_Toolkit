# Статический анализ `luasense.lua`

**Объект:** `/home/drcerg/Documents/luasense.lua`  
**Результат сравнения с игрой:** byte-for-byte идентичен восстановленному `Patch.dsPack:Script/luasense.lua`  
**Безопасность:** chunk прочитан как бинарные данные и статически разобран; код не запускался.

## Формат и идентичность

- Размер: **939 546 байт**.
- SHA-256 оригинала и восстановленного игрового файла: `5638c4cea924c34d6b5150b7d3db781c5eb8c7a714656ba64c240a4a4b930120`.
- Lua 5.1 bytecode header: `1b 4c 75 61 51 00 01 04 04 04 08 00`: standard Lua 5.1, little-endian, 4-byte `int`, `size_t` и instruction, 8-byte double.
- Источник внутри chunk: `/Script/luasense.lua`.
- Системный `luac5.1` версии 5.1.5 отвергает chunk как `bad header`: его ABI `size_t` равен 8 байтам, а этот chunk собран с 4-байтовым `size_t`. Простая замена header не годится, потому что размер `size_t` используется для длин строк по всему chunk.
- Статический ограниченный parser `/tmp/kilo/lua51_chunk_inspect.py` читает объявленные ширины Lua ABI и полностью потребляет chunk без хвоста.

## Что делает chunk

Это **каталог API для LuaSense/editor tooling**, а не обычный самостоятельный скрипт игры. В Lua 5.1 root chunk имеет 52 603 инструкции и 3 025 вложенных прототипов:

- 3 024 вложенных prototype состоят из одной инструкции `RETURN`: это функции-заглушки/объявления API, которым соответствуют имена и описания в таблицах.
- Один prototype содержит 67 инструкций (source lines 3–41), два параметра и локальные имена `namespace`, `method`, `qualifiedMethod`, `tipInfoTable`, `tipInfoParamsTable`, `tipInfoDesc`, `tipInfoText`, `firstParam`. Он ищет `Editor.CallTip[namespace .. "." .. method]` и собирает call-tip из `Params`, `Name`, `Description`, `Desc`; если записи нет, возвращает `N/A`.
- Root bytecode в основном строит таблицы и связывает stub prototypes с именами API. В root: 3 025 `CLOSURE`, всего 2 `CALL`; это подтверждает декларативную/каталожную структуру. Инструкции всей библиотеки: 55 694.
- Всего: 3 026 prototypes, 6 300 constants; 6 294 string constants / 6 286 уникальных строк; line-info присутствует для всех 55 694 инструкций; 2 776 local-variable records.
- Каталог описывает около **3 002** различных `Despair.*` API names в 15 namespaces: `Landscape`, `Bling`, `Facade`, `Forge`, `Poise`, `Spectator`, `Outtake`, `ChopShop`, `Burmester`, `ProfileEditor`, `FontMaker`, `Smash`, `BusEditor`, `Moznosti`, `Librarian`.
- Представлены как инструменты редактора (File, ResourceView, Object, ModelView, Dialog, toolbar, GenerateDocs/GenerateLuasense), так и `GameScript.*` игровые функции (world streaming, objects, camera, physics, inventory, missions, network events и т. п.).

## Описанные чувствительные возможности

Наличие записи в каталоге означает, что функция **описана/поддерживается как API**, а не что сам chunk её вызывает или что она исполнялась.

- OS/process: `Despair.<tool>.OS.CmdExec`, `ShellExec`, `ResourceView.Launch/TestLaunch`. Документация `CmdExec` говорит, что принимает executable alias/path, parameters и timeout; `ShellExec` описывает Windows ShellExecute verbs (`open`, `edit`, `explore`, `find`, `print`).
- Файлы: `File.Open/Save/SaveAs`, `AutomationUtility.FindFiles/FindFilesWithName/CreateDirectory/PathExists` и ресурсные операции.
- Perforce: `Perforce.Sync`, `Checkout`, `Checkin`, `Revert`, а также resource-view checkout/checkin operations.
- Debug: `GameScript.FEAR.GameDebugDump` описан как запись debug dump на диск.
- Нет литеральных HTTP/HTTPS URLs и очевидных socket/client API имен в строковом каталоге. Сетевые игровые APIs/event synchronization присутствуют, но это не свидетельствует о сетевой активности самого catalog chunk.
- Сам parser обнаружил только таблицы/stub function prototypes и formatter call-tip. Наличие `ShellExec`, `CmdExec`, Perforce API и т. п. в constants — это документация API, не подтвержденная активная shell/file/network операция данным chunk.

## Вывод

Файл — крупный сгенерированный LuaSense словарь API и call-tip metadata для семейства Despair tools и игровых `GameScript` APIs. Его безопасно статически индексировать или декомпилировать; не следует исполнять только ради чтения. Bytecode может содержать описание опасных tool capabilities, но статический анализ не показал, что сам файл запускает их.
