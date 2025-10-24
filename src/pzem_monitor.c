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
char *service_name = "pzem3";
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

float lsbVal (uint16_t dat) {
    return ((dat & 0xff) << 8) | (dat >> 8);
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
// Функция получения текущего времени в милисекундах
long long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
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
    snprintf(path, size, "%s/pzem3_%s_%s.log", log_dir, config_name, date_str);
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
    // Напряжение A
    if (config->voltage_high_alarm > 0) {
        if (data->voltage_A >= config->voltage_high_alarm) {
            data->voltage_state_A = 'H';
        } else if (data->voltage_A <= config->voltage_low_alarm) {
            data->voltage_state_A = 'L';
        } else if (data->voltage_state_A == 'H' && data->voltage_A > config->voltage_high_warning) {
            data->voltage_state_A = 'H';
        } else if (data->voltage_state_A == 'L' && data->voltage_A < config->voltage_low_warning) {
            data->voltage_state_A = 'L';
        } else {
            data->voltage_state_A = 'N';
        }
    } else {
        data->voltage_state_A = 'N';
    }
    
    // Напряжение B
    if (config->voltage_high_alarm > 0) {
        if (data->voltage_B >= config->voltage_high_alarm) {
            data->voltage_state_B = 'H';
        } else if (data->voltage_B <= config->voltage_low_alarm) {
            data->voltage_state_B = 'L';
        } else if (data->voltage_state_B == 'H' && data->voltage_B > config->voltage_high_warning) {
            data->voltage_state_A = 'H';
        } else if (data->voltage_state_B == 'L' && data->voltage_B < config->voltage_low_warning) {
            data->voltage_state_B = 'L';
        } else {
            data->voltage_state_B = 'N';
        }
    } else {
        data->voltage_state_B = 'N';
    }

    // Напряжение C
    if (config->voltage_high_alarm > 0) {
        if (data->voltage_C >= config->voltage_high_alarm) {
            data->voltage_state_C = 'H';
        } else if (data->voltage_C <= config->voltage_low_alarm) {
            data->voltage_state_C = 'L';
        } else if (data->voltage_state_C == 'H' && data->voltage_C > config->voltage_high_warning) {
            data->voltage_state_C = 'H';
        } else if (data->voltage_state_C == 'L' && data->voltage_C < config->voltage_low_warning) {
            data->voltage_state_C = 'L';
        } else {
            data->voltage_state_C = 'N';
        }
    } else {
        data->voltage_state_C = 'N';
    }

    // Ток A
    if (config->current_high_alarm > 0) {
        if (data->current_A >= config->current_high_alarm) {
            data->current_state_A = 'H';
        } else if (data->current_A <= config->current_low_alarm) {
            data->current_state_A = 'L';
        } else if (data->current_state_A == 'H' && data->current_A > config->current_high_warning) {
            data->current_state_A = 'H';
        } else if (data->current_state_A == 'L' && data->current_A < config->current_low_warning) {
            data->current_state_A = 'L';
        } else {
            data->current_state_A = 'N';
        }
    } else {
        data->current_state_A = 'N';
    }

    // Ток B
    if (config->current_high_alarm > 0) {
        if (data->current_B >= config->current_high_alarm) {
            data->current_state_B = 'H';
        } else if (data->current_B <= config->current_low_alarm) {
            data->current_state_B = 'L';
        } else if (data->current_state_B == 'H' && data->current_B > config->current_high_warning) {
            data->current_state_B = 'H';
        } else if (data->current_state_B == 'L' && data->current_B < config->current_low_warning) {
            data->current_state_B = 'L';
        } else {
            data->current_state_B = 'N';
        }
    } else {
        data->current_state_B = 'N';
    }
    
    // Ток C
    if (config->current_high_alarm > 0) {
        if (data->current_C >= config->current_high_alarm) {
            data->current_state_C = 'H';
        } else if (data->current_C <= config->current_low_alarm) {
            data->current_state_C = 'L';
        } else if (data->current_state_C == 'H' && data->current_C > config->current_high_warning) {
            data->current_state_C = 'H';
        } else if (data->current_state_C == 'L' && data->current_C < config->current_low_warning) {
            data->current_state_C = 'L';
        } else {
            data->current_state_C = 'N';
        }
    } else {
        data->current_state_C = 'N';
    }

    // Частота A
    if (config->frequency_high_alarm > 0) {
        if (data->frequency_A >= config->frequency_high_alarm) {
            data->frequency_state_A = 'H';
        } else if (data->frequency_A <= config->frequency_low_alarm) {
            data->frequency_state_A = 'L';
        } else if (data->frequency_state_A == 'H' && data->frequency_A > config->frequency_high_warning) {
            data->frequency_state_A = 'H';
        } else if (data->frequency_state_A == 'L' && data->frequency_A < config->frequency_low_warning) {
            data->frequency_state_A = 'L';
        } else {
            data->frequency_state_A = 'N';
        }
    } else {
        data->frequency_state_A = 'N';
    }

    // Частота B
    if (config->frequency_high_alarm > 0) {
        if (data->frequency_B >= config->frequency_high_alarm) {
            data->frequency_state_B = 'H';
        } else if (data->frequency_B <= config->frequency_low_alarm) {
            data->frequency_state_B = 'L';
        } else if (data->frequency_state_B == 'H' && data->frequency_B > config->frequency_high_warning) {
            data->frequency_state_B = 'H';
        } else if (data->frequency_state_B == 'L' && data->frequency_B < config->frequency_low_warning) {
            data->frequency_state_B = 'L';
        } else {
            data->frequency_state_B = 'N';
        }
    } else {
        data->frequency_state_B = 'N';
    }

    // Частота C
    if (config->frequency_high_alarm > 0) {
        if (data->frequency_C >= config->frequency_high_alarm) {
            data->frequency_state_C = 'H';
        } else if (data->frequency_C <= config->frequency_low_alarm) {
            data->frequency_state_C = 'L';
        } else if (data->frequency_state_C == 'H' && data->frequency_C > config->frequency_high_warning) {
            data->frequency_state_C = 'H';
        } else if (data->frequency_state_C == 'L' && data->frequency_C < config->frequency_low_warning) {
            data->frequency_state_C = 'L';
        } else {
            data->frequency_state_C = 'N';
        }
    } else {
        data->frequency_state_C = 'N';
    }

    // Угол фазы напряжения B
    if (config->angleV_high_alarm > 0) {
        if (data->angleV_B >= config->angleV_high_alarm) {
            data->angleV_state_B = 'H';
        } else if (data->angleV_B <= config->angleV_low_alarm) {
            data->angleV_state_B = 'L';
        } else if (data->angleV_state_B == 'H' && data->angleV_B > config->angleV_high_warning) {
            data->angleV_state_B = 'H';
        } else if (data->angleV_state_B == 'L' && data->angleV_B < config->angleV_low_warning) {
            data->angleV_state_B = 'L';
        } else {
            data->angleV_state_B = 'N';
        }
    } else {
        data->angleV_state_B = 'N';
    }

    // Угол фазы напряжения C
    if (config->angleV_high_alarm > 0) {
        if (data->angleV_C >= config->angleV_high_alarm) {
            data->angleV_state_C = 'H';
        } else if (data->angleV_C <= config->angleV_low_alarm) {
            data->angleV_state_C = 'L';
        } else if (data->angleV_state_C == 'H' && data->angleV_C > config->angleV_high_warning) {
            data->angleV_state_C = 'H';
        } else if (data->angleV_state_C == 'L' && data->angleV_C < config->angleV_low_warning) {
            data->angleV_state_C = 'L';
        } else {
            data->angleV_state_C = 'N';
        }
    } else {
        data->angleV_state_C = 'N';
    }

        // Угол фазы тока A
    if (config->angleI_high_alarm > 0) {
        if (data->angleI_A >= config->angleI_high_alarm) {
            data->angleI_state_A = 'H';
        } else if (data->angleI_A <= config->angleI_low_alarm) {
            data->angleI_state_A = 'L';
        } else if (data->angleI_state_A == 'H' && data->angleI_A > config->angleI_high_warning) {
            data->angleI_state_A = 'H';
        } else if (data->angleI_state_A == 'L' && data->angleI_A < config->angleI_low_warning) {
            data->angleI_state_A = 'L';
        } else {
            data->angleI_state_A = 'N';
        }
    } else {
        data->angleI_state_A = 'N';
    }

    // Угол фазы тока B
    if (config->angleI_high_alarm > 0) {
        if (data->angleI_B >= config->angleI_high_alarm) {
            data->angleI_state_B = 'H';
        } else if (data->angleI_B <= config->angleI_low_alarm) {
            data->angleI_state_B = 'L';
        } else if (data->angleI_state_B == 'H' && data->angleI_B > config->angleI_high_warning) {
            data->angleI_state_B = 'H';
        } else if (data->angleI_state_B == 'L' && data->angleI_B < config->angleI_low_warning) {
            data->angleI_state_B = 'L';
        } else {
            data->angleI_state_B = 'N';
        }
    } else {
        data->angleI_state_B = 'N';
    }

    // Угол фазы тока C
    if (config->angleI_high_alarm > 0) {
        if (data->angleI_C >= config->angleI_high_alarm) {
            data->angleI_state_C = 'H';
        } else if (data->angleI_C <= config->angleI_low_alarm) {
            data->angleI_state_C = 'L';
        } else if (data->angleI_state_C == 'H' && data->angleI_C > config->angleI_high_warning) {
            data->angleI_state_C = 'H';
        } else if (data->angleI_state_C == 'L' && data->angleI_C < config->angleI_low_warning) {
            data->angleI_state_C = 'L';
        } else {
            data->angleI_state_C = 'N';
        }
    } else {
        data->angleI_state_C = 'N';
    }

}

// Функция проверки изменения состояний порогов
int threshold_states_changed(const pzem_data_t *current, const pzem_data_t *previous) {
    return (current->voltage_state_A != previous->voltage_state_A) ||
           (current->voltage_state_B != previous->voltage_state_B) ||
           (current->voltage_state_C != previous->voltage_state_C) ||
           (current->current_state_A != previous->current_state_A) ||
           (current->current_state_B != previous->current_state_B) ||
           (current->current_state_C != previous->current_state_C) ||
           (current->frequency_state_A != previous->frequency_state_A) ||
           (current->frequency_state_B != previous->frequency_state_B) ||
           (current->frequency_state_C != previous->frequency_state_C) ||
           (current->angleV_state_B != previous->angleV_state_B) ||
           (current->angleV_state_C != previous->angleV_state_C) ||
           (current->angleI_state_A != previous->angleI_state_A) ||
           (current->angleI_state_B != previous->angleI_state_B) ||
           (current->angleI_state_C != previous->angleI_state_C);
}

// Функция подготовки строки лога с датой и временем
void prepare_log_entry(char *log_entry, size_t size, const pzem_data_t *data) {
    char date_str[32];
    char time_str[32];
    get_current_date(date_str, sizeof(date_str));
    get_current_time(time_str, sizeof(time_str));
    
    if (data->status == 0) {
        // Формат: дата, время, напряжение, состояние_напряжения, ток, состояние_тока, частота, состояние_частоты, угол напряжения, состояние угла, угол тока, состояние угла, мощность , статус
        snprintf(log_entry, size, "%s,%s,%.1f,%c,%.1f,%c,%.1f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.1f,%.1f,%.1f,%d\n",
                 date_str, time_str,
                 data->voltage_A, data->voltage_state_A, data->voltage_B, data->voltage_state_B, data->voltage_C, data->voltage_state_C,
                 data->current_A, data->current_state_A, data->current_B, data->current_state_B, data->current_C, data->current_state_C,
                 data->frequency_A, data->frequency_state_A, data->frequency_B, data->frequency_state_B, data->frequency_C, data->frequency_state_C,
                 data->angleV_B, data->angleV_state_B, data->angleV_C, data->angleV_state_C,
                 data->angleI_A, data->angleI_state_A, data->angleI_B, data->angleI_state_B, data->angleI_C, data->angleI_state_C,
                 data->power_A, data->power_B, data->power_C, data->status);
    } else {
        // При ошибке пишем прочерки
        snprintf(log_entry, size, "%s,%s,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,%d\n",
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
    strcpy(config->log_dir, "/var/log/pzem3");
    config->log_buffer_size = 10;
    
    // Чувствительность по умолчанию
    config->voltage_sensitivity = 0.1;
    config->current_sensitivity = 0.01;
    config->frequency_sensitivity = 0.01;
    config->angleV_sensitivity = 0.01;
    config->angleI_sensitivity = 0.01;
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

    config->angleV_high_alarm = 0;
    config->angleV_high_warning = 0;
    config->angleV_low_warning = 0;
    config->angleV_low_alarm = 0;

    config->angleI_high_alarm = 0;
    config->angleI_high_warning = 0;
    config->angleI_low_warning = 0;
    config->angleI_low_alarm = 0;
    
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
            else if (strcmp(key, "angleV_sensitivity") == 0) {
                config->angleV_sensitivity = atof(trimmed_value);
            }
            else if (strcmp(key, "angleI_sensitivity") == 0) {
                config->angleI_sensitivity = atof(trimmed_value);
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
            // Пороги угла напряжения
            else if (strcmp(key, "angleV_high_alarm") == 0) {
                config->angleV_high_alarm = atof(trimmed_value);
            }
            else if (strcmp(key, "angleV_high_warning") == 0) {
                config->angleV_high_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "angleV_low_warning") == 0) {
                config->angleV_low_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "angleV_low_alarm") == 0) {
                config->angleV_low_alarm = atof(trimmed_value);
            }
            // Пороги угла тока
            else if (strcmp(key, "angleI_high_alarm") == 0) {
                config->angleI_high_alarm = atof(trimmed_value);
            }
            else if (strcmp(key, "angleI_high_warning") == 0) {
                config->angleI_high_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "angleI_low_warning") == 0) {
                config->angleI_low_warning = atof(trimmed_value);
            }
            else if (strcmp(key, "angleI_low_alarm") == 0) {
                config->angleI_low_alarm = atof(trimmed_value);
            }
        }
    }
    
    fclose(file);

    // ПРОВЕРКИ КОРРЕКТНОСТИ ЗНАЧЕНИЙ
    int config_changed = 0;
    
    // Проверка интервала опроса
    if (config->poll_interval_ms < 150) {
        syslog(LOG_WARNING, "Poll interval too small (%dms), setting to 150ms", config->poll_interval_ms);
        config->poll_interval_ms = 150;
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
    if (fabsf(current->voltage_A - previous->voltage_A) > config->voltage_sensitivity) return 1;
    if (fabsf(current->voltage_B - previous->voltage_B) > config->voltage_sensitivity) return 1;
    if (fabsf(current->voltage_C - previous->voltage_C) > config->voltage_sensitivity) return 1;

    if (fabsf(current->current_A - previous->current_A) > config->current_sensitivity) return 1;
    if (fabsf(current->current_B - previous->current_B) > config->current_sensitivity) return 1;
    if (fabsf(current->current_C - previous->current_C) > config->current_sensitivity) return 1;

    if (fabsf(current->frequency_A - previous->frequency_A) > config->frequency_sensitivity) return 1;
    if (fabsf(current->frequency_B - previous->frequency_B) > config->frequency_sensitivity) return 1;
    if (fabsf(current->frequency_C - previous->frequency_C) > config->frequency_sensitivity) return 1;

    if (fabsf(current->angleV_B - previous->angleV_B) > config->angleV_sensitivity) return 1;
    if (fabsf(current->angleV_C - previous->angleV_C) > config->angleV_sensitivity) return 1;

    if (fabsf(current->angleI_A - previous->angleI_A) > config->angleI_sensitivity) return 1;
    if (fabsf(current->angleI_B - previous->angleI_B) > config->angleI_sensitivity) return 1;
    if (fabsf(current->angleI_C - previous->angleI_C) > config->angleI_sensitivity) return 1;

    if (fabsf(current->power_A - previous->power_A) > config->power_sensitivity) return 1;
    if (fabsf(current->power_B - previous->power_B) > config->power_sensitivity) return 1;
    if (fabsf(current->power_C - previous->power_C) > config->power_sensitivity) return 1;
    
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
    uint16_t tab_reg[20];
    int rc;

    if (ctx == NULL) {
        data->status = 2;
        return -1;
    }

    rc = modbus_read_input_registers(ctx, 0x0000, 20, tab_reg);
    if (rc == -1) {
        data->status = 1;
        return -1;
    }
/*
Register Values (from last successful query):
---------------------------------------------
Register 0x0000: 0xBC08 (223.6V)    0
Register 0x0001: 0xA108 (220.9V)    1
Register 0x0002: 0xD208 (225.8V)    2

Register 0x0003: 0x0000 (0.00A)     3
Register 0x0004: 0x0000 (0.00A)     4
Register 0x0005: 0x0000 (0.00A)     5

Register 0x0006: 0x7F13 (49.91Hz)   6
Register 0x0007: 0x7E13 (49.90Hz)   7
Register 0x0008: 0x7E13 (49.90Hz)   8
AngleV
Register 0x0009: 0x825D (239.38°)   9
Register 0x000A: 0xC62E (119.74°)   10
AngleI
Register 0x000B: 0x0000 (0.00°)     11
Register 0x000C: 0x0000 (0.00°)     12
Register 0x000D: 0x0000 (0.00°)     13
Power
Register 0x000E: 0x0000 (0.0W)  Low     14
Register 0x000F: 0x0000 (0)     High    15
Register 0x0010: 0x0000 (0.0W)  Low     16
Register 0x0011: 0x0000 (0)     High    17
Register 0x0012: 0x0000 (0.0W)  Low     18
Register 0x0013: 0x0000 (0)     High    19
*/
    data->voltage_A = lsbVal(tab_reg[0]) / 10.0f;
    data->voltage_B = lsbVal(tab_reg[1]) / 10.0f;
    data->voltage_C = lsbVal(tab_reg[2]) / 10.0f;
    
    data->current_A = lsbVal(tab_reg[3]) / 100.0f;
    data->current_B = lsbVal(tab_reg[4]) / 100.0f;
    data->current_C = lsbVal(tab_reg[5]) / 100.0f;
    
    data->frequency_A = lsbVal(tab_reg[6]) / 100.0f;
    data->frequency_B = lsbVal(tab_reg[7]) / 100.0f;
    data->frequency_C = lsbVal(tab_reg[8]) / 100.0f;

    data->angleV_B = lsbVal(tab_reg[9]) / 100.0f;
    data->angleV_C = lsbVal(tab_reg[10]) / 100.0f;

    data->angleI_A = lsbVal(tab_reg[11]) / 100.0f;
    data->angleI_B = lsbVal(tab_reg[12]) / 100.0f;
    data->angleI_C = lsbVal(tab_reg[13]) / 100.0f;

    // Мощность: объединяем два 16-битных регистра в 32-битное значение
    uint32_t power_A = ((uint32_t)tab_reg[15] << 16) | tab_reg[14];
    uint32_t power_B = ((uint32_t)tab_reg[17] << 16) | tab_reg[16];
    uint32_t power_C = ((uint32_t)tab_reg[19] << 16) | tab_reg[18];
    data->power_A = (float)power_A / 10.0f;
    data->power_B = (float)power_B / 10.0f;
    data->power_C = (float)power_C / 10.0f;
    
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
    const char *config_file = "/etc/pzem3/default.conf";
    
    if (argc > 1) {
        config_file = argv[1];
    }
    
    extract_config_name(config_file);
    
    char syslog_ident[128];
    snprintf(syslog_ident, sizeof(syslog_ident), "pzem3-%s", config_name);
    service_name = syslog_ident;
    long long start_time, end_time;
    
    openlog(service_name, LOG_PID | LOG_CONS, LOG_DAEMON);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_hup);
    signal(SIGQUIT, signal_handler);
    
    syslog(LOG_INFO, "PZEM-6L24 Monitor starting with config: %s", config_file);

    // Создаем FIFO для передачи данных
    snprintf(fifo_path, sizeof(fifo_path), "/tmp/pzem3_data_%s", config_name);
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
    current_data.voltage_state_A = 'N';
    current_data.voltage_state_B = 'N';
    current_data.voltage_state_C = 'N';

    current_data.current_state_A = 'N';
    current_data.current_state_B = 'N';
    current_data.current_state_C = 'N';

    current_data.frequency_state_A = 'N';
    current_data.frequency_state_B = 'N';
    current_data.frequency_state_C = 'N';

    current_data.angleV_state_B = 'N';
    current_data.angleV_state_C = 'N';

    current_data.angleI_state_A = 'N';
    current_data.angleI_state_B = 'N';
    current_data.angleI_state_C = 'N';
    
    previous_data.voltage_state_A = 'N';
    previous_data.voltage_state_B = 'N';
    previous_data.voltage_state_C = 'N';

    previous_data.current_state_A = 'N';
    previous_data.current_state_B = 'N';
    previous_data.current_state_C = 'N';

    previous_data.frequency_state_A = 'N';
    previous_data.frequency_state_B = 'N';
    previous_data.frequency_state_C = 'N';

    previous_data.angleV_state_B = 'N';
    previous_data.angleV_state_C = 'N';

    previous_data.angleI_state_A = 'N';
    previous_data.angleI_state_B = 'N';
    previous_data.angleI_state_C = 'N';

    char thresholds[128] = "";
    if (global_config.voltage_high_alarm > 0) strcat(thresholds, "V");
    if (global_config.current_high_alarm > 0) strcat(thresholds, "I");
    if (global_config.frequency_high_alarm > 0) strcat(thresholds, "Z");
    if (global_config.angleV_high_alarm > 0) strcat(thresholds, "Av");
    if (global_config.angleI_high_alarm > 0) strcat(thresholds, "Ai");
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
        start_time = get_time_ms();
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
        //syslog(LOG_DEBUG, "Changed: data - %d, state - %d", data_changed, states_changed);
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
        end_time = get_time_ms();
        u_int16_t duration_ms = end_time - start_time;
        u_int16_t tSleep = global_config.poll_interval_ms - duration_ms;
#ifdef DEBUG
//		syslog(LOG_DEBUG, "tSleep - %d", tSleep);
#endif
        if (tSleep > 0) {
            usleep(tSleep * 1000);
        } else {
            syslog(LOG_ALERT, "Attention! The time spent on the request and processing (%d) is longer than the specified survey period %d", duration_ms, global_config.poll_interval_ms);
        };
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
