#include <iostream>
#include <string>
#include <sstream>
#include <regex>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <curl/curl.h>
#include <algorithm>

using namespace std;

#define SERVER_URL "http://127.0.0.1:8080" // URL Nginx

std::atomic<bool> stop_orders{false}; // Флаг для остановки потока

// Структура для представления ордера
struct Order {
    int order_id;
    int user_id;
    int pair_id;
    double quantity;
    double price;
    std::string type;
    int closed;
};

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

// Функция для парсинга строки с ордерами в вектор структур Order
std::vector<Order> parse_orders(const std::string& orders_str) {
    std::vector<Order> orders;
    std::istringstream orders_stream(orders_str);
    std::string line;

    // Ищем начало таблицы
    while (std::getline(orders_stream, line)) {
        if (line.find("order_id,user_id,pair_id,quantity,price,type,closed") != std::string::npos) {
            break;
        }
    }

    // Парсим строки таблицы
    while (std::getline(orders_stream, line)) {
        std::istringstream line_stream(line);
        std::string value;
        std::vector<std::string> row;

        while (std::getline(line_stream, value, ',')) {
            row.push_back(value);
        }

        if (row.size() < 7) {
            //std::cerr << "Ошибка: строка содержит недостаточно столбцов: " << line << std::endl; // раскомментируйте эту строку, если нужно отлаживать неполные строки
            continue;
        }

        Order order;
        order.order_id = std::stoi(row[0]);
        order.user_id = std::stoi(row[1]);
        order.pair_id = std::stoi(row[2]);
        order.quantity = std::stod(row[3]);
        order.price = std::stod(row[4]);
        order.type = row[5];
        order.closed = std::stoi(row[6]);

        orders.push_back(order);
    }

    return orders;
}
// Функция отправки ордера
void send_order(const std::string& key, const Order& order, const std::string& action) {
    std::ostringstream request_stream;
    request_stream << key << " "
                   << order.pair_id << "," << order.quantity << "," << order.price << "," << action;
    std::string request_body = request_stream.str();

    std::cout << "Запрос: " << request_body << std::endl;

    std::string response = send_request("POST", "/order", request_body);
    if (response.find("Ордер успешно создан") != std::string::npos) {
        std::cout << "Успешная операция: " << response << std::endl;
    } else {
        std::cerr << "Ошибка операции: " << response << std::endl;
    }
}

// Функция обработки ордеров
void process_orders(const std::string& key, int check_interval = 5) {
    while (!stop_orders) {
        // Получаем список всех ордеров
        std::string orders_str = send_request("GET", "/order");
        std::vector<Order> orders = parse_orders(orders_str);

        // Группируем ордера по валютным парам и типам
        std::unordered_map<int, std::vector<Order>> buy_orders_by_pair;
        std::unordered_map<int, std::vector<Order>> sell_orders_by_pair;

        for (const auto& order : orders) {
            if (order.closed == 0) {
                if (order.type == "buy") {
                    buy_orders_by_pair[order.pair_id].push_back(order);
                } else if (order.type == "sell") {
                    sell_orders_by_pair[order.pair_id].push_back(order);
                }
            }
        }

        // Обработка самых выгодных ордеров
        for (const auto& [pair_id, buy_orders] : buy_orders_by_pair) {
            // Находим ордер с минимальной ценой (для покупки)
            auto best_buy = std::min_element(buy_orders.begin(), buy_orders.end(),
                                             [](const Order& a, const Order& b) {
                                                 return a.price < b.price;
                                             });

            if (best_buy != buy_orders.end()) {
                send_order(key, *best_buy, "sell");
            }
        }

        for (const auto& [pair_id, sell_orders] : sell_orders_by_pair) {
            // Находим ордер с максимальной ценой (для продажи)
            auto best_sell = std::max_element(sell_orders.begin(), sell_orders.end(),
                                              [](const Order& a, const Order& b) {
                                                  return a.price > b.price;
                                              });

            if (best_sell != sell_orders.end()) {
                send_order(key, *best_sell, "buy");
            }
        }

        // Ожидание перед следующей проверкой
        std::this_thread::sleep_for(std::chrono::seconds(check_interval));
    }
}

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
             stop_orders = true;
            break;
        } else if (choice == 1) {
            std::string response = send_request("POST", "/user", "TestBot");

            if (!response.empty()) {
                std::cout << "Ответ сервера:\n" << response << std::endl;
                key = extract_key(response);

                if (!key.empty()) {
                    std::cout << "Успешная регистрация! Ваш ключ: " << key << std::endl;
                } else {
                    std::cout << "Не удалось извлечь ключ. Введите ключ вручную: ";
                    std::cin >> key;
                }
            } else {
                std::cerr << "Ошибка: Пустой ответ от сервера.\n";
            }
        } else if (choice == 2) {
            if (key.empty()) {
                std::cerr << "Ошибка: Сначала зарегистрируйтесь и получите ключ (нажмите 1).\n";
            } else {
                 stop_orders = false;
                std::thread(process_orders, key, 10).detach(); // Запуск process_orders в отдельном потоке
            }
        }else if (choice == 3){
             stop_orders = true;
        } else {
            std::cerr << "Ошибка: Неверный выбор. Повторите попытку.\n";
        }
    }

    curl_global_cleanup();
    return 0;
}