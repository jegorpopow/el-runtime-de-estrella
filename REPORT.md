# Проект. Этап 1. Сборщик мусора

Автор: Егор Попов

[Репозиторий](https://github.com/jegorpopow/el-runtime-de-estrella): 
[Коммит](https://github.com/jegorpopow/el-runtime-de-estrella/commit/d2e28443a8a76ef42e266708cca8554748d81eea)

## Сборка и запуск

Язык реализации - C11

Тестировано под Linux (Ubuntu 26.04 @ WSL2)

Зависимости: 
* `gcc>=15.02`
* `docker` (опционально, для сборки кода на Stella )

Установка зависимостей
```bash
sudo apt install build-essential gcc
```

Сборщик мусора преставлен едиственной единице трансляции gc.c, которая может быть 
скомпилирована вместе с кодом проекта или предварительно в отдельный объектный файл.

Единица трансляции зависит от объявлений типов рантайма Stella, см. зависимости в `compile_flags.txt`

Сборка исполняемого файла: 
```bash
# from /stage-1 root:

# Компиляция STELLA (для примеров собранная версия уже лежит в /examples)
bash compile-stella.sh $STELLA_SOURCE_NAME > $COMPILED_STELLA_NAME 

# Для сбоки C-кода и линковки с runtime
# -d - debug-сборка - санитайзеры, отладочная информация и сбор trace-ов в stdout
bash assemble-stella.sh [-d] $COMPILED_STELLA_NAME $EXECUTABLE_NAME

# Запуск
./$EXECUTABLE_NAME
```

Запуск примеров:
```bash
bash assemble-stella.sh examples/chat.c main
./main 10 4096 # outputs 45
bash assemble-stella.sh examples/history.c main
./main  4096 # outputs 4096
bash assemble-stella.sh examples/double.c main
./main 2 # outputs 4
```

Запуск тестов:
```bash
bash run-abi-violation-tests.sh  
bash run-programs-tests.sh
```

## Алгоритм и устройство реализации

Используется алгоритм Baker-а (инкрементальная версия алгоритма копирующей сборки с semi-dfs перемещением),

Состояние сборщика мусора сохраняется в структуре `bakers_garbage_collector`, 
реализация алгоритма находится в списке её функций-методов, 
в частности:
* `allocate(size)` - выдедение памяти под объект известного размера `size`
* `forward(p)` - перенаправление ссылки `p` из from-space в to-space с копированрем при необходимости 
* `chase(p)` - копирование ранее не перенесённых объектов от ссылки `p` по ветке semi-dfs    

Инварианты алгоритма:
Память разделяется на 2 полупространства (to-/from- space):
* Корни указывают только на to-space или на память, не управляемую сборщиком
* В частности, все managed чтения возвращают указатели на to-space иои на память, не управляемую сборщиком
* В to-space указывают три указателя `scan`, `next`, `limit`. `snan` не больше `next`, между
ними лежит целое число "серых" объектов, образуя FIFO очередь. До `scan` лежит целое количество объектов, все ссылки в которых не указывают на from-space. 
После `limit` лежит целое число свежеаллоцированных объектов, `limit` не меньше `next` между ними не создаётся объектов, туда не ведут никакие ссылки. 
* При смене пространств местами старый from-space пуст, `scan` = `next`.

Так как сборщик представляет из себя одну единицу трансляции, в ней представлены как сам алгоритм, так и множетсво вспомогательных функций, см. заголовки в многострочных комментариях в коде, сам алгоритм лежит в разделе `Bakers algorithm implementation`

## Параметры

Все параметры задаются переменными среды 


| Параметр | Переменная окружения | Смысл | Допустимые значения | По умолчанию |
|---|---|---|---|---|
| `scan_per_step` | `STELLA_GC_STEPS` | Сколько объектов to-space сборщик сканирует на одной точке сборки | Целое, не меньше 1, не больше MAX_SIZE | 1 |
| `forced_scan_threshold` | `STELLA_GC_TRESHOLD` | Доля заполненности to-space, приводящая к фосированной сборке| 1.0|
| `collection_sites` | `STELLA_GC_COLLECTION_POINTS` | Битовая маска, в каких точках сборки выполняются шаги сканирования. `CS_ALLOCATION` = 1, `CS_READ_BARRIER` = 2, `CS_WRITE_BARRIER` = 4, `CS_UPDATE` = 8. | любое нечётное целое от 1 до 15 | 1 |


При нехватке места в to-space при выделении нового объекта или копировании вызывается 
OOM. Параметры сборщика не связаны с объёмом памяти, они определяют моменты сборкиии.

## Статистика и состояние

Формат статистики - список занчений метрик
Пример (`STELLA_GC_STATS=1 ./chat 10 4096`):
```
allocated bytes: 7080
allocated objects: 210
failed allocations: 0
collections initiated: 3
collections finished: 2
occupied bytes: 5104
maximum occupied bytes: 6048
budgeted steps: 88
budget violations: 1
non-budgeted steps: 5
managed reads: 245
managed writes: 0
primitive reads: 0
primitive writes: 0
read barrier activations: 44
write barrier activations: 0
dynamic roots: 2
maximum dynamic roots: 94
permanent roots: 0
```

## Формат статистики сборщика

| Имя метрики | Единица измерения | Описание |
|---|---|---|
| `allocated bytes` | байты | Суммарный объём, запрошенный через `stella_gc_alloc` |
| `allocated objects` | штуки | Число успешных вызовов `stella_gc_alloc` |
| `failed allocations` | штуки | Число вызовов `alloc`, завершившихся `stella_gc_out_of_memory`. Не больше 1: при нехватке памяти программа сразу завершается |
| `collections initiated` | штуки | Число начатых циклов сборки. Начальная конфигурация считается началом первого цикла, поэтому значение на 1 больше числа реально запущенных сборок. |
| `collections finished` | штуки | Число завершённых циклов сборки (flip). Должно быть на 1 меньше `collections initiated` в любой момент времени |
| `occupied bytes` | байты | Текущий объём достижимых занятой памяти объектов в конце последней операции интерфейса. В силу инкрементальности сборщик не может подсчитать без отдельного вызова обхода памяти суммарный объём достижимых из корней объектов, поэтому occupied_bytes - все объекты, лежащие в from-space + все свежеаллоцированные объекты в to-space, эта величина может быть выше размера полупространства, некоторые из этих объектов могут стать недоступны в процессе работы|
| `maximum occupied bytes` | байты | Максимум `occupied bytes` за всё время работы|
| `budgeted steps` | штуки | Число объектов, просканированных в рамках бюджета |
| `budget violations` | штуки | Число случаев, когда `allocate` был вынужден выйти за бюджет шага и выполнить внеплановое досканирование |
| `non-budgeted steps` | штуки | Число объектов, просканированных сверх бюджета при форсированном досканировании. |
| `managed reads` | штуки | Число вызовов `stella_gc_read_managed`. |
| `managed writes` | штуки | Число вызовов `stella_gc_write_managed`. |
| `primitive reads` | штуки | Число вызовов `stella_object_read_primitive`. |
| `primitive writes` | штуки | Число вызовов `stella_object_write_primitive`. |
| `read barrier activations` | штуки | Число вызовов `read_barrier`, в которых `forward` фактически переместил объект |
| `write barrier activations` | штуки | Число активаций барьера записи, всегда 0. |
| `dynamic roots` | штуки | Текущая глубина стека динамических корней, зарегистрированных через `stella_gc_push_root`. |
| `maximum dynamic roots` | штуки | Максимальная глубина стека динамических корней за всё время работы. |
| `permanent roots` | штуки | Текущее число постоянных корней, зарегистрированных через `stella_gc_register_permanent_root`. |


Пример вывода состояния (`STELLA_GC_STATE=1 ./chat 1 128`):

```
Garbage Collector State:
To-space:   [0x602832f49090; 0x602832f49110), total 128 bytes
From-space: [0x602832f49010; 0x602832f49090), total 128 bytes
Scan = 0x602832f49090 (to-space + 0)
Next = 0x602832f49090 (to-space + 0)
Limit = 0x602832f490b0 (to-space + 32)
Free to-space space = 32
Is not in flip state
Roots:
======== FROM-SPACE ========
=== Occupated bytes: 0, Free bytes: 128 ===
========= TO-SPACE =========
=== Occupated bytes: 96, Free bytes: 32 ===
----- SCANNED OBJECTS ------
----- 0 bytes ------
------------ QUEUE ---------
----- 0 bytes ------
---------- ALLOCATED -------
----- 96 bytes ------
Object at [0x602832f490b0: 0x602832f490d0) (32 bytes):
        Not forwarded, raw gc_word =  0
        Descriptor: Nat::succ
        Kind: 1
        Slots count: 1
        Managed fields (1):
                managed[0] = 0x60282ec320d0
        Primitive fields (0):
Object at [0x602832f490d0: 0x602832f490f0) (32 bytes):
        Not forwarded, raw gc_word =  0
        Descriptor: stella_fn_3
        Kind: 2
        Slots count: 1
        Managed fields (1):
                managed[0] = 0x60282ec32010
        Primitive fields (0):
Object at [0x602832f490f0: 0x602832f49110) (32 bytes):
        Not forwarded, raw gc_word =  0
        Descriptor: Nat::succ
        Kind: 1
        Slots count: 1
        Managed fields (1):
                managed[0] = 0x60282ec320d0
        Primitive fields (0):
Memory:
dynamic root 0: slot=0x7ffc16fc9930 value=0x602832f490f0
dynamic root 1: slot=0x7ffc16fc9938 value=0x60282ec320d0
```


Garbage Collector State - значения переменных `scan`, `next`, `limit`, управляемые полупространства (начало, конец, длина)
Roots - dump памяти, все объекты from-space и объекты to-space, разбитые по тому, между какими переменными они лежат. Для каждого объекта выводится span в памяти, заголовок, то, куда он скопирован, если скопирован и значения и количество полей/slot-ов.
Memory - список корней, с адресом и значением.

## Тестирование

Запуск тестов:
```bash
bash run-abi-violation-tests.sh  
bash run-programs-tests.sh
```

Есть два вида тестов:

Тесты на нарушения ABI просто вызывают функции из API, нарушая их контракты и ожидают падения программы через ABI violation.

Тесты с программами - тригерят разные ситуации, в которых сборщик при наличии бана может повести себя странно: spam - тригеррит много flip-ов, many-roots - порождает много корней и проверяет, что их стек правильно организован в сборщике, share/ref- создают много ссылок на один объект разными способами, проверяется отсутсвие падений, вывод проверяется мануально на предмет возможного corrupt-а данных.

При тестировании нашлась только забытая проверка на STATIC у аллоциремого объекта (скорее при написании теста, чем при запуске, но всё же)

### Собственные и совместно используемые тесты

Все тесты мои

## Известные ограничения

Статистика `occupied bytes` имеет причудливый смысл из-за инкрементальности алгоритма и мало показательна при выборе `max_heap_size`, но в противном случае её замер требовал бы дополнительного обхода памяти и свёл бы инкрементальный алгоритм к неинкрементальному, катастрофически ухудшив производитеольность. 


## Расширенная часть

Результат: 
Исследованы параметры `STELLA_GC_STEPS` и `STELLA_GC_TRESHOLD` 

В `bonus/` лежат подсчиатнные или сгененрированные:
* `run_measurements.sh` - скрипт, запускающий тесты
* `make_tables.sh` - скрипт, рисующий таблички по резльтатам тестов
* `results.csv` - сырые замеры
* `README.md` - исентрукция по запуску
* `report.md` - итоговые таблички
* `transcript.md` - переписка с моделью 

Запуск:

```bash
bash bonus/run_measurements.sh     
bash bonus/make_tables.sh           
```

Значения по умолчанию - `STELLA_GC_STEPS`=1, `STELLA_GC_TRESHOLD`=0

Ожидание - Чем больше STELLA_GC_STEPS, тем меньше
collections finished при том же heap и тем выше maximum occupied bytes, 
влияние на время работы на выбранных размерах должно быть незначительным. 

Релальность - влияние на время работы меньше погрешности, число сборок от роста порога увеличилось, макисмальная занятость - уменьшилась.

Вывод (без ИИ): Метрики влияют на распределение работы по отношению к мутатору по времени, 
а не на суммарное количество работы меняется мало. Гипотетически, более частые сборки 
приводят к большему числу копирований, но на подобранных рамер

Вручную повторены две строки:
chat,12000,1,0.0 и spam,2048,16,0.5 — по три запуска каждая. Время
совпало с CSV в пределах погрешности 5%. 

Переписка: bonus/transcript.md, первая строка --- модель и дата.

## Использование ИИ
Сборщик (основная часть): исходный код сборщика написан без ИИ.

Коммиты, сделанные с ИИ:

| Коммит | Что сделано с ИИ (репозиторий, сборка, тесты, расширенная часть) | Модель |
|---|---|---|
| a9f231c | бонус | DeepSeek |
| 99bb5ce | скрипты для сборки тестов | DeepSeek | 

Расширенная часть: переписка лежит в bonus/transcript.md; коммит ИИ перечислен в таблице выше.

## Источники

nada