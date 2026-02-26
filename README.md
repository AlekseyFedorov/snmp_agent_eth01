Команды для получения:sysContact (Контактное лицо/ID):
```Bash
snmpget -v 2c -c public 192.168.0.50 .1.3.6.1.2.1.1.4.0
Ожидаемый ответ: STRING: "user"
sysLocation (Местоположение):
snmpget -v 2c -c public 192.168.0.50 .1.3.6.1.2.1.1.6.0
Ожидаемый ответ: STRING: "Destrict"
```

В иерархии SNMP группа system всегда находится по адресу .1.3.6.1.2.1.1.
```
Основные узлы этой группы:
- sysDescr.1.3.6.1.2.1.1.1.0Описание устройства (по умолчанию LwIP)
- sysUpTime.1.3.6.1.2.1.1.3.0Системный аптайм (стандартный)
- sysContact.1.3.6.1.2.1.1.4.0То, что вы записали в syscontact_storage
- sysName.1.3.6.1.2.1.1.5.0Имя узла (обычно "esp32" или имя хоста)
- sysLocation.1.3.6.1.2.1.1.6.0То, что вы записали в syslocation_storage
```
Как увидеть всё сразу?
Чтобы не вводить OID по одному, вы можете запросить всю системную информацию одной командой:

```Bash
snmpwalk -v 2c -c public 192.168.0.50 .1.3.6.1.2.1.1
```

Быстрый старт

1. Скачать - git clone https://github.com/AlekseyFedorov/snmp_agent_eth01.git
2. LWIP_STATS = y
   Flash size = 8 MB ?????
3. Add .vscode subdirectory files