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