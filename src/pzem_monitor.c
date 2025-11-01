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
const char *version = "1.0.0";
// Глобальные переменные
modbus_t *ctx = NULL;
volatile sig_atomic_t keep_running = 1;
log_buffer_t log_buffer = {0};
pzem_config_t global_config;
char *service_name = "pzem3";
char config_name[64] = "default";
char fifo_path[256];
char device_type = 'U';
performance_metrics_t metrics = {0};

// Макрос для проверки изменений
#define CHECK_CHANGE(field, sensitivity) \
    (fabsf(current->field - previous->field) > config->sensitivity)

// Обработчик сигналов
void signal_handler(int sig) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "Received signal %d, shutting down", sig);
#endif
    keep_running = 0;
}

void setup_signal_handlers(void) {
    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = 0
    };
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Игнорируем SIGPIPE чтобы не падать при проблемах с FIFO
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
}

float lsbVal(uint16_t dat) {
    return (float)(((dat & 0xFF) << 8) | (dat >> 8));
}

// Функция извлечения имени конфигурации из пути
void extract_config_name(const char *config_path) {
    if (!config_path) {
        snprintf(config_name, sizeof(config_name), "default");
        return;
    }
    
    const char *last_slash = strrchr(config_path, '/');
    const char *basename = last_slash ? last_slash + 1 : config_path;
    
    // Копируем базовое имя
    strncpy(config_name, basename, sizeof(config_name) - 1);
    config_name[sizeof(config_name) - 1] = '\0';
    
    // Удаляем расширение если нужно
    char *dot = strrchr(config_name, '.');
    if (dot && (strcmp(dot, ".conf") == 0 || strcmp(dot, ".cfg") == 0)) {
        *dot = '\0';
    }
    
    if (config_name[0] == '\0') {
        snprintf(config_name, sizeof(config_name), "default");
    }
}

// Инициализация FIFO
int init_data_fifo(const char *fifo_path) {
    if (!fifo_path) {
        return -1;
    }
    
    unlink(fifo_path);
    if (mkfifo(fifo_path, 0666) == -1) {
        syslog(LOG_ERR, "Failed to create FIFO %s: %s", fifo_path, strerror(errno));
        return -1;
    }
    syslog(LOG_INFO, "Data FIFO created: %s", fifo_path);
    return 0;
}

// Запись данных в FIFO
int write_to_fifo(const char *fifo_path, const char *data) {
    if (!fifo_path || !data) {
        return -1;
    }
    
    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        if (errno == ENXIO) {
            return 0; // Нет читателей - это нормально
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "Failed to open FIFO %s: %s", fifo_path, strerror(errno));
#endif
        return -1;
    }

    ssize_t bytes_written = write(fd, data, strlen(data));
    close(fd);
    
    if (bytes_written == -1) {
#ifdef DEBUG
        syslog(LOG_DEBUG, "Failed to write to FIFO: %s", strerror(errno));
#endif
        return -1;
    }
    
#ifdef DEBUG
    syslog(LOG_DEBUG, "Successfully wrote to FIFO: %s", fifo_path);
#endif
    return 0;
}

// Очистка FIFO
void cleanup_fifo(const char *fifo_path) {
    if (fifo_path) {
        unlink(fifo_path);
    }
}

// Функция создания директории если не существует
int create_directory_if_not_exists(const char *path) {
    if (!path) {
        return -1;
    }
    
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

// Функция получения текущего времени в миллисекундах
long long get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// Функция получения текущей даты в формате YYYY-MM-DD
void get_current_date(char *date_str, size_t size) {
    if (!date_str || size == 0) return;
    
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(date_str, size, "%Y-%m-%d", tm_info);
}

// Функция получения текущего времени в формате HH:MM:SS
void get_current_time(char *time_str, size_t size) {
    if (!time_str || size == 0) return;
    
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(time_str, size, "%H:%M:%S", tm_info);
}

// Функция получения пути к файлу лога для текущей даты
void get_log_file_path(char *path, size_t size, const char *log_dir) {
    if (!path || !log_dir || size == 0) return;
    
    char date_str[32];
    get_current_date(date_str, sizeof(date_str));
    snprintf(path, size, "%s/pzem3_%s_%s.log", log_dir, config_name, date_str);
}

// Функция инициализации буфера логов
pzem_result_t init_log_buffer(log_buffer_t *buffer, int initial_capacity, const char *log_dir) {
    if (!buffer || !log_dir || initial_capacity <= 0) {
        return PZEM_ERROR_INVALID_PARAM;
    }
    
    // Освобождаем старый буфер если есть
    if (buffer->buffer != NULL) {
        free_log_buffer(buffer);
    }
    
    // Ограничиваем максимальный размер буфера
    if (initial_capacity > MAX_LOG_BUFFER_SIZE) {
        initial_capacity = MAX_LOG_BUFFER_SIZE;
    }
    
    buffer->buffer = (char **)malloc((size_t)initial_capacity * sizeof(char *));
    if (buffer->buffer == NULL) {
        syslog(LOG_ERR, "Failed to allocate log buffer");
        return PZEM_ERROR_MEMORY;
    }
    
    buffer->capacity = initial_capacity;
    buffer->size = 0;
    buffer->read_index = 0;
    buffer->write_index = 0;
    
    STRCPY_SAFE(buffer->log_dir, log_dir);
    STRCPY_SAFE(buffer->config_name, config_name);
    
    // Инициализируем мьютекс
    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        free(buffer->buffer);
        buffer->buffer = NULL;
        return PZEM_ERROR_MEMORY;
    }
    
    // Инициализируем все указатели в NULL
    for (int i = 0; i < buffer->capacity; i++) {
        buffer->buffer[i] = NULL;
    }
    
    return PZEM_SUCCESS;
}

// Функция добавления записи в буфер
pzem_result_t add_to_log_buffer(log_buffer_t *buffer, const char *log_entry) {
    if (!buffer || !log_entry || !buffer->buffer) {
        syslog(LOG_ERR, "Invalid parameters to add_to_log_buffer");
        return PZEM_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    
    // Если буфер полный, сбрасываем его в файл
    if (buffer->size >= buffer->capacity) {
#ifdef DEBUG
        syslog(LOG_DEBUG, "Buffer full (%d/%d), flushing...", buffer->size, buffer->capacity);
#endif
        if (flush_log_buffer(buffer) != PZEM_SUCCESS) {
            pthread_mutex_unlock(&buffer->mutex);
            syslog(LOG_ERR, "Failed to flush buffer to disk");
            return PZEM_ERROR_IO;
        }
    }
    
    char *entry_copy = strdup(log_entry);
    if (entry_copy == NULL) {
        pthread_mutex_unlock(&buffer->mutex);
        syslog(LOG_ERR, "Failed to allocate memory for log entry");
        return PZEM_ERROR_MEMORY;
    }
    
    // Освобождаем старую запись если нужно
    if (buffer->buffer[buffer->write_index] != NULL) {
        free(buffer->buffer[buffer->write_index]);
    }
    
    buffer->buffer[buffer->write_index] = entry_copy;
    buffer->write_index = (buffer->write_index + 1) % buffer->capacity;
    
    if (buffer->size < buffer->capacity) {
        buffer->size++;
    } else {
        // Буфер полный, двигаем read_index
        buffer->read_index = (buffer->read_index + 1) % buffer->capacity;
    }
    
#ifdef DEBUG
    syslog(LOG_DEBUG, "Added log entry to buffer (%d/%d)", buffer->size, buffer->capacity);
#endif
    
    pthread_mutex_unlock(&buffer->mutex);
    return PZEM_SUCCESS;
}

// Функция сброса буфера в файл
pzem_result_t flush_log_buffer(log_buffer_t *buffer) {
    if (!buffer || !buffer->buffer || buffer->size == 0) {
        return PZEM_SUCCESS;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    
    char log_path[512];
    get_log_file_path(log_path, sizeof(log_path), buffer->log_dir);
    
    // Проверяем доступность файла перед записью
    FILE *test_file = fopen(log_path, "a");
    if (test_file == NULL) {
        pthread_mutex_unlock(&buffer->mutex);
        syslog(LOG_ERR, "Cannot open log file '%s' for appending: %s", log_path, strerror(errno));
        return PZEM_ERROR_IO;
    }
    fclose(test_file);
    
    // Теперь открываем для реальной записи
    FILE *log_file = fopen(log_path, "a");
    if (log_file == NULL) {
        pthread_mutex_unlock(&buffer->mutex);
        syslog(LOG_ERR, "Error opening log file '%s': %s", log_path, strerror(errno));
        return PZEM_ERROR_IO;
    }
    
    // Устанавливаем правильные права на файл
    fchmod(fileno(log_file), 0644);
    
    for (int i = 0; i < buffer->size; i++) {
        int index = (buffer->read_index + i) % buffer->capacity;
        if (buffer->buffer[index] != NULL) {
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
    buffer->read_index = 0;
    buffer->write_index = 0;
    
    pthread_mutex_unlock(&buffer->mutex);
    return PZEM_SUCCESS;
}

// Функция освобождения буфера
void free_log_buffer(log_buffer_t *buffer) {
    if (!buffer) return;
    
    pthread_mutex_lock(&buffer->mutex);
    
    if (buffer->buffer != NULL) {
        for (int i = 0; i < buffer->capacity; i++) {
            safe_free((void**)&buffer->buffer[i]);
        }
        free(buffer->buffer);
        buffer->buffer = NULL;
    }
    
    buffer->size = 0;
    buffer->capacity = 0;
    buffer->read_index = 0;
    buffer->write_index = 0;
    
    pthread_mutex_unlock(&buffer->mutex);
    pthread_mutex_destroy(&buffer->mutex);
}

// Функция проверки необходимости сброса буфера
int should_flush_buffer(const log_buffer_t *buffer) {
    return buffer && buffer->size >= buffer->capacity;
}

// Вспомогательная функция для обработки одного параметра
void update_threshold_state(float value, char* state, const threshold_config_t* config) {
    if (!state || !config) return;
    
    if (config->high_alarm > 0) {
        if (value >= config->high_alarm) {
            *state = 'H';
        } else if (value <= config->low_alarm) {
            *state = 'L';
        } else if (*state == 'H' && value > config->high_warning) {
            *state = 'H';
        } else if (*state == 'L' && value < config->low_warning) {
            *state = 'L';
        } else {
            *state = 'N';
        }
    } else {
        *state = 'N';
    }
}

void update_threshold_states(pzem_data_t *data, const pzem_config_t *config) {
    if (!data || !config) return;
    
    // Обработка напряжений
    threshold_config_t voltage_config = {
        config->voltage_high_alarm,
        config->voltage_high_warning,
        config->voltage_low_warning,
        config->voltage_low_alarm
    };
    
    update_threshold_state(data->voltage_A, &data->voltage_state_A, &voltage_config);
    update_threshold_state(data->voltage_B, &data->voltage_state_B, &voltage_config);
    update_threshold_state(data->voltage_C, &data->voltage_state_C, &voltage_config);
    
    // Обработка токов
    threshold_config_t current_config = {
        config->current_high_alarm,
        config->current_high_warning,
        config->current_low_warning,
        config->current_low_alarm
    };
    
    update_threshold_state(data->current_A, &data->current_state_A, &current_config);
    update_threshold_state(data->current_B, &data->current_state_B, &current_config);
    update_threshold_state(data->current_C, &data->current_state_C, &current_config);
    
    // Обработка частот
    threshold_config_t frequency_config = {
        config->frequency_high_alarm,
        config->frequency_high_warning,
        config->frequency_low_warning,
        config->frequency_low_alarm
    };
    
    update_threshold_state(data->frequency_A, &data->frequency_state_A, &frequency_config);
    update_threshold_state(data->frequency_B, &data->frequency_state_B, &frequency_config);
    update_threshold_state(data->frequency_C, &data->frequency_state_C, &frequency_config);
    
    // Обработка углов напряжения
    threshold_config_t angleV_config = {
        config->angleV_high_alarm,
        config->angleV_high_warning,
        config->angleV_low_warning,
        config->angleV_low_alarm
    };
    
    update_threshold_state(data->angleV_B, &data->angleV_state_B, &angleV_config);
    update_threshold_state(data->angleV_C, &data->angleV_state_C, &angleV_config);
    
    // Обработка углов тока
    threshold_config_t angleI_config = {
        config->angleI_high_alarm,
        config->angleI_high_warning,
        config->angleI_low_warning,
        config->angleI_low_alarm
    };
    
    update_threshold_state(data->angleI_A, &data->angleI_state_A, &angleI_config);
    update_threshold_state(data->angleI_B, &data->angleI_state_B, &angleI_config);
    update_threshold_state(data->angleI_C, &data->angleI_state_C, &angleI_config);
}

// Функция проверки изменения состояний порогов
int threshold_states_changed(const pzem_data_t *current, const pzem_data_t *previous) {
    if (!current || !previous) return 1;
    
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
    if (!log_entry || !data || size == 0) return;
    
    char date_str[32];
    char time_str[32];
    get_current_date(date_str, sizeof(date_str));
    get_current_time(time_str, sizeof(time_str));
    
    if (data->status == 0) {
        snprintf(log_entry, size, 
                 "%s,%s,%.1f,%c,%.1f,%c,%.1f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.2f,%c,%.1f,%.1f,%.1f,%d\n",
                 date_str, time_str,
                 data->voltage_A, data->voltage_state_A, data->voltage_B, data->voltage_state_B, 
                 data->voltage_C, data->voltage_state_C, data->current_A, data->current_state_A,
                 data->current_B, data->current_state_B, data->current_C, data->current_state_C,
                 data->frequency_A, data->frequency_state_A, data->frequency_B, data->frequency_state_B,
                 data->frequency_C, data->frequency_state_C, data->angleV_B, data->angleV_state_B,
                 data->angleV_C, data->angleV_state_C, data->angleI_A, data->angleI_state_A,
                 data->angleI_B, data->angleI_state_B, data->angleI_C, data->angleI_state_C,
                 data->power_A, data->power_B, data->power_C, data->status);
    } else {
        snprintf(log_entry, size, "%s,%s,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,-,%d\n",
                 date_str, time_str, data->status);
    }
}

// Валидация конфигурации
pzem_result_t validate_config(const pzem_config_t *config) {
    if (!config) {
        return PZEM_ERROR_INVALID_PARAM;
    }
    
    if (strlen(config->tty_port) == 0) {
        syslog(LOG_ERR, "Invalid configuration: tty_port is empty");
        return PZEM_ERROR_CONFIG;
    }
    
    if (config->baudrate <= 0) {
        syslog(LOG_ERR, "Invalid configuration: baudrate must be positive");
        return PZEM_ERROR_CONFIG;
    }
    
    if (config->slave_addr < 1 || config->slave_addr > 247) {
        syslog(LOG_ERR, "Invalid configuration: slave_addr must be between 1 and 247");
        return PZEM_ERROR_CONFIG;
    }
    
    if (config->poll_interval_ms < MIN_POLL_INTERVAL) {
        syslog(LOG_ERR, "Invalid configuration: poll_interval_ms must be at least %dms", MIN_POLL_INTERVAL);
        return PZEM_ERROR_CONFIG;
    }
    
    if (strlen(config->log_dir) == 0) {
        syslog(LOG_ERR, "Invalid configuration: log_dir is empty");
        return PZEM_ERROR_CONFIG;
    }
    
    return validate_thresholds(config);
}

// Валидация порогов
pzem_result_t validate_thresholds(const pzem_config_t *config) {
    const struct {
        float high_alarm, high_warning, low_warning, low_alarm;
        const char *name;
    } thresholds[] = {
        {config->voltage_high_alarm, config->voltage_high_warning, 
         config->voltage_low_warning, config->voltage_low_alarm, "voltage"},
        {config->current_high_alarm, config->current_high_warning,
         config->current_low_warning, config->current_low_alarm, "current"},
        {config->frequency_high_alarm, config->frequency_high_warning,
         config->frequency_low_warning, config->frequency_low_alarm, "frequency"},
        {config->angleV_high_alarm, config->angleV_high_warning,
         config->angleV_low_warning, config->angleV_low_alarm, "angleV"},
        {config->angleI_high_alarm, config->angleI_high_warning,
         config->angleI_low_warning, config->angleI_low_alarm, "angleI"}
    };
    
    for (size_t i = 0; i < sizeof(thresholds)/sizeof(thresholds[0]); i++) {
        if (thresholds[i].high_alarm > 0) {
            if (thresholds[i].high_alarm <= thresholds[i].high_warning ||
                thresholds[i].high_warning <= thresholds[i].low_warning ||
                thresholds[i].low_warning <= thresholds[i].low_alarm) {
                syslog(LOG_ERR, "Invalid %s thresholds order", thresholds[i].name);
                return PZEM_ERROR_CONFIG;
            }
        }
    }
    return PZEM_SUCCESS;
}

// Функция загрузки конфигурации из файла
pzem_result_t load_config(const char *config_file, pzem_config_t *config) {
    if (!config_file || !config) {
        return PZEM_ERROR_INVALID_PARAM;
    }
    
    FILE *file = fopen(config_file, "r");
    if (file == NULL) {
        syslog(LOG_ERR, "Config file '%s' not found: %s", config_file, strerror(errno));
        return PZEM_ERROR_CONFIG;
    }
    
    // Установка значений по умолчанию
    *config = (pzem_config_t){
        .tty_port = "/dev/ttyS1@9600",
        .baudrate = 9600,
        .slave_addr = 1,
        .poll_interval_ms = DEFAULT_POLL_INTERVAL,
        .log_dir = "/var/log/pzem3",
        .log_buffer_size = 10,
        .voltage_sensitivity = 0.1f,
        .current_sensitivity = 0.01f,
        .frequency_sensitivity = 0.01f,
        .angleV_sensitivity = 0.01f,
        .angleI_sensitivity = 0.01f,
        .power_sensitivity = 1.0f,
    };
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Пропускаем комментарии и пустые строки
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char key[64], value[64];
        if (sscanf(line, "%63[^ =] = %63[^\n]", key, value) == 2) {
            char *trimmed_value = value;
            while (*trimmed_value == ' ') trimmed_value++;
            
            // Обработка каждого параметра индивидуально
            if (strcmp(key, "device") == 0) {
                STRCPY_SAFE(config->tty_port, trimmed_value);
            } else if (strcmp(key, "log_dir") == 0) {
                STRCPY_SAFE(config->log_dir, trimmed_value);
            } else if (strcmp(key, "slave_addr") == 0) {
                config->slave_addr = atoi(trimmed_value);
            } else if (strcmp(key, "poll_interval_ms") == 0) {
                config->poll_interval_ms = atoi(trimmed_value);
            } else if (strcmp(key, "log_buffer_size") == 0) {
                config->log_buffer_size = atoi(trimmed_value);
            } else if (strcmp(key, "voltage_sensitivity") == 0) {
                config->voltage_sensitivity = (float)atof(trimmed_value);
            } else if (strcmp(key, "current_sensitivity") == 0) {
                config->current_sensitivity = (float)atof(trimmed_value);
            } else if (strcmp(key, "frequency_sensitivity") == 0) {
                config->frequency_sensitivity = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleV_sensitivity") == 0) {
                config->angleV_sensitivity = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleI_sensitivity") == 0) {
                config->angleI_sensitivity = (float)atof(trimmed_value);
            } else if (strcmp(key, "power_sensitivity") == 0) {
                config->power_sensitivity = (float)atof(trimmed_value);
            } else if (strcmp(key, "voltage_high_alarm") == 0) {
                config->voltage_high_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "voltage_high_warning") == 0) {
                config->voltage_high_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "voltage_low_warning") == 0) {
                config->voltage_low_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "voltage_low_alarm") == 0) {
                config->voltage_low_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "current_high_alarm") == 0) {
                config->current_high_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "current_high_warning") == 0) {
                config->current_high_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "current_low_warning") == 0) {
                config->current_low_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "current_low_alarm") == 0) {
                config->current_low_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "frequency_high_alarm") == 0) {
                config->frequency_high_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "frequency_high_warning") == 0) {
                config->frequency_high_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "frequency_low_warning") == 0) {
                config->frequency_low_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "frequency_low_alarm") == 0) {
                config->frequency_low_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleV_high_alarm") == 0) {
                config->angleV_high_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleV_high_warning") == 0) {
                config->angleV_high_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleV_low_warning") == 0) {
                config->angleV_low_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleV_low_alarm") == 0) {
                config->angleV_low_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleI_high_alarm") == 0) {
                config->angleI_high_alarm = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleI_high_warning") == 0) {
                config->angleI_high_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleI_low_warning") == 0) {
                config->angleI_low_warning = (float)atof(trimmed_value);
            } else if (strcmp(key, "angleI_low_alarm") == 0) {
                config->angleI_low_alarm = (float)atof(trimmed_value);
            } else {
                syslog(LOG_WARNING, "Unknown config parameter: '%s'", key);
            }
        }
    }
    
    fclose(file);

    // Анализ параметра device
    char device_str[128];
    STRCPY_SAFE(device_str, config->tty_port);
    
    if (strchr(device_str, '@')) {
        device_type = 'U';
        char *port_part = strtok(device_str, "@");
        char *baud_part = strtok(NULL, "@");
        
        if (port_part && baud_part) {
            STRCPY_SAFE(config->tty_port, port_part);
            config->baudrate = atoi(baud_part);
            if (config->baudrate <= 0) {
                syslog(LOG_WARNING, "Invalid baudrate '%s', using default 9600", baud_part);
                config->baudrate = 9600;
            }
        }
        syslog(LOG_INFO, "UART device: %s, baudrate: %d", config->tty_port, config->baudrate);
    } else if (strchr(device_str, ':')) {
        device_type = 'T';
        char *ip_part = strtok(device_str, ":");
        char *port_part = strtok(NULL, ":");
        
        if (ip_part && port_part) {
            STRCPY_SAFE(config->tty_port, ip_part);
            config->baudrate = atoi(port_part);
            if (config->baudrate <= 0 || config->baudrate > 65535) {
                syslog(LOG_WARNING, "Invalid TCP port '%s', using default 502", port_part);
                config->baudrate = 502;
            }
        }
        syslog(LOG_INFO, "TCP device: %s, port: %d", config->tty_port, config->baudrate);
    } else {
        device_type = 'U';
        config->baudrate = 9600;
        syslog(LOG_WARNING, "Unknown device format: '%s', assuming UART with baudrate 9600", device_str);
    }

    // Проверки корректности значений
    if (config->poll_interval_ms < MIN_POLL_INTERVAL) {
        syslog(LOG_WARNING, "Poll interval too small (%dms), setting to %dms", 
               config->poll_interval_ms, MIN_POLL_INTERVAL);
        config->poll_interval_ms = MIN_POLL_INTERVAL;
    } else if (config->poll_interval_ms > MAX_POLL_INTERVAL) {
        syslog(LOG_WARNING, "Poll interval too large (%dms), setting to %dms", 
               config->poll_interval_ms, MAX_POLL_INTERVAL);
        config->poll_interval_ms = MAX_POLL_INTERVAL;
    }
    
    if (config->log_buffer_size < 1) {
        syslog(LOG_WARNING, "Log buffer size too small (%d), setting to 1", config->log_buffer_size);
        config->log_buffer_size = 1;
    } else if (config->log_buffer_size > MAX_LOG_BUFFER_SIZE) {
        syslog(LOG_WARNING, "Log buffer size too large (%d), setting to %d", 
               config->log_buffer_size, MAX_LOG_BUFFER_SIZE);
        config->log_buffer_size = MAX_LOG_BUFFER_SIZE;
    }

    return PZEM_SUCCESS;
}

// Функция для сравнения значений с учетом чувствительности
int values_changed(const pzem_data_t *current, const pzem_data_t *previous, const pzem_config_t *config) {
    if (!current || !previous || !config) return 1;
    
    if (previous->first_read || current->status != previous->status) return 1;
    
    if (CHECK_CHANGE(voltage_A, voltage_sensitivity)) return 1;
    if (CHECK_CHANGE(voltage_B, voltage_sensitivity)) return 1;
    if (CHECK_CHANGE(voltage_C, voltage_sensitivity)) return 1;
    if (CHECK_CHANGE(current_A, current_sensitivity)) return 1;
    if (CHECK_CHANGE(current_B, current_sensitivity)) return 1;
    if (CHECK_CHANGE(current_C, current_sensitivity)) return 1;
    if (CHECK_CHANGE(frequency_A, frequency_sensitivity)) return 1;
    if (CHECK_CHANGE(frequency_B, frequency_sensitivity)) return 1;
    if (CHECK_CHANGE(frequency_C, frequency_sensitivity)) return 1;
    if (CHECK_CHANGE(angleV_B, angleV_sensitivity)) return 1;
    if (CHECK_CHANGE(angleV_C, angleV_sensitivity)) return 1;
    if (CHECK_CHANGE(angleI_A, angleI_sensitivity)) return 1;
    if (CHECK_CHANGE(angleI_B, angleI_sensitivity)) return 1;
    if (CHECK_CHANGE(angleI_C, angleI_sensitivity)) return 1;
    if (CHECK_CHANGE(power_A, power_sensitivity)) return 1;
    if (CHECK_CHANGE(power_B, power_sensitivity)) return 1;
    if (CHECK_CHANGE(power_C, power_sensitivity)) return 1;
    
    return 0;
}

// Функция инициализации Modbus соединения
pzem_result_t init_modbus_connection(const pzem_config_t *config) {
    if (!config) return PZEM_ERROR_INVALID_PARAM;
    
    if (device_type == 'U') {
        ctx = modbus_new_rtu(config->tty_port, config->baudrate, 'N', 8, 1);
    } else if (device_type == 'T') {
        ctx = modbus_new_tcp(config->tty_port, config->baudrate);
    } else {
        syslog(LOG_ERR, "Unknown device type: %c", device_type);
        return PZEM_ERROR_CONFIG;
    }
    
    if (ctx == NULL) {
        syslog(LOG_ERR, "Unable to create Modbus context for %s", config->tty_port);
        return PZEM_ERROR_MODBUS;
    }
    
    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
    modbus_set_response_timeout(ctx, 1, 0);
    modbus_set_byte_timeout(ctx, 0, 500000);
    modbus_set_slave(ctx, config->slave_addr);
    
    if (modbus_connect(ctx) == -1) {
        syslog(LOG_ERR, "Connection failed to %s: %s", config->tty_port, modbus_strerror(errno));
        modbus_free(ctx);
        ctx = NULL;
        return PZEM_ERROR_MODBUS;
    }
    
    if (device_type == 'U') {
        syslog(LOG_INFO, "Modbus RTU connection established to %s@%d", config->tty_port, config->baudrate);
    } else {
        syslog(LOG_INFO, "Modbus TCP connection established to %s:%d", config->tty_port, config->baudrate);
    }
    
    return PZEM_SUCCESS;
}

// Функция чтения данных с PZEM
pzem_result_t read_pzem_data(pzem_data_t *data) {
    if (!data) return PZEM_ERROR_INVALID_PARAM;
    
    if (ctx == NULL) {
        data->status = 2;
        return PZEM_ERROR_MODBUS;
    }

    uint16_t tab_reg[20];
    int rc = modbus_read_input_registers(ctx, 0x0000, 20, tab_reg);
    if (rc == -1) {
        data->status = 1;
        return PZEM_ERROR_MODBUS;
    }

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
    return PZEM_SUCCESS;
}

// Функция чтения с повторными попытками
pzem_result_t read_pzem_data_with_retry(pzem_data_t *data, int max_retries) {
    for (int attempt = 0; attempt < max_retries; attempt++) {
        if (read_pzem_data(data) == PZEM_SUCCESS) {
            return PZEM_SUCCESS;
        }
        if (attempt < max_retries - 1) {
            usleep(100000 * (attempt + 1));
        }
    }
    return PZEM_ERROR_MODBUS;
}

// Функция очистки ресурсов
void cleanup(void) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "Cleanup started");
#endif
    
    if (log_buffer.buffer != NULL) {
#ifdef DEBUG
        syslog(LOG_DEBUG, "Flushing log buffer (%d records)", log_buffer.size);
#endif
        flush_log_buffer(&log_buffer);
        free_log_buffer(&log_buffer);
    }
    
    cleanup_fifo(fifo_path);
    
    if (ctx != NULL) {
        modbus_close(ctx);
        modbus_free(ctx);
        ctx = NULL;
        syslog(LOG_INFO, "Modbus connection closed");
    }
    
    print_metrics(&metrics);
    
#ifdef DEBUG
    syslog(LOG_DEBUG, "Cleanup completed");
#endif
}

// Функция безопасного переподключения
void safe_reconnect(const pzem_config_t *config) {
    syslog(LOG_WARNING, "Multiple errors detected, attempting reconnect...");
    cleanup();
    
    if (log_buffer.buffer == NULL) {
        if (init_log_buffer(&log_buffer, global_config.log_buffer_size, global_config.log_dir) != PZEM_SUCCESS) {
            syslog(LOG_ERR, "Failed to reinitialize log buffer");
        }
    }
    
    usleep(1000000);
    if (init_modbus_connection(config) == PZEM_SUCCESS) {
        syslog(LOG_INFO, "Reconnected successfully");
    }
}

// Инициализация системы
pzem_result_t initialize_system(const char *config_file) {
    extract_config_name(config_file);
    
    char syslog_ident[128];
    snprintf(syslog_ident, sizeof(syslog_ident), "pzem3-%s", config_name);
    service_name = syslog_ident;
    
    openlog(service_name, LOG_PID | LOG_CONS, LOG_DAEMON);
    setup_signal_handlers();
    
    syslog(LOG_INFO, "PZEM-6L24 Monitor v%s starting with config: %s", version, config_file);

    // Создаем FIFO
    snprintf(fifo_path, sizeof(fifo_path), "/tmp/pzem3_data_%s", config_name);
    if (init_data_fifo(fifo_path) != 0) {
        syslog(LOG_WARNING, "Failed to create data FIFO, data broadcasting disabled");
    } else {
        syslog(LOG_INFO, "Data broadcasting enabled via FIFO: %s", fifo_path);
    }
    
    if (load_config(config_file, &global_config) != PZEM_SUCCESS) {
        syslog(LOG_ERR, "Failed to load configuration");
        return PZEM_ERROR_CONFIG;
    }
    
    if (validate_config(&global_config) != PZEM_SUCCESS) {
        syslog(LOG_ERR, "Configuration validation failed");
        return PZEM_ERROR_CONFIG;
    }
    
    if (create_directory_if_not_exists(global_config.log_dir) != 0) {
        syslog(LOG_ERR, "Failed to create log directory");
        return PZEM_ERROR_IO;
    }
    
    if (init_log_buffer(&log_buffer, global_config.log_buffer_size, global_config.log_dir) != PZEM_SUCCESS) {
        syslog(LOG_ERR, "Failed to initialize log buffer");
        return PZEM_ERROR_MEMORY;
    }
    
    // Проверяем доступность лог-файла
    char log_path[512];
    get_log_file_path(log_path, sizeof(log_path), global_config.log_dir);
    FILE *test_file = fopen(log_path, "a");
    if (test_file == NULL) {
        syslog(LOG_ERR, "Cannot access log file '%s': %s", log_path, strerror(errno));
    } else {
        fclose(test_file);
        syslog(LOG_INFO, "Log file accessible: %s", log_path);
    }

    if (init_modbus_connection(&global_config) != PZEM_SUCCESS) {
        syslog(LOG_ERR, "Failed to initialize Modbus connection");
        return PZEM_ERROR_MODBUS;
    }

    // Инициализируем метрики
    metrics.start_time = get_time_ms();
    
    return PZEM_SUCCESS;
}

// Инициализация структур данных
void initialize_data_structures(pzem_data_t *current, pzem_data_t *previous) {
    if (!current || !previous) return;
    
    memset(current, 0, sizeof(pzem_data_t));
    memset(previous, 0, sizeof(pzem_data_t));
    
    previous->first_read = 1;
    current->first_read = 1;
    previous->status = 2;
    
    // Инициализация состояний
    const char default_state = 'N';
    current->voltage_state_A = default_state;
    current->voltage_state_B = default_state;
    current->voltage_state_C = default_state;
    current->current_state_A = default_state;
    current->current_state_B = default_state;
    current->current_state_C = default_state;
    current->frequency_state_A = default_state;
    current->frequency_state_B = default_state;
    current->frequency_state_C = default_state;
    current->angleV_state_B = default_state;
    current->angleV_state_C = default_state;
    current->angleI_state_A = default_state;
    current->angleI_state_B = default_state;
    current->angleI_state_C = default_state;
    
    // Копируем в previous
    *previous = *current;
}

// Обработка одной итерации
void process_iteration(pzem_data_t *current, pzem_data_t *previous) {
    if (!current || !previous) return;
    
    long long iteration_start = get_time_ms();
    long long modbus_start = get_time_ms();
    
    pzem_result_t read_result = read_pzem_data_with_retry(current, MAX_RETRIES);
    long long modbus_time = get_time_ms() - modbus_start;
    
    int had_error = (read_result != PZEM_SUCCESS);
    
    if (read_result == PZEM_SUCCESS) {
        update_threshold_states(current, &global_config);
        if (current->first_read) {
            if (current->angleV_B < 200 && current->angleV_B > 100 && current->angleV_C > 200) {
                current->rotaryP = 'R';
            } else {
                current->rotaryP = 'L';
            }
            syslog(LOG_INFO, "The order of rotation of the phases (L - reverse, R - forward): %c", current->rotaryP);
        }
    }

    int data_changed = values_changed(current, previous, &global_config);
    int states_changed = threshold_states_changed(current, previous);

    if (data_changed || states_changed) {
        char log_entry[256];
        prepare_log_entry(log_entry, sizeof(log_entry), current);
        
        // Отправляем в FIFO
        if (write_to_fifo(fifo_path, log_entry) != 0) {
#ifdef DEBUG
            syslog(LOG_DEBUG, "Failed to write to FIFO (no readers?)");
#endif
        }
        
        // Добавляем в буфер логов
        if (add_to_log_buffer(&log_buffer, log_entry) != PZEM_SUCCESS) {
#ifdef DEBUG
            syslog(LOG_DEBUG, "Failed to add to log buffer");
#endif
        }

        *previous = *current;
        previous->first_read = 0;
        current->first_read = 0;
    }

    // Сбрасываем буфер если он полный
    if (should_flush_buffer(&log_buffer)) {
        flush_log_buffer(&log_buffer);
    }
    
    long long iteration_time = get_time_ms() - iteration_start;
    update_metrics(&metrics, iteration_time, modbus_time, had_error);
    
    // Регулируем время сна
    int16_t sleep_time = global_config.poll_interval_ms - (int16_t)iteration_time;
    if (sleep_time > 0) {
        usleep((useconds_t)(sleep_time * 1000));
    } else {
        syslog(LOG_ALERT, "Attention! Processing time (%lldms) exceeds poll interval (%dms)", 
               iteration_time, global_config.poll_interval_ms);
    }
}

// Обновление метрик
void update_metrics(performance_metrics_t *metrics, long long iteration_time, 
                   long long modbus_time, int had_error) {
    if (!metrics) return;
    
    metrics->total_iterations++;
    metrics->error_count += had_error;
    metrics->modbus_time_total += modbus_time;
    metrics->processing_time_total += iteration_time;
    if (iteration_time > metrics->max_iteration_time) {
        metrics->max_iteration_time = iteration_time;
    }
}

// Вывод метрик
void print_metrics(const performance_metrics_t *metrics) {
    if (!metrics || metrics->total_iterations == 0) return;
    
    long long total_time = get_time_ms() - metrics->start_time;
    double avg_iteration = (double)metrics->processing_time_total / metrics->total_iterations;
    double avg_modbus = (double)metrics->modbus_time_total / metrics->total_iterations;
    double error_rate = (double)metrics->error_count / metrics->total_iterations * 100.0;
    
    syslog(LOG_INFO, "Performance metrics: total_time=%lldms, iterations=%lld, "
           "avg_iteration=%.2fms, avg_modbus=%.2fms, max_iteration=%lldms, error_rate=%.2f%%",
           total_time, metrics->total_iterations, avg_iteration, avg_modbus,
           metrics->max_iteration_time, error_rate);
}

// Безопасное освобождение памяти
void safe_free(void **ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "/etc/pzem3/default.conf";
    
    pzem_data_t current_data, previous_data;
    
    if (initialize_system(config_file) != PZEM_SUCCESS) {
        closelog();
        return 1;
    }
    
    initialize_data_structures(&current_data, &previous_data);
    
    // Логируем информацию о конфигурации
    char thresholds[128] = "";
    if (global_config.voltage_high_alarm > 0) strcat(thresholds, "V");
    if (global_config.current_high_alarm > 0) strcat(thresholds, "I");
    if (global_config.frequency_high_alarm > 0) strcat(thresholds, "Z");
    if (global_config.angleV_high_alarm > 0) strcat(thresholds, "Av");
    if (global_config.angleI_high_alarm > 0) strcat(thresholds, "Ai");
    
    if (device_type == 'U') {
        syslog(LOG_INFO, "Config: UART %s@%d, addr=%d, interval=%dms, thresholds=%s", 
               global_config.tty_port, global_config.baudrate, global_config.slave_addr, 
               global_config.poll_interval_ms, thresholds);
    } else {
        syslog(LOG_INFO, "Config: TCP %s:%d, addr=%d, interval=%dms, thresholds=%s", 
               global_config.tty_port, global_config.baudrate, global_config.slave_addr, 
               global_config.poll_interval_ms, thresholds);
    }
    
    syslog(LOG_INFO, "Monitoring started for config: %s", config_name);
    
    int error_count = 0;
    const int max_error_count = 10;
    
    while (keep_running) {
        process_iteration(&current_data, &previous_data);
        
        if (current_data.status != 0) {
            error_count++;
            if (error_count > max_error_count) {
                safe_reconnect(&global_config);
                error_count = 0;
                // Переинициализируем структуры данных после переподключения
                initialize_data_structures(&current_data, &previous_data);
            }
        } else {
            error_count = 0;
        }
    }
    
    syslog(LOG_INFO, "Monitoring stopped for config: %s", config_name);
    cleanup();
    closelog();
    
    return 0;
}