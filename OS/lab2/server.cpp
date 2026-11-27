#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

volatile sig_atomic_t was_sighup = 0;
volatile sig_atomic_t stop_requested = 0;

static void sighup_handler(int signal_number) {
    (void)signal_number;
    was_sighup = 1;
}

static void stop_handler(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

int main() {
    const uint16_t PORT = 8080;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (server_fd >= FD_SETSIZE) {
        std::cerr << "Ошибка: server_fd >= FD_SETSIZE" << std::endl;
        close(server_fd);
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 16) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    sigset_t blocked_mask;
    sigset_t original_mask;
    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGHUP);
    sigaddset(&blocked_mask, SIGINT);
    sigaddset(&blocked_mask, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &blocked_mask, &original_mask) == -1) {
        perror("sigprocmask");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sigaction sa_hup;
    std::memset(&sa_hup, 0, sizeof(sa_hup));
    sa_hup.sa_handler = sighup_handler;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;

    if (sigaction(SIGHUP, &sa_hup, nullptr) == -1) {
        perror("sigaction(SIGHUP)");
        sigprocmask(SIG_SETMASK, &original_mask, nullptr);
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sigaction sa_stop;
    std::memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_handler = stop_handler;
    sigemptyset(&sa_stop.sa_mask);
    sa_stop.sa_flags = 0;

    if (sigaction(SIGINT, &sa_stop, nullptr) == -1 ||
        sigaction(SIGTERM, &sa_stop, nullptr) == -1) {
        perror("sigaction(SIGINT/SIGTERM)");
        sigprocmask(SIG_SETMASK, &original_mask, nullptr);
        close(server_fd);
        return EXIT_FAILURE;
    }

    std::cout << "[Сервер] Запущен (PID: " << getpid() << "), прослушивание порта "
              << PORT << "..." << std::endl;
    std::cout << "[Сервер] Отправка сигнала: kill -HUP " << getpid() << std::endl;

    int client_fd = -1;

    while (!stop_requested) {
        if (was_sighup) {
            was_sighup = 0;
            std::cout << "[Сигнал] Получен сигнал SIGHUP. Продолжаем работу." << std::endl;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        if (client_fd != -1) {
            FD_SET(client_fd, &read_fds);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
        }

        int ready = pselect(max_fd + 1, &read_fds, nullptr, nullptr, nullptr, &original_mask);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("pselect");
            break;
        }

        int existing_client = client_fd;

        // Новое входящее соединение
        if (FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(server_fd,
                                reinterpret_cast<struct sockaddr*>(&client_addr),
                                &client_len);

            if (new_fd == -1) {
                if (errno != EINTR) {
                    perror("accept");
                }
            } else {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                uint16_t client_port = ntohs(client_addr.sin_port);

                std::cout << "[Подключение] Новый клиент " << client_ip << ":" << client_port
                          << " (fd=" << new_fd << ")" << std::endl;

                if (client_fd == -1 && new_fd < FD_SETSIZE) {
                    client_fd = new_fd;
                    std::cout << "[Сервер] Соединение сохранено как активное (fd="
                              << client_fd << ")" << std::endl;
                } else {
                    std::cout << "[Сервер] Активное соединение уже занято. Закрываем fd="
                              << new_fd << std::endl;
                    close(new_fd);
                }
            }
        }

        if (existing_client != -1 && FD_ISSET(existing_client, &read_fds)) {
            char buffer[4096];
            ssize_t received = recv(existing_client, buffer, sizeof(buffer), 0);

            if (received > 0) {
                std::cout << "[Данные] Получено байтов: " << received
                          << " (fd=" << existing_client << ")" << std::endl;
            } else if (received == 0) {
                std::cout << "[Отключение] Клиент корректно закрыл соединение (fd="
                          << existing_client << ")" << std::endl;
                close(existing_client);
                client_fd = -1;
            } else if (errno != EINTR) {
                perror("recv");
                close(existing_client);
                client_fd = -1;
            }
        }
    }

    std::cout << "\n[Сервер] Завершение работы, освобождение ресурсов..." << std::endl;
    if (client_fd != -1) {
        close(client_fd);
        client_fd = -1;
    }
    close(server_fd);
    sigprocmask(SIG_SETMASK, &original_mask, nullptr);

    return EXIT_SUCCESS;
}