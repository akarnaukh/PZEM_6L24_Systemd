
# Компиляция
gcc -o pzem_monitor pzem_monitor.c -lmodbus -lm

arm-linux-gnueabihf-gcc -o pzem_monitor pzem_monitor.c -lmodbus -lm

# Установка
sudo cp pzem_monitor /usr/local/bin/
sudo mkdir -p /etc/pzem
sudo cp config1.conf /etc/pzem/

sudo cp pzem@.service /lib/systemd/system/
--- OR ----
sudo cp pzem@.service /etc/systemd/system/

# Запуск сервиса
sudo systemctl daemon-reload
sudo systemctl enable pzem@config1.service
sudo systemctl start pzem@config1.service

# Просмотр логов
sudo journalctl -u pzem@config1 -f


# Запуск с разными конфигурациями
sudo systemctl start pzem@config1
sudo systemctl start pzem@config2

# Статус сервиса
sudo systemctl status pzem@config1

# Остановка
sudo systemctl stop pzem@config1




# Создать шаблоны конфигурации и сервиса
make templates

# Собрать программу
make

# Собрать с отладочной информацией
make debug

# Установить в систему
sudo make install

# Запустить сервис
sudo systemctl start pzem@default

# Создать дополнительную конфигурацию
sudo cp /etc/pzem/default.conf /etc/pzem/garage.conf
# отредактировать garage.conf
sudo systemctl start pzem@garage

# Полностью удалить
sudo make uninstall

# Очистка
make clean
make distclean

# Помощь
make help