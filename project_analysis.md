# Анализ проекта `snmp_agent_eth01`

## Обзор

**Платформа:** ESP32 (ESP-IDF ≥ 5.0)  
**Протокол:** SNMP v2c через lwIP  
**Интерфейс:** Проводной Ethernet (PHY: LAN87xx, MDC=23, MDIO=18)  
**Назначение:** SNMP-агент для мониторинга серверных помещений (температура, протечка воды, состояние дверей), удалённая настройка сетевых параметров.

---

## Архитектура

```
app_main()
├── nvs_flash_init()                  # Инициализация NVS
├── esp_event_loop_create_default()   # Событийный цикл
├── [Task] factory_reset_task         # Кнопка сброса (GPIO 0)
└── ethernet_init_static()
    ├── load_network_settings()        # Загрузка IP из NVS
    ├── eth_event_handler()            # Установка статического IP при Link-Up
    └── esp_eth_start()
        └── [On success]:
            ├── sensors_init()         # 1-Wire DS18B20 + GPIO датчики
            ├── [Task] sensor_polling_task  # Опрос каждые 2 сек
            └── snmp_init()            # Запуск SNMP агента
```

### SNMP MIB-дерево (OID: 1.3.6.1.4.1.9999.1.X)

| OID .X | Тип           | R/W        | Данные                        |
|--------|---------------|------------|-------------------------------|
| .1     | INTEGER       | Read-Only  | Температура × 10 (°C×10)      |
| .2     | GAUGE32       | Read-Only  | Свободная heap-память (байт)  |
| .3     | TIMETICKS     | Read-Only  | Uptime (×10 мс)               |
| .4     | INTEGER       | Read-Only  | "Загрузка CPU" (фиктивная)    |
| .5     | OCTET STRING  | Read-Only  | MAC-адрес (6 байт)            |
| .6     | INTEGER       | Read-Only  | Протечка воды (0/1)           |
| .10    | OCTET STRING  | Read-Write | IP-адрес (строка)             |
| .11    | OCTET STRING  | Read-Write | Шлюз (строка)                 |
| .12    | OCTET STRING  | Read-Write | Маска подсети (строка)        |

---

## ⚠️ Найденные ошибки и проблемы

### 🔴 Критические

#### 1. `esp_event_loop_create_default()` вызывается дважды
**Файлы:** `main.c:247`, `ethernet_app.c:71`  
`app_main` вызывает `ESP_ERROR_CHECK(esp_event_loop_create_default())`, а затем сразу вызывает `ethernet_init_static()`, которая повторно вызывает `esp_event_loop_create_default()`. В `ethernet_app.c` это учтено (`ret != ESP_ERR_INVALID_STATE`), но вызов из `main.c` с `ESP_ERROR_CHECK` произойдёт **раньше** — это нормально, однако создаёт путаницу и дублирование инициализации.

**Решение:** Удалить вызов `esp_event_loop_create_default()` из `main.c`, оставить только в `ethernet_init_static()`.

---

#### 2. Некорректный расчёт "загрузки CPU" (OID .4)
**Файл:** `main.c:113`
```c
case 4:
    *uint_ptr = 100 - (esp_get_free_heap_size() % 10);
    return sizeof(uint32_t);
```
Это **не** загрузка CPU — это бессмысленная формула (остаток от деления размера heap на 10, вычитаемый из 100). Значение будет случайным числом от 90 до 100.

**Решение:** Если нужна реальная загрузка — использовать `vTaskGetRunTimeStats()` или убрать OID .4 из MIB.

---

#### 3. `get_sensor_temperature()` блокирует задачу на 800 мс
**Файл:** `sensors_app.c:65`
```c
vTaskDelay(pdMS_TO_TICKS(800)); // Ждем завершения преобразования
```
Вызов `get_sensor_temperature()` происходит внутри `sensor_polling_task` с приоритетом 5. 800 мс блокировки — это нормально для FreeRTOS задачи, но нужно убедиться, что это не мешает другим задачам с таким же приоритетом (например, `factory_reset_task` тоже priority=5). Желательно снизить приоритет sensor_task до 3.

---

#### 4. `test_system_config` применяется ко ВСЕМ записываемым OID одинаково
**Файл:** `main.c:145-157`
```c
static snmp_err_t test_system_config(...)
{
    ...
    ip4_addr_t test_ip;
    if (!ip4addr_aton(buffer, &test_ip)) return SNMP_ERR_WRONGVALUE;
    return SNMP_ERR_NOERROR;
}
```
Функция валидирует **любой** записываемый OID (.10, .11, .12) одинаково — просто как IPv4 адрес. Это правильно для IP и GW, но **маска подсети** тоже является валидным IPv4-адресом, так что формально работает. Тем не менее отсутствует проверка конкретного `node->oid`, что может стать проблемой при расширении MIB.

---

### 🟡 Некритические / Предупреждения

#### 5. `esp_log.h` закомментирован в `ethernet_app.c`
**Файл:** `ethernet_app.c:5`
```c
// #include "esp_log.h"
```
При этом переменная `TAG` объявлена и используется (строка 11). Компилятор выдаст предупреждение `unused variable 'TAG'` — в CMakeLists это подавляется флагом `-Wno-unused-variable`. Нужно либо раскомментировать `#include "esp_log.h"` и использовать TAG для логов, либо удалить объявление TAG.

---

#### 6. Датчики дверей объявлены в `.c`, но не в `.h` (оригинальная версия)
**Файлы:** `sensors_app.h` vs `sensors_app.c`  
Функции `get_door_open_status_1()` и `get_door_open_status_2()` реализованы в `sensors_app.c`, **объявлены** в `sensors_app.h`, но нигде **не используются** в `main.c`. Датчики дверей (GPIO 25, 26) инициализируются, но данные с них не передаются через SNMP.

**Решение:** Добавить OID .7 и .8 в MIB для дверных датчиков или удалить нереализованный код.

---

#### 7. Утечка дескриптора таймера перезагрузки
**Файл:** `main.c:195-197`
```c
esp_timer_handle_t timer;
esp_timer_create(&reboot_args, &timer);
esp_timer_start_once(timer, 1000000);
```
После срабатывания таймера вызывается `esp_restart()`, поэтому `esp_timer_delete(timer)` никогда не будет вызван. На практике это не критично (перезагрузка очистит всё), но архитектурно некорректно.

---

#### 8. Размер буфера в `test_system_config` vs `set_system_config`
**Файл:** `main.c:147,162`
```c
char buffer[16]; // в обеих функциях
if (len >= sizeof(buffer)) return SNMP_ERR_WRONGLENGTH;
```
Буфер 16 байт достаточен для IPv4 в формате строки (максимум "255.255.255.0" = 13 символов + `\0`). Но проверка `len >= 16` отклоняет строку длиной ровно 15 символов — это нормально, т.к. IPv4 не бывает длиннее 15 символов. Всё корректно, но `buffer[15]` (индекс 15 при `buffer[16]`) является предельным случаем.

---

#### 9. NVS открывается в `NVS_READWRITE` при чтении
**Файл:** `ethernet_app.c:34`
```c
esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
```
Функция `load_network_settings()` только читает (или пишет defaults). Правильнее было бы использовать `NVS_READONLY` при чтении и отдельно открывать на запись, либо оставить `NVS_READWRITE` и явно комментировать, что запись дефолтных значений — intentional.

---

#### 10. `project_name` отсутствует в корневом `CMakeLists.txt`
**Файл:** `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(snmp_agent_eth01)
```
Забыт `set(PROJECT_NAME snmp_agent_eth01)` **до** `include(...)` — некритично для ESP-IDF, т.к. `project()` устанавливает имя. Но порядок `include → project` является стандартным для ESP-IDF и корректен.

---

## ✅ Что реализовано правильно

| Аспект | Оценка |
|--------|--------|
| Статический IP через обработчик события `ETHERNET_EVENT_CONNECTED` | ✅ Правильный подход |
| Двухфазный SNMP SET (test + set) | ✅ Соответствует RFC |
| Перезагрузка через `esp_timer` (асинхронно из SNMP callback) | ✅ Безопасно |
| Сохранение сетевых настроек в NVS | ✅ Корректно |
| Factory reset через удержание кнопки (50 × 100мс = 5 сек) | ✅ |
| Кэширование температуры в глобальной переменной (опрос отдельной задачей) | ✅ |
| Использование стандартного MIB2 + кастомный MIB | ✅ |
| Валидация IP-адреса через `ip4addr_aton` | ✅ |

---

## 📋 Рекомендации по улучшению

1. **Добавить OID для датчиков дверей** (.7, .8) — GPIO 25 и 26 инициализированы, но данные не передаются через SNMP.
2. **Убрать дублирование `esp_event_loop_create_default()`** из `main.c`.
3. **Исправить псевдо-метрику загрузки CPU** (OID .4) или переименовать её.
4. **Раскомментировать `esp_log.h`** в `ethernet_app.c` для нормального логирования.
5. **Снизить приоритет `sensor_polling_task`** до 3 (ниже `factory_reset_task`).
6. **Рассмотреть SNMP Trap-ы** — при протечке или срабатывании датчика двери активно отправлять уведомление на NMS, не ждать опроса.

