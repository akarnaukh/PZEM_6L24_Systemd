/*
Copyright (c) 2010, 2011 the Friendika Project
All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "pzem_monitor.h"

// Глобальные переменные
modbus_t *ctx = NULL;
volatile int keep_running = 1;
log_buffer_t log_buffer = {0};
pzem_config_t global_config;
char *service_name = "pzem";
char config_name[64] = "default";
// Глобальная переменная для FIFO
char fifo_path[256];

// Обработчик сигналов
void signal_handler(int sig) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "Received signal %d, setting keep_running to 0", sig);
#endif
    keep_running = 0;
    syslog(LOG_INFO, "Received signal %d, shutting down", sig);
}

void signal_hup(int sig) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "Received signal hup %d, setting keep_running to 0", sig);
#endif
}

// Функция извлечения имени конфигурации из пути
void extract_config_name(const char *config_path) {
    const char *last_slash = strrchr(config_path, '/');
    if (last_slash == NULL) {
        last_slash = config_path;
    } else {
        last_slash++;
    }
    
    strncpy(config_name, last_slash, sizeof(config_name) - 1);
    config_name[sizeof(config_name) - 1] = '\0';
    
    char *dot = strrchr(config_name, '.');
    if (dot != NULL && (strcmp(dot, ".conf") == 0 || strcmp(dot, ".cfg") == 0)) {
        *dot = '\0';
    }
    
    if (strlen(config_name) == 0) {
        strcpy(config_name, "default");
    }
}

// Инициализация FIFO
int init_data_fifo(const char *fifo_path) {
    unlink(fifo_path); // Удаляем старый FIFO если существует
    if (mkfifo(fifo_path, 0666) == -1) {
        syslog(LOG_ERR, "Failed to create FIFO %s: %s", fifo_path, strerror(errno));
        return -1;
    }
    syslog(LOG_INFO, "Data FIFO created: %s", fifo_path);
    return 0;
}

// Запись данных в FIFO const
int write_to_fifo(const char *fifo_path, char *data) {
    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        // Если нет читателей - это нормально, просто пропускаем запись
        if (errno == ENXIO) {
            return 0;
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "Failed to open FIFO %s: %s", fifo_path, strerror(errno));
#endif
        return -1;
    }

//    const char *estr = "\n";
//    strcat(data, estr);
    int bytes_written = write(fd, data, strlen(data));
    if (bytes_written == -1) {
#ifdef DEBUG
	// Ошибка записи (скорее всего нет читателей)
        syslog(LOG_DEBUG, "Failed to write to FIFO: %s", strerror(errno));
#endif
    } else {
#ifdef DEBUG
        syslog(LOG_DEBUG, "Successful to write to FIFO: %s", fifo_path);
#endif
    }

    close(fd);
    return (bytes_written == -1) ? -1 : 0;
}

// Очистка FIFO
void cleanup_fifo(const char *fifo_path) {
    unlink(fifo_path);
}

// Функция создания директории если не существует
int create_directory_if_not_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) == -1) {
            syslog(LOG_ERR, "Error creating directory '%s': %s", path, strerror(errno));
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        syslog(LOG_ERR, "Path '%s' exists but is not a directory", path);
        return -1;
    }
    return 0;
}

// Функция получения текущей даты в формате YYYY-MM-DD
void get_current_date(char *date_str, size_t size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(date_str, size, "%Y-%m-%d", tm_info);
}

// Функция получения текущего времени в формате HH:MM:SS
void get_current_time(char *time_str, size_t size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(time_str, size, "%H:%M:%S", tm_info);
}

// Функция получения пути к файлу лога для текущей даты
void get_log_file_path(char *path, size_t size, const char *log_dir) {
    char date_str[32];
    get_current_date(date_str, sizeof(date_str));
    snprintf(path, size, "%s/pzem_%s_%s.log", log_dir, config_name, date_str);
}

// Функция инициализации буфера логов
int init_log_buffer(log_buffer_t *buffer, int initial_capacity, const char *log_dir) {
    if (buffer->buffer != NULL) {
        free_log_buffer(buffer);
    }
    
    buffer->buffer = (char **)malloc(initial_capacity * sizeof(char *));
    if (buffer->buffer == NULL) {
        return -1;
    }
    buffer->capacity = initial_capacity;
    buffer->size = 0;
    buffer->write_index = 0;
    
    strncpy(buffer->log_dir, log_dir, sizeof(buffer->log_dir) - 1);
    buffer->log_dir[sizeof(buffer->log_dir) - 1] = '\0';
    
    strncpy(buffer->config_name, config_name, sizeof(buffer->config_name) - 1);
    buffer->config_name[sizeof(buffer->config_name) - 1] = '\0';
    
    for (int i = 0; i < buffer->capacity; i++) {
        buffer->buffer[i] = NULL;
    }
    
    return 0;
}

// Функция добавления записи в буфер
int add_to_log_buffer(log_buffer_t *buffer, const char *log_entry) {
    if (buffer->buffer == NULL) {
        syslog(LOG_ERR, "Log buffer is not initialized");
        return -1;
    }
    
    // Если буфер полный, сбрасываем его в файл
    if (buffer->size >= buffer->capacity) {
#ifdef DEBUG
        syslog(LOG_DEBUG, "Buffer full (%d/%d), flushing...", buffer->size, buffer->capacity);
#endif
        if (flush_log_buffer(buffer) == -1) {
            syslog(LOG_ERR, "Failed to flush buffer to disk");
            return -1;
        }
    }
    
    char *entry_copy = strdup(log_entry);
    if (entry_copy == NULL) {
        syslog(LOG_ERR, "Failed to allocate memory for log entry");
        return -1;
    }
    
    buffer->buffer[buffer->write_index] = entry_copy;
    buffer->write_index = (buffer->write_index + 1) % buffer->capacity;
    buffer->size++;
#ifdef DEBUG
    syslog(LOG_DEBUG, "Added log entry to buffer (%d/%d)", buffer->size, buffer->capacity);
#endif
    return 0;
}

// Функция сброса буфера в файл
int flush_log_buffer(log_buffer_t *buffer) {
    if (buffer->buffer == NULL || buffer->size == 0) {
        return 0;
    }
    
    char log_path[512];
    get_log_file_path(log_path, sizeof(log_path), buffer->log_dir);
    
    // Проверяем доступность файла перед записью
    FILE *test_file = fopen(log_path, "a");
    if (test_file == NULL) {
        syslog(LOG_ERR, "Cannot open log file '%s' for appending: %s", log_path, strerror(errno));
        return -1;
    }
    fclose(test_file);
    
    // Теперь открываем для реальной записи
    FILE *log_file = fopen(log_path, "a");
    if (log_file == NULL) {
        syslog(LOG_ERR, "Error opening log file '%s': %s", log_path, strerror(errno));
        return -1;
    }
    
    // Устанавливаем правильные права на файл
    fchmod(fileno(log_file), 0644);
    
    for (int i = 0; i < buffer->size; i++) {
        int index = (buffer->write_index - buffer->size + i + buffer->capacity) % buffer->capacity;
        if (buffer->buffer[index] != NULL) {
//            fprintf(log_file, "%s\n", buffer->buffer[index]);
            fprintf(log_file, "%s", buffer->buffer[index]);
            free(buffer->buffer[index]);
            buffer->buffer[index] = NULL;
        }
    }

#ifdef DEBUG
    syslog(LOG_DEBUG, "Write log entry to log file %s, size: %d)", log_path, buffer->size);
#endif
    
    fflush(log_file);
    fclose(log_file);
    
    buffer->size = 0;
    buffer->write_index = 0;
    
    return 0;
}

// Функция освобождения буфера
void free_log_buffer(log_buffer_t *buffer) {
    if (buffer->buffer != NULL) {
        for (int i = 0; i < buffer->capacity; i++) {
            if (buffer->buffer[i] != NULL) {
                free(buffer->buffer[i]);
                buffer->buffer[i] = NULL;
            }
        }
        free(buffer->buffer);
        buffer->buffer = NULL;
    }
    buffer->size = 0;
    buffer->capacity = 0;
    buffer->write_index = 0;
}

// Функция проверки необходимости сброса буфера
int should_flush_buffer(const log_buffer_t *buffer) {
    return buffer->size >= buffer->capacity;
}

// Функция обновления состояний порогов
void update_threshold_states(pzem_data_t *data, const pzem_config_t *config) {
    // Напряжение
    if (config->voltage_high_alarm > 0) {
        if (data->voltage >= config->voltage_high_alarm) {
            data->voltage_state = 'H';
        } else if (data->voltage <= config->voltage_low_alarm) {
            data->voltage_state = 'L';
        } else if (data->voltage_state == 'H' && data->voltage > config->voltage_high_warning) {
            data->voltage_state = 'H';
        } else if (data->voltage_state == 'L' && data->voltage < config->voltage_low_warning) {
            data->voltage_state = 'L';
        } else {
            data->voltage_state = 'N';
        }
    } else {
        data->voltage_state = 'N';
    }
    
    // Ток
    if (config->current_high_alarm > 0) {
        if (data->current >= config->current_high_alarm) {
            data->current_state = 'H';
        } else if (data->current <= config->current_low_alarm) {
            data->current_state = 'L';
        } else if (data->current_state == 'H' && data->current > config->current_high_warning) {
            data->current_state = 'H';
        } else if (data->current_state == 'L' && data->current < config->current_low_warning) {
            data->current_state = 'L';
        } else {
            data->current_state = 'N';
        }
    } else {
        data->current_state = 'N';
    }
    
    // Частота
    if (config->frequency_high_alarm > 0) {
        if (data->frequency >= config->frequency_high_alarm) {
            data->frequency_state = 'H';
        } else if (data->frequency <= config->frequency_low_alarm) {
            data->frequency_state = 'L';
        } else if (data->frequency_state == 'H' && data->frequency > config->frequency_high_warning) {
            data->frequency_state = 'H';
        } else if (data->frequency_state == 'L' && data->frequency < config->frequency_low_warning) {
            data->frequency_state = 'L';
        } else {
            data->frequency_state = 'N';
        }
    } else {
        data->frequency_state = 'N';
    }
}

// Функция проверки изменения состояний порогов
int threshold_states_changed(const pzem_data_t *current, const pzem_data_t *previous) {
    return (current->voltage_state != previous->voltage_state) ||
           (current->current_state != previous->current_state) ||
           (current->frequency_state != previous->frequency_state);
}

// Функция подготовки строки лога с датой и временем
void prepare_log_entry(char *log_entry, size_t size, const pzem_data_t *data) {
    char date_str[32];
    char time_str[32];
    get_current_date(date_str, sizeof(date_str));
    get_current_time(time_str, sizeof(time_str));
    
    if (data->status == 0) {
        // Формат: дата, время, напряжение, состояние_напряжения, ток, состояние_тока, частота, состояние_частоты, мощность, статус
        snprintf(log_entry, size, "%s,%s,%.1f,%c,%.3f,%c,%.1f,%c,%.1f,%d\n",
                 date_str, time_str,
                 data->voltage, data->voltage_state,
                 data->current, data->current_state,
                 data->frequency, data->frequency_state,
                 data->power, data->status);
    } else {
        // При ошибке пишем прочерки
        snprintf(log_entry, size, "%s,%s,-,-,-,-,-,-,-,%d\n",
                 date_str, time_str, data->status);
    }
}

// Валидация конфигурации
int validate_config(const pzem_config_t *config) {
    if (strlen(config->tty_port) == 0) {
        syslog(LOG_ERR, "Invalid configuration: tty_port is empty");
        return -1;
    }
    
    if (config->baudrate <= 0) {
        syslog(LOG_ERR, "Invalid configuration: baudrate must be positive");
        return -1;
    }
    
    if (config->slave_addr < 1 || config->slave_addr > 247) {
        syslog(LOG_ERR, "Invalid configuration: slave_addr must be between 1 and 247");
        return -1;
    }
    
    if (config->poll_interval_ms < 100) {
        syslog(LOG_ERR, "Invalid configuration: poll_interval_ms must be at least 100ms");
        return -1;
    }
    
    if (strlen(config->log_dir) == 0) {
        syslog(LOG_ERR, "Invalid configuration: log_dir is empty");
        return -1;
    }
    
    // Проверка порогов напряжения
    if (config->voltage_high_alarm > 0) {
        if (config->voltage_high_alarm <= config->voltage_high_warning) {
            syslog(LOG_ERR, "Invalid voltage thresholds: high_alarm must be greater than high_warning");
            return -1;
        }
        if (config->voltage_high_warning <= config->voltage_low_warning) {
            syslog(LOG_ERR, "Invalid voltage thresholds: high_warning must be greater than low_warning");
            return -1;
        }
        if (config->voltage_low_warning <= config->voltage_low_alarm) {
            syslog(LOG_ERR, "Invalid voltage thresholds: low_warning must be greater than low_alarm");
            return -1;
        }
    }
    
    return 0;
}

// Функция загрузки конфигурации из файла
int load_config(const char *config_file, pzem_config_t *config) {
    FILE *file;
    char line[256];
    char key[64], value[64];
    
    // Установка значений по умолчанию
    strcpy(config->tty_port, "/dev/ttyS1");
    config->baudrate = 9600;
    config->slave_addr = 1;
    config->poll_interval_ms = 500;
    strcpy(config->log_dir, "/var/log/pzem");
    config->log_buffer_size = 20;
    
    // Чувствительность по умолчанию
    config->voltage_sensitivity = 0.1;
    config->current_sensitivity = 0.001;
    config->frequency_sensitivity = 0.1;
    config->power_sensitivity = 1.0;
    
    // Пороги по умолчанию (0 = отключено)
    config->voltage_high_alarm = 0;
    config->voltage_high_warning = 0;
    config->voltage_low_warning = 0;
    config->voltage_low_alarm = 0;
    
    config->current_high_alarm = 0;
    config->current_high_warning = 0;
    config->current_low_warning = 0;
    config->current_low_alarm = 0;
    
    config->frequency_high_alarm = 0;
    config->frequency_high_warning = 0;
    config->frequency_low_warning = 0;
    config->frequency_low_alarm = 0;
    
    file = fopen(config_file, "r");
    if (file == NULL) {
        syslog(LOG_ERR, "Config file '%s' not found", config_file);
        return -1;
    }
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        if (sscanf(line, "%63[^ =] = %63[^\n]", key, value) == 2) {
            char *trimmed_value = value;
            while (*trimmed_value == ' ') trimmed_value++;
            
            if (strcmp(key, "tty_port") == 0) {
                strncpy(config->tty_port, trimmed_value, sizeof(config->tty_port) - 1);
                config->tty_port[sizeof(config->tty_port) - 1] = '\0';
            }
            else if (strcmp(key, "baudrate") == 0) {
                config->baudrate = atoi(trimmed_value);
            }
            else if (strcmp(key, "slave_addr") == 0) {
                config->slave_addr = atoi(trimmed_value);
            }
            else if (strcmp(key, "poll_interval_ms") == 0) {
                config->poll_interval_ms = atoi(trimmed_value);
            }
            else if (strcmp(key, "log_dir") == 0) {
                strncpy(config->log_dir, trimmed_value, sizeof(config->log_dir) - 1);
                config->log_dir[sizeof(config->log_dir) - 1] = '\0';
            }
	    else if (strcmp(key, "log_buffer_size") == 0) {
                config->log_buffer_size = atoi(trimmed_value);
            }
            // Чувствительность
            else if (strcmp(key, "voltage_sensitivity") == 0) {
                config->voltage_sensitivity = atof(trimmed_value);
            }
            else if (strcmp(key, "current_sensitivity") == 0) {
                config->current_sensitivity = atof(trimmed_value);
            }
            else if (strcmp(key, "frequency_sensitivity") == 0) {
                config->frequency_sensitivity = atof(trimmed_value);
            }
            else if (strcmp(key, "power_sensitivity") == 0) {
                config->power_sensitivity = atof(trimmed_value);
            }
            // Пороги напряжения
            else if (strcmp(key, "voltage_high_alarm") == 0) {
                config->voltage_high_alarm = atof(trimmed_value);
            }
            else if (strcmp(key, "voltage_high_warning") == 0) {
                config->voltage_high_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "voltage_low_warning") == 0) {
                config->voltage_low_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "voltage_low_alarm") == 0) {
                config->voltage_low_alarm = atof(trimmed_value);
            }
            // Пороги тока
            else if (strcmp(key, "current_high_alarm") == 0) {
                config->current_high_alarm = atof(trimmed_value);
            }
            else if (strcmp(key, "current_high_warning") == 0) {
                config->current_high_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "current_low_warning") == 0) {
                config->current_low_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "current_low_alarm") == 0) {
                config->current_low_alarm = atof(trimmed_value);
            }
            // Пороги частоты
            else if (strcmp(key, "frequency_high_alarm") == 0) {
                config->frequency_high_alarm = atof(trimmed_value);
            }
            else if (strcmp(key, "frequency_high_warning") == 0) {
                config->frequency_high_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "frequency_low_warning") == 0) {
                config->frequency_low_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "frequency_low_alarm") == 0) {
                config->frequency_low_alarm = atof(trimmed_value);
            }
        }
    }
    
    fclose(file);

    // ПРОВЕРКИ КОРРЕКТНОСТИ ЗНАЧЕНИЙ
    int config_changed = 0;
    
    // Проверка интервала опроса
    if (config->poll_interval_ms < 100) {
        syslog(LOG_WARNING, "Poll interval too small (%dms), setting to 100ms", config->poll_interval_ms);
        config->poll_interval_ms = 100;
        config_changed = 1;
    } else if (config->poll_interval_ms > 10000) {
        syslog(LOG_WARNING, "Poll interval too large (%dms), setting to 10000ms", config->poll_interval_ms);
        config->poll_interval_ms = 10000;
        config_changed = 1;
    }
    
    // Проверка размера буфера логов
    if (config->log_buffer_size < 1) {
        syslog(LOG_WARNING, "Log buffer size too small (%d), setting to 1", config->log_buffer_size);
        config->log_buffer_size = 1;
        config_changed = 1;
    } else if (config->log_buffer_size > 25) {
        syslog(LOG_WARNING, "Log buffer size too large (%d), setting to 25", config->log_buffer_size);
        config->log_buffer_size = 25;
        config_changed = 1;
    }
    
    if (config_changed) {
        syslog(LOG_INFO, "Adjusted configuration: poll_interval=%dms, buffer_size=%d", 
               config->poll_interval_ms, config->log_buffer_size);
    }

    return 0;
}

// Функция для сравнения значений с учетом чувствительности
int values_changed(const pzem_data_t *current, const pzem_data_t *previous, const pzem_config_t *config) {
    if (previous->first_read) return 1;
    if (current->status != previous->status) return 1;
    if (fabsf(current->voltage - previous->voltage) > config->voltage_sensitivity) return 1;
    if (fabsf(current->current - previous->current) > config->current_sensitivity) return 1;
    if (fabsf(current->frequency - previous->frequency) > config->frequency_sensitivity) return 1;
    if (fabsf(current->power - previous->power) > config->power_sensitivity) return 1;
    
    return 0;
}

// Функция инициализации Modbus соединения
int init_modbus_connection(const pzem_config_t *config) {
    ctx = modbus_new_rtu(config->tty_port, config->baudrate, 'N', 8, 1);
    if (ctx == NULL) {
        syslog(LOG_ERR, "Unable to create Modbus context for %s", config->tty_port);
        return -1;
    }
    
    modbus_set_response_timeout(ctx, 1, 0);
    modbus_set_byte_timeout(ctx, 0, 500000);
    modbus_set_slave(ctx, config->slave_addr);
    
    if (modbus_connect(ctx) == -1) {
        syslog(LOG_ERR, "Connection failed to %s: %s", config->tty_port, modbus_strerror(errno));
        modbus_free(ctx);
        ctx = NULL;
        return -1;
    }
    
    syslog(LOG_INFO, "Modbus connection established to %s", config->tty_port);
    return 0;
}

// Функция чтения данных с PZEM (добавляем чтение мощности)
int read_pzem_data(pzem_data_t *data) {
    uint16_t tab_reg[10];
    int rc;

    if (ctx == NULL) {
        data->status = 2;
        return -1;
    }

    rc = modbus_read_input_registers(ctx, 0x0000, 10, tab_reg);
    if (rc == -1) {
        data->status = 1;
        return -1;
    }

    data->voltage = (float)tab_reg[0] / 10.0f;
    
    // Ток: объединяем два 16-битных регистра в 32-битное значение
    uint32_t current = ((uint32_t)tab_reg[1] << 16) | tab_reg[2];
    data->current = (float)current / 1000.0f;
    
    // Мощность: объединяем два 16-битных регистра в 32-битное значение
    uint32_t power = ((uint32_t)tab_reg[3] << 16) | tab_reg[4];
    data->power = (float)power / 10.0f;
    
    data->frequency = (float)tab_reg[7] / 10.0f;
    data->status = 0;

    return 0;
}

// Функция очистки ресурсов
void cleanup(void) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "Cleanup started");
#endif
    
    // Принудительно сбрасываем буфер логов
    if (log_buffer.buffer != NULL) {
#ifdef DEBUG
        syslog(LOG_DEBUG, "Flushing log buffer (%d records)", log_buffer.size);
#endif
        flush_log_buffer(&log_buffer);
    }
    
    // Очищаем FIFO
    //cleanup_fifo(fifo_path);
    
    if (ctx != NULL) {
        modbus_close(ctx);
        modbus_free(ctx);
        ctx = NULL;
        syslog(LOG_INFO, "Modbus connection closed");
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "Cleanup completed");
#endif
}

// Функция безопасного переподключения
void safe_reconnect(const pzem_config_t *config) {
    syslog(LOG_WARNING, "Multiple errors detected, attempting reconnect...");
    cleanup();
    
    if (log_buffer.buffer == NULL) {
//        if (init_log_buffer(&log_buffer, LOG_BUFFER_SIZE, config->log_dir) == -1) {
	if (init_log_buffer(&log_buffer, global_config.log_buffer_size, global_config.log_dir) == -1) {
            syslog(LOG_ERR, "Failed to reinitialize log buffer");
        }
    }
    
    usleep(1000000);
    if (init_modbus_connection(config) == 0) {
        syslog(LOG_INFO, "Reconnected successfully");
/*
	if (init_data_fifo(fifo_path) == -1) {
	    syslog(LOG_WARNING, "Failed to create data FIFO, data broadcasting disabled");
	} else {
	    syslog(LOG_INFO, "Data broadcasting enabled via FIFO: %s", fifo_path);
	}
*/
    }
}

int main(int argc, char *argv[]) {
    pzem_data_t current_data, previous_data;
    const char *config_file = "/etc/pzem/default.conf";
    
    if (argc > 1) {
        config_file = argv[1];
    }
    
    extract_config_name(config_file);
    
    char syslog_ident[128];
    snprintf(syslog_ident, sizeof(syslog_ident), "pzem-%s", config_name);
    service_name = syslog_ident;
    
    openlog(service_name, LOG_PID | LOG_CONS, LOG_DAEMON);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_hup);
    signal(SIGQUIT, signal_handler);
    
    syslog(LOG_INFO, "PZEM-004T Monitor starting with config: %s", config_file);

    // Создаем FIFO для передачи данных
    snprintf(fifo_path, sizeof(fifo_path), "/tmp/pzem_data_%s", config_name);
    if (init_data_fifo(fifo_path) == -1) {
	syslog(LOG_WARNING, "Failed to create data FIFO, data broadcasting disabled");
    } else {
	syslog(LOG_INFO, "Data broadcasting enabled via FIFO: %s", fifo_path);
    }
    
    if (load_config(config_file, &global_config) == -1) {
        syslog(LOG_ERR, "Failed to load configuration");
        closelog();
        return 1;
    }
    
    if (validate_config(&global_config) == -1) {
        syslog(LOG_ERR, "Configuration validation failed");
        closelog();
        return 1;
    }
    
    if (create_directory_if_not_exists(global_config.log_dir) == -1) {
        syslog(LOG_ERR, "Failed to create log directory");
        closelog();
        return 1;
    }
    
    // Инициализация буфера логов
//    if (init_log_buffer(&log_buffer, LOG_BUFFER_SIZE, global_config.log_dir) == -1) {
    if (init_log_buffer(&log_buffer, global_config.log_buffer_size, global_config.log_dir) == -1) {
        syslog(LOG_ERR, "Failed to initialize log buffer");
        closelog();
        return 1;
    }
    
    // ДОБАВЛЕНО: Проверяем доступность лог-файла при старте
    char log_path[512];
    get_log_file_path(log_path, sizeof(log_path), global_config.log_dir);
    FILE *test_file = fopen(log_path, "a");
    if (test_file == NULL) {
        syslog(LOG_ERR, "Cannot access log file '%s': %s", log_path, strerror(errno));
    } else {
        fclose(test_file);
        syslog(LOG_INFO, "Log file accessible: %s", log_path);
    }

    memset(&current_data, 0, sizeof(current_data));
    memset(&previous_data, 0, sizeof(previous_data));
    previous_data.first_read = 1;
    previous_data.status = 2;
    
    // Инициализация состояний
    current_data.voltage_state = 'N';
    current_data.current_state = 'N';
    current_data.frequency_state = 'N';
    previous_data.voltage_state = 'N';
    previous_data.current_state = 'N';
    previous_data.frequency_state = 'N';

    char thresholds[128] = "";
    if (global_config.voltage_high_alarm > 0) strcat(thresholds, "V");
    if (global_config.current_high_alarm > 0) strcat(thresholds, "I");
    if (global_config.frequency_high_alarm > 0) strcat(thresholds, "Z");
#ifdef DEBUG
    syslog(LOG_DEBUG, "Debug mode enabled");
#endif
    syslog(LOG_INFO, "Config: %s@%d, addr=%d, interval=%dms, thresholds=%s", 
           global_config.tty_port, global_config.baudrate, global_config.slave_addr, 
           global_config.poll_interval_ms, thresholds);

    if (init_modbus_connection(&global_config) == -1) {
        syslog(LOG_ERR, "Failed to initialize Modbus connection");
        cleanup();
        closelog();
        return 1;
    }

    syslog(LOG_INFO, "Monitoring started for config: %s", config_name);
    
    int error_count = 0;
    
    while (keep_running) {
        if (read_pzem_data(&current_data) == -1) {
            current_data.status = 1;
        } else {
            // Обновляем состояния порогов только при успешном чтении
            update_threshold_states(&current_data, &global_config);
        }

        // Проверяем изменения значений ИЛИ изменений состояний порогов
        int data_changed = values_changed(&current_data, &previous_data, &global_config);
        int states_changed = threshold_states_changed(&current_data, &previous_data);

	if (data_changed || states_changed) {
	    char log_entry[256];
	    prepare_log_entry(log_entry, sizeof(log_entry), &current_data);
#ifdef DEBUG
	    // Отладочное сообщение
	    syslog(LOG_DEBUG, "Data changed, preparing to write: %s", log_entry);
#endif
	    // СРАЗУ отправляем в FIFO
	    if (write_to_fifo(fifo_path, log_entry) == -1) {
#ifdef DEBUG
		syslog(LOG_DEBUG, "Failed to write to FIFO (no readers?)");
	    } else {
		syslog(LOG_DEBUG, "Data sent to FIFO: %s", log_entry);
#endif
	    }
	    // Затем добавляем в буфер логов
	    if (add_to_log_buffer(&log_buffer, log_entry) == -1) {
#ifdef DEBUG
		syslog(LOG_DEBUG, "Failed to add to log buffer");
#endif
	    }

	    previous_data = current_data;
	    previous_data.first_read = 0;
	}

        // Сбрасываем буфер если он полный
        if (should_flush_buffer(&log_buffer)) {
            flush_log_buffer(&log_buffer);
        }
        
        if (current_data.status != 0) {
            error_count++;
            if (error_count > 10) {
                safe_reconnect(&global_config);
                error_count = 0;
            }
        } else {
            error_count = 0;
        }
        
        usleep(global_config.poll_interval_ms * 1000);
    }
    
    syslog(LOG_INFO, "Monitoring stopped for config: %s", config_name);
    
    if (log_buffer.buffer != NULL) {
        flush_log_buffer(&log_buffer);
        free_log_buffer(&log_buffer);
    }
    
    if (ctx != NULL) {
        modbus_close(ctx);
        modbus_free(ctx);
    }
    
    closelog();
    
    return 0;
}
