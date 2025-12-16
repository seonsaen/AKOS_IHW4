#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdatomic.h>
#include <errno.h>


#define MAX_PLAYERS 10
const char *STR_MOVES[] = {"Камень", "Ножницы", "Бумага"};

// Структура для разделяемых данных с атомарными операциями
typedef struct {
    int current_moves[MAX_PLAYERS];
    int scores[MAX_PLAYERS];
    atomic_int game_over;  // Атомарный флаг завершения игры
    int matches_played;
    int total_matches;
    atomic_int registered_players;  // Атомарный счетчик зарегистрированных игроков
    int total_players;
    int current_player1;
    int current_player2;
    pthread_mutex_t data_mutex;
    pthread_cond_t data_cond;  // Условная переменная для ожидания
    pthread_mutex_t player_mutexes[MAX_PLAYERS];  // Мьютексы для каждого игрока
    pthread_cond_t player_conds[MAX_PLAYERS];  // Условные переменные для каждого игрока
    atomic_int moves_ready;  // Атомарный счетчик готовых ходов
    atomic_int has_moved[MAX_PLAYERS];  // Флаг: игрок уже сделал ход в этом матче
    atomic_int match_in_progress;  // Флаг: матч в процессе
} SharedData;

// Структура для данных потока
typedef struct {
    int id;
    SharedData *shared;
    pthread_mutex_t *output_mutex;
    FILE *log_file;
    pthread_mutex_t *file_mutex;
} ThreadData;

// Глобальные переменные для обработки сигналов
volatile sig_atomic_t stop_flag = 0;
SharedData *shared = NULL;
ThreadData *thread_data_array = NULL;
pthread_t *threads = NULL;

// Обработчик сигнала
void handle_signal(int sig) {
    if (sig == SIGINT) {
        printf("\nПолучен сигнал прерывания. Завершение турнира...\n");
        stop_flag = 1;
    }
}

// Функция для очистки ресурсов
void cleanup_resources() {
    printf("Очистка ресурсов...\n");

    if (shared) {
        atomic_store(&shared->game_over, 1);
        // Будим всех ожидающих игроков
        for (int i = 0; i < shared->total_players; i++) {
            pthread_cond_broadcast(&shared->player_conds[i]);
        }

        sleep(1);

        // Уничтожаем мьютексы и условные переменные
        pthread_mutex_destroy(&shared->data_mutex);
        pthread_cond_destroy(&shared->data_cond);
        for (int i = 0; i < shared->total_players; i++) {
            pthread_mutex_destroy(&shared->player_mutexes[i]);
            pthread_cond_destroy(&shared->player_conds[i]);
        }

        free(shared);
        shared = NULL;
    }

    if (thread_data_array) {
        free(thread_data_array);
        thread_data_array = NULL;
    }

    if (threads) {
        free(threads);
        threads = NULL;
    }

    printf("Ресурсы очищены\n");
}

// Функция для преобразования выбора в строку
const char* choice_to_string(int choice) {
    switch(choice) {
        case 0: return "Камень";
        case 1: return "Ножницы";
        case 2: return "Бумага";
        default: return "Неизвестно";
    }
}

// Функция для определения победителя
int determine_winner(int choice1, int choice2) {
    if (choice1 == choice2) {
        return 0;
    }
    if ((choice1 == 0 && choice2 == 1) || (choice1 == 1 && choice2 == 2) || (choice1 == 2 && choice2 == 0)) {
        return 1;
    }
    return 2;
}

// Функция потока-студента с использованием условных переменных
void* student_thread(void* arg) {
    ThreadData *data = (ThreadData*)arg;
    int id = data->id;

    unsigned int seed = time(NULL) + id + getpid();

    pthread_mutex_lock(data->output_mutex);
    printf("[Студент %d] Зарегистрирован\n", id);
    pthread_mutex_unlock(data->output_mutex);

    atomic_fetch_add(&data->shared->registered_players, 1);

    while (1) {
        // Ожидаем начала матча или завершения турнира
        pthread_mutex_lock(&data->shared->player_mutexes[id]);

        // Ждем, пока мы не станем участником матча И матч начался И мы еще не сделали ход
        while (!stop_flag &&
               !atomic_load(&data->shared->game_over) &&
               (atomic_load(&data->shared->match_in_progress) == 0 ||
                (data->shared->current_player1 != id &&
                 data->shared->current_player2 != id) ||
                atomic_load(&data->shared->has_moved[id]) == 1)) {
            pthread_cond_wait(&data->shared->player_conds[id],
                            &data->shared->player_mutexes[id]);
        }

        if (stop_flag || atomic_load(&data->shared->game_over)) {
            pthread_mutex_unlock(&data->shared->player_mutexes[id]);
            break;
        }

        // Делаем ход
        int move = rand_r(&seed) % 3;

        pthread_mutex_lock(&data->shared->data_mutex);
        data->shared->current_moves[id] = move;
        pthread_mutex_unlock(&data->shared->data_mutex);

        pthread_mutex_lock(data->output_mutex);
        printf("[Студент %d] Сделал ход: %s\n", id, choice_to_string(move));
        pthread_mutex_unlock(data->output_mutex);

        if (data->log_file) {
            pthread_mutex_lock(data->file_mutex);
            fprintf(data->log_file, "[Студент %d] Сделал ход: %s\n",
                   id, choice_to_string(move));
            fflush(data->log_file);
            pthread_mutex_unlock(data->file_mutex);
        }

        // Увеличиваем счетчик готовых ходов
        int ready = atomic_fetch_add(&data->shared->moves_ready, 1);

        // Помечаем, что мы уже сделали ход
        atomic_store(&data->shared->has_moved[id], 1);

        // Если это был второй ход, будим главный поток
        if (ready == 1) {
            pthread_mutex_lock(&data->shared->data_mutex);
            pthread_cond_signal(&shared->data_cond);
            pthread_mutex_unlock(&data->shared->data_mutex);
        }

        pthread_mutex_unlock(&data->shared->player_mutexes[id]);
    }

    pthread_mutex_lock(data->output_mutex);
    printf("[Студент %d] Завершает работу\n", id);
    pthread_mutex_unlock(data->output_mutex);

    return NULL;
}

// Функция для проведения турнира с использованием условных переменных
void run_tournament(int n, pthread_mutex_t *output_mutex, FILE *log_file,
                   pthread_mutex_t *file_mutex) {
    while (atomic_load(&shared->registered_players) < n) {
        sleep(1);
        if (stop_flag) {
            printf("Турнир прерван во время регистрации\n");
            return;
        }
    }

    printf("Все студенты зарегистрированы\n");

    pthread_mutex_lock(&shared->data_mutex);
    shared->total_matches = n * (n - 1) / 2;
    shared->matches_played = 0;
    pthread_mutex_unlock(&shared->data_mutex);

    printf("\n=== Начало турнира! Всего матчей: %d ===\n", shared->total_matches);

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (stop_flag) {
                printf("Турнир прерван\n");
                return;
            }

            pthread_mutex_lock(&shared->data_mutex);
            shared->matches_played++;
            shared->current_player1 = i;
            shared->current_player2 = j;
            atomic_store(&shared->moves_ready, 0);
            atomic_store(&shared->match_in_progress, 1);

            // Сбрасываем флаги только для участников текущего матча
            atomic_store(&shared->has_moved[i], 0);
            atomic_store(&shared->has_moved[j], 0);
            pthread_mutex_unlock(&shared->data_mutex);

            pthread_mutex_lock(output_mutex);
            printf("\n--- Матч %d/%d: Студент %d vs Студент %d ---\n",
                   shared->matches_played, shared->total_matches, i, j);
            pthread_mutex_unlock(output_mutex);

            if (log_file) {
                pthread_mutex_lock(file_mutex);
                fprintf(log_file, "\n--- Матч %d/%d: Студент %d vs Студент %d ---\n",
                       shared->matches_played, shared->total_matches, i, j);
                fflush(log_file);
                pthread_mutex_unlock(file_mutex);
            }

            // Будим студентов для игры
            pthread_cond_signal(&shared->player_conds[i]);
            pthread_cond_signal(&shared->player_conds[j]);

            // Ждем, пока оба студента сделают ходы
            pthread_mutex_lock(&shared->data_mutex);
            while (atomic_load(&shared->moves_ready) < 2 && !stop_flag) {
                pthread_cond_wait(&shared->data_cond, &shared->data_mutex);
            }
            pthread_mutex_unlock(&shared->data_mutex);

            if (stop_flag) {
                printf("Турнир прерван во время матча\n");
                return;
            }

            // Определяем победителя
            int move1, move2;
            pthread_mutex_lock(&shared->data_mutex);
            move1 = shared->current_moves[i];
            move2 = shared->current_moves[j];
            pthread_mutex_unlock(&shared->data_mutex);

            int result = determine_winner(move1, move2);

            // Обновляем очки
            pthread_mutex_lock(&shared->data_mutex);
            if (result == 0) {
                shared->scores[i] += 1;
                shared->scores[j] += 1;
            } else if (result == 1) {
                shared->scores[i] += 2;
            } else {
                shared->scores[j] += 2;
            }
            pthread_mutex_unlock(&shared->data_mutex);

            // Выводим результат матча
            pthread_mutex_lock(output_mutex);
            if (result == 0) {
                printf("Ничья (+1 каждому)\n");
            } else if (result == 1) {
                printf("Победил Студент %d (+2)\n", i);
            } else {
                printf("Победил Студент %d (+2)\n", j);
            }
            pthread_mutex_unlock(output_mutex);

            if (log_file) {
                pthread_mutex_lock(file_mutex);
                if (result == 0) {
                    fprintf(log_file, "Ничья (+1 каждому)\n");
                } else if (result == 1) {
                    fprintf(log_file, "Победил Студент %d (+2)\n", i);
                } else {
                    fprintf(log_file, "Победил Студент %d (+2)\n", j);
                }
                fflush(log_file);
                pthread_mutex_unlock(file_mutex);
            }

            // Завершаем матч
            atomic_store(&shared->match_in_progress, 0);

            // Пауза для наглядности
            sleep(1);
        }
    }

    // Турнир завершен
    atomic_store(&shared->game_over, 1);
    for (int i = 0; i < n; i++) {
        pthread_cond_broadcast(&shared->player_conds[i]);
    }
}

// Функция для вывода результатов
void print_results(int n, pthread_mutex_t *output_mutex, FILE *log_file,
                  pthread_mutex_t *file_mutex) {

    printf("\n=== Итоги турнира ===\n");

    // Структура для результатов
    typedef struct {
        int id;
        int score;
    } PlayerResult;

    PlayerResult results[MAX_PLAYERS];

    pthread_mutex_lock(&shared->data_mutex);
    for (int i = 0; i < n; i++) {
        results[i].id = i;
        results[i].score = shared->scores[i];
    }
    pthread_mutex_unlock(&shared->data_mutex);

    // Сортировка по убыванию очков
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (results[j].score > results[i].score) {
                PlayerResult temp = results[i];
                results[i] = results[j];
                results[j] = temp;
            }
        }
    }

    // Вывод результатов
    for (int i = 0; i < n; i++) {
        printf("%d место: Студент %d - %d очков\n",
               i + 1, results[i].id, results[i].score);
    }

    // Запись в файл
    if (log_file) {
        fprintf(log_file, "\n=== Итоги турнира ===\n");
        for (int i = 0; i < n; i++) {
            fprintf(log_file, "%d место: Студент %d - %d очков\n",
                   i + 1, results[i].id, results[i].score);
        }
    }
}

int main(int argc, char *argv[]) {
    // Установка обработчика сигнала
    signal(SIGINT, handle_signal);

    // Параметры по умолчанию
    int n = 4;
    char *output_file = NULL;
    char *input_file = NULL;
    int use_input_file = 0;

    // Обработка аргументов командной строки
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n = atoi(argv[++i]);
            if (n < 2 || n > MAX_PLAYERS) {
                fprintf(stderr, "Ошибка: количество студентов должно быть от 2 до %d\n", MAX_PLAYERS);
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            input_file = argv[++i];
            use_input_file = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Использование: %s [опции]\n", argv[0]);
            printf("Опции:\n");
            printf("  -n <число>    Количество студентов (2-%d, по умолчанию: 4)\n", MAX_PLAYERS);
            printf("  -o <файл>     Файл для вывода результатов\n");
            printf("  -i <файл>     Файл с входными данными (вместо -n)\n");
            printf("  -h, --help    Показать эту справку\n");
            return 0;
        }
    }

    // Чтение из файла, если указан
    if (use_input_file && input_file) {
        FILE *infile = fopen(input_file, "r");
        if (infile) {
            if (fscanf(infile, "%d", &n) != 1) {
                fprintf(stderr, "Ошибка чтения из файла\n");
                fclose(infile);
                return 1;
            }
            fclose(infile);
            if (n < 2 || n > MAX_PLAYERS) {
                fprintf(stderr, "Ошибка: количество студентов должно быть от 2 до %d\n", MAX_PLAYERS);
                return 1;
            }
        } else {
            fprintf(stderr, "Не удалось открыть файл: %s\n", input_file);
            return 1;
        }
    }

    printf("Турнир 'Камень, ножницы, бумага' с %d студентами\n", n);
    printf("Нажмите Ctrl+C для досрочного завершения\n\n");

    // Инициализация генератора случайных чисел
    srand(time(NULL));

    // Открытие файла для записи, если указан
    FILE *log_file = NULL;
    pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

    if (output_file) {
        log_file = fopen(output_file, "w");
        if (!log_file) {
            fprintf(stderr, "Не удалось открыть файл для записи: %s\n", output_file);
            return 1;
        }
        fprintf(log_file, "Турнир 'Камень, ножницы, бумага' с %d студентами\n", n);
    }

    // Выделение памяти для общих данных
    shared = (SharedData*)calloc(1, sizeof(SharedData));
    if (!shared) {
        fprintf(stderr, "Ошибка выделения памяти\n");
        return 1;
    }

    // Инициализация атомарных переменных
    atomic_init(&shared->game_over, 0);
    atomic_init(&shared->registered_players, 0);
    atomic_init(&shared->moves_ready, 0);
    atomic_init(&shared->match_in_progress, 0);

    shared->total_players = n;
    shared->current_player1 = -1;
    shared->current_player2 = -1;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        atomic_init(&shared->has_moved[i], 0);
    }

    // Инициализация мьютексов и условных переменных
    pthread_mutex_init(&shared->data_mutex, NULL);
    pthread_cond_init(&shared->data_cond, NULL);

    for (int i = 0; i < n; i++) {
        pthread_mutex_init(&shared->player_mutexes[i], NULL);
        pthread_cond_init(&shared->player_conds[i], NULL);
    }

    // Создание мьютекса для вывода
    pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

    // Создание потоков
    threads = (pthread_t*)malloc(n * sizeof(pthread_t));
    thread_data_array = (ThreadData*)malloc(n * sizeof(ThreadData));

    if (!threads || !thread_data_array) {
        fprintf(stderr, "Ошибка выделения памяти\n");
        cleanup_resources();
        return 1;
    }

    // Создание потоков-студентов
    for (int i = 0; i < n; i++) {
        thread_data_array[i].id = i;
        thread_data_array[i].shared = shared;
        thread_data_array[i].output_mutex = &output_mutex;
        thread_data_array[i].log_file = log_file;
        thread_data_array[i].file_mutex = &file_mutex;

        if (pthread_create(&threads[i], NULL, student_thread, &thread_data_array[i]) != 0) {
            fprintf(stderr, "Ошибка создания потока %d\n", i);
            cleanup_resources();
            return 1;
        }

        sleep(1);
    }

    // Пауза для регистрации всех студентов
    sleep(1);

    // Запуск турнира
    run_tournament(n, &output_mutex, log_file, &file_mutex);

    // Ожидание завершения всех потоков
    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
    }

    // Вывод результатов
    print_results(n, &output_mutex, log_file, &file_mutex);

    // Уничтожение мьютексов
    pthread_mutex_destroy(&output_mutex);
    pthread_mutex_destroy(&file_mutex);

    // Освобождение памяти
    cleanup_resources();

    // Закрытие файла
    if (log_file) {
        fclose(log_file);
    }
    
    printf("\nТурнир завершен\n");
    return 0;
}