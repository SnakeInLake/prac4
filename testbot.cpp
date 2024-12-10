#include <iostream>
#include <string>
#include <cstring>
#include <regex>
#include <thread>
#include <chrono>
#include <atomic>
#include <curl/curl.h>
#include <sstream>

#define SERVER_URL "http://127.0.0.1:8080" // URL Nginx

std::atomic<bool> stop_orders{false}; // Флаг для остановки потока

// Функция для обработки ответа cURL
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Функция отправки HTTP-запроса
std::string send_request(const std::string &method, const std::string &path, const std::string &body = "") {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        std::string url = std::string(SERVER_URL) + path;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
             curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "CLIENT ERROR: cURL error: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

// Функция обработки ответа и получения ключа
std::string extract_key(const std::string &response) {
    std::regex key_regex(R"(Запись значения: ([a-zA-Z0-9]{10,}))");
    std::smatch match;

    if (std::regex_search(response, match, key_regex) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

void place_order(const std::string &key) {
    // Инициализация генератора случайных чисел
    srand(time(nullptr));

    // Генерация случайных значений
    int pair_id = rand() % 10 + 1;    // Случайный ID пары (от 1 до 10)
    int quantity = rand() % 10 + 1;   // Случайное количество (от 1 до 10)
    double price = (rand() % 100) / 100.0; // Случайная цена (от 0.01 до 99.99)

    // Генерация случайного типа ордера
    std::string type = (rand() % 2 == 0) ? "buy" : "sell";

    // Формируем запрос
    std::ostringstream request_stream;
    request_stream << key <<" "<<pair_id
                   << "," << quantity
                   << "," << price
                   << "," << type;
    std::string request_body = request_stream.str();

    // Отправляем запрос
    std::string response = send_request("POST", "/order", request_body);

    // Проверяем ответ
    if (response.find("Ордер успешно создан") != std::string::npos) {
        std::cout << "Успех: " << response << "\n";
    } else {
        std::cerr << "Ошибка: " << response << "\n";
    }
}

void auto_place_orders(const std::string &key, int num_orders) {
    for (int i = 0; i < num_orders; ++i) {
        if (stop_orders) {
            std::cout << "Автоматическое выставление ордеров остановлено.\n";
            return;
        }
        place_order(key); // Разместить ордер
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Ждать 1 секунду
    }
    std::cout << "Все ордера размещены.\n";
     std::cout << "Выберите действие:\n";
        std::cout << "1. Зарегистрировать пользователя\n";
        std::cout << "2. Запустить автоматическое выставление ордеров\n";
        std::cout << "3. Остановить автоматическое выставление ордеров\n";
        std::cout << "0. Выход\n";
        std::cout << "Ваш выбор: ";

}



// Основная программа
int main() {
     curl_global_init(CURL_GLOBAL_ALL);
    std::string key;

    while (true) {
        std::cout << "Выберите действие:\n";
        std::cout << "1. Зарегистрировать пользователя\n";
        std::cout << "2. Запустить автоматическое выставление ордеров\n";
        std::cout << "3. Остановить автоматическое выставление ордеров\n";
        std::cout << "0. Выход\n";
        std::cout << "Ваш выбор: ";

        int choice;
        std::cin >> choice;

        if (choice == 0) {
            std::cout << "Завершение программы.\n";
            stop_orders = true; // Устанавливаем флаг остановки
             curl_global_cleanup();
            break;
        } else if (choice == 1) {
            std::string response = send_request("POST", "/user", "TestBot");

            if (!response.empty()) {
                std::cout << "Ответ сервера:\n" << response << std::endl;

                // Пытаемся извлечь ключ из ответа
                key = extract_key(response);

                if (!key.empty()) {
                    std::cout << "Успешная регистрация! Ваш ключ: " << key << std::endl;
                } else {
                    std::cout << "Не удалось извлечь ключ. Возможно, пользователь уже существует.\n";
                    std::cout << "Введите ваш ключ вручную: ";
                    std::cin >> key;
                }
            } else {
                std::cerr << "Ошибка: Пустой ответ от сервера.\n";
            }
        } else if (choice == 2) {
            if (key.empty()) {
                std::cerr << "Ошибка: Сначала зарегистрируйтесь и получите ключ (нажмите 1).\n";
            } else {
                std::cout << "Введите количество ордеров для автоматического выставления: ";
                int num_orders;
                std::cin >> num_orders;

                // Запуск автоматического выставления ордеров в отдельном потоке
                stop_orders = false;
                std::thread order_thread(auto_place_orders,key, num_orders);
                order_thread.detach(); // Отделяем поток, чтобы он работал в фоне
            }
        } else if (choice == 3) {
            stop_orders = true; // Устанавливаем флаг остановки
        } else {
            std::cerr << "Ошибка: Неверный выбор. Повторите попытку.\n";
        }
    }
     curl_global_cleanup();

    return 0;
}