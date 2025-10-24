# PZEM-6L24 3 Phase Monitor & Log Service
## ⚠️ Проект еще в работе ⚠️
## Описание
PZEM-6L24 Monitor - это системный сервис для мониторинга электроэнергии с помощью датчиков PZEM-6L24 через интерфейс UART / Modbus-RTU.
На основе [PZEM_004T_Systemd](https://github.com/akarnaukh/PZEM_004T_Systemd).
Сервис предназначен для работы на embedded Linux системах (Raspberry Pi, Orange Pi, Luckfox Pico и др.).


## Основные возможности:

- Мониторинг параметров: напряжение, ток, частота, угол фаз
- Минимальный период опроса 200мс, вывод предуреждения в syslog
- Пороговые значения: настраиваемые пределы с состояниями H/L/N
- Автоматическое логирование: запись данных в CSV-файлы
- Буферизация: эффективное сохранение данных с минимальным IO
- Real-time данные: передача через FIFO для других сервисов
- Автовосстановление: автоматическое переподключение при ошибках
- Гибкая конфигурация: отдельные конфиги для каждого экземпляра
- Время периода опроса учитвает реальное затраченое время на сам запрос и выполнеие всех расчетов

### В процессе:
- Исправить посчет мощности
- Определение порядка фаз по углу и учитывать для формирование статусов
- Поправить статусы углов фаз... (как учитывать 🤷) 

## Установка и сборка

### Предварительные требования
```bash
# Установка зависимостей (Debian/Ubuntu)
sudo apt update
sudo apt install build-essential libmodbus-dev

# Или для Alpine Linux
sudo apk add build-base libmodbus-dev
```
### Сборка из исходников

-  Клонирование или создание структуры проекта:
```bash
git clone https://github.com/akarnaukh/PZEM_6L24_Systemd.git
cd ./PZEM_6L24_Systemd
```
- Размещение файлов:
```text
src/pzem_monitor.h - заголовочный файл
src/pzem_monitor.c - основной код
config/pzem3_default.conf - конфигурация по умолчанию
systemd/pzem3@.service - systemd сервис
Makefile - система сборки
```
 - Сборка проекта:
 ```bash
 # Стандартная сборка с шаблонами
make

# Сборка с отладочной информацией
make debug

# Только создание шаблонов конфигурации
make templates
```
- Установка в систему:
```bash
sudo make install
```
## Удаление сервиса
```bash
# Полное удаление из системы
sudo make uninstall

# Логи и конфиги не удаляються автоматически
# Ручное удаление логов и конфигов
sudo rm -rf /etc/pzem3
sudo rm -rf /var/log/pzem3 # или как указано в конфигурации
```

## Настройка конфигурации
- Основной конфигурационный файл создается автоматически в /etc/pzem/default.conf:
```ini
# PZEM-6L24 Default Configuration

# Serial port settings
tty_port = /dev/ttyS1 
baudrate = 9600
slave_addr = 1
# Период опроса в мс (допустимый диапазон 200 - 10000мс)
poll_interval_ms = 500 

# Logging settings
log_dir = /var/log/pzem3
# Размер буфера логов в строках (1-25)
log_buffer_size = 10

# Sensitivity settings
# Чувствительность, на какие значения должны измениться данные
# Чтобы считать, что они изменились
voltage_sensitivity = 0.1
current_sensitivity = 0.01
frequency_sensitivity = 0.01
angleV_sensitivity = 0.1
angleI_sensitivity = 0.1
power_sensitivity = 0.1

# Voltage thresholds (0 = disabled)
# Пороговые значения, по которым выставляются статусы H, L, N
voltage_high_alarm = 245
voltage_high_warning = 240
voltage_low_warning = 210
voltage_low_alarm = 200

# Frequency thresholds (0 = disabled)
frequency_high_alarm = 52
frequency_high_warning = 51
frequency_low_warning = 49
frequency_low_alarm = 48

# Angle rotate V thresholds (0 = disabled)
angleV_high_alarm = 0
angleV_high_warning = 0
angleV_low_warning = 0
angleV_low_alarm = 0

# Angle rotate I thresholds (0 = disabled)
angleI_high_alarm = 0
angleI_high_warning = 0
angleI_low_warning = 0
angleI_low_alarm = 0
```
- Создание дополнительных конфигураций:
```bash
sudo cp /etc/pzem3/default.conf /etc/pzem3/input1.conf
sudo nano /etc/pzem3/input1.conf  # редактирование настроек
```

## Управление сервисом
```bash
# Запуск сервиса с разными конфигурациями
sudo systemctl start pzem3@default
sudo systemctl start pzem3@input1

# Автозагрузка при старте системы
sudo systemctl enable pzem3@input1

# Просмотр статуса
sudo systemctl status pzem3@input1

# Просмотр логов
sudo journalctl -u pzem3@input1 -f

# Остановка сервиса
sudo systemctl stop pzem3@input1
```
## Структура лог-файлов
- Лог-файлы создаются в директории указанной в конфигурации (default /var/log/pzem3/) в формате: `pzem3_<config>_YYYY-MM-DD.log`
```text
/var/log/pzem3/
├── pzem3_default_2024-01-15.log
└── pzem3_input1_2024-01-15.log
```

- Формат данных в логе (CSV):
```csv
дата, время, напряжение A, состояние_напряжения A, напряжение B, состояние_напряжения B, напряжение C, состояние_напряжения C, ток A, состояние_тока A, ток B, состояние_тока B, ток C, состояние_тока C, частота A, состояние_частоты A, частота B, состояние_частоты B, частота C, состояние_частоты C, угол V B, сотояние угла V B, угол V C, сотояние угла V C, угол I A, сотояние угла I A, угол I B, сотояние угла I B, угол I C, сотояние угла I C, мощность A, мощность B, мощность C, статус
2025-10-24,16:12:58,221.3,N,221.8,N,224.9,N,0.00,N,0.00,N,0.00,N,49.93,N,49.89,N,49.91,N,239.31,N,119.75,N,0.00,N,0.00,N,0.00,N,0.0,0.0,0.0,0
2025-10-24,16:13:03,219.9,N,221.7,N,225.7,N,0.00,N,0.00,N,0.00,N,49.93,N,49.89,N,49.90,N,239.15,N,120.02,N,0.00,N,0.00,N,0.00,N,0.0,0.0,0.0,0
2025-10-24,16:13:06,221.3,N,221.8,N,224.7,N,0.00,N,0.00,N,0.00,N,49.90,N,49.91,N,49.87,N,239.17,N,119.98,N,0.00,N,0.00,N,0.00,N,0.0,0.0,0.0,0
2025-10-24,16:13:21,222.5,N,221.7,N,224.3,N,0.00,N,0.00,N,0.00,N,49.93,N,49.91,N,49.86,N,239.93,N,120.27,N,0.00,N,0.00,N,0.00,N,0.0,0.0,0.0,0
```
### Статусы состояний:
- N - норма (в пределах порогов)
- H - высокое значение (превышение верхнего порога)
- L - низкое значение (ниже нижнего порога)
### Коды статуса:
- 0 - OK (данные успешно прочитаны)
- 1 - DEVICE_ERROR (ошибка устройства)
- 2 - PORT_ERROR (ошибка последовательного порта)

## Использование FIFO для внешних сервисов

- Сервис создает named pipe для реальной передачи данных:
```bash
# Чтение данных в реальном времени ( /tmp/pzem3_data_{config_name} )
tail -f /tmp/pzem3_data_input1

# Использование в скриптах
while read line; do
    echo "Received: $line"
    # Обработка данных...
done < /tmp/pzem3_data_input1
```
## Примеры использования

### Для мониторинга одной фазы:
```bash
sudo systemctl start pzem3@default
sudo journalctl -u pzem3@default -f
```
### Для трехфазной системы:
```bash
sudo systemctl start pzem3@input1
sudo systemctl start pzem3@input2  
sudo systemctl enable pzem3@input1 pzem3@input2
```
### Для embedded устройства (минимальная нагрузка):
```ini
# В конфиге
# Опрашиваем 1 раз в секунду
poll_interval_ms = 1000
# Буфер на 10 строк
log_buffer_size = 10
```
## Решение проблем

### Сервис не запускается

- Проверьте правильность пути к serial порту в конфиге
- Убедитесь что порт доступен: `ls -la /dev/ttyS*` 
- Проверьте права: `s`udo usermod -a -G dialout $USER`
### Нет данных в логах
- Проверьте подключение PZEM-6L24
- Убедитесь в правильности slave address
- Проверьте логи: `sudo journalctl -u pzem3@{config_name}`
### Высокая нагрузка CPU
- Увеличьте `poll_interval_ms` в конфигурации (рекомендуется 500-1000ms)
## Authors

- [@AKA_ZejroN](https://github.com/akarnaukh)
