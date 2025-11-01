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
    if (config->poll_interval_ms < 200) {
        syslog(LOG_WARNING, "Poll interval too small (%dms), setting to 200ms", config->poll_interval_ms);
        config->poll_interval_ms = 200;
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