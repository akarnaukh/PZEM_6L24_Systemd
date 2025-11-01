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
#ifndef PZEM_MONITOR_H
#define PZEM_MONITOR_H

#include <stdio.h>
#include <errno.h>
#include <modbus/modbus.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <syslog.h>
#include <fcntl.h>
#include <pthread.h>

#define PZEM_FIFO_PATH "/tmp/pzem3_data_%s"
#define MAX_RETRIES 3
#define DEFAULT_POLL_INTERVAL 500
#define MAX_LOG_BUFFER_SIZE 25
#define MIN_POLL_INTERVAL 200
#define MAX_POLL_INTERVAL 10000

// Макросы для безопасного копирования строк
#define STRCPY_SAFE(dest, src) do { \
    strncpy(dest, src, sizeof(dest) - 1); \
    dest[sizeof(dest) - 1] = '\0'; \
} while(0)

/*
#define STRNCPY_SAFE(dest, src, count) do { \
    strncpy(dest, src, (count) < sizeof(dest) ? (count) : sizeof(dest) - 1); \
    dest[sizeof(dest) - 1] = '\0'; \
} while(0)
*/

// Коды возврата
typedef enum {
    PZEM_SUCCESS = 0,
    PZEM_ERROR_CONFIG = -1,
    PZEM_ERROR_MODBUS = -2,
    PZEM_ERROR_IO = -3,
    PZEM_ERROR_MEMORY = -4,
    PZEM_ERROR_INVALID_PARAM = -5
} pzem_result_t;

// Структура для хранения конфигурации
typedef struct {
    char tty_port[64];
    int baudrate;
    int slave_addr;
    int poll_interval_ms;
    char log_dir[256];
    int log_buffer_size;
    
    // Чувствительность изменений
    float voltage_sensitivity;
    float current_sensitivity;
    float frequency_sensitivity;
    float power_sensitivity;
    float angleV_sensitivity;
    float angleI_sensitivity;

    // Пороги
    float angleV_high_alarm;
    float angleV_high_warning;
    float angleV_low_warning;
    float angleV_low_alarm;

    float angleI_high_alarm;
    float angleI_high_warning;
    float angleI_low_warning;
    float angleI_low_alarm;

    float voltage_high_alarm;
    float voltage_high_warning;
    float voltage_low_warning;
    float voltage_low_alarm;
    
    float current_high_alarm;
    float current_high_warning;
    float current_low_warning;
    float current_low_alarm;
    
    float frequency_high_alarm;
    float frequency_high_warning;
    float frequency_low_warning;
    float frequency_low_alarm;
} pzem_config_t;

// Структура для хранения данных
typedef struct {
    float voltage_A;
    float voltage_B;
    float voltage_C;
    float current_A;
    float current_B;
    float current_C;
    float frequency_A;
    float frequency_B;
    float frequency_C;
    float angleV_B;
    float angleV_C;
    float angleI_A;
    float angleI_B;
    float angleI_C;
    float power_A;
    float power_B;
    float power_C;
    int status;
    int first_read;
    
    // Состояния порогов
    char voltage_state_A;
    char voltage_state_B;
    char voltage_state_C;
    char current_state_A;
    char current_state_B;
    char current_state_C;
    char frequency_state_A;
    char frequency_state_B;
    char frequency_state_C;
    char angleV_state_B;
    char angleV_state_C;
    char angleI_state_A;
    char angleI_state_B;
    char angleI_state_C;
    char rotaryP;
} pzem_data_t;

// Структура для порогов
typedef struct {
    float high_alarm;
    float high_warning;
    float low_warning;
    float low_alarm;
} threshold_config_t;

// Структура для буферизации логов
typedef struct {
    char **buffer;
    int size;
    int capacity;
    int read_index;
    int write_index;
    pthread_mutex_t mutex;
    char log_dir[256];
    char config_name[64];
} log_buffer_t;

// Структура для метрик производительности
typedef struct {
    long long total_iterations;
    long long error_count;
    long long modbus_time_total;
    long long processing_time_total;
    long long max_iteration_time;
    long long start_time;
} performance_metrics_t;

// Глобальные переменные
extern modbus_t *ctx;
extern volatile sig_atomic_t keep_running;
extern log_buffer_t log_buffer;
extern pzem_config_t global_config;
extern char *service_name;
extern char config_name[64];
extern char fifo_path[256];
extern char device_type;
extern performance_metrics_t metrics;

// Функции конфигурации
pzem_result_t load_config(const char *config_file, pzem_config_t *config);
int create_directory_if_not_exists(const char *path);
pzem_result_t validate_config(const pzem_config_t *config);
void extract_config_name(const char *config_path);

// Функции для FIFO
int init_data_fifo(const char *fifo_path);
int write_to_fifo(const char *fifo_path, const char *data);
void cleanup_fifo(const char *fifo_path);

// Функции работы с логами
pzem_result_t init_log_buffer(log_buffer_t *buffer, int initial_capacity, const char *log_dir);
pzem_result_t add_to_log_buffer(log_buffer_t *buffer, const char *log_entry);
pzem_result_t flush_log_buffer(log_buffer_t *buffer);
void free_log_buffer(log_buffer_t *buffer);
long long get_time_ms(void);
void get_current_date(char *date_str, size_t size);
void get_current_time(char *time_str, size_t size);
void get_log_file_path(char *path, size_t size, const char *log_dir);
void prepare_log_entry(char *log_entry, size_t size, const pzem_data_t *data);
int should_flush_buffer(const log_buffer_t *buffer);

// Функции Modbus
pzem_result_t init_modbus_connection(const pzem_config_t *config);
pzem_result_t read_pzem_data(pzem_data_t *data);
pzem_result_t read_pzem_data_with_retry(pzem_data_t *data, int max_retries);
void cleanup(void);
void safe_reconnect(const pzem_config_t *config);

// Функции обработки данных
float lsbVal(uint16_t dat);
int values_changed(const pzem_data_t *current, const pzem_data_t *previous, const pzem_config_t *config);
void update_threshold_state(float value, char* state, const threshold_config_t* config);
void update_threshold_states(pzem_data_t *data, const pzem_config_t *config);
int threshold_states_changed(const pzem_data_t *current, const pzem_data_t *previous);
pzem_result_t validate_thresholds(const pzem_config_t *config);

// Сигналы и инициализация
void signal_handler(int sig);
void setup_signal_handlers(void);
pzem_result_t initialize_system(const char *config_file);
void initialize_data_structures(pzem_data_t *current, pzem_data_t *previous);
void process_iteration(pzem_data_t *current, pzem_data_t *previous);
void update_metrics(performance_metrics_t *metrics, long long iteration_time, 
                   long long modbus_time, int had_error);
void print_metrics(const performance_metrics_t *metrics);

// Утилиты
void safe_free(void **ptr);

#endif